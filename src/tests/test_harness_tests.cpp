#include "../test_harness.h"

#include <gtest/gtest.h>

#include <cstdlib>

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
