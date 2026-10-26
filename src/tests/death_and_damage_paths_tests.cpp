/* Putting gear on and taking it off, dying of negative regeneration, area spells and an NPC
 * olog-hai's skills, when a blow, a death or a script set off on the way removes one of the
 * characters involved: nothing touches the removed one, and what stays behaves as it does
 * without the removal.
 *
 * Poison here comes from an item that gives its wearer the poison flag (APPLY_BITVECTOR), which
 * hurts the wearer as the item goes on and comes off. A test that needs a roll to go one way
 * queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../char_utils.h"
#include "../skill_timer.h"
#include "../spells.h"

void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wield(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_remove(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_cleave(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_order(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void assign_command_pointers(void);
void complete_delay(struct char_data* ch);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void fast_update();
void affect_update_room(struct room_data* room);

namespace olog_hai {
int get_base_skill_damage(char_data& olog_hai, int prob);
}

extern struct weather_data weather_info;
extern struct index_data* mob_index;

using namespace scripted_room;

namespace {

/* The poison bit, as the APPLY_BITVECTOR modifier that gives it (the bit's position). */
constexpr int POISON_BIT_POSITION = 11;
/* What the poison deals as an item goes on or comes off. */
constexpr int POISON_DAMAGE = 5;
/* Few enough hit points that the poison kills. */
constexpr int FEWER_HIT_POINTS_THAN_THE_POISON = 3;
/* A weapon at least this bulky is wielded in two hands where it can be. */
constexpr int TWO_HANDED_BULK = 5;

class DeathAndDamagePathsTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; damage and the skills ask them. Making them again keeps the ones there
         * are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        m_saved_room_flags = world[0].room_flags;
        /* No crack opens under an earthquake indoors (it would add a room). */
        world[0].room_flags = INDOORS;
        /* A test mob has no prototype (nr -1), and a corpse is marked with its mob's vnum from
         * mob_index[nr]: index -1 is given an entry. */
        m_saved_mob_index = mob_index;
        mob_index = &m_mob_index[1];
    }

    void TearDown() override
    {
        world[0].room_flags = m_saved_room_flags;
        ScriptedRoomFixture::TearDown();
        mob_index = m_saved_mob_index;
    }

    /* Makes the ring and the sword poison whoever wears them. */
    void make_ring_and_sword_poisonous()
    {
        for (int vnum : { RING_VNUM, SWORD_VNUM }) {
            obj_affected_type& affect = prototype_for(vnum).affected[0];
            affect.location = APPLY_BITVECTOR;
            affect.modifier = POISON_BIT_POSITION;
        }
    }

    /* Takes the player out of the room, so that area spells and skills leave it alone. */
    void take_player_out_of_the_room() { char_from_room(&m_player); }

    /* The objects lying in room 0 that are corpses. */
    static int count_corpses_in_room()
    {
        int corpses = 0;
        for (const obj_data* content = world[0].contents; content; content = content->next_content) {
            if (GET_ITEM_TYPE(content) == ITEM_CONTAINER && content->obj_flags.value[3] == 1) {
                ++corpses;
            }
        }
        return corpses;
    }

    /* Whether <object> lies on the floor of room 0. */
    static bool is_on_the_floor(const obj_data* object)
    {
        return is_on_content_list(world[0].contents, object);
    }

    /* Makes script SCRIPT_VNUM say "vanish" when its mob is hurt, and script OTHER_SCRIPT_VNUM
     * remove its own mob when it hears that word: a blow at the first removes the second. */
    void install_hurt_then_vanish_scripts()
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { ON_DAMAGE, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
    }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }

    // Room 0's flags as SetUp found them; the tests make the room indoors.
    long m_saved_room_flags = 0;
    // The mob index the tests use: its first entry is index -1, a test mob's.
    index_data m_mob_index[2] {};
    // The mob index SetUp found, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
};

} // namespace

/* ---- putting gear on and taking it off ---- */

/* A poisonous item hurts a healthy mob as it goes on, and stays worn. */
TEST_F(DeathAndDamagePathsTest, APoisonousItemHurtsItsNewWearerAndStaysOn)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);

    run_command_as(wearer, do_wear, "ring");

    ASSERT_TRUE(is_in_game(wearer));
    EXPECT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(MOB_HIT_POINTS - POISON_DAMAGE, GET_HIT(wearer));
    EXPECT_TRUE(IS_AFFECTED(wearer, AFF_POISON));
}

/* The poison's blow sets off the wearer's damage script, which removes the wearer: the wearing
 * stops there. */
TEST_F(DeathAndDamagePathsTest, AWearerItsDamageScriptRemovesAsPoisonTakesHoldIsLetGo)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", SCRIPT_VNUM);
    obj_to_char(make_object(RING_VNUM, 0), wearer);

    run_command_as(wearer, do_wear, "ring");

    EXPECT_FALSE(is_in_game(wearer));
}

