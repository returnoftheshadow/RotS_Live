#include "../skill_timer.h"
#include "../spells.h"
#include <cstring>
#include <gtest/gtest.h>

namespace {

weather_data& test_weather()
{
    static weather_data weather{};
    return weather;
}

struct SkillTimerTestContext {
    char_data player{};

    SkillTimerTestContext()
    {
        game_timer::skill_timer::create(test_weather(), nullptr);
        player.specials2.idnum = 4242;
    }
};

} // namespace

TEST(SkillTimer, ReportsTrackedSkillCooldownsIntoProvidedBuffer)
{
    SkillTimerTestContext context;
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    char buffer[128] = "";

    timer.add_skill_timer(context.player, SKILL_TRACK, 5);
    timer.report_skill_status(context.player.specials2.idnum, buffer, sizeof(buffer));

    EXPECT_NE(strstr(buffer, "track"), nullptr);
    EXPECT_NE(strstr(buffer, "5"), nullptr);
}

TEST(SkillTimer, AppendsCooldownTextAfterExistingContent)
{
    SkillTimerTestContext context;
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    char buffer[128] = "Existing\n\r";

    context.player.specials2.idnum = 4343;
    timer.add_skill_timer(context.player, SKILL_TRACK, 3);
    timer.report_skill_status(context.player.specials2.idnum, buffer, sizeof(buffer));

    EXPECT_NE(strstr(buffer, "Existing"), nullptr);
    EXPECT_NE(strstr(buffer, "track"), nullptr);
}

TEST(SkillTimer, LeavesNearFullBuffersNullTerminatedWhenAppending)
{
    SkillTimerTestContext context;
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    char buffer[12] = "prefix";

    context.player.specials2.idnum = 4444;
    timer.add_skill_timer(context.player, SKILL_TRACK, 7);
    timer.report_skill_status(context.player.specials2.idnum, buffer, sizeof(buffer));

    EXPECT_EQ(buffer[sizeof(buffer) - 1], '\0');
    EXPECT_EQ(strncmp(buffer, "prefix", 6), 0);
}
