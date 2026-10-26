#include "../handler.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>

// Declared only where it is called (act_offe.cpp); handler.h has no prototype.
int get_number(char** name);

/* ---- get_number: splits an "N.keyword" argument in place, leaving the keyword in the
 * buffer and returning N (1 with no dot, 0 for a non-numeric N). ---- */

TEST(GetNumber, SplitsALeadingOrdinalFromTheKeywordInPlace)
{
    char buffer[16] = "2.sword";
    char* name = buffer;

    int number = get_number(&name);

    EXPECT_EQ(2, number);
    EXPECT_STREQ("sword", name);
}

TEST(GetNumber, LeavesAKeywordWithNoOrdinalUnchangedAndReturnsOne)
{
    char buffer[16] = "sword";
    char* name = buffer;

    int number = get_number(&name);

    EXPECT_EQ(1, number);
    EXPECT_STREQ("sword", name);
}

TEST(GetNumber, ReturnsZeroForANonNumericOrdinalButStillLeavesTheKeyword)
{
    char buffer[16] = "x.sword";
    char* name = buffer;

    int number = get_number(&name);

    EXPECT_EQ(0, number);
    EXPECT_STREQ("sword", name) << "The keyword is moved down before the ordinal is checked.";
}

/* ---- find_all_dots: classifies "all", "all.keyword" and a plain keyword, leaving only the
 * keyword in the buffer for "all.keyword". ---- */

TEST(FindAllDots, SplitsTheAllDotPrefixFromTheKeywordInPlace)
{
    char arg[16] = "all.sword";

    int result = find_all_dots(arg);

    EXPECT_EQ(FIND_ALLDOT, result);
    EXPECT_STREQ("sword", arg);
}

TEST(FindAllDots, LeavesABareAllArgumentUnchanged)
{
    char arg[16] = "all";

    int result = find_all_dots(arg);

    EXPECT_EQ(FIND_ALL, result);
    EXPECT_STREQ("all", arg);
}

TEST(FindAllDots, LeavesAPlainKeywordUnchanged)
{
    char arg[16] = "sword";

    int result = find_all_dots(arg);

    EXPECT_EQ(FIND_INDIV, result);
    EXPECT_STREQ("sword", arg);
}

/* ---- one_argument: copies the first word, lower-cased, and returns the rest. ---- */

char* one_argument(char* argument, char* first_arg);

TEST(OneArgument, CopiesTheFirstWordLowerCasedAndReturnsTheRest)
{
    char input[] = "  Sword shield";
    char word[32];

    const char* rest = one_argument(input, word);

    EXPECT_STREQ("sword", word);
    EXPECT_STREQ(" shield", rest);
}

TEST(OneArgument, SkipsFillWords)
{
    char input[] = "in the bag";
    char word[32];

    one_argument(input, word);

    EXPECT_STREQ("bag", word);
}

TEST(OneArgument, AWordOpeningWithAQuoteComesBackEmptyNotAStaleLetter)
{
    char input[] = "'bob' rest";
    char word[32];
    std::fill(std::begin(word), std::end(word), 'z');

    const char* rest = one_argument(input, word);

    EXPECT_STREQ("", word) << "The first byte is written, not left from an earlier call.";
    EXPECT_STREQ("bob' rest", rest) << "Only the opening quote is consumed.";
}
