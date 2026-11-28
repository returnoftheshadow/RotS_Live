#include "run_token.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {
// The characters a run token may hold.
constexpr std::string_view LOWERCASE_HEX_DIGITS = "0123456789abcdef";
} // namespace

TEST(RunToken, IsRunTokenLengthLowercaseHexadecimalCharacters)
{
    const std::string token = harness::create_run_token();

    EXPECT_EQ(token.size(), harness::RUN_TOKEN_LENGTH) << "Unexpected token: " << token;
    EXPECT_EQ(token.find_first_not_of(LOWERCASE_HEX_DIGITS), std::string::npos)
        << "Expected only lowercase hexadecimal digits: " << token;
}

TEST(RunToken, DiffersFromOneRunToTheNext)
{
    const std::string first_token = harness::create_run_token();
    const std::string second_token = harness::create_run_token();

    EXPECT_NE(first_token, second_token) << "Expected two different tokens.";
}

TEST(RunToken, FollowsTheReadyStateAfterASpace)
{
    EXPECT_EQ(harness::with_entry_token("hours=0", "00ff"), "hours=0 entry=00ff");
}

TEST(RunToken, IsTheWholeReadyStateWhenTheScheduleReportsNone)
{
    EXPECT_EQ(harness::with_entry_token("", "00ff"), "entry=00ff");
}
