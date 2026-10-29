#include "../platdef.h"
#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

/* ---- string_to_new_value: the shared number reader for the shape editors'
 * number prompts.  "-N" subtracts, which is why a few prompts also use
 * string_to_negative_value. ---- */

TEST(StringToNewValue, MinusSubtractsFromTheCurrentValue)
{
    char arg[] = "-1";
    int value = 1105;
    string_to_new_value(arg, &value);
    EXPECT_EQ(1104, value);
}

TEST(StringToNewValue, SeveralFlagChangesApplyLeftToRight)
{
    char arg[] = "p1 p7  m2 P4 M7";
    int value = 4;
    char* stopped = (char*)1;
    string_to_new_value(arg, &value, &stopped);
    EXPECT_EQ((1 << 1) | (1 << 4), value);
    EXPECT_EQ(nullptr, stopped);
}

TEST(StringToNewValue, FlagListStopsAtAWordThatDoesNotFit)
{
    const char* inputs[] = { "p2 5 p9", "p2 x p9", "p2 p p9", "p2 p40 p9", "p2 p3x p9", "p2 m-1" };
    for (const char* in : inputs) {
        char arg[32];
        strcpy(arg, in);
        int value = 0;
        char* stopped = 0;
        string_to_new_value(arg, &value, &stopped);
        EXPECT_EQ(1 << 2, value) << "input '" << in << "'";
        ASSERT_NE(nullptr, stopped) << "input '" << in << "'";
        EXPECT_EQ(arg + 3, stopped) << "input '" << in << "'";
    }
}

TEST(StringToNewValue, FirstFlagWordThatDoesNotFitChangesNothing)
{
    const char* inputs[] = { "p", "p32", "px m1", "p 3" };
    for (const char* in : inputs) {
        char arg[16];
        strcpy(arg, in);
        int value = 10;
        char* stopped = 0;
        string_to_new_value(arg, &value, &stopped);
        EXPECT_EQ(10, value) << "input '" << in << "'";
        EXPECT_NE(nullptr, stopped) << "input '" << in << "'";
    }
}

TEST(StringToNewValue, BitThirtyOneIsAccepted)
{
    char arg[] = "p31 m31 p0";
    int value = 0;
    char* stopped = 0;
    string_to_new_value(arg, &value, &stopped);
    EXPECT_EQ(1, value);
    EXPECT_EQ(nullptr, stopped);
}

TEST(StringToNewValue, NumberAnswersAreUnchanged)
{
    struct {
        const char* in;
        int start, want;
    } cases[] = { { "17", 5, 17 }, { "+3", 5, 8 }, { "-2", 5, 3 }, { "", 5, 5 }, { "  ", 5, 5 },
        { "17 p3", 5, 17 } };
    for (auto& c : cases) {
        char arg[16];
        strcpy(arg, c.in);
        int value = c.start;
        char* stopped = (char*)1;
        string_to_new_value(arg, &value, &stopped);
        EXPECT_EQ(c.want, value) << "input '" << c.in << "'";
        EXPECT_EQ(nullptr, stopped) << "input '" << c.in << "'";
    }
}

/* ---- string_to_negative_value: for the prompts where a negative number is
 * a real value (alignment, saving throw, "no keyhole", "leads nowhere"). ---- */

TEST(StringToNegativeValue, MinusNumberSetsTheNegativeValue)
{
    char arg[] = "-1";
    int value = 1105;
    EXPECT_EQ(1, string_to_negative_value(arg, &value));
    EXPECT_EQ(-1, value);
}

TEST(StringToNegativeValue, LeadingSpacesAreSkipped)
{
    char arg[] = "   -500";
    int value = 300;
    EXPECT_EQ(1, string_to_negative_value(arg, &value));
    EXPECT_EQ(-500, value);
}

TEST(StringToNegativeValue, OtherInputIsLeftAlone)
{
    const char* inputs[] = { "5", "+5", "p3", "m3", "", "   ", "-", "-x", "abc" };
    for (const char* in : inputs) {
        char arg[16];
        strcpy(arg, in);
        int value = 42;
        EXPECT_EQ(0, string_to_negative_value(arg, &value)) << "input '" << in << "'";
        EXPECT_EQ(42, value) << "input '" << in << "'";
    }
}
