#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

extern char* preference_bits[];
extern char* extra_messages[];
extern char* wear_messages[];

namespace {

TEST(Sprintbit, ShowsAdvancedPromptInsteadOfAnError)
{
    char result[MAX_STRING_LENGTH];
    sprintbit(PRF_COLOR | PRF_ADVANCED_PROMPT, preference_bits, result, 0);
    EXPECT_STREQ(" COLOR ADVPRMPT.", result);
}

TEST(Sprintbit, PreferenceNamesMatchTheirBits)
{
    char result[MAX_STRING_LENGTH];
    sprintbit(PRF_CHAT | PRF_ROOMFLAGS | PRF_SPAM | PRF_MSDP | PRF_WRAP | PRF_LATIN1
            | PRF_SPINNER | PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_ADVANCED_VIEW,
        preference_bits, result, 0);
    EXPECT_STREQ(" CHAT RMFLG SPAM MSDP WRAP LATIN1 SPINNER SORT1 SORT2 ADVVIEW.", result);
}

TEST(Sprintbit, IdentifyShowsRaceAndStayZoneFlags)
{
    char result[MAX_STRING_LENGTH];
    sprintbit(ITEM_DWARF | ITEM_STAY_ZONE, extra_messages, result, 1);
    EXPECT_STREQ("has the following attributes:\r\n"
                 "Dwarves only\r\n"
                 "It cannot leave this area",
        result);
}

TEST(Sprintbit, StopsAtTheEndOfTheNameList)
{
    char a[] = "A", end[] = "\n", past[] = "PAST_THE_END";
    char* names[] = { a, end, past };
    char result[MAX_STRING_LENGTH];
    sprintbit((1 << 0) | (1 << 2), names, result, 0);
    EXPECT_STREQ(" A UNDEFINE.", result);
}

TEST(Sprintbit, IdentifyPutsUndefinedBitsOnTheirOwnLine)
{
    char a[] = "A", end[] = "\n";
    char* names[] = { a, end };
    char result[MAX_STRING_LENGTH];
    sprintbit((1 << 0) | (1 << 2), names, result, 1);
    EXPECT_STREQ("has the following attributes:\r\nA\r\nUNDEFINE", result);
}

TEST(Sprintbit, WearMessagesStopAtTheEndOfTheList)
{
    char result[MAX_STRING_LENGTH];
    sprintbit(ITEM_TAKE | ITEM_WEAR_BELT | (1 << 18), wear_messages, result, 2);
    EXPECT_STREQ(" taken and worn on a belt and UNDEFINE.", result);
}

} // namespace
