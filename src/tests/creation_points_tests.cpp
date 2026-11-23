#include "creation_points.h"

#include "db.h"
#include "owned_character.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <string>

namespace {

CreationPoints::Split make_split(int mage, int mystic, int ranger, int warrior)
{
    CreationPoints::Split split {};
    split[PROF_MAGE] = mage;
    split[PROF_CLERIC] = mystic;
    split[PROF_RANGER] = ranger;
    split[PROF_WARRIOR] = warrior;
    return split;
}

} // namespace

TEST(CreationPoints, StandardClassReturnsNothingForALetterNoClassUses)
{
    EXPECT_FALSE(CreationPoints::standard_class('z').has_value());
    EXPECT_FALSE(CreationPoints::standard_class('o').has_value());
}

TEST(CreationPoints, StandardClassGivesTheClassPoints)
{
    const std::optional<CreationPoints> wizard = CreationPoints::standard_class('i');
    ASSERT_TRUE(wizard.has_value());
    EXPECT_EQ(wizard->points(PROF_MAGE), 121);
    EXPECT_EQ(wizard->points(PROF_CLERIC), 16);
    EXPECT_EQ(wizard->points(PROF_RANGER), 9);
    EXPECT_EQ(wizard->points(PROF_WARRIOR), 4);
    EXPECT_EQ(wizard->total(), CREATION_POINT_BUDGET);
}

TEST(CreationPoints, StandardClassOffersEveryMenuLetterWithinTheBudget)
{
    const std::string menu_letters = "mtrwnihsba";
    for (const char letter : menu_letters) {
        const std::optional<CreationPoints> standard = CreationPoints::standard_class(letter);
        ASSERT_TRUE(standard.has_value()) << letter;
        for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
            EXPECT_GE(standard->points(profession), 0) << letter;
        }
        EXPECT_LE(standard->total(), CREATION_POINT_BUDGET) << letter;
    }
}

TEST(CreationPoints, CustomAcceptsTheWholeBudget)
{
    std::string error_message;
    const std::optional<CreationPoints> split = CreationPoints::custom(make_split(70, 20, 0, 60), error_message);
    ASSERT_TRUE(split.has_value()) << error_message;
    EXPECT_EQ(split->total(), CREATION_POINT_BUDGET);
}

TEST(CreationPoints, CustomAcceptsLessThanTheBudget)
{
    std::string error_message;
    EXPECT_TRUE(CreationPoints::custom(make_split(10, 0, 0, 0), error_message).has_value()) << error_message;
}

TEST(CreationPoints, CustomRefusesOnePointOverTheBudget)
{
    std::string error_message;
    EXPECT_FALSE(CreationPoints::custom(make_split(CREATION_POINT_BUDGET, 1, 0, 0), error_message).has_value());
    EXPECT_NE(error_message.find(std::to_string(CREATION_POINT_BUDGET)), std::string::npos);
}

TEST(CreationPoints, CustomRefusesOneProfessionOverTheBudget)
{
    std::string error_message;
    EXPECT_FALSE(CreationPoints::custom(make_split(CREATION_POINT_BUDGET + 1, 0, 0, 0), error_message).has_value());
    EXPECT_EQ(error_message, "One profession has more than " + std::to_string(CREATION_POINT_BUDGET) + " points.");
}

TEST(CreationPoints, CustomRefusesANegativeProfession)
{
    std::string error_message;
    EXPECT_FALSE(CreationPoints::custom(make_split(-1, 0, 0, 0), error_message).has_value());
    EXPECT_FALSE(error_message.empty());
}

TEST(CreationPoints, CustomIgnoresTheUnusedSlot)
{
    CreationPoints::Split split = make_split(10, 0, 0, 0);
    split[PROF_GENERAL] = 99;
    std::string error_message;
    const std::optional<CreationPoints> points = CreationPoints::custom(split, error_message);
    ASSERT_TRUE(points.has_value()) << error_message;
    EXPECT_EQ(points->total(), 10);
}

TEST(CreationPoints, PointsOutsideTheFourProfessionsAreZero)
{
    const std::optional<CreationPoints> mage = CreationPoints::standard_class('m');
    ASSERT_TRUE(mage.has_value());
    EXPECT_EQ(mage->points(PROF_GENERAL), 0);
    EXPECT_EQ(mage->points(PROF_WARRIOR + 1), 0);
}

TEST(CreationPoints, ApplyToSetsTheCharacterPointsAndFromCharacterReadsThemBack)
{
    owned_character character;
    std::string error_message;
    const std::optional<CreationPoints> split = CreationPoints::custom(make_split(70, 20, 0, 60), error_message);
    ASSERT_TRUE(split.has_value()) << error_message;

    split->apply_to(*character);
    EXPECT_EQ(GET_PROF_POINTS(PROF_MAGE, character), 70);
    EXPECT_EQ(GET_PROF_POINTS(PROF_WARRIOR, character), 60);
    EXPECT_EQ(points_used(*character), CREATION_POINT_BUDGET);

    const std::optional<CreationPoints> read_back = CreationPoints::from_character(*character, error_message);
    ASSERT_TRUE(read_back.has_value()) << error_message;
    EXPECT_EQ(read_back->points(PROF_CLERIC), 20);
}

TEST(CreationPoints, FromCharacterRefusesACharacterOverTheBudget)
{
    owned_character character;
    GET_PROF_POINTS(PROF_MAGE, character) = CREATION_POINT_STEP_CAP;
    std::string error_message;
    EXPECT_FALSE(CreationPoints::from_character(*character, error_message).has_value());
}
