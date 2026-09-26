// Who may learn mist of baazunga and blaze, and which guildmasters teach them. Both are
// specialization spells: SPECIAL(guild) (spec_pro.cpp) lists and teaches a LEARN_SPEC skill only
// to a player whose specialization matches the skill's. Guildmaster tables are consts.cpp's
// guildmasters[], numbered from 1 by a guild mob's prog number.
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

// consts.cpp's global tables; neither is declared in a header.
extern struct skill_data skills[MAX_SKILLS];
extern struct skill_teach_data guildmasters[];

namespace {

// Guildmaster table numbers (a guild mob's prog number) and the mobs that use them.
constexpr int kTravellerTable = 7;         // a mysterious traveller (mob 1503)
constexpr int kUrukMageTable = 12;         // the young uruk mage (mob 4601)
constexpr int kLaketownMageTable = 16;     // Scorther (mob 10003)
constexpr int kMagusTable = 30;            // Magus, the Uruk-Lhuth trainer (mob 13600)
constexpr int kGlassEyedShamanTable = 47;  // a glass-eyed shaman (mob 32200)
constexpr int kUrukGuildmasterTable = 58;  // an uruk guildmaster (mob 2043)

constexpr int kTaughtToTheFull = 100;

// How far guildmaster table `table_number` teaches `skill`.
int taught_to(int table_number, int skill) {
    return guildmasters[table_number - 1].knowledge[skill];
}

} // namespace

TEST(SpellLearning, OnlyDarknessSpecialistsCanLearnMistOfBaazunga) {
    const skill_data& mist = skills[SPELL_MIST_OF_BAAZUNGA];
    EXPECT_TRUE(IS_SET(mist.learn_type, LEARN_SPEC));
    EXPECT_EQ(mist.skill_spec, PLRSPEC_DARK);
}

TEST(SpellLearning, OnlyFireSpecialistsCanLearnBlaze) {
    const skill_data& blaze = skills[SPELL_BLAZE];
    EXPECT_TRUE(IS_SET(blaze.learn_type, LEARN_SPEC));
    EXPECT_EQ(blaze.skill_spec, PLRSPEC_FIRE);
}

TEST(SpellLearning, DarkSideMageTrainersTeachMistToTheFull) {
    EXPECT_EQ(taught_to(kMagusTable, SPELL_MIST_OF_BAAZUNGA), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kUrukMageTable, SPELL_MIST_OF_BAAZUNGA), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kUrukGuildmasterTable, SPELL_MIST_OF_BAAZUNGA), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kGlassEyedShamanTable, SPELL_MIST_OF_BAAZUNGA), kTaughtToTheFull);
}

TEST(SpellLearning, FireTrainersTeachBlazeToTheFull) {
    EXPECT_EQ(taught_to(kTravellerTable, SPELL_BLAZE), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kLaketownMageTable, SPELL_BLAZE), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kUrukMageTable, SPELL_BLAZE), kTaughtToTheFull);
    EXPECT_EQ(taught_to(kUrukGuildmasterTable, SPELL_BLAZE), kTaughtToTheFull);
}
