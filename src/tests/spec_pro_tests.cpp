// Tests for the zone special procedures in spec_pro.cpp. A test that needs a roll to go one way
// queues it with push_test_random_value.
#include "scripted_room_fixture.h"

#include "../spec_pro.h"
#include "../spells.h"
#include "test_cleanup.h"

#include <memory>
#include <string>

SPECIAL(ar_tarthalon);
SPECIAL(ghoul);
SPECIAL(vampire_killer);
SPECIAL(vampire_doorkeep);
SPECIAL(wolf_summoner);
SPECIAL(reciter);
SPECIAL(herald);
SPECIAL(vampire_huntress);
SPECIAL(thuringwethil);

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

// Rolls that number(from, to) turns into its lowest and its highest result.
constexpr double LOWEST_ROLL = 0.0;
constexpr double HIGHEST_ROLL = 0.999;

// spell_list's columns, as get_mob_spell_type picks them.
constexpr int PLAIN_MAGE = 0;
constexpr int ORC_MAGE = 1;
constexpr int MAGUS = 2;

// How mage_spell_for_roll picks one of spell_list's SPELL_ROWS rows: a roll up to
// ROLL_STEP_CHANGE moves one row every LOW_ROLL_STEP levels, a higher roll one every
// HIGH_ROLL_STEP.
constexpr int SPELL_ROWS = 10;
constexpr int LAST_SPELL_ROW = SPELL_ROWS - 1;
constexpr int ROLL_STEP_CHANGE = 50;
constexpr int LOW_ROLL_STEP = 5;
constexpr int HIGH_ROLL_STEP = 8;
// A roll far past the last row, as a mage above level 80 can make.
constexpr int FAR_PAST_LAST_ROW_ROLL = 93;

// The level and every stat of a test mob prototype, as the fixture gives its test mobs.
constexpr int PROTOTYPE_LEVEL = 10;
constexpr int PROTOTYPE_STAT = 18;

// A door's key number when it has no keyhole (do_lock and do_unlock find none).
constexpr int NO_KEYHOLE = -1;

// Ar-Tarthalon's chamber, which has no step on his wights' route, and a room on it from which a
// wight steps east (cmd 2).
constexpr int AR_TARTHALON_CHAMBER_VNUM = 8483;
constexpr int WIGHT_ROUTE_EAST_VNUM = 8482;

// The two cells of Thuringwethil's dungeon, which the vampire killer watches.
constexpr int LOWER_CELL_VNUM = 15398;
constexpr int UPPER_CELL_VNUM = 15399;
// The two rooms on the vampire killer's route from which he goes south through a cell's door:
// from the first when the prisoner is in the lower cell, from the second when in the upper.
constexpr int FIRST_CELL_DOOR_ROOM_VNUM = 15396;
constexpr int SECOND_CELL_DOOR_ROOM_VNUM = 15397;
// The room past room SECOND_ROOM, for a test that needs three.
constexpr int THIRD_ROOM = 2;
// The two rooms of Thuringwethil's tower whose shared door the vampire doorkeeper keeps.
constexpr int DOORKEEPERS_NORTH_ROOM_VNUM = 15345;
constexpr int DOORKEEPERS_SOUTH_ROOM_VNUM = 15355;

// The wolf summoner (3948) and the helper it summons, the next vnum.
constexpr int SUMMONER_VNUM = 3948;
constexpr int HELPER_VNUM = SUMMONER_VNUM + 1;

// The ghoul the ghoul special raises, which sinks back into the ground when idle.
constexpr int RISEN_GHOUL_VNUM = 15300;

// Enough movement points for any step.
constexpr int PLENTY_OF_MOVES = 100;

// Thuringwethil's bat form (the Vampire Huntress) and her human form, and the tower room both fly
// back to.
constexpr int THURINGWETHIL_BAT_VNUM = 15301;
constexpr int THURINGWETHIL_HUMAN_VNUM = 15302;
constexpr int VAMPIRE_TOWER_VNUM = 15379;
// The most hit points Thuringwethil can have and still be beaten.
constexpr int THURINGWETHIL_BEATEN_HIT_POINTS = 500;
// Hit points of a Vampire Huntress badly enough hurt to flee (below a fifth of MOB_HIT_POINTS).
constexpr int FLEEING_HUNTRESS_HIT_POINTS = 50;

