#include "../test_harness.h"

#include "../spells.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdlib>

extern struct skill_data skills[];
void affect_update_person(struct char_data* i, int mode);

namespace {

class HarnessSeedTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        harness_mode = 0;
        unsetenv("ROTS_RANDOM_SEED");
    }
};

} // namespace

TEST_F(HarnessSeedTest, SeedIsIgnoredOutsideHarnessMode)
{
    harness_mode = 0;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    EXPECT_FALSE(seed_random_from_environment()) << "a seed must not apply unless -t was given";
}

TEST_F(HarnessSeedTest, SeedIsIgnoredWhenTheVariableIsUnset)
{
    harness_mode = 1;
    unsetenv("ROTS_RANDOM_SEED");

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, RejectsANonNumericSeed)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "forty-two", 1);

    EXPECT_FALSE(seed_random_from_environment());
}

TEST_F(HarnessSeedTest, SeedMakesTheRandomSequenceRepeatable)
{
    harness_mode = 1;
    setenv("ROTS_RANDOM_SEED", "42", 1);

    ASSERT_TRUE(seed_random_from_environment());
    const long first_draw = random();
    const long second_draw = random();

    ASSERT_TRUE(seed_random_from_environment());
    EXPECT_EQ(random(), first_draw) << "reseeding with the same value must replay the sequence";
    EXPECT_EQ(random(), second_draw);
}

namespace {

// A person affect whose switch arm in affect_update_person does nothing (SPELL_CURING), placed
// on a phase that never matches the test binary's pulse-0 phase, so only the harness flag can
// make it tick.
struct ForcedPhaseFixture {
    char_data character {};
    affected_type affect {};
    byte previous_is_fast = 0;

    ForcedPhaseFixture()
    {
        previous_is_fast = skills[SPELL_CURING].is_fast;
        skills[SPELL_CURING].is_fast = 0;
        affect.type = SPELL_CURING;
        affect.duration = 5;
        affect.time_phase = 1; // pulse is 0 in the test binary, so the live phase is 0
        character.affected = &affect;
    }

    ~ForcedPhaseFixture()
    {
        skills[SPELL_CURING].is_fast = previous_is_fast;
        harness_force_affect_phase = 0;
    }
};

} // namespace

TEST(HarnessAffects, FlagIsOffByDefault)
{
    EXPECT_EQ(harness_force_affect_phase, 0);
}

TEST(HarnessAffects, SlowAffectDoesNotTickOnAMismatchedPhaseWithoutTheFlag)
{
    ForcedPhaseFixture fixture;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 5);
}

TEST(HarnessAffects, FlagForcesExactlyOneTickPerCall)
{
    ForcedPhaseFixture fixture;
    harness_force_affect_phase = 1;
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 4);
    affect_update_person(&fixture.character, 0);
    EXPECT_EQ(fixture.affect.duration, 3);
}
