/* A player with a link who dies as a stay-zone item comes off on the way to another zone: the
 * death sends them to their start room, and the move or spell that was taking them on stops.
 *
 * The item is a ring that gives its wearer the poison flag (APPLY_BITVECTOR), so taking it off
 * hurts the wearer; a wearer with fewer hit points than the poison deals dies of it. */
#include "scripted_room_fixture.h"

#include "../act_move.h"
#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <filesystem>
#include <string>
#include <system_error>

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

extern struct weather_data weather_info;
extern int top_of_p_table;
extern int r_mortal_start_room[];

using namespace scripted_room;

namespace {

/* The poison bit, as the APPLY_BITVECTOR modifier that gives it (the bit's position). */
constexpr int POISON_BIT_POSITION = 11;
/* Few enough hit points that the poison kills. */
constexpr int FEWER_HIT_POINTS_THAN_THE_POISON = 3;
/* The player's constitution; the fixture gives none. */
constexpr int UNHURT_CONSTITUTION = 18;
/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;
/* The room a human player comes back to life in; neither room 0 nor the second room. */
constexpr int START_ROOM = 2;
constexpr int START_ROOM_VNUM = 5102;
/* The zone the second room is put in, so that going there leaves room 0's zone. */
constexpr int OTHER_ZONE = 1;

/* Runs the test in a fresh, empty temporary folder, so whatever a death saves lands there and
 * goes with it. */
class ScopedTemporaryWorkingFolder {
public:
    ScopedTemporaryWorkingFolder()
    {
        std::error_code error;
        m_original = std::filesystem::current_path(error);
        if (error) {
            ADD_FAILURE() << "no current folder: " << error.message();
            return;
        }
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        if (error) {
            ADD_FAILURE() << "no temporary folder: " << error.message();
            return;
        }
        const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_folder = temporary_root / (std::string("rots_stay_zone_death_") + test->name());
        std::filesystem::remove_all(m_folder, error);
        std::filesystem::create_directories(m_folder, error);
        if (error) {
            ADD_FAILURE() << "cannot make " << m_folder << ": " << error.message();
            return;
        }
        std::filesystem::current_path(m_folder, error);
        if (error) {
            ADD_FAILURE() << "cannot enter " << m_folder << ": " << error.message();
        }
    }

    ~ScopedTemporaryWorkingFolder()
    {
        std::error_code error;
        if (!m_original.empty()) {
            std::filesystem::current_path(m_original, error);
        }
        if (!m_folder.empty()) {
            std::filesystem::remove_all(m_folder, error);
        }
    }

    ScopedTemporaryWorkingFolder(const ScopedTemporaryWorkingFolder&) = delete;
    ScopedTemporaryWorkingFolder& operator=(const ScopedTemporaryWorkingFolder&) = delete;

private:
    // The working folder before the test, entered again when it ends.
    std::filesystem::path m_original;
    // The temporary folder the test runs in, removed when it ends.
    std::filesystem::path m_folder;
};

class StayZoneDeathTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        /* Made at boot; damage and a death report to them. Making them again keeps the ones
         * there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        /* No player table: a death's save finds nobody to save. */
        m_saved_top_of_p_table = top_of_p_table;
        top_of_p_table = -1;

        zone_table = m_two_zones;
        m_saved_second_room_zone = world[SECOND_ROOM].zone;
        m_saved_second_room_flags = world[SECOND_ROOM].room_flags;
        world[SECOND_ROOM].zone = OTHER_ZONE;
        world[SECOND_ROOM].room_flags = 0;

        m_saved_start_room.number = world[START_ROOM].number;
        m_saved_start_room.zone = world[START_ROOM].zone;
        m_saved_start_room.people = world[START_ROOM].people;
        m_saved_start_room.contents = world[START_ROOM].contents;
        m_saved_start_room.name = world[START_ROOM].name;
        m_saved_start_room.description = world[START_ROOM].description;
        world[START_ROOM].number = START_ROOM_VNUM;
        world[START_ROOM].zone = 0;
        world[START_ROOM].people = nullptr;
        world[START_ROOM].contents = nullptr;
        world[START_ROOM].name = m_room_name;
        world[START_ROOM].description = m_room_description;
        top_of_world = START_ROOM;
        m_saved_mortal_start_room = r_mortal_start_room[RACE_HUMAN];
        r_mortal_start_room[RACE_HUMAN] = START_ROOM;