// A poem the reciter must only read: it lives in read-only memory, so a write into it faults.
const char READ_ONLY_POEM[] = "First line\n\rSecond line\n\r";
// A poem whose one line ends in a lone newline.
const char NEWLINE_ONLY_POEM[] = "Only line\n";

// Runs spec_pro.cpp's zone special procedures in a scripted room with a mob table of its own.
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
        room_data& third_room = world[THIRD_ROOM];
        m_saved_third_room.number = third_room.number;
        m_saved_third_room.zone = third_room.zone;
        m_saved_third_room.light = third_room.light;
        m_saved_third_room.people = third_room.people;
        m_saved_third_room.contents = third_room.contents;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            m_saved_third_room.exits[direction] = third_room.dir_option[direction];
        }
    }

    void TearDown() override
    {
        // The fixture extracts the mobs, and read_mobile's copies count down in this table.
        ScriptedRoomFixture::TearDown();
        world[0].ex_description = m_saved_extra_descriptions[0];
        world[SECOND_ROOM].ex_description = m_saved_extra_descriptions[1];
        room_data& third_room = world[THIRD_ROOM];
        third_room.number = m_saved_third_room.number;
        third_room.zone = m_saved_third_room.zone;
        third_room.light = m_saved_third_room.light;
        third_room.people = m_saved_third_room.people;
        third_room.contents = m_saved_third_room.contents;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            third_room.dir_option[direction] = m_saved_third_room.exits[direction];
        }
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        m_player.player.title = nullptr;
        for (int position = 0; position < m_mob_prototype_count; ++position) {
            release_cleared_character(&m_mob_prototypes[position]);
        }
    }

    // Makes the mob table these vnums, in ascending order, each a plain NPC prototype on the
    // heap, so a read before the first one is outside any object.
    void install_mob_table(std::initializer_list<int> virtual_numbers)
    {
        for (int position = 0; position < m_mob_prototype_count; ++position) {
            release_cleared_character(&m_mob_prototypes[position]);
        }
        const int count = static_cast<int>(virtual_numbers.size());
        m_mob_prototypes = std::make_unique<char_data[]>(count);
        m_mob_prototype_count = count;
        m_mob_index = std::make_unique<index_data[]>(count);
        int position = 0;
        for (const int virtual_number : virtual_numbers) {
            char_data& prototype = m_mob_prototypes[position];
            clear_char(&prototype, MOB_ISNPC);
            prototype.specials2.act = MOB_ISNPC;
            prototype.nr = position;
            prototype.player.name = m_helper_name;
            prototype.player.short_descr = m_helper_name;
            prototype.player.level = PROTOTYPE_LEVEL;
            prototype.abilities.str = PROTOTYPE_STAT;
            prototype.abilities.intel = PROTOTYPE_STAT;
            prototype.abilities.wil = PROTOTYPE_STAT;
            prototype.abilities.dex = PROTOTYPE_STAT;
            prototype.abilities.con = PROTOTYPE_STAT;
            prototype.abilities.lea = PROTOTYPE_STAT;
            prototype.abilities.hit = MOB_HIT_POINTS;
            prototype.tmpabilities.hit = MOB_HIT_POINTS;
            prototype.specials.position = POSITION_STANDING;
            prototype.specials.default_pos = POSITION_STANDING;
            m_mob_index[position].virt = virtual_number;
            ++position;
        }
        mob_proto = m_mob_prototypes.get();
        mob_index = m_mob_index.get();
        top_of_mobt = count - 1;
    }

    // A test mob that the game counts as a copy of mob table entry <real_number>. Its own
    // strings stay its own: give it back with release_from_table before it is extracted.
    char_data* make_table_mob(const char* keywords, int real_number)
    {
        char_data* mob = make_mob(keywords, 0);
        mob->nr = real_number;
        return mob;
    }

    // Makes <mob> a test mob with no table entry again, so extracting it frees its strings.
    static void release_from_table(char_data* mob) { mob->nr = -1; }

    // A ghoul the ghoul special raised, as read_mobile makes one: a copy of the only mob table
    // entry, the risen ghoul's, sharing the entry's keywords and proficiencies, which the game
    // leaves alone when it removes the mob.
    char_data* make_risen_ghoul()
    {
        install_mob_table({ RISEN_GHOUL_VNUM });
        char_data* risen = make_table_mob("risen", 0);
        const char_data& prototype = m_mob_prototypes[0];
        RELEASE(risen->player.name);
        RELEASE(risen->player.short_descr);
        RELEASE(risen->profs);
        risen->player.name = prototype.player.name;
        risen->player.short_descr = prototype.player.short_descr;
        risen->profs = prototype.profs;
        return risen;
    }

    // A mob in <room> with the moves to walk anywhere.
    char_data* make_walker(const char* keywords, int room = 0)
    {
        char_data* walker = make_mob(keywords, 0, room);
        walker->abilities.move = PLENTY_OF_MOVES;
        walker->tmpabilities.move = PLENTY_OF_MOVES;
        return walker;
    }

    // Gives room <room> an extra description on the heap, which is what dir_option[-1] reads.
    void give_room_an_extra_description(int room)
    {
        m_room_description = std::make_unique<extra_descr_data>();
        m_room_description->keyword = m_exit_keyword;
        m_room_description->description = m_exit_keyword;
        world[room].ex_description = m_room_description.get();
    }

    // A stone made into a "textscroll" whose poem is <poem>, not placed.
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

    // Brings room THIRD_ROOM into the world after SECOND_ROOM, lit, numbered <virtual_number>
    // and with no exits; TearDown puts back what it held.
    static void add_third_room(int virtual_number)
    {
        top_of_world = THIRD_ROOM;
        room_data& third_room = world[THIRD_ROOM];
        third_room.number = virtual_number;
        third_room.zone = 0;
        third_room.light = 1;
        third_room.people = nullptr;
        third_room.contents = nullptr;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            third_room.dir_option[direction] = nullptr;
        }
    }

    // Runs special procedure <procedure> for <host> on its own turn.
    static int run_own_turn(special_func procedure, char_data* host)
    {
        char no_argument[1] = "";
        return procedure(host, host, 0, no_argument, SPECIAL_SELF, nullptr);
    }

    // Runs special procedure <procedure> for <host>, set off by <actor>.
    static int run_set_off_by(special_func procedure, char_data* host, char_data* actor)
    {
        char no_argument[1] = "";
        return procedure(host, actor, 0, no_argument, SPECIAL_COMMAND, nullptr);
    }

    // How many characters are in the game.
    static int count_characters()
    {
        int count = 0;
        for (const char_data* listed = character_list; listed; listed = listed->next) {
            ++count;
        }
        return count;
    }

    // How many followers <leader> has.
    static int count_followers(const char_data* leader)
    {
        int count = 0;
        for (const follow_type* follower = leader->followers; follower; follower = follower->next) {
            ++count;
        }
        return count;
    }

    // The prototypes of the mob table install_mob_table makes, on the heap.
    std::unique_ptr<char_data[]> m_mob_prototypes;
    // An empty keyword and description for exits and extra descriptions.
    char m_exit_keyword[1] = "";

