#include "../mob_options.h"
#include "../mob_progs/mage_prog.h"

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern struct char_data* combat_list;
extern struct char_data* waiting_list;
extern int no_specials;
extern struct weather_data weather_info;
extern struct zone_data* zone_table;
void remove_char_exists(int num);
void set_char_exists(int num);
void clear_char(struct char_data* ch, int mode);
int in_waiting_list(const struct char_data* ch);
void abort_delay(struct char_data* wait_ch);
SPECIAL(mob_magic_user_spec);
SPECIAL(mob_magic_user);
SPECIAL(mob_cleric);
void* virt_program_number(int number);
void get_combat_spells(const char_data* host, const mage_config& opts, int* spell_list, double current_health_pct);

namespace {

std::vector<std::string> problem_texts(const std::vector<vendor_problem>& problems)
{
    std::vector<std::string> out;
    for (const vendor_problem& p : problems)
        out.push_back(std::to_string(p.line) + ": " + p.text);
    return out;
}

/* ---- The settings ---- */

TEST(MageOptions, NoTextIsAllOff)
{
    std::vector<vendor_problem> problems;
    mage_config config = parse_mage_options(nullptr, &problems);
    EXPECT_FALSE(config.has_type());
    EXPECT_FALSE(config.conjurer);
    EXPECT_FALSE(config.shield);
    EXPECT_FALSE(config.terror);
    EXPECT_TRUE(problems.empty());
}

TEST(MageOptions, ReadsEachSetting)
{
    std::vector<vendor_problem> problems;
    mage_config config = parse_mage_options("fire=yes\nlightning = yes\ncold=yes\ndark=yes\nlhuth=yes\n"
                                            "mixed=yes\nconjurer=yes\nshield=yes\nterror=yes",
        &problems);
    EXPECT_TRUE(config.fire);
    EXPECT_TRUE(config.lightning);
    EXPECT_TRUE(config.cold);
    EXPECT_TRUE(config.dark);
    EXPECT_TRUE(config.lhuth);
    EXPECT_TRUE(config.mixed);
    EXPECT_TRUE(config.conjurer);
    EXPECT_TRUE(config.shield);
    EXPECT_TRUE(config.terror);
    EXPECT_TRUE(problems.empty());

    config = parse_mage_options("fire=no\ndark=yes", &problems);
    EXPECT_FALSE(config.fire);
    EXPECT_TRUE(config.dark);
    EXPECT_TRUE(config.has_type());
}

TEST(MageOptions, BadDuplicateUnknownAndOldKeywordLinesAreReported)
{
    std::vector<vendor_problem> problems;
    mage_config config
        = parse_mage_options("fire=maybe\ndark=yes\ndark=no\nfmage=yes\nconj\n// a comment", &problems);
    EXPECT_FALSE(config.fire);
    EXPECT_TRUE(config.dark) << "the first dark line stands";
    EXPECT_EQ(problem_texts(problems),
        (std::vector<std::string> { "1: bad fire - line ignored", "3: duplicate dark - line ignored",
            "4: unknown setting - line ignored", "5: unknown setting - line ignored" }));
}

TEST(MageOptions, ACorrectedLineAfterABadOneCounts)
{
    std::vector<vendor_problem> problems;
    mage_config config = parse_mage_options("fire=yse\nfire=yes", &problems);
    EXPECT_TRUE(config.fire);
    EXPECT_EQ(problem_texts(problems), (std::vector<std::string> { "1: bad fire - line ignored" }));
}

TEST(MageOptions, WalkerLinesArePassedOver)
{
    std::vector<vendor_problem> problems;
    mage_config config = parse_mage_options("walker_type=path\nwalker_rooms=1-3\ncold=yes", &problems);
    EXPECT_TRUE(config.cold);
    EXPECT_TRUE(problems.empty());
}

TEST(MageOptions, NoTypeIsWarnedWithinSeventyEightColumns)
{
    EXPECT_TRUE(mage_setting_warnings("dark=yes", 18000).empty());
    EXPECT_TRUE(mage_setting_warnings("mixed=yes\nconjurer=yes", 26223).empty());

    std::vector<std::string> lines = mage_setting_warnings("conjurer=yes", 1999999);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "MOB WARNING: mobile #1999999: no mage type set, no type spells");
    EXPECT_LE(lines[0].size(), 78u);
    EXPECT_EQ(mage_setting_warnings(nullptr, 1).size(), 1u);
}

