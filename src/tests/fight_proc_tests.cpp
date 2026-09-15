#include "../spells.h"
#include "../utils.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

bool is_victim_around(const char_data* character);
bool can_double_hit(const char_data* character);
bool does_double_hit_proc(const char_data* character);
bool can_beorning_swipe(char_data* character);
bool does_beorning_swipe_proc(char_data* character);

namespace {

struct FightProcTestContext {
    char_data attacker{};
    char_data victim{};
    char_prof_data profs{};
    byte skills[MAX_SKILLS]{};
    obj_data weapon{};

    FightProcTestContext()
    {
        attacker.profs = &profs;
        attacker.skills = skills;
        attacker.in_room = 1001;

        victim.in_room = 1001;
        attacker.specials.fighting = &victim;

        weapon.obj_flags.type_flag = ITEM_WEAPON;
        attacker.equipment[WIELD] = &weapon;
    }
};

} // namespace

class FightProcTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
    }
};

TEST(FightHelpers, ReportsVictimAsMissingWhenCombatTargetIsNull) {
    FightProcTestContext context;
    context.attacker.specials.fighting = nullptr;

    EXPECT_FALSE(is_victim_around(&context.attacker))
        << "Expected victim checks to fail once the attacker is no longer fighting anyone.";
}

TEST(FightHelpers, ReportsVictimAsMissingWhenTargetLeavesTheRoom) {
    FightProcTestContext context;
    context.victim.in_room = 2002;

    EXPECT_FALSE(is_victim_around(&context.attacker))
        << "Expected victim checks to fail when the target is no longer in the same room.";
}

TEST(FightHelpers, ReportsVictimAsAvailableWhenTargetRemainsInTheRoom) {
    FightProcTestContext context;

    EXPECT_TRUE(is_victim_around(&context.attacker))
        << "Expected victim checks to succeed while the target remains in the same room.";
}

TEST(FightHelpers, RequiresLightFightingSpecializationForDoubleHit) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_WeaponMaster);

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to stay unavailable for non-light-fighting specializations.";
}

TEST(FightHelpers, RejectsDoubleHitWhenWeaponIsTooHeavy) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.weapon.obj_flags.value[2] = 3;
    context.weapon.obj_flags.weight = LIGHT_WEAPON_WEIGHT_CUTOFF + 1;

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to reject bulk-3 weapons once they cross the light-weapon weight cutoff.";
}

TEST(FightHelpers, RejectsDoubleHitWhenAttackerIsUsingTwoHandedStyle) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.attacker.specials.affected_by = AFF_TWOHANDED;

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to stay unavailable while the attacker is flagged as two-handed.";
}

TEST(FightHelpers, AllowsDoubleHitForLightWeaponAgainstNearbyVictim) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.weapon.obj_flags.value[2] = 2;
    context.weapon.obj_flags.weight = LIGHT_WEAPON_WEIGHT_CUTOFF + 50;

    EXPECT_TRUE(can_double_hit(&context.attacker))
        << "Expected light-fighting characters to double-hit with a light one-handed weapon against a nearby victim.";
}

TEST_F(FightProcTest, DoubleHitProcSucceedsAtOrAboveTwentyPercentThreshold) {
    FightProcTestContext context;

    push_test_random_value(0.80);
    EXPECT_TRUE(does_double_hit_proc(&context.attacker))
        << "Expected double-hit procs to succeed when the random roll reaches the 20 percent threshold.";

    push_test_random_value(0.79);
    EXPECT_FALSE(does_double_hit_proc(&context.attacker))
        << "Expected double-hit procs to fail when the random roll stays below the 20 percent threshold.";
}

TEST(FightHelpers, RequiresBeorningRaceForSwipe) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_HUMAN;

    EXPECT_FALSE(can_beorning_swipe(&context.attacker))
        << "Expected swipe to stay unavailable for non-beorning characters.";
}

TEST(FightHelpers, RequiresNearbyVictimForBeorningSwipe) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_BEORNING;
    context.victim.in_room = 2002;

    EXPECT_FALSE(can_beorning_swipe(&context.attacker))
        << "Expected beorning swipe to require the fighting target to remain nearby.";
}

TEST(FightHelpers, AllowsBeorningSwipeWhenVictimIsNearby) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_BEORNING;

    EXPECT_TRUE(can_beorning_swipe(&context.attacker))
        << "Expected beorning swipe to be available when a beorning is actively fighting a nearby target.";
}

TEST_F(FightProcTest, BeorningSwipeProcUsesCombinedWarriorSkillAndLevelChance) {
    FightProcTestContext context;
    context.attacker.player.level = 30;
    context.attacker.profs = &context.profs;
    context.attacker.skills = context.skills;
    context.profs.prof_level[PROF_WARRIOR] = 18;
    context.skills[SKILL_SWIPE] = 70;

    push_test_random_value(0.16);
    EXPECT_TRUE(does_beorning_swipe_proc(&context.attacker))
        << "Expected swipe procs to succeed when the roll stays within the warrior+skill+level chance.";

    push_test_random_value(0.18);
    EXPECT_FALSE(does_beorning_swipe_proc(&context.attacker))
        << "Expected swipe procs to fail once the roll exceeds the computed warrior+skill+level chance.";
}