private:
    // The vnums of the mob table install_mob_table makes.
    std::unique_ptr<index_data[]> m_mob_index;
    // How many prototypes m_mob_prototypes holds, for TearDown to release.
    int m_mob_prototype_count = 0;
    // The prototypes' keywords and short description.
    char m_helper_name[16] = "wolf";
    // The extra description give_room_an_extra_description puts in a room.
    std::unique_ptr<extra_descr_data> m_room_description;
    // The keyword the reciter looks for, and the scroll's poem.
    char m_scroll_name[16] = "textscroll";
    extra_descr_data m_poem_description { };
    // The game state SetUp replaces, put back by TearDown.
    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    extra_descr_data* m_saved_extra_descriptions[2] { };
    // What room THIRD_ROOM held before the test, put back by TearDown.
    struct saved_room {
        // The fields add_third_room changes.
        int number = 0;
        int zone = 0;
        byte light = 0;
        char_data* people = nullptr;
        obj_data* contents = nullptr;
        room_direction_data* exits[NUM_OF_DIRS] {};
    };
    saved_room m_saved_third_room;
};

} // namespace

// ---- the program-4 mage's spell table ----

// The first row's first roll, the last roll of the row before the last, and the last row's first
// roll and its last below fifty.
TEST(MageSpellForRoll, RollsUpToFiftyStepEveryFiveLevels)
{
    EXPECT_EQ(SPELL_MAGIC_MISSILE, mage_spell_for_roll(0, PLAIN_MAGE));
    EXPECT_EQ(SPELL_CONE_OF_COLD,
        mage_spell_for_roll(LAST_SPELL_ROW * LOW_ROLL_STEP - 1, PLAIN_MAGE));
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(LAST_SPELL_ROW * LOW_ROLL_STEP, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SEARING_DARKNESS, mage_spell_for_roll(ROLL_STEP_CHANGE - 1, ORC_MAGE));
}