/* ---- The program's turn ----
 *
 * One room (index 990), lit and outdoors. The mage mob (level 20) and a
 * player start there; a second player (a group member of the first) is
 * placed by the tests that use it. */

class MageProgTest : public ::testing::Test {
protected:
    static constexpr int kRoom = 990;
    static constexpr int kMobSlot = 4700;
    static constexpr int kPlayerSlot = 4701;
    static constexpr int kFriendSlot = 4702;

    void SetUp() override
    {
        m_saved_combat_list = combat_list;
        combat_list = nullptr;
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_saved_descriptor_list = descriptor_list;
        m_saved_weather = weather_info;
        m_saved_room = world[kRoom];
        m_saved_zone_table = zone_table;
        zone_table = m_zone_table;
        m_next_seed = std::rand(); /* later tests go on from here, not from seed 1 */
        std::srand(1);
        descriptor_list = nullptr;

        world[kRoom].number = 99000 + kRoom;
        world[kRoom].zone = 0;
        world[kRoom].light = 1;
        world[kRoom].room_flags = 0;
        world[kRoom].sector_type = SECT_FIELD;
        world[kRoom].people = nullptr;
        world[kRoom].contents = nullptr;
        for (int dir = 0; dir < NUM_OF_DIRS; ++dir)
            world[kRoom].dir_option[dir] = nullptr;
        weather_info.sunlight = SUN_DARK;
        weather_info.sky[SECT_FIELD] = SKY_CLOUDLESS;

        clear_char(&m_mob_proto[0], MOB_ISNPC);
        m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
        m_mob_proto[0].specials.store_prog_number = PROG_MAGE;
        m_mob_proto[0].specials.mob_options = m_options;
        m_mob_index[0].virt = 26223;
        m_mob_index[0].func = nullptr;
        mob_proto = m_mob_proto;
        mob_index = m_mob_index;
        top_of_mobt = 0;

        make_mob();
        make_player(&m_player, kPlayerSlot, m_player_name);
        make_player(&m_friend, kFriendSlot, m_friend_name);
        place(&m_mob);
        place(&m_player);
        /* A linkless player flees from any fight, so the player is connected. */
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1;
        m_descriptor.character = &m_player;
        m_player.desc = &m_descriptor;
        set_options("");
    }

    void TearDown() override
    {
        for (char_data* ch : { &m_mob, &m_player, &m_friend }) {
            if (in_waiting_list(ch) || ch->delay.wait_value)
                abort_delay(ch);
            if (ch->specials.fighting)
                stop_fighting(ch);
            while (ch->affected)
                affect_remove(ch, ch->affected);
            ch->group = nullptr;
            if (ch->in_room != NOWHERE)
                char_from_room(ch);
        }
        combat_list = m_saved_combat_list;
        remove_char_exists(kMobSlot);
        remove_char_exists(kPlayerSlot);
        remove_char_exists(kFriendSlot);
        m_mob_proto[0].specials.store_prog_number = 0;
        m_mob_proto[0].specials.mob_options = nullptr;
        mage_config_rebuild(0, nullptr, false); /* erases the registry entry */
        world[kRoom] = m_saved_room;
        weather_info = m_saved_weather;
        std::srand(m_next_seed);
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        descriptor_list = m_saved_descriptor_list;
        zone_table = m_saved_zone_table;
    }

    static void give_stats(char_data* ch)
    {
        ch->tmpabilities.str = ch->abilities.str = 15;
        ch->tmpabilities.intel = ch->abilities.intel = 15;
        ch->tmpabilities.dex = ch->abilities.dex = 15;
        ch->tmpabilities.con = ch->abilities.con = 15;
        ch->tmpabilities.wil = ch->abilities.wil = 15;
        ch->tmpabilities.lea = ch->abilities.lea = 15;
        ch->constabilities = ch->abilities;
    }

    void make_mob()
    {
        clear_char(&m_mob, MOB_ISNPC);
        m_mob.nr = 0;
        m_mob.abs_number = kMobSlot;
        set_char_exists(kMobSlot);
        m_mob.specials2.act = MOB_ISNPC | MOB_SPEC;
        m_mob.specials.store_prog_number = PROG_MAGE;
        m_mob.player.name = m_mob_name;
        m_mob.player.short_descr = m_mob_name;
        m_mob.player.level = 20;
        m_mob.player.race = 0;
        m_mob.tmpabilities.hit = m_mob.abilities.hit = 200;
        m_mob.tmpabilities.mana = m_mob.abilities.mana = 500;
        m_mob.tmpabilities.move = m_mob.abilities.move = 200;
        m_mob.specials.position = POSITION_STANDING;
        m_mob.specials.default_pos = POSITION_STANDING;
        give_stats(&m_mob);
        m_mob.in_room = NOWHERE;
    }

