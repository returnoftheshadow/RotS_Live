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