// The first roll above fifty (row 6), and the last roll of the last row.
TEST(MageSpellForRoll, RollsAboveFiftyStepEveryEightLevels)
{
    EXPECT_EQ(SPELL_FIREBOLT, mage_spell_for_roll(ROLL_STEP_CHANGE + 1, PLAIN_MAGE));
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(SPELL_ROWS * HIGH_ROLL_STEP - 1, PLAIN_MAGE));
}

// Mauglir (level 50) rolls 50 one time in 51.
TEST(MageSpellForRoll, ARollOfFiftyCastsFromTheLastRow)
{
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(ROLL_STEP_CHANGE, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SPEAR_OF_DARKNESS, mage_spell_for_roll(ROLL_STEP_CHANGE, MAGUS));
}

TEST(MageSpellForRoll, ARollPastTheLastRowCastsFromTheLastRow)
{
    EXPECT_EQ(SPELL_FIREBALL, mage_spell_for_roll(SPELL_ROWS * HIGH_ROLL_STEP, PLAIN_MAGE));
    EXPECT_EQ(SPELL_SEARING_DARKNESS, mage_spell_for_roll(FAR_PAST_LAST_ROW_ROLL, ORC_MAGE));
}

// ---- Ar-Tarthalon's wights ----

// Ar-Tarthalon calls Balkazor while Balkazor already stands in the chamber, whose extra
// description sits where a direction below north would be read.
TEST_F(SpecialProcTest, AWightInArTarthalonsChamberTakesNoStep)
{
    world[0].number = AR_TARTHALON_CHAMBER_VNUM;
    give_room_an_extra_description(0);
    char_data* tarthalon = make_mob("tarthalon", 0);
    tarthalon->specials.position = POSITION_FIGHTING;
    char_data* wight = make_walker("balkazor");
    push_test_random_value(LOWEST_ROLL); // he calls for help
    push_test_random_value(HIGHEST_ROLL); // Balkazor, not Karahaz
    push_test_random_value(LOWEST_ROLL); // says nothing more

    run_own_turn(ar_tarthalon, tarthalon);

    EXPECT_EQ(0, wight->in_room);
}

TEST_F(SpecialProcTest, AWightOnTheRouteStepsTowardsArTarthalon)
{
    world[0].number = WIGHT_ROUTE_EAST_VNUM;
    char_data* tarthalon = make_mob("tarthalon", 0, SECOND_ROOM);
    tarthalon->specials.position = POSITION_FIGHTING;
    char_data* wight = make_walker("balkazor");
    push_test_random_value(LOWEST_ROLL); // he calls for help
    push_test_random_value(HIGHEST_ROLL); // Balkazor, not Karahaz

    run_own_turn(ar_tarthalon, tarthalon);

    EXPECT_EQ(SECOND_ROOM, wight->in_room);
}

// ---- the ghouls ----

// The summoning ghoul carries something and the ghoul it dismisses carries nothing.
TEST_F(SpecialProcTest, ADismissedGhoulLeavesTheSummonersBelongingsAlone)
{
    char_data* summoner = make_mob("ghoul", 0);
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_char(stone, summoner);
    char_data* risen = make_risen_ghoul();
    add_follower(risen, summoner, FOLLOW_MOVE);
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed

    run_own_turn(ghoul, summoner);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_EQ(stone, summoner->carrying);
}

TEST_F(SpecialProcTest, WhatADismissedGhoulCarriedFallsToTheGround)
{
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_risen_ghoul();
    add_follower(risen, summoner, FOLLOW_MOVE);
    obj_data* bread = make_object(BREAD_VNUM, 0);
    obj_to_char(bread, risen);
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed

    run_own_turn(ghoul, summoner);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_EQ(0, bread->in_room);
}