    static void make_player(char_data* pc, int slot, char* name)
    {
        clear_char(pc, 0);
        pc->abs_number = slot;
        set_char_exists(slot);
        pc->player.name = name;
        pc->player.race = RACE_HUMAN;
        pc->player.level = 20;
        pc->tmpabilities.hit = pc->abilities.hit = 200;
        pc->tmpabilities.move = pc->abilities.move = 200;
        pc->specials.position = POSITION_STANDING;
        give_stats(pc);
        pc->in_room = NOWHERE;
    }

    static void place(char_data* ch)
    {
        if (ch->in_room != NOWHERE)
            char_from_room(ch);
        char_to_room(ch, kRoom);
    }

    void set_options(const char* text)
    {
        std::strncpy(m_options, text, sizeof(m_options) - 1);
        m_options[sizeof(m_options) - 1] = '\0';
        mage_config_rebuild(0, nullptr, false);
    }

    void fight_player()
    {
        set_fighting(&m_mob, &m_player);
        set_fighting(&m_player, &m_mob);
    }

    void set_health_pct(int pct) { m_mob.tmpabilities.hit = m_mob.abilities.hit * pct / 100; }

    void clear_delay()
    {
        if (in_waiting_list(&m_mob) || m_mob.delay.wait_value)
            abort_delay(&m_mob);
        m_mob.delay.cmd = 0;
        m_mob.delay.subcmd = 0;
        REMOVE_BIT(m_mob.specials.affected_by, AFF_WAITING | AFF_WAITWHEEL);
    }

    int turn() { return mob_magic_user_spec(&m_mob, &m_mob, 0, m_arg, SPECIAL_SELF, nullptr); }

    bool cast_started() const { return m_mob.delay.cmd == CMD_CAST && IS_AFFECTED(&m_mob, AFF_WAITING); }

    struct cast {
        int spell;
        char_data* target;
    };

    /* `turns` own turns of the program; the casts that started. */
    template <typename Turn>
    std::vector<cast> casts_over(int turns, Turn one_turn)
    {
        std::vector<cast> out;
        for (int i = 0; i < turns; ++i) {
            clear_delay();
            one_turn();
            if (cast_started())
                out.push_back({ m_mob.delay.subcmd, m_mob.delay.targ2.ptr.ch });
        }
        clear_delay();
        return out;
    }
    std::vector<cast> casts_over(int turns)
    {
        return casts_over(turns, [this] { turn(); });
    }

    std::set<int> spells_over(int turns)
    {
        std::set<int> out;
        for (const cast& c : casts_over(turns))
            out.insert(c.spell);
        return out;
    }

    static int count_of(const std::vector<cast>& casts, int spell)
    {
        int n = 0;
        for (const cast& c : casts)
            n += c.spell == spell;
        return n;
    }

    static int aimed_at(const std::vector<cast>& casts, const char_data* target)
    {
        int n = 0;
        for (const cast& c : casts)
            n += c.target == target;
        return n;
    }

    void affect(char_data* ch, int spell)
    {
        affected_type af {};
        af.type = spell;
        af.duration = 10;
        af.modifier = 0;
        af.location = APPLY_NONE;
        af.bitvector = 0;
        affect_to_char(ch, &af);
    }

    char m_options[256] = "";
    char m_mob_name[32] = "robed figure";
    char m_player_name[16] = "Caster";
    char m_friend_name[16] = "Friend";
    char m_arg[8] = "";
    char_data m_mob_proto[1] {};
    index_data m_mob_index[1] {};
    char_data m_mob {};
    char_data m_player {};
    char_data m_friend {};
    descriptor_data m_descriptor {};

    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    char_data* m_saved_combat_list = nullptr;
    weather_data m_saved_weather {};
    room_data m_saved_room {};
    zone_data* m_saved_zone_table = nullptr;
    zone_data m_zone_table[1] {};
    unsigned m_next_seed = 1;
};

