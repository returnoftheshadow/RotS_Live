#include "../comm.h"
#include "../interpre.h"
#include "../room_exits.h"
#include "../structs.h"
#include "../utils.h"

#include "ScopedGameClock.h"
#include "scoped_exit_test_rooms.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

extern room_data world;
extern weather_data weather_info;

void clear_char(struct char_data* character, int mode);
void do_look(
    char_data* character, char* argument, waiting_type* wait_list, int command, int subcommand);

namespace {

// A player standing in ScopedExitTestRooms' hall, with a descriptor that keeps what it is sent.
class RoomLineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_player, 0);
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.specials.position = POSITION_STANDING;
        m_player.in_room = ScopedExitTestRooms::HALL;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1;
        m_descriptor.character = &m_player;
        m_player.desc = &m_descriptor;
        world[ScopedExitTestRooms::HALL].people = &m_player;
        m_player.next_in_room = nullptr;
    }

    void TearDown() override
    {
        release_cleared_character(&m_player);
        release_test_descriptor(&m_descriptor);
    }

    // Looks at the room and returns the first line the player was sent, with its "\n\r".
    std::string look_and_read_first_line()
    {
        char no_argument[1] = "";
        do_look(&m_player, no_argument, nullptr, CMD_LOOK, 0);
        constexpr std::string_view LINE_END = "\n\r";
        const std::string sent_text(m_descriptor.output);
        const std::size_t line_end = sent_text.find(LINE_END);
        if (line_end == std::string::npos) {
            return sent_text;
        }
        return sent_text.substr(0, line_end + LINE_END.size());
    }

    // Everything the player has been sent.
    std::string output() const { return std::string(m_descriptor.output); }

    // The hall and its exits; declared first so it is set up before the player stands in it.
    ScopedExitTestRooms m_rooms;
    // The player who looks.
    char_data m_player {};
    // The player's connection; its small output buffer keeps what the player is sent.
    descriptor_data m_descriptor {};

private:
    // The player's name.
    char m_player_name[16] = "Looker";
};

} // namespace

TEST_F(RoomLineTest, AMortalSeesOpenClosedBrokenAndOpenedHiddenExits)
{
    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) U D\n\r");
}

TEST_F(RoomLineTest, AGodAlsoSeesHiddenDoorsAndWindows)
{
    m_player.player.level = LEVEL_GOD;

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) *S* {W} U D\n\r");
}

TEST_F(RoomLineTest, AnImmortalBelowGodSeesWhatAMortalSees)
{
    m_player.player.level = LEVEL_IMMORT;

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) U D\n\r");
}

TEST_F(RoomLineTest, AnOrcSeesSunlitExitsMarkedButNotAWindowIntoTheSun)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    m_player.player.race = RACE_ORC;
    for (int direction : { NORTH, WEST }) {
        room_data& destination = world[ScopedExitTestRooms::destination(direction)];
        destination.room_flags = 0;
        destination.sector_type = SECT_FIELD;
    }

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: #N# (E) U D\n\r");
}

TEST_F(RoomLineTest, AnOrcGodSeesAWindowIntoTheSunWithItsWindowMark)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    m_player.player.race = RACE_ORC;
    m_player.player.level = LEVEL_GOD;
    room_data& destination = world[ScopedExitTestRooms::destination(WEST)];
    destination.room_flags = 0;
    destination.sector_type = SECT_FIELD;

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) *S* {W} U D\n\r");
}

TEST_F(RoomLineTest, AnOrcSeesAShadowyRoomThroughABrokenDoor)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    m_player.player.race = RACE_ORC;
    world[ScopedExitTestRooms::destination(UP)].room_flags = INDOORS | SHADOWY;

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) %U% D\n\r");
}

TEST_F(RoomLineTest, AMortalSeesAShutWindowAsAClosedDoor)
{
    m_rooms.set_exit(
        WEST, EX_ISDOOR | EX_CLOSED | EX_NOWALK, ScopedExitTestRooms::destination(WEST));

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) (W) U D\n\r");
}

TEST_F(RoomLineTest, AHumanSeesTheSameSunlitExitsUnmarked)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    for (int direction : { NORTH, WEST }) {
        room_data& destination = world[ScopedExitTestRooms::destination(direction)];
        destination.room_flags = 0;
        destination.sector_type = SECT_FIELD;
    }

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) U D\n\r");
}