// The summoning ghoul and the ghoul it dismisses each carry something: what the dismissed one
// carried falls to the ground, and the summoner keeps its own.
TEST_F(SpecialProcTest, ADismissedGhoulsBelongingsFallAndTheSummonerKeepsItsOwn)
{
    char_data* summoner = make_mob("ghoul", 0);
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_char(stone, summoner);
    char_data* risen = make_risen_ghoul();
    add_follower(risen, summoner, FOLLOW_MOVE);
    obj_data* bread = make_object(BREAD_VNUM, 0);
    obj_to_char(bread, risen);
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed

    run_own_turn(ghoul, summoner);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_EQ(stone, summoner->carrying);
    EXPECT_EQ(nullptr, stone->next_content);
    EXPECT_EQ(0, bread->in_room);
}

// A fighting ghoul calls for help, but the mob table has no risen ghoul: that is logged, and
// nothing rises.
TEST_F(SpecialProcTest, AGhoulWithNoRisenGhoulInTheTableRaisesNothing)
{
    install_mob_table({ SUMMONER_VNUM });
    char_data* summoner = make_mob("ghoul", 0);
    summoner->specials.position = POSITION_FIGHTING;
    const int characters_before = count_characters();
    push_test_random_value(LOWEST_ROLL); // it calls for help

    testing::internal::CaptureStderr();
    run_own_turn(ghoul, summoner);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(characters_before, count_characters());
    EXPECT_EQ(0, count_followers(summoner));
    EXPECT_NE(std::string::npos, logged.find("ghoul: the risen ghoul does not exist!")) << logged;
}

// ---- the vampire killer ----

// Nobody is in either cell, so he would leave the upper cell by its north door, but the cell has
// no north exit: that is logged, and he stays.
TEST_F(SpecialProcTest, TheVampireKillerInACellWithNoNorthExitLogsAndStays)
{
    world[0].number = LOWER_CELL_VNUM;
    world[SECOND_ROOM].number = UPPER_CELL_VNUM;
    char_from_room(&m_player);
    char_data* killer = make_walker("killer", SECOND_ROOM);

    testing::internal::CaptureStderr();
    run_own_turn(vampire_killer, killer);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(SECOND_ROOM, killer->in_room);
    EXPECT_NE(std::string::npos, logged.find("vampire_killer: his cell has no north exit"))
        << logged;
}

// Nobody is in either cell, so he leaves the upper cell through its closed door.
TEST_F(SpecialProcTest, TheVampireKillerInAnEmptyCellLeavesThroughItsDoor)
{
    world[0].number = LOWER_CELL_VNUM;
    world[SECOND_ROOM].number = UPPER_CELL_VNUM;
    char_from_room(&m_player);
    char door_keyword[] = "irondoor";
    room_direction_data cell_door { };
    cell_door.keyword = door_keyword;
    cell_door.general_description = m_exit_keyword;
    cell_door.key = NO_KEYHOLE;
    cell_door.to_room = 0;
    cell_door.exit_info = EX_ISDOOR | EX_CLOSED;
    world[SECOND_ROOM].dir_option[NORTH] = &cell_door;
    char_data* killer = make_walker("killer", SECOND_ROOM);

    run_own_turn(vampire_killer, killer);

    EXPECT_EQ(0, killer->in_room);
}

// A player waits in the upper cell while he stands in the lower one, which is off his route.
TEST_F(SpecialProcTest, TheVampireKillerOffHisRouteTakesNoStep)
{
    world[0].number = LOWER_CELL_VNUM;
    world[SECOND_ROOM].number = UPPER_CELL_VNUM;
    give_room_an_extra_description(0);
    char_from_room(&m_player);
    char_to_room(&m_player, SECOND_ROOM);
    char_data* killer = make_walker("killer");
    push_test_random_value(LOWEST_ROLL); // he goes after the prisoner

    run_own_turn(vampire_killer, killer);

    EXPECT_EQ(0, killer->in_room);
}