TEST_F(MageProgTest, SettingsReachOnlyAProgram31Mob)
{
    set_options("dark=yes\nterror=yes");
    EXPECT_TRUE(mage_settings(&m_mob).dark);
    EXPECT_TRUE(mage_settings(&m_mob).terror);
    m_mob.specials.store_prog_number = 32;
    EXPECT_FALSE(mage_settings(&m_mob).dark);
    EXPECT_FALSE(mage_settings(&m_player).dark);
}

TEST_F(MageProgTest, CopiesLeftOnTheProgramKeepTheTypesSettings)
{
    set_options("dark=yes");
    /* /implement took the type off program 31; this copy still runs it. */
    m_mob_proto[0].specials.store_prog_number = 0;
    mage_config_rebuild(0, nullptr, false);
    EXPECT_TRUE(mage_settings(&m_mob).dark);
    m_mob_proto[0].specials.store_prog_number = PROG_MAGE;
}

TEST_F(MageProgTest, CastChanceIsLevelPlusTwenty)
{
    set_health_pct(50); /* cure self, out of a fight */
    const int casts = static_cast<int>(casts_over(1000).size());
    EXPECT_GT(casts, 330) << "level 20 casts on about 40% of its turns";
    EXPECT_LT(casts, 470) << "level 20 casts on about 40% of its turns";
}

TEST_F(MageProgTest, AStartedCastEndsTheTurn)
{
    m_mob.player.level = 80; /* the cast roll always passes */
    set_health_pct(50);
    EXPECT_TRUE(turn());
    EXPECT_TRUE(cast_started());
    EXPECT_EQ(m_mob.delay.subcmd, SPELL_CURE_SELF);
}

TEST_F(MageProgTest, AWaitingMobWaitsInsteadOfCastingAtOnce)
{
    m_mob.player.level = 80;
    set_health_pct(50);
    const int mana = GET_MANA(&m_mob);
    char_data* mob = &m_mob;
    WAIT_STATE(mob, 1); /* the last beat of a wait with no command */
    EXPECT_FALSE(turn());
    EXPECT_EQ(GET_MANA(&m_mob), mana) << "no spell went off with no casting time";
    EXPECT_NE(m_mob.delay.cmd, CMD_CAST);
    clear_delay();
    EXPECT_TRUE(turn());
    EXPECT_TRUE(cast_started());
}

TEST_F(MageProgTest, ACastRefusedInAPeaceRoomLeavesTheTurn)
{
    m_mob.player.level = 80;
    set_health_pct(50);
    world[kRoom].room_flags = PEACEROOM;
    EXPECT_FALSE(turn());
    EXPECT_FALSE(cast_started());
}

TEST_F(MageProgTest, ACastRefusedForManaLeavesTheTurn)
{
    m_mob.player.level = 80;
    set_health_pct(50);
    m_mob.tmpabilities.mana = 0;
    EXPECT_FALSE(turn());
    EXPECT_FALSE(cast_started());
}

TEST_F(MageProgTest, NoTypeCastsNoCombatSpells)
{
    fight_player();
    EXPECT_TRUE(casts_over(200).empty());
}

TEST_F(MageProgTest, EachHealthBandHasItsList)
{
    set_options("fire=yes");
    fight_player();
    EXPECT_EQ(spells_over(200), (std::set<int> { SPELL_FIREBOLT }));
    set_health_pct(40);
    EXPECT_EQ(spells_over(200), (std::set<int> { SPELL_CHILL_RAY, SPELL_FIREBOLT }));
    set_health_pct(20);
    EXPECT_EQ(spells_over(200), (std::set<int> { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE }));
}

TEST_F(MageProgTest, BelowZeroHitPointsIsTheLowestBand)
{
    mage_config mixed;
    mixed.mixed = true;
    for (double pct : { -0.25, std::nan("") }) {
        int spell_list[20] = { 0 };
        get_combat_spells(&m_mob, mixed, spell_list, pct);
        EXPECT_EQ(std::set<int>(spell_list + 1, spell_list + 1 + spell_list[0]),
            (std::set<int> { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE }))
            << pct;
    }
}

TEST_F(MageProgTest, NoMaximumHitPointsIsTheLowestBand)
{
    set_options("mixed=yes");
    fight_player();
    m_mob.abilities.hit = 0; /* never /recalc'd */
    m_mob.tmpabilities.hit = 50;
    EXPECT_EQ(spells_over(300), (std::set<int> { SPELL_CHILL_RAY, SPELL_MAGIC_MISSILE }));
}

