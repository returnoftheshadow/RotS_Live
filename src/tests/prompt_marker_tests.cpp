#include "../prompt_marker.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

namespace {

// IAC EOR: telnet's end-of-record marker.
constexpr std::string_view IAC_EOR("\xFF\xEF", 2);

// IAC GA: telnet's go-ahead marker.
constexpr std::string_view IAC_GA("\xFF\xF9", 2);

// A prompt as send_prompts() builds it.
constexpr std::string_view GAME_PROMPT = "HP:Healthy MV:Fresh >";

// The byte an untouched part of a test buffer holds, so a test sees what was written.
constexpr char UNWRITTEN = 'x';

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

TEST(ComposePromptWithMarker, WritesThePromptThenTheMarkerAndATerminator)
{
    char buffer[64];
    std::memset(buffer, UNWRITTEN, sizeof(buffer));

    ASSERT_TRUE(compose_prompt_with_marker(GAME_PROMPT, IAC_GA, buffer, sizeof(buffer)));

    const std::size_t expected_length = GAME_PROMPT.size() + IAC_GA.size();
    EXPECT_EQ(std::strlen(buffer), expected_length);
    EXPECT_EQ(std::string_view(buffer, GAME_PROMPT.size()), GAME_PROMPT);
    EXPECT_EQ(std::string_view(buffer + GAME_PROMPT.size(), IAC_GA.size()), IAC_GA);
}

TEST(ComposePromptWithMarker, FillsABufferThatHasExactlyEnoughRoom)
{
    char buffer[GAME_PROMPT.size() + IAC_EOR.size() + 1];

    ASSERT_TRUE(compose_prompt_with_marker(GAME_PROMPT, IAC_EOR, buffer, sizeof(buffer)));

    EXPECT_EQ(buffer[sizeof(buffer) - 1], '\0');
    EXPECT_EQ(std::string_view(buffer + GAME_PROMPT.size(), IAC_EOR.size()), IAC_EOR);
}

TEST(ComposePromptWithMarker, RefusesAndWritesNothingWhenOneByteShort)
{
    char buffer[GAME_PROMPT.size() + IAC_EOR.size()];
    std::memset(buffer, UNWRITTEN, sizeof(buffer));

    EXPECT_FALSE(compose_prompt_with_marker(GAME_PROMPT, IAC_EOR, buffer, sizeof(buffer)));

    for (std::size_t index = 0; index < sizeof(buffer); ++index) {
        EXPECT_EQ(buffer[index], UNWRITTEN) << "byte " << index << " was written";
    }
}

TEST(ComposePromptWithMarker, WritesOnlyThePromptForAnEmptyMarker)
{
    char buffer[64];

    const std::string_view no_marker;
    ASSERT_TRUE(compose_prompt_with_marker(GAME_PROMPT, no_marker, buffer, sizeof(buffer)));

    EXPECT_EQ(std::string_view(buffer), GAME_PROMPT);
}

TEST(ComposePromptWithMarker, TheLongestMarkerFitsTheDeclaredMaximum)
{
    EXPECT_LE(IAC_EOR.size(), PROMPT_END_MARKER_MAX_LENGTH);
    EXPECT_LE(IAC_GA.size(), PROMPT_END_MARKER_MAX_LENGTH);
}
