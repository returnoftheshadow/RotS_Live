#include "../warrior_spec_handlers.h"
#include "../utils.h"
#include "ObjFlagDataBuilder.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

namespace {

struct WeaponMasterTestContext {
    char_data character{};
    char_prof_data profs{};
    obj_data weapon{};

    WeaponMasterTestContext(game_types::player_specs specialization, game_types::weapon_type weapon_type) {
        character.profs = &profs;
        profs.specialization = static_cast<int>(specialization);

        weapon.obj_flags = builders::ObjFlagDataBuilder().setWeaponType(weapon_type).build();
        weapon.obj_flags.type_flag = ITEM_WEAPON;
    }
};

} // namespace

class WeaponMasterProcTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
    }
};

TEST(WeaponMasterHandler, ReturnsDefaultAttackSpeedMultiplierForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_PIERCING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.0f)
        << "Expected non-weapon masters to receive no attack speed bonus.";
}

TEST(WeaponMasterHandler, GrantsAttackSpeedBonusForPiercingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.15f)
        << "Expected weapon masters using piercing weapons to gain a speed bonus.";
}

TEST(WeaponMasterHandler, GrantsAttackSpeedBonusForWhippingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_WHIPPING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.15f)
        << "Expected weapon masters using whipping weapons to gain the same speed bonus as piercing weapons.";
}

TEST(WeaponMasterHandler, ReadsWeaponTypeFromWieldedEquipmentInDefaultConstructor) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    context.character.equipment[WIELD] = &context.weapon;
    player_spec::weapon_master_handler handler(&context.character);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.15f)
        << "Expected the default constructor to read the weapon type from the wielded weapon slot.";
}

TEST(WeaponMasterHandler, GrantsBonusDamageForCleavingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_CLEAVING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_total_damage(100), 115)
        << "Expected cleaving weapons to gain 15% bonus damage for weapon masters.";
}

TEST(WeaponMasterHandler, GrantsBonusDamageForAllHeavyDamageWeaponBranches) {
    struct TestCase {
        game_types::weapon_type weapon_type;
        const char *description;
    };

    const TestCase cases[] = {
        {game_types::WT_CLEAVING_TWO, "two-handed cleaving weapons"},
        {game_types::WT_FLAILING, "flailing weapons"},
    };

    for (const TestCase &test_case : cases) {
        WeaponMasterTestContext context(game_types::PS_WeaponMaster, test_case.weapon_type);
        player_spec::weapon_master_handler handler(&context.character, &context.weapon);

        EXPECT_EQ(handler.get_total_damage(100), 115)
            << "Expected " << test_case.description
            << " to gain 15% bonus damage for weapon masters.";
    }
}

TEST(WeaponMasterHandler, LeavesDamageUnchangedForNonBonusWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_total_damage(100), 100)
        << "Expected slashing weapons to keep their original damage total.";
}

TEST(WeaponMasterHandler, GrantsOffensiveBonusForBludgeoningWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_BLUDGEONING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 10)
        << "Expected bludgeoning weapon masters to gain a +10 offensive bonus.";
}

TEST(WeaponMasterHandler, GrantsOffensiveBonusForAllHeavyOffenseWeaponBranches) {
    struct TestCase {
        game_types::weapon_type weapon_type;
        const char *description;
    };

    const TestCase cases[] = {
        {game_types::WT_BLUDGEONING_TWO, "two-handed bludgeoning weapons"},
        {game_types::WT_SMITING, "smiting weapons"},
    };

    for (const TestCase &test_case : cases) {
        WeaponMasterTestContext context(game_types::PS_WeaponMaster, test_case.weapon_type);
        player_spec::weapon_master_handler handler(&context.character, &context.weapon);

        EXPECT_EQ(handler.get_bonus_OB(), 10)
            << "Expected " << test_case.description
            << " to grant a +10 offensive bonus for weapon masters.";
    }
}

TEST(WeaponMasterHandler, GrantsBalancedBonusesForSlashingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 5)
        << "Expected slashing weapon masters to gain a +5 offensive bonus.";
    EXPECT_EQ(handler.get_bonus_PB(), 5)
        << "Expected slashing weapon masters to gain a +5 parry bonus.";
}

TEST(WeaponMasterHandler, GrantsBalancedBonusesForTwoHandedSlashingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING_TWO);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 5)
        << "Expected two-handed slashing weapon masters to gain a +5 offensive bonus.";
    EXPECT_EQ(handler.get_bonus_PB(), 5)
        << "Expected two-handed slashing weapon masters to gain a +5 parry bonus.";
}

TEST(WeaponMasterHandler, GrantsParryBonusForStabbingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_STABBING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_PB(), 10)
        << "Expected stabbing weapon masters to gain a +10 parry bonus.";
}

TEST(WeaponMasterHandler, ReturnsNoBonusesForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_BLUDGEONING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    EXPECT_EQ(handler.get_bonus_OB(), 0)
        << "Expected non-weapon masters to receive no offensive bonus.";
    EXPECT_EQ(handler.get_bonus_PB(), 0)
        << "Expected non-weapon masters to receive no parry bonus.";
}

TEST(WeaponMasterHandler, AppendsReadableScoreMessageForSlashingWeapons) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);
    char message_buffer[256] = {};

    const int written = handler.append_score_message(message_buffer);

    EXPECT_GT(written, 0)
        << "Expected weapon masters to receive a score message for supported weapon types.";
    EXPECT_STREQ(message_buffer, "Your mastery grants balanced prowess and occasional swift strikes.\r\n")
        << "Expected the slashing weapon score message to match the in-game description.";
}

