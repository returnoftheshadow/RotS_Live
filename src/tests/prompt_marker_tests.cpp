#include "../prompt_marker.h"

#include <gtest/gtest.h>

#include <string_view>

namespace {

// IAC EOR: telnet's end-of-record marker.
constexpr std::string_view IAC_EOR("\xFF\xEF", 2);

// IAC GA: telnet's go-ahead marker.
constexpr std::string_view IAC_GA("\xFF\xF9", 2);

} // namespace

TEST(PromptEndMarker, IsIacEorWhenTheClientAgreedToEor)
{
    EXPECT_EQ(prompt_end_marker(EorAgreement::Agreed, GoAhead::Sent), IAC_EOR);
}

TEST(PromptEndMarker, EorWinsWhenTheClientAlsoSuppressedGoAhead)
{
    EXPECT_EQ(prompt_end_marker(EorAgreement::Agreed, GoAhead::Suppressed), IAC_EOR);
}

TEST(PromptEndMarker, IsIacGaWhenTheClientRefusedEor)
{
    EXPECT_EQ(prompt_end_marker(EorAgreement::Refused, GoAhead::Sent), IAC_GA);
}

TEST(PromptEndMarker, IsEmptyWhenTheClientSuppressedGoAheadWithoutEor)
{
    EXPECT_TRUE(prompt_end_marker(EorAgreement::Refused, GoAhead::Suppressed).empty());
}
