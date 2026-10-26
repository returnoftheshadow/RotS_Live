// The weather a character feels: a room's sector picks the message column and that sector's sky
// picks the row, so a sector or sky value outside the message table gets no weather.

#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <string>

extern struct room_data world;
void clear_char(struct char_data* ch, int mode);
void weather_to_char(const char_data* ch);

namespace {

constexpr int WEATHER_ROOM = 972;
constexpr char NO_FEELING_MESSAGE[] = "You can have no feeling about the weather here.\n\r";

// A player standing outdoors in WEATHER_ROOM whose output is readable; the room and the sky
// table are restored afterwards.
class WeatherToCharTest : public testing::Test {
protected:
    void SetUp() override
    {
        m_saved_room = world[WEATHER_ROOM];
        m_saved_weather = weather_info;
        world[WEATHER_ROOM].room_flags = 0;
        world[WEATHER_ROOM].people = nullptr;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;

        clear_char(&m_player, MOB_VOID);
        m_player.player.name = const_cast<char*>("player");
        m_player.desc = &m_descriptor;
        m_player.in_room = WEATHER_ROOM;
    }

    void TearDown() override
    {
        free(m_player.profs);
        free(m_player.skills);
        free(m_player.knowledge);
        weather_info = m_saved_weather;
        world[WEATHER_ROOM] = m_saved_room;
    }

    std::string output() const { return m_descriptor.output; }

    // The room as SetUp found it; TearDown puts it back.
    room_data m_saved_room;
    // The sky table as SetUp found it; TearDown puts it back.
    weather_data m_saved_weather {};
    // The player's connection, holding what the player was sent.
    descriptor_data m_descriptor {};
    // The player feeling the weather.
    char_data m_player {};
};

} // namespace

TEST_F(WeatherToCharTest, AFieldUnderACloudlessSkyGivesItsMessage)
{
    world[WEATHER_ROOM].sector_type = SECT_FIELD;
    weather_info.sky[SECT_FIELD] = SKY_CLOUDLESS;

    weather_to_char(&m_player);

    EXPECT_EQ(output(), "Above the fields, not a cloud can be seen in the sky.\n\r");
}

TEST_F(WeatherToCharTest, IndoorsGivesNoFeeling)
{
    world[WEATHER_ROOM].sector_type = SECT_FIELD;
    world[WEATHER_ROOM].room_flags = INDOORS;

    weather_to_char(&m_player);

    EXPECT_EQ(output(), NO_FEELING_MESSAGE);
}

TEST_F(WeatherToCharTest, ASectorPastTheTableGivesNoFeelingAndIsLogged)
{
    world[WEATHER_ROOM].sector_type = SECT_SWAMP + 1;

    testing::internal::CaptureStderr();
    weather_to_char(&m_player);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(output(), NO_FEELING_MESSAGE);
    EXPECT_NE(log_text.find("is not a sector"), std::string::npos) << log_text;
}

TEST_F(WeatherToCharTest, ASkyValuePastTheTableGivesNoFeelingAndIsLogged)
{
    world[WEATHER_ROOM].sector_type = SECT_FIELD;
    weather_info.sky[SECT_FIELD] = SKY_BLIZZARD + 1;

    testing::internal::CaptureStderr();
    weather_to_char(&m_player);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(output(), NO_FEELING_MESSAGE);
    EXPECT_NE(log_text.find("has sky value"), std::string::npos) << log_text;
}