/* A mob too hurt to survive the poison dies wielding a two-handed weapon: the wield does not go
 * on to take it in two hands. */
TEST_F(DeathAndDamagePathsTest, AMobThePoisonKillsAsItWieldsIsLetGo)
{
    make_ring_and_sword_poisonous();
    prototype_for(SWORD_VNUM).obj_flags.value[2] = TWO_HANDED_BULK;
    char_data* wielder = make_mob("wielder", 0);
    wielder->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
    obj_data* sword = make_object(SWORD_VNUM, 0);
    obj_to_char(sword, wielder);

    run_command_as(wielder, do_wield, "sword");

    EXPECT_FALSE(is_in_game(wielder));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_TRUE(is_in_object_list(sword));
}

/* Taking a poisonous item off hurts a healthy wearer, who keeps it. */
TEST_F(DeathAndDamagePathsTest, TakingAPoisonousItemOffHurtsTheWearerWhoKeepsIt)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);

    run_command_as(wearer, do_remove, "ring");

    ASSERT_TRUE(is_in_game(wearer));
    EXPECT_EQ(MOB_HIT_POINTS - 2 * POISON_DAMAGE, GET_HIT(wearer));
    EXPECT_EQ(ring, wearer->carrying);
    EXPECT_EQ(nullptr, wearer->equipment[WEAR_FINGER_R]);
}

/* Taking the item off sets off the wearer's damage script, which removes the wearer: the item
 * lies on the floor where the wearer was. */
TEST_F(DeathAndDamagePathsTest, AWearerItsDamageScriptRemovesAsPoisonLeavesDropsTheItem)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->specials.script_number = SCRIPT_VNUM;

    run_command_as(wearer, do_remove, "all");

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_TRUE(is_on_the_floor(ring));
    EXPECT_EQ(nullptr, ring->carried_by);
}

/* A mob slain while it wears a poisonous item is stripped into one corpse: taking the item off
 * the dying mob does not kill it again. */
TEST_F(DeathAndDamagePathsTest, AMobSlainWearingAPoisonousItemLeavesOneCorpse)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;

    raw_kill(wearer, nullptr, 0);

    EXPECT_FALSE(is_in_game(wearer));
    ASSERT_EQ(1, count_corpses_in_room());
    EXPECT_TRUE(is_in_object_list(ring));
    EXPECT_FALSE(is_on_the_floor(ring));
}

/* A mob removed from the game while it wears a poisonous item is not hurt on its way out, so the
 * poison cannot kill it there; the item is left on the floor. */
TEST_F(DeathAndDamagePathsTest, AMobRemovedWearingAPoisonousItemIsRemovedOnce)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;

    extract_char(wearer);

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_TRUE(is_on_the_floor(ring));
}

/* Nobody in no room is hurt: a blow at someone being taken out of the game lands nowhere. */
TEST_F(DeathAndDamagePathsTest, ABlowAtSomeoneInNoRoomLandsNowhere)
{
    char_data* attacker = make_mob("attacker", 0);
    char_data* victim = make_mob("victim", 0);
    char_from_room(victim);

    EXPECT_EQ(0, damage(attacker, victim, MOB_HIT_POINTS * 2, TYPE_HIT, 0));

    EXPECT_TRUE(is_in_game(victim));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(victim));
}

/* ---- dying of negative regeneration ---- */

/* A mob whose regeneration drains its last hit points dies of it, and its death is noted
 * without touching what is left of it. */
TEST_F(DeathAndDamagePathsTest, AMobNegativeRegenerationKillsIsLetGo)
{
    char_data* drained = make_mob("drained", 0);
    drained->tmpabilities.hit = 0;
    drained->points.health_regen = -10 * MOB_HIT_POINTS;

    fast_update();

    EXPECT_FALSE(is_in_game(drained));
    EXPECT_EQ(1, count_corpses_in_room());
}

/* ---- spells ---- */

/* The shout's blow at the first mob sets off a script that removes the mob after it: the shout
 * strikes the first and stops there. */
TEST_F(DeathAndDamagePathsTest, AShoutOfPainStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(second, first->next_in_room);

    spell_shout_of_pain(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(first));
    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
}

/* The shout's blow at the first mob sets off its damage script, which removes the caster: the
 * shout ends with the caster, and the mob after it is not struck. */
TEST_F(DeathAndDamagePathsTest, AShoutOfPainEndsWithTheCasterItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", 0);

    spell_shout_of_pain(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(first));
    ASSERT_TRUE(is_in_game(second));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(second));
}