        /* Players look at the rooms they come to. */
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            m_saved_room_names[room] = world[room].name;
            m_saved_room_descriptions[room] = world[room].description;
            world[room].name = m_room_name;
            world[room].description = m_room_description;
        }

        /* Healthy enough to survive the poison as the ring goes on. A player's unhurt stats are
         * worked out again from their permanent ones as gear goes on, and a step weighs the
         * stats against the unhurt ones. */
        m_player.constabilities.str = m_player.tmpabilities.str;
        m_player.constabilities.dex = m_player.tmpabilities.dex;
        m_player.constabilities.intel = m_player.tmpabilities.intel;
        m_player.constabilities.con = UNHURT_CONSTITUTION;
        m_player.constabilities.hit = MOB_HIT_POINTS;
        m_player.constabilities.move = PLENTY_OF_MOVES;
        m_player.abilities = m_player.constabilities;
        m_player.tmpabilities.con = UNHURT_CONSTITUTION;
        m_player.tmpabilities.hit = MOB_HIT_POINTS;
        m_player.tmpabilities.move = PLENTY_OF_MOVES;
    }

    void TearDown() override
    {
        /* The dead player came back to life in the start room; the fixture puts back rooms 0 and
         * 1 only. */
        if (m_player.in_room != NOWHERE) {
            char_from_room(&m_player);
        }
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            world[room].name = m_saved_room_names[room];
            world[room].description = m_saved_room_descriptions[room];
        }
        world[SECOND_ROOM].zone = m_saved_second_room_zone;
        world[SECOND_ROOM].room_flags = m_saved_second_room_flags;
        ScriptedRoomFixture::TearDown();
        r_mortal_start_room[RACE_HUMAN] = m_saved_mortal_start_room;
        world[START_ROOM].number = m_saved_start_room.number;
        world[START_ROOM].zone = m_saved_start_room.zone;
        world[START_ROOM].people = m_saved_start_room.people;
        world[START_ROOM].contents = m_saved_start_room.contents;
        world[START_ROOM].name = m_saved_start_room.name;
        world[START_ROOM].description = m_saved_start_room.description;
        top_of_p_table = m_saved_top_of_p_table;
    }

    /* Puts on the player a stay-zone ring that poisons its wearer, and leaves the player too
     * hurt to survive the poison as the ring comes off. Returns the ring. */
    obj_data* wear_deadly_stay_zone_ring()
    {
        obj_data& prototype = prototype_for(RING_VNUM);
        prototype.affected[0].location = APPLY_BITVECTOR;
        prototype.affected[0].modifier = POISON_BIT_POSITION;
        prototype.obj_flags.extra_flags |= ITEM_STAY_ZONE;
        obj_data* ring = make_object(RING_VNUM, 0);
        obj_to_char(ring, &m_player);
        run_command(do_wear, "ring");
        EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]) << output();
        m_player.tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
        return ring;
    }

    /* Whether <object> lies on the floor of room 0. */
    static bool is_on_the_floor_of_room_zero(const obj_data* object)
    {
        return is_on_content_list(world[0].contents, object);
    }

    /* A writable empty argument for the commands and spells. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }

    // Holds whatever the deaths save.
    ScopedTemporaryWorkingFolder m_working_folder;
    // Room 0's zone and the second room's, which the tests make different.
    zone_data m_two_zones[2] {};
    // The name and description every room the tests use has during the test.
    char m_room_name[16] = "A room";
    char m_room_description[16] = "A room.\n\r";

    /* What SetUp found in the start room, put back by TearDown. */
    struct saved_start_room {
        // The fields the tests change.
        int number = 0;
        int zone = 0;
        char_data* people = nullptr;
        obj_data* contents = nullptr;
        char* name = nullptr;
        char* description = nullptr;
    };

    // The game state SetUp replaces, put back by TearDown.
    saved_start_room m_saved_start_room;
    char* m_saved_room_names[SECOND_ROOM + 1] {};
    char* m_saved_room_descriptions[SECOND_ROOM + 1] {};
    int m_saved_second_room_zone = 0;
    long m_saved_second_room_flags = 0;
    int m_saved_mortal_start_room = 0;
    int m_saved_top_of_p_table = 0;
};

} // namespace

/* The death stops the move: the ring stays where the player fell, and the player is where the
 * death sent them. */
TEST_F(StayZoneDeathTest, LeavingTheZoneStopsWhenTheStayZoneItemKillsThePlayer)
{
    obj_data* ring = wear_deadly_stay_zone_ring();

    EXPECT_FALSE(prohibit_item_stay_zone_move(&m_player, 0));

    EXPECT_EQ(START_ROOM, m_player.in_room);
    EXPECT_TRUE(is_on_the_floor_of_room_zero(ring));
}

TEST_F(StayZoneDeathTest, APlayerTheStayZoneItemKillsAsTheyWalkOutDoesNotArrive)
{
    obj_data* ring = wear_deadly_stay_zone_ring();

    do_move(&m_player, empty_argument(), nullptr, NORTH + 1, 0);

    EXPECT_EQ(START_ROOM, m_player.in_room);
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
    EXPECT_TRUE(is_on_the_floor_of_room_zero(ring));
}

TEST_F(StayZoneDeathTest, APlayerTheStayZoneItemKillsAsTheyAreSummonedIsNotSummoned)
{
    char_data* caster = make_mob("caster", 0, SECOND_ROOM);
    obj_data* ring = wear_deadly_stay_zone_ring();

    spell_summon(caster, empty_argument(), SPELL_TYPE_SPELL, &m_player, nullptr, 0, 0);

    EXPECT_EQ(START_ROOM, m_player.in_room);
    EXPECT_EQ(caster, world[SECOND_ROOM].people);
    EXPECT_EQ(nullptr, caster->next_in_room);
    EXPECT_FALSE(output_has("summons you"));
    EXPECT_TRUE(is_on_the_floor_of_room_zero(ring));
}

/* A player the item only hurts leaves it behind and walks on. */
TEST_F(StayZoneDeathTest, APlayerTheStayZoneItemOnlyHurtsWalksOutWithoutIt)
{
    obj_data* ring = wear_deadly_stay_zone_ring();
    m_player.tmpabilities.hit = m_player.abilities.hit;

    do_move(&m_player, empty_argument(), nullptr, NORTH + 1, 0);

    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    EXPECT_TRUE(is_on_the_floor_of_room_zero(ring));
}
