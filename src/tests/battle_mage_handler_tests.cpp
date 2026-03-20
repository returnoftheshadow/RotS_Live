#include "../warrior_spec_handlers.h"
#include "../utils.h"
#include <gtest/gtest.h>

namespace {

struct BattleMageTestContext {
    char_data character{};
    char_prof_data profs{};

    BattleMageTestContext(game_types::player_specs specialization, int tactics, int mage_level, int warrior_level) {
        character.profs = &profs;
        character.specials.tactics = tactics;
        profs.specialization = static_cast<int>(specialization);
        profs.prof_level[PROF_MAGE] = mage_level;
        profs.prof_level[PROF_WARRIOR] = warrior_level;
    }
};

} // namespace

class BattleMageProcTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
    }
};

TEST(BattleMageHandler, LeavesSpellPenUnchangedForNonSpecialists) {
    BattleMageTestContext context(game_types::PS_None, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_EQ(handler.get_bonus_spell_pen(50), 50)
        << "Expected non-battle mages to keep their original spell penetration.";
}

TEST(BattleMageHandler, AddsTacticsAndMageLevelBonusToSpellPen) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_EQ(handler.get_bonus_spell_pen(50), 54)
        << "Expected battle mages to gain tactics and mage-level bonuses to spell penetration.";
}

TEST(BattleMageHandler, LeavesSpellPowerUnchangedForNonSpecialists) {
    BattleMageTestContext context(game_types::PS_None, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_EQ(handler.get_bonus_spell_power(60), 60)
        << "Expected non-battle mages to keep their original spell power.";
}

TEST(BattleMageHandler, AddsTacticsAndMageLevelBonusToSpellPower) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_EQ(handler.get_bonus_spell_power(60), 64)
        << "Expected battle mages to gain tactics and mage-level bonuses to spell power.";
}

TEST(BattleMageHandler, AllowsNonSpecialistsToPrepareSpells) {
    BattleMageTestContext context(game_types::PS_None, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_TRUE(handler.can_prepare_spell())
        << "Expected non-battle mages to be able to prepare spells normally.";
}

TEST(BattleMageHandler, PreventsBattleMagesFromPreparingSpells) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_FALSE(handler.can_prepare_spell())
        << "Expected battle mages to lose the normal spell preparation flow.";
}

TEST(BattleMageHandler, AppliesIntegerDivisionThresholdsWhenCalculatingBonuses) {
    struct TestCase {
        int tactics;
        int mage_level;
        int expected_bonus;
    };

    const TestCase cases[] = {
        {3, 11, 1},
        {3, 12, 2},
        {5, 23, 3},
        {5, 24, 4},
    };

    for (const TestCase &test_case : cases) {
        BattleMageTestContext context(
            game_types::PS_BattleMage, test_case.tactics, test_case.mage_level, 18);
        player_spec::battle_mage_handler handler(&context.character);

        EXPECT_EQ(handler.get_bonus_spell_pen(50), 50 + test_case.expected_bonus)
            << "Expected spell penetration bonuses to use integer division for tactics="
            << test_case.tactics << " and mage_level=" << test_case.mage_level << ".";
        EXPECT_EQ(handler.get_bonus_spell_power(60), 60 + test_case.expected_bonus)
            << "Expected spell power bonuses to use integer division for tactics="
            << test_case.tactics << " and mage_level=" << test_case.mage_level << ".";
    }
}

TEST_F(BattleMageProcTest, NonSpecialistsAlwaysReportInterruptionsAndArmorFailures) {
    BattleMageTestContext context(game_types::PS_None, TACTICS_NORMAL, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    EXPECT_TRUE(handler.does_spell_get_interrupted())
        << "Expected non-battle mages to report normal spell interruption behavior.";
    EXPECT_TRUE(handler.does_mental_attack_interrupt_spell())
        << "Expected non-battle mages to report normal mental interruption behavior.";
    EXPECT_TRUE(handler.does_armor_fail_spell())
        << "Expected non-battle mages to report normal armor failure behavior.";
}

TEST_F(BattleMageProcTest, BelowAggressiveTacticsUseBaseChanceForSpellInterruptions) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_NORMAL, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    push_test_random_value(0.20);
    EXPECT_FALSE(handler.does_spell_get_interrupted())
        << "Expected sub-aggressive battle mages to avoid interruption when the roll stays within base chance.";

    push_test_random_value(0.30);
    EXPECT_TRUE(handler.does_spell_get_interrupted())
        << "Expected sub-aggressive battle mages to be interrupted when the roll exceeds base chance.";
}

TEST_F(BattleMageProcTest, AggressiveTacticsUseCombinedBonusesForSpellInterruptions) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_AGGRESSIVE, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    push_test_random_value(0.75);
    EXPECT_FALSE(handler.does_spell_get_interrupted())
        << "Expected aggressive battle mages to avoid interruption when the roll stays within the combined bonus threshold.";

    push_test_random_value(0.76);
    EXPECT_TRUE(handler.does_spell_get_interrupted())
        << "Expected aggressive battle mages to be interrupted when the roll exceeds the combined bonus threshold.";
}

TEST_F(BattleMageProcTest, AggressiveTacticsUseMageAndTacticsBonusesForMentalInterruptions) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_BERSERK, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    push_test_random_value(0.59);
    EXPECT_FALSE(handler.does_mental_attack_interrupt_spell())
        << "Expected mental interruption checks to use mage and tactics bonuses for battle mages.";

    push_test_random_value(0.60);
    EXPECT_TRUE(handler.does_mental_attack_interrupt_spell())
        << "Expected mental interruption checks to fail once the roll exceeds the mage+tactics threshold.";
}

TEST_F(BattleMageProcTest, AggressiveTacticsUseWarriorAndTacticsBonusesForArmorFailure) {
    BattleMageTestContext context(game_types::PS_BattleMage, TACTICS_BERSERK, 24, 18);
    player_spec::battle_mage_handler handler(&context.character);

    push_test_random_value(0.52);
    EXPECT_FALSE(handler.does_armor_fail_spell())
        << "Expected armor failure checks to use warrior and tactics bonuses for battle mages.";

    push_test_random_value(0.54);
    EXPECT_TRUE(handler.does_armor_fail_spell())
        << "Expected armor failure checks to fail once the roll exceeds the warrior+tactics threshold.";
}