TEST_F(MageProgTest, AStunnedMobDoesNotCast)
{
    set_options("mixed=yes");
    fight_player();
    m_mob.player.level = 80;
    m_mob.tmpabilities.hit = -5;
    m_mob.specials.position = POSITION_STUNNED;
    EXPECT_FALSE(turn());
    EXPECT_FALSE(cast_started());
}

TEST_F(MageProgTest, AStunnedMobDoesNotBreakOffTheFight)
{
    /* do_cast would refuse the shield for a stunned mob, but only after the
     * break-off had stopped everyone fighting it. */
    set_options("shield=yes");
    fight_player();
    m_mob.player.level = 80;
    m_mob.tmpabilities.mana = 100;
    m_mob.interrupt_count = 3;
    m_mob.tmpabilities.hit = -5;
    m_mob.specials.position = POSITION_STUNNED;
    for (int i = 0; i < 40; ++i) {
        clear_delay();
        turn();
    }
    EXPECT_EQ(m_player.specials.fighting, &m_mob);
    EXPECT_EQ(m_mob.specials.fighting, &m_player);
}

TEST_F(MageProgTest, TypesAddTheirListsTogether)
{
    set_options("fire=yes\ncold=yes");
    fight_player();
    EXPECT_EQ(spells_over(300), (std::set<int> { SPELL_FIREBOLT, SPELL_CHILL_RAY }));
}

TEST_F(MageProgTest, LevelGatedSpellsJoinAtTheirLevel)
{
    struct gate {
        const char* option;
        int spell;
        int level;
    };
    for (const gate& g : { gate { "fire=yes", SPELL_FIREBALL, 40 }, gate { "cold=yes", SPELL_CONE_OF_COLD, 30 },
             gate { "dark=yes", SPELL_SEARING_DARKNESS, 40 }, gate { "mixed=yes", SPELL_EARTHQUAKE, 30 } }) {
        set_options(g.option);
        if (!m_mob.specials.fighting)
            fight_player();
        m_mob.player.level = g.level - 1;
        EXPECT_EQ(spells_over(300).count(g.spell), 0u) << g.option << " below level " << g.level;
        m_mob.player.level = g.level;
        EXPECT_EQ(spells_over(300).count(g.spell), 1u) << g.option << " at level " << g.level;
    }
}

TEST_F(MageProgTest, LightningStrikeOnlyOutdoorsInAStorm)
{
    set_options("lightning=yes");
    fight_player();
    m_mob.player.level = 35;
    EXPECT_EQ(spells_over(300).count(SPELL_LIGHTNING_STRIKE), 0u) << "clear sky";
    weather_info.sky[SECT_FIELD] = SKY_LIGHTNING;
    EXPECT_EQ(spells_over(300).count(SPELL_LIGHTNING_STRIKE), 1u) << "storm";
    world[kRoom].room_flags = INDOORS;
    EXPECT_EQ(spells_over(300).count(SPELL_LIGHTNING_STRIKE), 0u) << "indoors";
}

TEST_F(MageProgTest, SpearOfDarknessOnlyOutOfTheSun)
{
    set_options("lhuth=yes");
    fight_player();
    m_mob.player.level = 40;
    m_mob.player.race = RACE_ORC;
    EXPECT_EQ(spells_over(300).count(SPELL_SPEAR_OF_DARKNESS), 1u) << "night";
    weather_info.sunlight = SUN_LIGHT;
    EXPECT_EQ(spells_over(300).count(SPELL_SPEAR_OF_DARKNESS), 0u) << "in the sun";
}

TEST_F(MageProgTest, TerrorComesInTheLowestHealthBand)
{
    set_options("terror=yes");
    fight_player();
    set_health_pct(30);
    EXPECT_EQ(count_of(casts_over(300), SPELL_TERROR), 0) << "30% is above the lowest band";
    set_health_pct(20);
    const int terrors = count_of(casts_over(300), SPELL_TERROR);
    EXPECT_GT(terrors, 0) << "a 20% chance on each cast turn";
}

