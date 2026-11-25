#include "../prompt_marker.h"

#include <gtest/gtest.h>

#include <string_view>

namespace {

// IAC EOR: the end-of-record marker a client agrees to with DO EOR.
constexpr std::string_view IAC_EOR("\xFF\xEF", 2);

// IAC GA: the go-ahead marker sent to a client that has not agreed to EOR.
constexpr std::string_view IAC_GA("\xFF\xF9", 2);

} // namespace

TEST(PromptEndMarker, IsIacEorWhenTheClientAgreedToEor)
{
    EXPECT_EQ(prompt_end_marker(true, false), IAC_EOR);
}

TEST(PromptEndMarker, EorWinsWhenTheClientAlsoSuppressedGoAhead)
{
    EXPECT_EQ(prompt_end_marker(true, true), IAC_EOR);
}

TEST(PromptEndMarker, IsIacGaWhenTheClientRefusedEor)
{
    EXPECT_EQ(prompt_end_marker(false, false), IAC_GA);
}

TEST(PromptEndMarker, IsEmptyWhenTheClientSuppressedGoAheadWithoutEor)
{
    EXPECT_TRUE(prompt_end_marker(false, true).empty());
}
