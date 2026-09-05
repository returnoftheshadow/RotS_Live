#include "../account_management_types.h"
#include "../color.h"
#include "../structs.h"

#include <gtest/gtest.h>

namespace {

TEST(AccountPpcMask, CoversEveryPpcOptionAndNothingElse)
{
    const long expected = PRF_PROMPT | PRF_ADVANCED_PROMPT | PRF_ADVANCED_VIEW
        | PRF_BRIEF | PRF_COMPACT | PRF_SPAM | PRF_WRAP | PRF_ECHO
        | PRF_MSDP | PRF_LATIN1 | PRF_SPINNER
        | PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_COLOR;
    EXPECT_EQ(PPC_PRF_MASK, expected);
}

TEST(AccountPpcMask, ExcludesCharacterAndMoodSettings)
{
    EXPECT_EQ(PPC_PRF_MASK & PRF_NOTELL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_NARRATE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_CHAT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SING, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_MENTAL, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SWIM, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_SUMMONABLE, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_HOLYLIGHT, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_ROOMFLAGS, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_WIZ, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG1, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG2, 0);
    EXPECT_EQ(PPC_PRF_MASK & PRF_LOG3, 0);
}

TEST(AccountPreferences, DefaultsToAbsent)
{
    account::AccountData account;
    EXPECT_FALSE(account.preferences.present);
    EXPECT_EQ(account.preferences.preference_flags, 0);
}

TEST(AccountPreferences, ColourArraysAreZeroInitialised)
{
    account::AccountPreferences preferences;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        EXPECT_EQ(preferences.colors[index], 0) << "slot " << index;
        EXPECT_EQ(preferences.color_settings[index].foreground.mode, COLOR_VALUE_DEFAULT);
        EXPECT_EQ(preferences.color_settings[index].background.mode, COLOR_VALUE_DEFAULT);
    }
}

} // namespace
