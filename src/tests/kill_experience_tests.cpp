#include "../char_utils.h"
#include "../structs.h"
#include "../world_clock.h"

#include <algorithm>
#include <gtest/gtest.h>

int exp_with_modifiers(char_data* character, char_data* dead_man, int base_exp);

extern int average_mob_life;

namespace {
// The killer's level, low enough that the mob is not reduced for being far beneath it.
constexpr int KILLER_LEVEL = 20;
// The mob's level, high enough (above 5) that its age changes the experience.
constexpr int MOB_LEVEL = 30;
// The experience the kill is worth before any modifier.
constexpr int BASE_EXP = 10000;

// Today's kill-experience formula for this test's killer and mob, written out independently of
// exp_with_modifiers(): the level-scaled base, scaled by the mob's age, plus the low-level bonus.
int expected_kill_exp(int mob_age_in_ticks)
{
    const int level_scaled_exp = BASE_EXP / std::max(KILLER_LEVEL + 1, MOB_LEVEL - 2);
    const int age = mob_age_in_ticks * 40 / (MOB_LEVEL + 20);
    int exp = 0;
    if (age < average_mob_life) {
        exp = level_scaled_exp * (average_mob_life * 60 + age * 40) / (average_mob_life * 100);
    } else {
        exp = level_scaled_exp * (140 - 40 * average_mob_life / age) / 100;
    }
    return exp + 2 * exp / std::max(1, KILLER_LEVEL - 1);
}

// A neutral, non-good-race killer and a plain standing mob of the test levels, so the mob's age
// is the only modifier that varies.
struct KillContext {
    KillContext()
    {
        killer.player.race = RACE_ORC;
        killer.player.level = KILLER_LEVEL;
        mob.specials2.act = MOB_ISNPC;
        mob.player.level = MOB_LEVEL;
        mob.specials.default_pos = POSITION_STANDING;
    }

    // The player who made the kill.
    char_data killer {};
    // The mob that died.
    char_data mob {};
};

// The experience for killing a mob that is mob_age_in_ticks game hours old.
int kill_exp_for_mob_aged(KillContext& context, int mob_age_in_ticks)
{
    const WorldClock::time_point now = WorldClock::now();
    utils::set_mob_age_in_ticks(context.mob, mob_age_in_ticks, now);
    return exp_with_modifiers(&context.killer, &context.mob, BASE_EXP);
}
} // namespace

TEST(KillExperience, FollowsTheAgeFormulaForAMobThatHasJustArrived)
{
    KillContext context;

    EXPECT_EQ(kill_exp_for_mob_aged(context, 0), expected_kill_exp(0));
}

TEST(KillExperience, FollowsTheAgeFormulaForAMobAnAverageLifetimeOld)
{
    KillContext context;
    const int average_age = average_mob_life;

    EXPECT_EQ(kill_exp_for_mob_aged(context, average_age), expected_kill_exp(average_age));
}

TEST(KillExperience, FollowsTheAgeFormulaForAMobTwiceAnAverageLifetimeOld)
{
    KillContext context;
    const int old_age = 2 * average_mob_life;

    EXPECT_EQ(kill_exp_for_mob_aged(context, old_age), expected_kill_exp(old_age));
}