TEST_F(MageProgTest, ConjurerHealsOutOfAFight)
{
    set_options("conjurer=yes");
    m_mob.player.level = 80;
    EXPECT_TRUE(turn());
    EXPECT_EQ(m_mob.delay.subcmd, SPELL_REGENERATION);
    clear_delay();
    affect(&m_mob, SPELL_REGENERATION);
    EXPECT_TRUE(turn());
    EXPECT_EQ(m_mob.delay.subcmd, SPELL_CURING);
}

TEST_F(MageProgTest, ConjurerConfusesAndPoisonsInAFight)
{
    set_options("conjurer=yes\ndark=yes");
    fight_player();
    EXPECT_GT(count_of(casts_over(300), SPELL_CONFUSE), 0);
    affect(&m_player, SPELL_CONFUSE);
    EXPECT_GT(count_of(casts_over(300), SPELL_POISON), 0) << "poison once the target is confused";
}

TEST_F(MageProgTest, ConfuseIsCheckedOnTheOneItFights)
{
    /* Confuse only lands on the one the mob fights, whoever the picker chose. */
    set_options("conjurer=yes\ndark=yes");
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    ASSERT_GT(count_of(casts_over(300), SPELL_CONFUSE), 0);
    affect(&m_player, SPELL_CONFUSE);
    std::vector<cast> casts = casts_over(300);
    EXPECT_EQ(count_of(casts, SPELL_CONFUSE), 0) << "its foe is confused already";
    EXPECT_GT(count_of(casts, SPELL_POISON), 0);
}

TEST_F(MageProgTest, LhuthNeverPoisons)
{
    set_options("conjurer=yes\nlhuth=yes");
    fight_player();
    affect(&m_player, SPELL_CONFUSE);
    EXPECT_EQ(count_of(casts_over(300), SPELL_POISON), 0);
}

TEST_F(MageProgTest, AtTheInterruptLimitItCastsNothingAtItsFoes)
{
    set_options("conjurer=yes\ndark=yes");
    fight_player();
    m_mob.interrupt_count = 3;
    EXPECT_TRUE(casts_over(300).empty());
}

TEST_F(MageProgTest, ShieldTacticBreaksOffTheFightToCastShield)
{
    set_options("shield=yes\ndark=yes");
    fight_player();
    m_mob.interrupt_count = 3;
    m_mob.player.level = 80;
    std::vector<cast> casts;
    for (int i = 0; i < 20 && casts.empty(); ++i)
        casts = casts_over(1);
    ASSERT_EQ(casts.size(), 1u);
    EXPECT_EQ(casts[0].spell, SPELL_SHIELD);
    EXPECT_EQ(m_mob.specials.fighting, nullptr);
    EXPECT_EQ(m_player.specials.fighting, nullptr);
}

TEST_F(MageProgTest, ShieldTacticWaitsForTheManaShieldCosts)
{
    set_options("shield=yes");
    fight_player();
    m_mob.interrupt_count = 3;
    m_mob.player.level = 5; /* shield costs a level 5 caster 20 mana */
    m_mob.tmpabilities.mana = 15;
    for (int i = 0; i < 40; ++i) {
        turn();
        clear_delay();
    }
    EXPECT_EQ(m_mob.specials.fighting, &m_player) << "no break-off it cannot pay for";
    EXPECT_EQ(m_player.specials.fighting, &m_mob);

    m_mob.tmpabilities.mana = 25;
    std::vector<cast> casts;
    for (int i = 0; i < 40 && casts.empty(); ++i)
        casts = casts_over(1);
    ASSERT_EQ(casts.size(), 1u);
    EXPECT_EQ(casts[0].spell, SPELL_SHIELD);
}

TEST_F(MageProgTest, SettingsFollowTheProgramTheMobRuns)
{
    set_options("dark=yes");
    /* /implement switched the type to program 31; this copy kept its old number. */
    m_mob.specials.store_prog_number = 4;
    m_mob_index[0].func = mob_magic_user_spec;
    EXPECT_TRUE(mage_settings(&m_mob).dark);
    /* and the other way: the type runs program 4 now. */
    m_mob.specials.store_prog_number = PROG_MAGE;
    m_mob_index[0].func = mob_magic_user;
    EXPECT_FALSE(mage_settings(&m_mob).dark);
    m_mob_index[0].func = nullptr;
}