/* As for the shout: an earthquake stops at the mob its blow removes. */
TEST_F(DeathAndDamagePathsTest, AnEarthquakeStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(second, first->next_in_room);

    spell_earthquake(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
}

/* A fire mage's fireball removes its target (the target's damage script): the rest of the room
 * is spared as the target, friendly to the mage, decided before it went. */
TEST_F(DeathAndDamagePathsTest, AFireMagesFireballGoesOnAfterRemovingItsTarget)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    if (m_player.profs == nullptr) {
        CREATE1(m_player.profs, char_prof_data);
    }
    m_player.profs->specialization = static_cast<int>(game_types::PS_Fire);
    m_player.tmpabilities.hit = MOB_HIT_POINTS;
    char_data* target = make_mob("target", SCRIPT_VNUM);
    char_data* bystander = make_mob("bystander", 0);

    spell_fireball(&m_player, empty_argument(), SPELL_TYPE_SPELL, target, nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(target));
    ASSERT_TRUE(is_in_game(bystander));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(bystander));
}

/* A blaze burning in the room strikes the first mob, whose script removes the mob after it: the
 * blaze burns nobody more this time. */
TEST_F(DeathAndDamagePathsTest, ARoomBlazeStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(first, world[0].people);
    ASSERT_EQ(second, first->next_in_room);
    affected_type blaze {};
    blaze.type = ROOMAFF_SPELL;
    blaze.duration = 5;
    blaze.modifier = 10;
    blaze.location = SPELL_BLAZE;
    affect_to_room(&world[0], &blaze);
    push_test_random_value(0.99); /* whether a mist would move: there is none */
    push_test_random_value(0.0); /* the 1 in 13 chance that the blaze strikes the first */

    affect_update_room(&world[0]);

    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
    while (world[0].affected) {
        affect_remove_room(&world[0], world[0].affected);
    }
}

/* A mob finishes casting a shout of pain whose blow sets off a script that removes it: the cast
 * ends there, leaving the delay that went with the caster alone. */
TEST_F(DeathAndDamagePathsTest, ACastEndsWithTheCasterItsSpellRemoves)
{
    assign_command_pointers();
    take_player_out_of_the_room();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* caster = make_mob("caster", 0);
    caster->player.level = 50; /* a mob this strong never fumbles the spell */
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    char line[] = "cast 'shout of pain'";
    command_interpreter(caster, line, nullptr);
    ASSERT_GT(caster->delay.wait_value, 0) << "the cast did not start";

    complete_delay(caster);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(victim));
}

/* ---- orders ---- */

/* Each follower ordered to speak is removed by what it says (a mob that removes whoever speaks):
 * the order goes on from one removed follower to the next. */
TEST_F(DeathAndDamagePathsTest, AnOrderGoesOnPastAFollowerItRemoves)
{
    assign_command_pointers();
    install_script(ON_HEAR_SAY,
        { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } }, { SCRIPT_BEGIN, nullptr, {} },
            { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } }, { SCRIPT_END, nullptr, {} } });
    make_mob("warden", SCRIPT_VNUM);
    char_data* followers[2] = { make_mob("first", 0), make_mob("second", 0) };
    for (char_data* follower : followers) {
        add_follower(follower, &m_player, FOLLOW_MOVE);
        SET_BIT(follower->specials.affected_by, AFF_CHARM);
    }

    run_command(do_order, "followers say vanish");

    EXPECT_FALSE(is_in_game(followers[0]));
    EXPECT_FALSE(is_in_game(followers[1]));
    EXPECT_EQ(nullptr, m_player.followers);
}

/* ---- an NPC olog-hai's skills ---- */

/* An NPC has no tactics; its olog-hai skills strike as on normal tactics. */
TEST_F(DeathAndDamagePathsTest, AnNpcOlogHaiStrikesAsOnNormalTactics)
{
    char_data* olog = make_mob("olog", 0);
    olog->player.race = RACE_OLOGHAI;
    const int prob = 50;
    const int expected = (2 + utils::get_prof_level(PROF_WARRIOR, *olog)) * (100 + prob)
        / (1000 / TACTICS_NORMAL);

    EXPECT_EQ(expected, olog_hai::get_base_skill_damage(*olog, prob));
}

/* An NPC olog-hai's cleave hurts the mob next to it. */
TEST_F(DeathAndDamagePathsTest, AnNpcOlogHaiCleaveHurtsItsVictim)
{
    take_player_out_of_the_room();
    char_data* olog = make_mob("olog", 0);
    olog->player.race = RACE_OLOGHAI;
    olog->points.OB = 200; /* sure to land; a mob knows every skill at 80 */
    equip_char(olog, make_object(SWORD_VNUM, 0), WIELD);
    char_data* victim = make_mob("victim", 0);
    push_test_random_value(0.99); /* the cleave's to-hit roll: it lands */

    do_cleave(olog, empty_argument(), nullptr, 0, 0);

    EXPECT_LT(GET_HIT(victim), MOB_HIT_POINTS);
}
