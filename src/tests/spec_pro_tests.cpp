/* The zone special procedures in spec_pro.cpp that once read or wrote memory they did not own: the
 * program-4 mage's spell table, Ar-Tarthalon's wights, the ghouls, the vampire killer, the wolf summoner, the reciter and the herald. Each runs here in a room of test mobs,
 * and a test that needs a roll to go one way queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../spells.h"

#include <memory>

SPECIAL(ar_tarthalon);
SPECIAL(ghoul);
SPECIAL(vampire_killer);
SPECIAL(wolf_summoner);
SPECIAL(reciter);
SPECIAL(herald);
int mage_spell_for_roll(int level_roll, int spell_type);

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

/* Rolls that number(from, to) turns into its lowest and its highest result. */
constexpr double LOWEST_ROLL = 0.0;
constexpr double HIGHEST_ROLL = 0.999;

/* spell_list's columns, as get_mob_spell_type picks them. */
constexpr int PLAIN_MAGE = 0;
constexpr int ORC_MAGE = 1;
constexpr int MAGUS = 2;

/* Ar-Tarthalon's chamber, which has no step on his wights' route, and a room on it from which a
 * wight steps east (cmd 2). */
constexpr int AR_TARTHALON_CHAMBER_VNUM = 8483;
constexpr int WIGHT_ROUTE_EAST_VNUM = 8482;

/* The two cells of Thuringwethil's dungeon, which the vampire killer watches. */
constexpr int LOWER_CELL_VNUM = 15398;
constexpr int UPPER_CELL_VNUM = 15399;

/* The wolf summoner (3948) and the helper it summons, the next vnum. */
constexpr int SUMMONER_VNUM = 3948;
constexpr int HELPER_VNUM = SUMMONER_VNUM + 1;

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;

/* A poem the reciter must only read: it lives in read-only memory, so a write into it faults. */
const char READ_ONLY_POEM[] = "First line\n\rSecond line\n\r";
/* A poem whose one line ends in a lone newline. */
const char NEWLINE_ONLY_POEM[] = "Only line\n";

class SpecialProcTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_saved_extra_descriptions[0] = world[0].ex_description;
        m_saved_extra_descriptions[1] = world[SECOND_ROOM].ex_description;
    }

    void TearDown() override
    {
        /* The fixture extracts the mobs, and read_mobile's copies count down in this table. */
        ScriptedRoomFixture::TearDown();
        world[0].ex_description = m_saved_extra_descriptions[0];
        world[SECOND_ROOM].ex_description = m_saved_extra_descriptions[1];
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        m_player.player.title = nullptr;
    }

    /* Makes the mob table these vnums, in ascending order, each a plain NPC prototype on the
     * heap, so a read before the first one is outside any object. */
    void install_mob_table(std::initializer_list<int> vnums)
    {
        const int count = static_cast<int>(vnums.size());
        m_mob_prototypes = std::make_unique<char_data[]>(count);
        m_mob_index = std::make_unique<index_data[]>(count);
        int position = 0;
        for (const int vnum : vnums) {
            char_data& prototype = m_mob_prototypes[position];
            clear_char(&prototype, MOB_ISNPC);
            prototype.specials2.act = MOB_ISNPC;
            prototype.nr = position;
            prototype.player.name = m_helper_name;
            prototype.player.short_descr = m_helper_name;
            prototype.player.level = 10;
            prototype.abilities.str = 18;
            prototype.abilities.intel = 18;
            prototype.abilities.wil = 18;
            prototype.abilities.dex = 18;
            prototype.abilities.con = 18;
            prototype.abilities.lea = 18;
            prototype.abilities.hit = MOB_HIT_POINTS;
            prototype.tmpabilities.hit = MOB_HIT_POINTS;
            prototype.specials.position = POSITION_STANDING;
            prototype.specials.default_pos = POSITION_STANDING;
            m_mob_index[position].virt = vnum;
            ++position;
        }
        mob_proto = m_mob_prototypes.get();
        mob_index = m_mob_index.get();
        top_of_mobt = count - 1;
    }

    /* A test mob that the game counts as a copy of mob table entry <real_number>. Its own
     * strings stay its own: give it back with release_from_table before it is extracted. */
    char_data* make_table_mob(const char* keywords, int real_number)
    {
        char_data* mob = make_mob(keywords, 0);
        mob->nr = real_number;
        return mob;
    }

    /* Makes <mob> a test mob with no table entry again, so extracting it frees its strings. */
    static void release_from_table(char_data* mob) { mob->nr = -1; }

    /* A mob in <room> with the moves to walk anywhere. */
    char_data* make_walker(const char* keywords, int room = 0)
    {
        char_data* walker = make_mob(keywords, 0, room);
        walker->abilities.move = PLENTY_OF_MOVES;
        walker->tmpabilities.move = PLENTY_OF_MOVES;
        return walker;
    }

    /* Gives room <room> an extra description on the heap, which is what dir_option[-1] reads. */
    void give_room_an_extra_description(int room)
    {
        m_room_description = std::make_unique<extra_descr_data>();
        m_room_description->keyword = m_exit_keyword;
        m_room_description->description = m_exit_keyword;
        world[room].ex_description = m_room_description.get();
    }

    /* A stone made into a "textscroll" whose poem is <poem>, not placed. */
    obj_data* make_scroll(const char* poem)
    {
        obj_data* scroll = make_object(STONE_VNUM, 0);
        scroll->name = m_scroll_name;
        m_poem_description.keyword = m_scroll_name;
        m_poem_description.description = const_cast<char*>(poem);
        m_poem_description.next = nullptr;
        scroll->ex_description = &m_poem_description;
        return scroll;
    }

    /* Runs special procedure <procedure> for <host> on its own turn. */
    static int run_own_turn(special_func procedure, char_data* host)
    {
        char no_argument[1] = "";
        return procedure(host, host, 0, no_argument, SPECIAL_SELF, nullptr);
    }

    /* Runs special procedure <procedure> for <host>, set off by <actor>. */
    static int run_set_off_by(special_func procedure, char_data* host, char_data* actor)
    {
        char no_argument[1] = "";
        return procedure(host, actor, 0, no_argument, SPECIAL_COMMAND, nullptr);
    }

    /* How many characters are in the game. */
    static int count_characters()
    {
        int count = 0;
        for (const char_data* listed = character_list; listed; listed = listed->next) {
            ++count;
        }
        return count;
    }

    /* How many followers <leader> has. */
    static int count_followers(const char_data* leader)
    {
        int count = 0;
        for (const follow_type* follower = leader->followers; follower; follower = follower->next) {
            ++count;
        }
        return count;
    }

    // The mob table install_mob_table makes: prototypes on the heap and their vnums.
    std::unique_ptr<char_data[]> m_mob_prototypes;
    std::unique_ptr<index_data[]> m_mob_index;
    // The prototypes' keywords and short description.
    char m_helper_name[16] = "wolf";
    // The extra description give_room_an_extra_description puts in a room.
    std::unique_ptr<extra_descr_data> m_room_description;
    // An empty keyword and description for exits and extra descriptions.
    char m_exit_keyword[1] = "";
    // The keyword the reciter looks for, and the scroll's poem.
    char m_scroll_name[16] = "textscroll";
    extra_descr_data m_poem_description {};

private:
    // The game state SetUp replaces, put back by TearDown.
    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    extra_descr_data* m_saved_extra_descriptions[2] {};
};

} // namespace

/* ---- the program-4 mage's spell table ---- */

TEST(MageSpellForRoll, RollsUpToFiftyStepEveryFiveLevels)
{
    EXPECT_EQ(SPELL_MAGIC_MISSILE, mage_spell_for_roll(0, PLAIN_MAGE));
    EXPECT_EQ(SPELL_CONE_OF_COLD, mage_spell_for_roll(44, PLAIN_MAGE));
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(45, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SEARING_DARKNESS, mage_spell_for_roll(49, ORC_MAGE));
}

TEST(MageSpellForRoll, RollsAboveFiftyStepEveryEightLevels)
{
    EXPECT_EQ(SPELL_FIREBOLT, mage_spell_for_roll(51, PLAIN_MAGE));
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(79, PLAIN_MAGE));
}

/* Mauglir (level 50) rolls 50 one time in 51. */
TEST(MageSpellForRoll, ARollOfFiftyCastsFromTheLastRow)
{
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(50, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SPEAR_OF_DARKNESS, mage_spell_for_roll(50, MAGUS));
}

TEST(MageSpellForRoll, ARollPastTheLastRowCastsFromTheLastRow)
{
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(80, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SEARING_DARKNESS, mage_spell_for_roll(93, ORC_MAGE));
}

/* ---- Ar-Tarthalon's wights ---- */

/* Ar-Tarthalon calls Balkazor while Balkazor already stands in the chamber, whose extra
 * description sits where a direction below north would be read. */
TEST_F(SpecialProcTest, AWightInArTarthalonsChamberTakesNoStep)
{
    world[0].number = AR_TARTHALON_CHAMBER_VNUM;
    give_room_an_extra_description(0);
    char_data* tarthalon = make_mob("tarthalon", 0);
    tarthalon->specials.position = POSITION_FIGHTING;
    char_data* wight = make_walker("balkazor");
    push_test_random_value(LOWEST_ROLL); /* he calls for help */
    push_test_random_value(HIGHEST_ROLL); /* Balkazor, not Karahaz */
    push_test_random_value(LOWEST_ROLL); /* says nothing more */

    run_own_turn(ar_tarthalon, tarthalon);

    EXPECT_EQ(0, wight->in_room);
}

