#include "../limits.h"
#include <gtest/gtest.h>

namespace {

TEST(CapExpGain, LeavesAnAwardBelowTheCapAlone)
{
    EXPECT_EQ(100, cap_exp_gain(100));
}

TEST(CapExpGain, ClampsAnAwardAboveTheCap)
{
    EXPECT_EQ(7000, cap_exp_gain(12000));
}

TEST(CapExpGain, LeavesTheCapItselfAlone)
{
    EXPECT_EQ(7000, cap_exp_gain(7000));
}

} // namespace