// <killer> stands in room 0, numbered <door_room_vnum>, and <prisoner> waits in the cell he goes
// to through its south door (room <prisoner_room>), but room 0 has no south exit: that is
// logged, and he takes no step.
static void expect_killer_at_a_missing_cell_door_logs(
    int door_room_vnum, int prisoner_room, char_data& prisoner, char_data* killer)
{
    char_from_room(&prisoner);
    char_to_room(&prisoner, prisoner_room);
    push_test_random_value(LOWEST_ROLL); // he goes after the prisoner

    testing::internal::CaptureStderr();
    char no_argument[1] = "";
    vampire_killer(killer, killer, 0, no_argument, SPECIAL_SELF, nullptr);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, killer->in_room);
    const std::string expected
        = "vampire_killer: room " + std::to_string(door_room_vnum) + " has no south exit";
    EXPECT_NE(std::string::npos, logged.find(expected)) << logged;
}

TEST_F(SpecialProcTest, TheVampireKillerAtTheFirstCellDoorRoomWithNoDoorIsLogged)
{
    world[0].number = FIRST_CELL_DOOR_ROOM_VNUM;
    world[0].dir_option[SOUTH] = nullptr;
    world[SECOND_ROOM].number = LOWER_CELL_VNUM;
    add_third_room(UPPER_CELL_VNUM);
    char_data* killer = make_walker("killer");

    expect_killer_at_a_missing_cell_door_logs(
        FIRST_CELL_DOOR_ROOM_VNUM, SECOND_ROOM, m_player, killer);
}

TEST_F(SpecialProcTest, TheVampireKillerAtTheSecondCellDoorRoomWithNoDoorIsLogged)
{
    world[0].number = SECOND_CELL_DOOR_ROOM_VNUM;
    world[0].dir_option[SOUTH] = nullptr;
    world[SECOND_ROOM].number = LOWER_CELL_VNUM;
    add_third_room(UPPER_CELL_VNUM);
    char_data* killer = make_walker("killer");

    expect_killer_at_a_missing_cell_door_logs(
        SECOND_CELL_DOOR_ROOM_VNUM, THIRD_ROOM, m_player, killer);
}

// ---- the vampire doorkeeper ----

// The door the doorkeeper keeps has no north side in the room south of it: that is logged, and
// the side there is is left alone.
TEST_F(SpecialProcTest, TheVampireDoorkeepersDoorMissingASideIsLogged)
{
    world[0].number = DOORKEEPERS_NORTH_ROOM_VNUM;
    world[SECOND_ROOM].number = DOORKEEPERS_SOUTH_ROOM_VNUM;
    char_data* doorkeeper = make_mob("doorkeeper", 0);

    testing::internal::CaptureStderr();
    run_own_turn(vampire_doorkeep, doorkeeper);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, world[0].dir_option[SOUTH]->exit_info);
    EXPECT_NE(std::string::npos,
        logged.find("vampire_doorkeep: the door between rooms 15345 and 15355 is missing a side"))
        << logged;
}

// ---- the wolf summoner ----

TEST_F(SpecialProcTest, SummoningTwiceKeepsTheHelpersDeathCry)
{
    install_mob_table({ SUMMONER_VNUM, HELPER_VNUM });
    // On the heap, as the world loader leaves it, so freeing it is a real free.
    char* death_cry = str_dup("A wolf howls in answer.");
    m_mob_prototypes[1].player.death_cry2 = death_cry;
    char_data* summoner = make_table_mob("summoner", 0);
    summoner->specials.position = POSITION_FIGHTING;

    push_test_random_value(HIGHEST_ROLL); // it howls
    run_own_turn(wolf_summoner, summoner);
    push_test_random_value(HIGHEST_ROLL); // it howls again
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
    push_test_random_value(HIGHEST_ROLL); // it howls

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
    push_test_random_value(HIGHEST_ROLL); // it howls

    run_own_turn(wolf_summoner, summoner);

    EXPECT_EQ(characters_before, count_characters());
    EXPECT_EQ(0, count_followers(summoner));
    release_from_table(summoner);
}

// ---- the reciter ----

TEST_F(SpecialProcTest, AReciterSpeaksEachLineOfItsPoemWithoutWritingIntoIt)
{
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(READ_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); // it takes up the scroll
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); // the first line
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); // the second line
    run_own_turn(reciter, bard);

    EXPECT_TRUE(output_has("First line"));
    EXPECT_TRUE(output_has("Second line"));
}

TEST_F(SpecialProcTest, AReciterStopsAfterALineEndingInANewlineAlone)
{
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(NEWLINE_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); // it takes up the scroll
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); // the only line
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); // nothing is left
    run_own_turn(reciter, bard);

    EXPECT_EQ(1, count_in_output("Only line"));
    EXPECT_EQ(nullptr, bard->specials.recite_lines);
}