TEST_F(MageProgTest, SaveChecksTheNewProgramBeforeImplement)
{
    /* A builder's copy set to /29 31; the type still runs program 4, as the
     * old /29 gave it, until /implement. */
    char_data editing {};
    clear_char(&editing, MOB_ISNPC);
    editing.specials2.act = MOB_ISNPC | MOB_SPEC;
    editing.specials.store_prog_number = PROG_MAGE;
    m_mob_proto[0].specials.store_prog_number = 4;
    m_mob_index[0].func = (special_func)virt_program_number(4);
    EXPECT_TRUE(is_mage_candidate(&editing, 0));
    /* A function assigned by vnum (a guild, say) still wins. */
    m_mob_index[0].func = mob_cleric;
    EXPECT_FALSE(is_mage_candidate(&editing, 0));
    m_mob_index[0].func = mob_magic_user_spec;
    EXPECT_TRUE(is_mage_candidate(&editing, 0));
    /* Without the spec flag the loaded number is a mudlle index, so a
     * function that matches it was assigned by vnum (as implement_proto). */
    m_mob_proto[0].specials2.act = MOB_ISNPC;
    m_mob_index[0].func = (special_func)virt_program_number(4);
    EXPECT_FALSE(is_mage_candidate(&editing, 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
    m_mob_index[0].func = nullptr;
    m_mob_proto[0].specials.store_prog_number = PROG_MAGE;
}

TEST_F(MageProgTest, AMobRunningNoProgramGetsNoSettings)
{
    set_options("dark=yes");
    ASSERT_TRUE(mage_settings(&m_mob).dark);
    REMOVE_BIT(m_mob.specials2.act, MOB_SPEC);
    EXPECT_FALSE(mage_settings(&m_mob).dark) << "no spec flag";
    SET_BIT(m_mob.specials2.act, MOB_SPEC);
    no_specials = 1;
    EXPECT_FALSE(mage_settings(&m_mob).dark) << "a -s boot";
    no_specials = 0;
}

TEST_F(MageProgTest, AGroupMemberItCanSeeIsATarget)
{
    set_options("dark=yes");
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    EXPECT_GT(aimed_at(casts_over(300), &m_friend), 0);
}

TEST_F(MageProgTest, AGroupMemberItCannotSeeIsNeverATarget)
{
    set_options("dark=yes");
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    SET_BIT(m_friend.specials.affected_by, AFF_INVISIBLE);
    std::vector<cast> casts = casts_over(300);
    EXPECT_FALSE(casts.empty());
    EXPECT_EQ(aimed_at(casts, &m_friend), 0);
}

TEST_F(MageProgTest, TheOneItFightsIsATargetEvenUnseen)
{
    set_options("dark=yes");
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    SET_BIT(m_player.specials.affected_by, AFF_INVISIBLE);
    std::vector<cast> casts = casts_over(300);
    EXPECT_GT(aimed_at(casts, &m_friend), 0) << "the seen group member is a target";
    EXPECT_GT(aimed_at(casts, &m_player), 0) << "and so is the unseen one it fights";
}

TEST_F(MageProgTest, TheOldMysticProgramDoesNotTargetTheUnseenEither)
{
    m_mob.specials.store_prog_number = 3;
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    auto old_turn = [this] { mob_cleric(&m_mob, &m_mob, 0, m_arg, SPECIAL_SELF, nullptr); };
    EXPECT_GT(aimed_at(casts_over(300, old_turn), &m_friend), 0) << "seen: a target";
    SET_BIT(m_friend.specials.affected_by, AFF_INVISIBLE);
    std::vector<cast> casts = casts_over(300, old_turn);
    EXPECT_FALSE(casts.empty());
    EXPECT_EQ(aimed_at(casts, &m_friend), 0) << "unseen: never";
}

TEST_F(MageProgTest, TheOldMageProgramDoesNotTargetTheUnseenEither)
{
    m_mob.specials.store_prog_number = 4;
    group_data group(&m_player);
    group.add_member(&m_friend);
    place(&m_friend);
    fight_player();
    auto old_turn = [this] { mob_magic_user(&m_mob, &m_mob, 0, m_arg, SPECIAL_SELF, nullptr); };
    EXPECT_GT(aimed_at(casts_over(300, old_turn), &m_friend), 0) << "seen: a target";
    SET_BIT(m_friend.specials.affected_by, AFF_INVISIBLE);
    std::vector<cast> casts = casts_over(300, old_turn);
    EXPECT_FALSE(casts.empty());
    EXPECT_EQ(aimed_at(casts, &m_friend), 0) << "unseen: never";
}

} // namespace
