#include "../ordinal.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

TEST(AppendOrdinal, MatchesNthForEveryNumberTheGameShows)
{
    // Days run 1 to 30 and years into the thousands; a few negatives cover a corrupt value.
    std::string ordinal;
    for (int number = -30; number <= 3000; ++number) {
        char* legacy_ordinal = nth(number);
        const std::string expected_ordinal = legacy_ordinal;
        free(legacy_ordinal);

        ordinal.clear();
        append_ordinal(ordinal, number);

        EXPECT_EQ(ordinal, expected_ordinal) << "for " << number;
    }
}

TEST(OrdinalSuffix, ElevenTwelveAndThirteenTakeTh)
{
    EXPECT_EQ(ordinal_suffix(1), "st");
    EXPECT_EQ(ordinal_suffix(2), "nd");
    EXPECT_EQ(ordinal_suffix(3), "rd");
    EXPECT_EQ(ordinal_suffix(4), "th");
    EXPECT_EQ(ordinal_suffix(11), "th");
    EXPECT_EQ(ordinal_suffix(12), "th");
    EXPECT_EQ(ordinal_suffix(13), "th");
    EXPECT_EQ(ordinal_suffix(21), "st");
}

TEST(OrdinalSuffix, OnlyElevenItselfTakesThNotHundredEleven)
{
    EXPECT_EQ(ordinal_suffix(111), "st");
    EXPECT_EQ(ordinal_suffix(1311), "st");
    EXPECT_EQ(ordinal_suffix(1312), "nd");
}

TEST(AppendOrdinal, AppendsNumberAndSuffixAfterExistingText)
{
    std::string text = "the ";

    append_ordinal(text, 5);
    text += " and ";
    append_ordinal(text, 1234);

    EXPECT_EQ(text, "the 5th and 1234th");
}

TEST(AppendOrdinal, WritesIntoReservedCapacityWithoutReallocating)
{
    std::string text;
    text.reserve(64);
    const char* buffer_before = text.data();

    append_ordinal(text, -2147483647 - 1);

    EXPECT_EQ(text, "-2147483648th");
    EXPECT_EQ(text.data(), buffer_before);
}
