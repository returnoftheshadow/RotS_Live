#include "../comm.h"
#include "../interpre.h"
#include "../room_exits.h"
#include "../structs.h"
#include "../utils.h"

#include "ScopedGameClock.h"
#include "scoped_exit_test_rooms.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

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

    // The hall and its exits.
    ScopedExitTestRooms m_rooms;
    // The character who looks.
    char_data m_viewer {};

private:
    // The viewer's name.
    char m_viewer_name[16] = "Viewer";
};

// The states of view, north to down.
std::vector<exit_state> states_of(const room_exit_view& view)
{
    std::vector<exit_state> states;
    states.reserve(view.size());
    for (const room_exit& exit : view) {
        states.push_back(exit.state);
    }
    return states;
}

} // namespace

TEST_F(ExitViewTest, TheFullViewHoldsEveryExitTheRoomHasWithItsDestination)
{
    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    EXPECT_EQ(states_of(exits),
        (std::vector<exit_state> { exit_state::open, exit_state::closed_door,
            exit_state::hidden_door, exit_state::window, exit_state::open, exit_state::open }));
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        EXPECT_EQ(exits[direction].to_room, ScopedExitTestRooms::destination(direction))
            << direction;
    }
}

TEST_F(ExitViewTest, MissingAndNowhereExitsAreNoneLeadingNowhere)
{
    m_rooms.remove_exit(NORTH);
    m_rooms.set_exit(EAST, 0, NOWHERE);

    const room_exit_view exits = room_exits(ScopedExitTestRooms::HALL);

    for (int direction : { NORTH, EAST }) {
        EXPECT_EQ(exits[direction].state, exit_state::none) << direction;
        EXPECT_EQ(exits[direction].to_room, NOWHERE) << direction;
    }
}

TEST_F(ExitViewTest, ARoomOutsideTheWorldHasNoExits)
{
    const room_exit_view exits = room_exits(-1);

    EXPECT_EQ(states_of(exits), std::vector<exit_state>(NUM_OF_DIRS, exit_state::none));
}

TEST_F(ExitViewTest, AMortalDoesNotSeeHiddenDoorsOrWindows)
{
    const room_exit_view seen = room_line_view();

    EXPECT_EQ(states_of(seen),
        (std::vector<exit_state> { exit_state::open, exit_state::closed_door, exit_state::none,
            exit_state::none, exit_state::open, exit_state::open }));
    EXPECT_EQ(seen[SOUTH].to_room, NOWHERE);
    EXPECT_EQ(seen[WEST].to_room, NOWHERE);
}

TEST_F(ExitViewTest, AGodSeesEveryExit)
{
    m_viewer.player.level = LEVEL_GOD;

    EXPECT_EQ(states_of(room_line_view()), states_of(room_exits(ScopedExitTestRooms::HALL)));
}

TEST_F(ExitViewTest, AViewerWhoCannotSeeSeesNoExits)
{
    const std::vector<exit_state> no_exits(NUM_OF_DIRS, exit_state::none);

    m_viewer.specials.position = POSITION_SLEEPING;
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(states_of(room_line_view()), no_exits) << "asleep";

    m_viewer.specials.position = POSITION_STANDING;
    SET_BIT(m_viewer.specials.affected_by, AFF_BLIND);
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(states_of(room_line_view()), no_exits) << "blind";

    REMOVE_BIT(m_viewer.specials.affected_by, AFF_BLIND);
    room_data& hall = world[ScopedExitTestRooms::HALL];
    hall.light = 0;
    hall.room_flags = INDOORS | DARK;
    EXPECT_FALSE(can_see_room(m_viewer));
    EXPECT_EQ(states_of(room_line_view()), no_exits) << "dark";
}
