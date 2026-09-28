#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

extern char* preference_bits[];

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

} // namespace
