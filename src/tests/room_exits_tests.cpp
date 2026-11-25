#include "../comm.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include "ScopedExitTestRooms.h"
#include "ScopedGameClock.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern room_data world;
extern weather_data weather_info;

void clear_char(struct char_data* ch, int mode);
void do_look(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

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
        const std::string output(m_descriptor.output);
        const std::size_t line_end = output.find("\n\r");
        if (line_end == std::string::npos) {
            return output;
        }
        return output.substr(0, line_end + 2);
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

TEST_F(RoomLineTest, AnOrcSeesSunlitExitsMarkedIncludingAWindowAMortalOtherwiseCannotSee)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    m_player.player.race = RACE_ORC;
    for (int direction : { 0, 3 }) {
        room_data& destination = world[ScopedExitTestRooms::destination(direction)];
        destination.room_flags = 0;
        destination.sector_type = SECT_FIELD;
    }

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: #N# (E) #W# U D\n\r");
}

TEST_F(RoomLineTest, AHumanSeesTheSameSunlitExitsUnmarked)
{
    ScopedGameClock clock_scope;
    weather_info.sunlight = SUN_LIGHT;
    for (int direction : { 0, 3 }) {
        room_data& destination = world[ScopedExitTestRooms::destination(direction)];
        destination.room_flags = 0;
        destination.sector_type = SECT_FIELD;
    }

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N (E) U D\n\r");
}

TEST_F(RoomLineTest, MissingAndNowhereExitsAreLeftOutButAnExitToRealRoomZeroIsShown)
{
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        m_rooms.remove_exit(direction);
    }
    m_rooms.set_exit(0, 0, 0);
    m_rooms.set_exit(1, 0, NOWHERE);

    EXPECT_EQ(look_and_read_first_line(), "Exit Test Hall    Exits are: N\n\r");
}

TEST_F(RoomLineTest, AHallWithNoExitsShowsAnEmptyExitList)
{
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
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