// A mob in the room removes whoever speaks to it, and the reciter speaks its first line.
TEST_F(SpecialProcTest, AReciterRemovedAsItSpeaksIsLetGo)
{
    install_script(ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    make_mob("listener", SCRIPT_VNUM);
    char_data* bard = make_mob("bard", 0);
    obj_to_char(make_scroll(READ_ONLY_POEM), bard);

    push_test_random_value(HIGHEST_ROLL); // it takes up the scroll
    run_own_turn(reciter, bard);
    push_test_random_value(HIGHEST_ROLL); // the first line
    run_own_turn(reciter, bard);

    EXPECT_FALSE(is_in_game(bard));
    EXPECT_TRUE(output_has("First line"));
}

// The reciter carries a stone made just after a scroll lying on the ground, so the stone is
// before the scroll on the world's object list; a character with no hit points sets it off.
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

// ---- the herald ----

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

// ---- Thuringwethil changing form ----

// The Vampire Huntress, badly hurt, flies to her tower to become a woman there, but the mob
// table has no human form: she stays in the tower as the bat, and nothing is read before the
// table.
TEST_F(SpecialProcTest, AFleeingVampireHuntressWithNoHumanFormStaysABat)
{
    install_mob_table({ THURINGWETHIL_BAT_VNUM });
    world[SECOND_ROOM].number = VAMPIRE_TOWER_VNUM;
    char_data* huntress = make_mob("huntress", 0);
    huntress->specials.position = POSITION_FIGHTING;
    huntress->tmpabilities.hit = FLEEING_HUNTRESS_HIT_POINTS;
    const int characters_before = count_characters();

    testing::internal::CaptureStderr();
    const int handled = run_own_turn(vampire_huntress, huntress);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(1, handled);
    ASSERT_TRUE(is_in_game(huntress));
    EXPECT_EQ(SECOND_ROOM, huntress->in_room);
    EXPECT_EQ(characters_before, count_characters());
    EXPECT_NE(std::string::npos, logged.find("vampire_huntress: her human form does not exist!"))
        << logged;
}

// Thuringwethil, healed, melts away to become the bat again, but the mob table has no bat form:
// she leaves, and no bat is made from before the table.
TEST_F(SpecialProcTest, AHealedThuringwethilWithNoBatFormLeavesNothingBehind)
{
    install_mob_table({ THURINGWETHIL_HUMAN_VNUM });
    world[SECOND_ROOM].number = VAMPIRE_TOWER_VNUM;
    char_data* vampire = make_mob("thuringwethil", 0);
    vampire->abilities.hit = 2 * THURINGWETHIL_BEATEN_HIT_POINTS;
    vampire->tmpabilities.hit = 2 * THURINGWETHIL_BEATEN_HIT_POINTS;
    const int characters_before = count_characters();

    testing::internal::CaptureStderr();
    const int handled = run_own_turn(thuringwethil, vampire);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(1, handled);
    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_EQ(characters_before - 1, count_characters());
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
    EXPECT_NE(std::string::npos, logged.find("thuringwethil: her bat form does not exist!"))
        << logged;
}

// ---- the herald, continued ----

// A newcomer with no title is announced with nothing where the title goes.
TEST_F(SpecialProcTest, TheHeraldAnnouncesANewcomerWithNoTitleByNameAlone)
{
    char announcement[] = "Hail %s, %s!";
    char_data* herald_mob = make_mob("herald", 0);
    herald_mob->player.death_cry2 = announcement;
    m_player.player.title = nullptr;

    run_set_off_by(herald, herald_mob, &m_player);

    EXPECT_TRUE(output_has("Hail Tester, !")) << output();
}

// Only the first two "%s" are filled in; a third is kept as written.
TEST_F(SpecialProcTest, TheHeraldKeepsAThirdPlaceholderAsWritten)
{
    char announcement[] = "Hail %s, %s! Beware %s.";
    char title[] = "the Brave";
    char_data* herald_mob = make_mob("herald", 0);
    herald_mob->player.death_cry2 = announcement;
    m_player.player.title = title;

    run_set_off_by(herald, herald_mob, &m_player);

    EXPECT_TRUE(output_has("Hail Tester, the Brave! Beware %s.")) << output();
}