TEST_F(SpecialProcTest, AWightOnTheRouteStepsTowardsArTarthalon)
{
    world[0].number = WIGHT_ROUTE_EAST_VNUM;
    char_data* tarthalon = make_mob("tarthalon", 0, SECOND_ROOM);
    tarthalon->specials.position = POSITION_FIGHTING;
    char_data* wight = make_walker("balkazor");
    push_test_random_value(LOWEST_ROLL); /* he calls for help */
    push_test_random_value(HIGHEST_ROLL); /* Balkazor, not Karahaz */

    run_own_turn(ar_tarthalon, tarthalon);

    EXPECT_EQ(SECOND_ROOM, wight->in_room);
}

/* ---- the ghouls ---- */

/* The summoning ghoul carries something and the ghoul it dismisses carries nothing. */
TEST_F(SpecialProcTest, ADismissedGhoulLeavesTheSummonersBelongingsAlone)
{
    char_data* summoner = make_mob("ghoul", 0);
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_char(stone, summoner);
    char_data* risen = make_mob("risen", 0);
    add_follower(risen, summoner, FOLLOW_MOVE);
    push_test_random_value(LOWEST_ROLL); /* the idle follower is dismissed */

    run_own_turn(ghoul, summoner);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_EQ(stone, summoner->carrying);
}

TEST_F(SpecialProcTest, WhatADismissedGhoulCarriedFallsToTheGround)
{
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_mob("risen", 0);
    add_follower(risen, summoner, FOLLOW_MOVE);
    obj_data* bread = make_object(BREAD_VNUM, 0);
    obj_to_char(bread, risen);
    push_test_random_value(LOWEST_ROLL); /* the idle follower is dismissed */

    run_own_turn(ghoul, summoner);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_EQ(0, bread->in_room);
}

/* ---- the vampire killer ---- */

/* Nobody is in either cell, so he leaves the upper cell through its closed door. */
TEST_F(SpecialProcTest, TheVampireKillerInAnEmptyCellLeavesThroughItsDoor)
{
    world[0].number = LOWER_CELL_VNUM;
    world[SECOND_ROOM].number = UPPER_CELL_VNUM;
    char_from_room(&m_player);
    char door_keyword[] = "irondoor";
    room_direction_data cell_door {};
    cell_door.keyword = door_keyword;
    cell_door.general_description = m_exit_keyword;
    cell_door.key = -1;
    cell_door.to_room = 0;
    cell_door.exit_info = EX_ISDOOR | EX_CLOSED;
    world[SECOND_ROOM].dir_option[NORTH] = &cell_door;
    char_data* killer = make_walker("killer", SECOND_ROOM);

    run_own_turn(vampire_killer, killer);

    EXPECT_EQ(0, killer->in_room);
}

/* A player waits in the upper cell while he stands in the lower one, which is off his route. */
TEST_F(SpecialProcTest, TheVampireKillerOffHisRouteTakesNoStep)
{
    world[0].number = LOWER_CELL_VNUM;
    world[SECOND_ROOM].number = UPPER_CELL_VNUM;
    give_room_an_extra_description(0);
    char_from_room(&m_player);
    char_to_room(&m_player, SECOND_ROOM);
    char_data* killer = make_walker("killer");
    push_test_random_value(LOWEST_ROLL); /* he goes after the prisoner */

    run_own_turn(vampire_killer, killer);

    EXPECT_EQ(0, killer->in_room);
}

/* ---- the wolf summoner ---- */

TEST_F(SpecialProcTest, SummoningTwiceKeepsTheHelpersDeathCry)
{
    install_mob_table({ SUMMONER_VNUM, HELPER_VNUM });
    /* On the heap, as the world loader leaves it, so freeing it is a real free. */
    char* death_cry = str_dup("A wolf howls in answer.");
    m_mob_prototypes[1].player.death_cry2 = death_cry;
    char_data* summoner = make_table_mob("summoner", 0);
    summoner->specials.position = POSITION_FIGHTING;

    push_test_random_value(HIGHEST_ROLL); /* it howls */
    run_own_turn(wolf_summoner, summoner);
    push_test_random_value(HIGHEST_ROLL); /* it howls again */
    run_own_turn(wolf_summoner, summoner);

    EXPECT_EQ(2, count_followers(summoner));
    EXPECT_EQ(death_cry, m_mob_prototypes[1].player.death_cry2);
    EXPECT_EQ(2, count_in_output("A wolf howls in answer."));
    for (const follow_type* follower = summoner->followers; follower; follower = follower->next) {
        EXPECT_EQ(nullptr, follower->follower->player.death_cry2);
    }
    release_from_table(summoner);
    RELEASE(m_mob_prototypes[1].player.death_cry2);
}