TEST_F(RoomLineTest, MissingAndNowhereExitsAreLeftOutButAnExitToRealRoomZeroIsShown)
{
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        m_rooms.remove_exit(direction);
    }
    m_rooms.set_exit(NORTH, 0, 0);
    m_rooms.set_exit(EAST, 0, NOWHERE);

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N\n\r");
}

TEST_F(RoomLineTest, AHallWithNoExitsShowsAnEmptyExitList)
{
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        m_rooms.remove_exit(direction);
    }

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are:\n\r");
}

TEST_F(RoomLineTest, InTheDarkThePlayerIsShownNoExits)
{
    room_data& hall = world[ScopedExitTestRooms::HALL];
    hall.light = 0;
    hall.room_flags = INDOORS | DARK;

    EXPECT_EQ(look_and_read_first_line(), "It is pitch black...\n\r");
    EXPECT_EQ(output().find("Exits are:"), std::string::npos);
}

namespace {

// A character standing in ScopedExitTestRooms' hall, awake and able to see.
class ExitViewTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_viewer, 0);
        m_viewer.player.name = m_viewer_name;
        m_viewer.player.race = RACE_HUMAN;
        m_viewer.player.level = 10;
        m_viewer.specials.position = POSITION_STANDING;
        m_viewer.in_room = ScopedExitTestRooms::HALL;
    }

    void TearDown() override { release_cleared_character(&m_viewer); }

    // The exits m_viewer sees from the hall in the room line.
    room_exit_view room_line_view() const
    {
        const room_exit_view room_view = room_exits(ScopedExitTestRooms::HALL);
        return exits_seen_by(m_viewer, room_view, exit_view_purpose::room_line);
    }

    // Sets the hall's north exit to exit_info and returns the flags room_exits() gives it.
    exit_flags flags_for(int exit_info)
    {
        m_rooms.set_exit(NORTH, exit_info, ScopedExitTestRooms::destination(NORTH));
        return room_exits(ScopedExitTestRooms::HALL)[NORTH].flags;
    }

    // The hall and its exits.
    ScopedExitTestRooms m_rooms;
    // The character who looks.
    char_data m_viewer {};

private:
    // The viewer's name.
    char m_viewer_name[16] = "Viewer";
};

// Whether view has an exit in each direction, north to down.
std::vector<bool> exits_present(const room_exit_view& view)
{
    std::vector<bool> present;
    present.reserve(view.size());
    for (const room_exit& exit : view) {
        present.push_back(exit.to_room.has_value());
    }
    return present;
}

// The flags of view's exits, north to down.
std::vector<exit_flags> flags_of(const room_exit_view& view)
{
    std::vector<exit_flags> flags;
    flags.reserve(view.size());
    for (const room_exit& exit : view) {
        flags.push_back(exit.flags);
    }
    return flags;
}

// The default hall's exit flags, north to down, as ScopedExitTestRooms describes them.
const std::vector<exit_flags> HALL_FLAGS = { exit_flags::none, exit_flags::door | exit_flags::shut,
    exit_flags::door | exit_flags::shut | exit_flags::hidden, exit_flags::window,
    exit_flags::door | exit_flags::broken, exit_flags::door | exit_flags::hidden };

} // namespace

// Prints exit flags by number in failure messages; ADL finds it beside exit_flags.
static void PrintTo(exit_flags flags, std::ostream* out_stream)
{
    *out_stream << static_cast<int>(flags);
}

TEST_F(ExitViewTest, TheFullViewHoldsEveryExitTheRoomHasWithItsDestinationAndDoor)
{
    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    EXPECT_EQ(flags_of(exits), HALL_FLAGS);
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        EXPECT_EQ(exits[direction].to_room, ScopedExitTestRooms::destination(direction))
            << direction;
    }
    EXPECT_EQ(exits[NORTH].door_name, "");
    EXPECT_EQ(exits[EAST].door_name, "door");
    EXPECT_EQ(exits[WEST].door_name, "");
}

TEST_F(ExitViewTest, LockedAndHiddenAreKeptTogetherOnAShutDoor)
{
    EXPECT_EQ(flags_for(EX_ISDOOR | EX_CLOSED | EX_LOCKED | EX_ISHIDDEN),
        exit_flags::door | exit_flags::shut | exit_flags::locked | exit_flags::hidden);
}

