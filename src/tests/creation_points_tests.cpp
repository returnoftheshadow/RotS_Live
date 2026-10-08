#include "creation_points.h"

#include "db.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// A fresh player character for applying points to; freed when the scope ends.
class ScopedCharacter {
public:
    ScopedCharacter()
    {
        CREATE(character, struct char_data, 1);
        clear_char(character, MOB_VOID);
    }

    ~ScopedCharacter()
    {
        free_char(character);
    }

    ScopedCharacter(const ScopedCharacter&) = delete;
    ScopedCharacter& operator=(const ScopedCharacter&) = delete;

    // The character, owned by this object.
    char_data& get()
    {
        return *character;
    }

private:
    // The character under test; allocated as the game allocates one.
    char_data* character = nullptr;
};

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
    EXPECT_EQ(wizard->total(), creation_point_budget);
}

TEST(CreationPoints, CustomAcceptsTheWholeBudget)
{
    std::string error_message;
    const std::optional<CreationPoints> split = CreationPoints::custom(make_split(70, 20, 0, 60), error_message);
    ASSERT_TRUE(split.has_value()) << error_message;
    EXPECT_EQ(split->total(), creation_point_budget);
}

TEST(CreationPoints, CustomAcceptsLessThanTheBudget)
{
    std::string error_message;
    EXPECT_TRUE(CreationPoints::custom(make_split(10, 0, 0, 0), error_message).has_value()) << error_message;
}

TEST(CreationPoints, CustomRefusesOnePointOverTheBudget)
{
    std::string error_message;
    EXPECT_FALSE(CreationPoints::custom(make_split(creation_point_budget, 1, 0, 0), error_message).has_value());
    EXPECT_NE(error_message.find(std::to_string(creation_point_budget)), std::string::npos);
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
    ScopedCharacter character;
    std::string error_message;
    const std::optional<CreationPoints> split = CreationPoints::custom(make_split(70, 20, 0, 60), error_message);
    ASSERT_TRUE(split.has_value()) << error_message;

    split->apply_to(character.get());
    EXPECT_EQ(GET_PROF_POINTS(PROF_MAGE, &character.get()), 70);
    EXPECT_EQ(GET_PROF_POINTS(PROF_WARRIOR, &character.get()), 60);
    EXPECT_EQ(points_used(character.get()), creation_point_budget);

    const std::optional<CreationPoints> read_back = CreationPoints::from_character(character.get(), error_message);
    ASSERT_TRUE(read_back.has_value()) << error_message;
    EXPECT_EQ(read_back->points(PROF_CLERIC), 20);
}

TEST(CreationPoints, FromCharacterRefusesACharacterOverTheBudget)
{
    ScopedCharacter character;
    GET_PROF_POINTS(PROF_MAGE, &character.get()) = creation_point_step_cap;
    std::string error_message;
    EXPECT_FALSE(CreationPoints::from_character(character.get(), error_message).has_value());
}