TEST_F(SpecialProcTest, ASummonerWithNoHelperInTheTableSummonsNothing)
{
    install_mob_table({ SUMMONER_VNUM });
    char_data* summoner = make_table_mob("summoner", 0);
    summoner->specials.position = POSITION_FIGHTING;
    const int characters_before = count_characters();
    push_test_random_value(HIGHEST_ROLL); /* it howls */

    run_own_turn(wolf_summoner, summoner);

    EXPECT_EQ(characters_before, count_characters());
    EXPECT_EQ(0, count_followers(summoner));
    release_from_table(summoner);
}

TEST_F(SpecialProcTest, AHelperWithNoDeathCryIsNotLeftInTheGame)
{
    install_mob_table({ SUMMONER_VNUM, HELPER_VNUM });
    char_data* summoner = make_table_mob("summoner", 0);
    summoner->specials.position = POSITION_FIGHTING;
    const int characters_before = count_characters();
    push_test_random_value(HIGHEST_ROLL); /* it howls */

    run_own_turn(wolf_summoner, summoner);

    EXPECT_EQ(characters_before, count_characters());
    EXPECT_EQ(0, count_followers(summoner));
    release_from_table(summoner);
}

/* ---- the reciter ---- */

TEST_F(SpecialProcTest, AReciterSpeaksEachLineOfItsPoemWithoutWritingIntoIt)
{
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(READ_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); /* it takes up the scroll */
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); /* the first line */
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); /* the second line */
    run_own_turn(reciter, bard);

    EXPECT_TRUE(output_has("First line"));
    EXPECT_TRUE(output_has("Second line"));
}

TEST_F(SpecialProcTest, AReciterStopsAfterALineEndingInANewlineAlone)
{
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(NEWLINE_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); /* it takes up the scroll */
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); /* the only line */
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); /* nothing is left */
    run_own_turn(reciter, bard);

    EXPECT_EQ(1, count_in_output("Only line"));
    EXPECT_EQ(nullptr, bard->specials.recite_lines);
}

/* A mob in the room removes whoever speaks to it, and the reciter speaks its first line. */
TEST_F(SpecialProcTest, AReciterRemovedAsItSpeaksIsLetGo)
{
    install_script(ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    make_mob("listener", SCRIPT_VNUM);
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(READ_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); /* it takes up the scroll */
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); /* the first line */
    run_own_turn(reciter, bard);

    EXPECT_FALSE(is_in_game(bard));
    EXPECT_TRUE(output_has("First line"));
}

/* The reciter carries a stone made just after a scroll lying on the ground, so the stone is
 * before the scroll on the world's object list; a character with no hit points sets it off. */
TEST_F(SpecialProcTest, AReciterNeverTakesAScrollItDoesNotCarry)
{
    obj_data* scroll = make_scroll(READ_ONLY_POEM);
    obj_to_room(scroll, 0);
    char_data* bard = make_mob("bard", 0);
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_char(stone, bard);
    m_player.tmpabilities.hit = 0;

    run_set_off_by(reciter, bard, &m_player);

    EXPECT_TRUE(is_in_object_list(scroll));
    EXPECT_EQ(0, scroll->in_room);
    EXPECT_EQ(stone, bard->carrying);
}

/* ---- the herald ---- */

TEST_F(SpecialProcTest, TheHeraldAnnouncesANewcomersNameAndTitle)
{
    char announcement[] = "Hail %s, %s! Half of all heroes, 50%%, fall here.";
    char title[] = "the Brave";
    char_data* herald_mob = make_mob("herald", 0);
    herald_mob->player.death_cry2 = announcement;
    m_player.player.title = title;

    run_set_off_by(herald, herald_mob, &m_player);

    EXPECT_TRUE(output_has("Hail Tester, the Brave! Half of all heroes, 50%, fall here."))
        << output();
}

TEST_F(SpecialProcTest, TheHeraldKeepsAStrayPercentSignAsWritten)
{
    char announcement[] = "Hail %s, %s! I am 100% sure.";
    char title[] = "the Brave";
    char_data* herald_mob = make_mob("herald", 0);
    herald_mob->player.death_cry2 = announcement;
    m_player.player.title = title;

    run_set_off_by(herald, herald_mob, &m_player);

    EXPECT_TRUE(output_has("Hail Tester, the Brave! I am 100% sure.")) << output();
}