TEST(WeaponMasterHandler, AppendsReadableScoreMessagesForAdditionalWeaponBranches) {
    struct TestCase {
        game_types::weapon_type weapon_type;
        const char *expected_message;
    };

    const TestCase cases[] = {
        {game_types::WT_SLASHING_TWO, "Your mastery grants balanced prowess and occasional swift strikes.\r\n"},
        {game_types::WT_FLAILING, "Your mastery grants power to your blows and renders shields ineffective.\r\n"},
        {game_types::WT_PIERCING, "Your mastery grants swiftness to your blows and renders armor useless.\r\n"},
        {game_types::WT_WHIPPING, "Your mastery grants swiftness to your blows and renders armor useless.\r\n"},
        {game_types::WT_SMITING, "Your mastery grants offensive prowess and dazing blows.\r\n"},
    };

    for (const TestCase &test_case : cases) {
        WeaponMasterTestContext context(game_types::PS_WeaponMaster, test_case.weapon_type);
        player_spec::weapon_master_handler handler(&context.character, &context.weapon);
        char message_buffer[256] = {};

        const int written = handler.append_score_message(message_buffer);

        EXPECT_GT(written, 0)
            << "Expected a score message for weapon type " << static_cast<int>(test_case.weapon_type) << ".";
        EXPECT_STREQ(message_buffer, test_case.expected_message)
            << "Expected the score message to match the in-game description for weapon type "
            << static_cast<int>(test_case.weapon_type) << ".";
    }
}

TEST(WeaponMasterHandler, ReturnsNoScoreMessageForNonSpecialists) {
    WeaponMasterTestContext context(game_types::PS_None, game_types::WT_SLASHING);
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);
    char message_buffer[256] = {};

    EXPECT_EQ(handler.append_score_message(message_buffer), 0)
        << "Expected non-weapon masters to receive no specialization score message.";
}

TEST_F(WeaponMasterProcTest, ImprovesDamageRollWhenCleaveProcSucceedsAndRollsHigherDamage) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_CLEAVING);
    char_data victim{};
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);
    push_test_random_value(0.79);

    EXPECT_EQ(handler.do_on_damage_rolled(20, &victim), 80)
        << "Expected a successful cleave proc to replace the damage roll with the higher reroll.";
}

TEST_F(WeaponMasterProcTest, KeepsDamageRollWhenCleaveProcSucceedsButRerollIsLower) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_CLEAVING_TWO);
    char_data victim{};
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);
    push_test_random_value(0.09);

    EXPECT_EQ(handler.do_on_damage_rolled(20, &victim), 20)
        << "Expected cleave to keep the original damage roll when the reroll is not higher.";
}

TEST_F(WeaponMasterProcTest, IgnoresArmorWhenPiercingProcSucceeds) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    char_data victim{};
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    EXPECT_TRUE(handler.ignores_armor(&victim))
        << "Expected a successful piercing proc to ignore armor.";
}

TEST_F(WeaponMasterProcTest, DoesNotIgnoreArmorWhenPiercingProcFails) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_PIERCING);
    char_data victim{};
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.5);

    EXPECT_FALSE(handler.ignores_armor(&victim))
        << "Expected piercing weapons to leave armor intact when the proc roll fails.";
}

TEST_F(WeaponMasterProcTest, IgnoresShieldWhenWhipProcSucceeds) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_WHIPPING);
    char_data victim{};
    obj_data shield{};
    victim.equipment[WEAR_SHIELD] = &shield;
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    EXPECT_TRUE(handler.ignores_shields(&victim))
        << "Expected a successful whipping proc to bypass shields.";
}

TEST_F(WeaponMasterProcTest, IgnoresShieldWhenFlailProcSucceeds) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_FLAILING);
    char_data victim{};
    obj_data shield{};
    victim.equipment[WEAR_SHIELD] = &shield;
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    EXPECT_TRUE(handler.ignores_shields(&victim))
        << "Expected a successful flailing proc to bypass shields.";
}

TEST_F(WeaponMasterProcTest, SpearProcSucceedsWhenRollIsWithinThreshold) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_STABBING);
    char_data victim{};
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    EXPECT_TRUE(handler.does_spear_proc(&victim))
        << "Expected a successful stabbing proc to punch through armor.";
}

TEST_F(WeaponMasterProcTest, SwordProcRegainsEnergyWhenSlashProcSucceeds) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_SLASHING);
    char_data victim{};
    context.character.specials.ENERGY = 10;
    context.character.specials.fighting = &victim;
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    handler.regain_energy(&victim);

    EXPECT_EQ(context.character.specials.ENERGY, 10 + ENE_TO_HIT / 2)
        << "Expected a successful sword proc to restore half an attack's worth of energy.";
}

TEST_F(WeaponMasterProcTest, BludgeoningProcRemovesVictimEnergyAndClampsAtZero) {
    WeaponMasterTestContext context(game_types::PS_WeaponMaster, game_types::WT_BLUDGEONING_TWO);
    char_data victim{};
    victim.specials.ENERGY = 100;
    player_spec::weapon_master_handler handler(&context.character, &context.weapon);

    push_test_random_value(0.0);

    handler.do_on_damage_dealt(15, &victim);

    EXPECT_EQ(victim.specials.ENERGY, 0)
        << "Expected a successful bludgeoning proc to remove energy without going below zero.";
}