TEST_F(ExitViewTest, ADoorIsLockedOnlyWhileShutAndNeverShutOnceBroken)
{
    EXPECT_EQ(flags_for(EX_ISDOOR | EX_LOCKED), exit_flags::door);
    EXPECT_EQ(flags_for(EX_ISDOOR | EX_CLOSED | EX_LOCKED | EX_ISBROKEN),
        exit_flags::door | exit_flags::broken);
}

TEST_F(ExitViewTest, AClosedExitNotMarkedAsADoorIsStillADoor)
{
    EXPECT_EQ(flags_for(EX_CLOSED), exit_flags::door | exit_flags::shut);
}

TEST_F(ExitViewTest, AShutteredWindowIsADoorAndAWindow)
{
    EXPECT_EQ(flags_for(EX_ISDOOR | EX_CLOSED | EX_NOWALK),
        exit_flags::door | exit_flags::shut | exit_flags::window);
}

TEST_F(ExitViewTest, ADoorWithNoKeywordsHasNoName)
{
    m_rooms.remove_keyword(EAST);

    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    EXPECT_TRUE(has(exits[EAST].flags, exit_flags::door));
    EXPECT_EQ(exits[EAST].door_name, "");
}

TEST_F(ExitViewTest, MissingAndNowhereExitsAreNoExit)
{
    m_rooms.remove_exit(NORTH);
    m_rooms.set_exit(EAST, EX_ISDOOR, NOWHERE);

    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    for (int direction : { NORTH, EAST }) {
        EXPECT_FALSE(exits[direction].to_room.has_value()) << direction;
        EXPECT_EQ(exits[direction].flags, exit_flags::none) << direction;
        EXPECT_EQ(exits[direction].door_name, "") << direction;
    }
}

TEST_F(ExitViewTest, ARoomOutsideTheWorldHasNoExits)
{
    const room_exit_view exits = room_exits(-1);

    EXPECT_EQ(exits_present(exits), std::vector<bool>(NUM_OF_DIRS, false));
}

TEST_F(ExitViewTest, OnlyAnExitThatLeadsSomewhereAndIsNeitherAWindowNorShutIsPassable)
{
    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    std::vector<bool> passable;
    passable.reserve(exits.size());
    for (const room_exit& exit : exits) {
        passable.push_back(is_passable(exit));
    }

    EXPECT_EQ(passable, (std::vector<bool> { true, false, false, false, true, true }));
    EXPECT_FALSE(is_passable(room_exit {}));
}

TEST_F(ExitViewTest, AMortalDoesNotSeeShutHiddenDoorsOrWindows)
{
    const room_exit_view seen = room_line_view();

    EXPECT_EQ(exits_present(seen), (std::vector<bool> { true, true, false, false, true, true }));
    EXPECT_EQ(seen[EAST].flags, exit_flags::door | exit_flags::shut);
    EXPECT_EQ(seen[EAST].door_name, "door");
    EXPECT_EQ(seen[SOUTH].flags, exit_flags::none);
}

TEST_F(ExitViewTest, AMortalSeesAShutWindow)
{
    m_rooms.set_exit(
        WEST, EX_ISDOOR | EX_CLOSED | EX_NOWALK, ScopedExitTestRooms::destination(WEST));

    const room_exit_view seen = room_line_view();

    EXPECT_TRUE(seen[WEST].to_room.has_value());
}

TEST_F(ExitViewTest, AGodSeesEveryExit)
{
    m_viewer.player.level = LEVEL_GOD;

    const room_exit_view seen = room_line_view();

    EXPECT_EQ(flags_of(seen), HALL_FLAGS);
    EXPECT_EQ(exits_present(seen), std::vector<bool>(NUM_OF_DIRS, true));
}

TEST_F(ExitViewTest, AViewerWhoCannotSeeSeesNoExits)
{
    const std::vector<bool> no_exits(NUM_OF_DIRS, false);

    m_viewer.specials.position = POSITION_SLEEPING;
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(exits_present(room_line_view()), no_exits) << "asleep";

    m_viewer.specials.position = POSITION_STANDING;
    SET_BIT(m_viewer.specials.affected_by, AFF_BLIND);
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(exits_present(room_line_view()), no_exits) << "blind";

    REMOVE_BIT(m_viewer.specials.affected_by, AFF_BLIND);
    room_data& hall = world[ScopedExitTestRooms::HALL];
    hall.light = 0;
    hall.room_flags = INDOORS | DARK;
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(exits_present(room_line_view()), no_exits) << "dark";
}
