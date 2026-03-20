#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"
#include "../utils.h"
#include <gtest/gtest.h>

namespace olog_hai {
int get_prob_skill(char_data* attacker, char_data* victim, int skill);
void apply_victim_delay(char_data* victim, int delay);
bool is_skill_valid(char_data* ch, const int& skill_id);
char_data* is_target_valid(char_data* attacker, waiting_type* target);
char_data* is_smash_target_valid(char_data* attacker, waiting_type* target);
bool is_target_in_room(char_data* attacker, char_data* victim);
char_data* get_random_target(char_data* ch, char_data* original_victim);
int get_base_skill_damage(char_data& olog_hai, int prob);
int calculate_overrun_damage(char_data& attacker, int prob);
int calculate_smash_damage(char_data& attacker, int prob);
int calculate_cleave_damage(char_data& attacker, int prob);
int calculate_stomp_damage(char_data& attacker, int prob);
void apply_frenzy_affect(char_data* character);
void room_target(char_data* ch, void (*skill_damage)(char_data* character, char_data* victim));
}

int get_direction(std::string direction);
bool is_direction_valid(char_data* ch, int cmd);
extern room_data world;
extern char_data* waiting_list;

namespace {

char_data* targeted_victims[4]{};
int targeted_victim_count = 0;

void clear_targeted_victims()
{
    targeted_victim_count = 0;
    for (char_data*& victim : targeted_victims) {
        victim = nullptr;
    }
}

void record_target(char_data*, char_data* victim)
{
    if (targeted_victim_count < 4) {
        targeted_victims[targeted_victim_count++] = victim;
    }
}

struct OlogHaiTestContext {
    char_data attacker{};
    char_data original_victim{};
    char_data extra_target{};
    char_data mount{};
    char_prof_data profs{};
    byte skills[MAX_SKILLS]{};
    byte knowledge[MAX_SKILLS]{};
    affected_type frenzy{};
    room_data room{};
    obj_data weapon{};
    waiting_type target{};
    long original_room_flags = 0;
    char_data* original_room_people = nullptr;

    OlogHaiTestContext()
    {
        attacker.profs = &profs;
        attacker.skills = skills;
        attacker.knowledge = knowledge;
        attacker.in_room = 7;
        attacker.player.race = RACE_OLOGHAI;
        attacker.specials.tactics = TACTICS_AGGRESSIVE;
        attacker.tmpabilities.str = 18;
        attacker.tmpabilities.dex = 12;
        attacker.points.OB = 10;
        attacker.points.parry = 4;
        attacker.points.dodge = 3;

        original_victim.in_room = 7;
        original_victim.tmpabilities.dex = 12;
        original_victim.points.dodge = 8;
        original_victim.points.parry = 6;
        extra_target.in_room = 7;
        mount.in_room = 7;

        attacker.next_in_room = &original_victim;
        original_victim.next_in_room = &extra_target;
        extra_target.next_in_room = nullptr;

        original_room_flags = world[attacker.in_room].room_flags;
        original_room_people = world[attacker.in_room].people;
        world[attacker.in_room].room_flags = 0;
        world[attacker.in_room].people = &attacker;

        weapon.obj_flags.type_flag = ITEM_WEAPON;
        attacker.equipment[WIELD] = &weapon;
    }

    ~OlogHaiTestContext()
    {
        if (attacker.abs_number) {
            remove_char_exists(attacker.abs_number);
        }
        if (original_victim.abs_number) {
            remove_char_exists(original_victim.abs_number);
        }
        if (extra_target.abs_number) {
            remove_char_exists(extra_target.abs_number);
        }
        if (mount.abs_number) {
            remove_char_exists(mount.abs_number);
        }
        world[attacker.in_room].room_flags = original_room_flags;
        world[attacker.in_room].people = original_room_people;
    }
};

} // namespace

class OlogHaiProcTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
        clear_targeted_victims();
    }
};

TEST(OlogHaiHelpers, ParsesShortAndLongDirectionsCaseInsensitively) {
    EXPECT_EQ(get_direction("north"), NORTH);
    EXPECT_EQ(get_direction("W"), WEST);
    EXPECT_EQ(get_direction("Down"), DOWN);
}

TEST(OlogHaiHelpers, RejectsUnknownDirections) {
    EXPECT_EQ(get_direction("sideways"), -1);
}

TEST(OlogHaiHelpers, DetectsWhetherTargetRemainsInSameRoom) {
    OlogHaiTestContext context;

    EXPECT_TRUE(olog_hai::is_target_in_room(&context.attacker, &context.original_victim));

    context.original_victim.in_room = 8;
    EXPECT_FALSE(olog_hai::is_target_in_room(&context.attacker, &context.original_victim));
}

TEST_F(OlogHaiProcTest, ReturnsOriginalVictimWhenNoAlternateTargetsExist) {
    OlogHaiTestContext context;
    context.attacker.next_in_room = &context.original_victim;
    context.original_victim.next_in_room = nullptr;

    EXPECT_EQ(olog_hai::get_random_target(&context.attacker, &context.original_victim), &context.original_victim)
        << "Expected random target selection to fall back to the original victim when no one else is present.";
}

TEST_F(OlogHaiProcTest, ChoosesAlternateTargetWhenRandomSelectionFindsOne) {
    OlogHaiTestContext context;
    push_test_random_value(0.0);

    EXPECT_EQ(olog_hai::get_random_target(&context.attacker, &context.original_victim), &context.extra_target)
        << "Expected random target selection to choose another room occupant when one is available.";
}

TEST_F(OlogHaiProcTest, ComputesProbSkillFromStatsAndRandomRoll) {
    OlogHaiTestContext context;
    context.attacker.specials2.act = MOB_ISNPC;
    context.attacker.player.level = 20;
    context.original_victim.specials2.act = MOB_ISNPC;
    context.original_victim.player.level = 20;
    context.knowledge[SKILL_SMASH] = 70;

    push_test_random_value(0.0);
    EXPECT_EQ(olog_hai::get_prob_skill(&context.attacker, &context.original_victim, SKILL_SMASH), -50)
        << "Expected the minimum queued roll to use the low end of the olog-hai skill probability formula.";

    push_test_random_value(0.99);
    EXPECT_EQ(olog_hai::get_prob_skill(&context.attacker, &context.original_victim, SKILL_SMASH), 49)
        << "Expected the high queued roll to use the top end of the olog-hai skill probability formula.";
}

TEST(OlogHaiHelpers, RejectsSkillUseForNonOlogHaiCharacters) {
    OlogHaiTestContext context;
    context.attacker.player.race = RACE_HUMAN;
    context.knowledge[SKILL_SMASH] = 50;

    EXPECT_FALSE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected olog-hai skills to stay unavailable to other races.";
}

TEST(OlogHaiHelpers, RejectsSkillUseForShadowCharacters) {
    OlogHaiTestContext context;
    context.knowledge[SKILL_SMASH] = 50;
    context.attacker.specials2.act = PLR_ISSHADOW;

    EXPECT_FALSE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected shadow-form characters to fail olog-hai skill validation.";
}

TEST(OlogHaiHelpers, RejectsSkillUseInPeaceRooms) {
    OlogHaiTestContext context;
    context.knowledge[SKILL_SMASH] = 50;
    world[context.attacker.in_room].room_flags = PEACEROOM;

    EXPECT_FALSE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected olog-hai skills to be blocked in peace rooms.";
}

TEST(OlogHaiHelpers, RejectsSkillUseWhenSkillIsUntrained) {
    OlogHaiTestContext context;
    context.knowledge[SKILL_SMASH] = 0;

    EXPECT_FALSE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected skill validation to fail when the character has not learned the requested skill.";
}

TEST(OlogHaiHelpers, RejectsSkillUseWithoutWeaponEquipped) {
    OlogHaiTestContext context;
    context.knowledge[SKILL_SMASH] = 50;
    context.attacker.equipment[WIELD] = nullptr;

    EXPECT_FALSE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected olog-hai skill validation to require a wielded weapon.";
}

TEST(OlogHaiHelpers, AcceptsSkillUseWhenAllValidationChecksPass) {
    OlogHaiTestContext context;
    context.knowledge[SKILL_SMASH] = 50;

    EXPECT_TRUE(olog_hai::is_skill_valid(&context.attacker, SKILL_SMASH))
        << "Expected a trained olog-hai with a weapon in a non-peaceful room to pass skill validation.";
}

TEST(OlogHaiHelpers, ResolvesCharacterTargetsWhenTheCharacterExists) {
    OlogHaiTestContext context;
    context.original_victim.abs_number = 41;
    context.target.targ1.type = TARGET_CHAR;
    context.target.targ1.ptr.ch = &context.original_victim;
    context.target.targ1.ch_num = 41;
    set_char_exists(41);

    EXPECT_EQ(olog_hai::is_target_valid(&context.attacker, &context.target), &context.original_victim)
        << "Expected character targets to resolve when the referenced victim still exists.";
}

TEST(OlogHaiHelpers, ReturnsNullForMissingCharacterTargets) {
    OlogHaiTestContext context;
    context.original_victim.abs_number = 42;
    context.target.targ1.type = TARGET_CHAR;
    context.target.targ1.ptr.ch = &context.original_victim;
    context.target.targ1.ch_num = 42;

    EXPECT_EQ(olog_hai::is_target_valid(&context.attacker, &context.target), nullptr)
        << "Expected character targets to fail resolution when the victim no longer exists.";
}

TEST(OlogHaiHelpers, ResolvesTextTargetsUsingRoomVisibilityLookup) {
    OlogHaiTestContext context;
    txt_block target_text{};
    char victim_name[] = "ologvictim";
    context.original_victim.player.name = victim_name;
    target_text.text = victim_name;
    context.target.targ1.type = TARGET_TEXT;
    context.target.targ1.ptr.text = &target_text;

    EXPECT_EQ(olog_hai::is_target_valid(&context.attacker, &context.target), &context.original_victim)
        << "Expected text targets to resolve through room visibility lookup when the named victim is present.";
}

TEST(OlogHaiHelpers, UsesCurrentFightTargetWhenSmashTargetIsOmitted) {
    OlogHaiTestContext context;
    context.attacker.specials.fighting = &context.original_victim;

    EXPECT_EQ(olog_hai::is_smash_target_valid(&context.attacker, &context.target), &context.original_victim)
        << "Expected smash validation to fall back to the current combat target when no explicit target is supplied.";
}

TEST(OlogHaiHelpers, RejectsSmashTargetWhenNoTargetIsProvidedAndNobodyIsFighting) {
    OlogHaiTestContext context;

    EXPECT_EQ(olog_hai::is_smash_target_valid(&context.attacker, &context.target), nullptr)
        << "Expected smash validation to fail when no explicit target exists and the attacker is not fighting anyone.";
}

TEST(OlogHaiHelpers, RejectsSmashTargetWhenVictimCannotBeSeen) {
    OlogHaiTestContext context;
    context.original_victim.abs_number = 46;
    context.target.targ1.type = TARGET_CHAR;
    context.target.targ1.ptr.ch = &context.original_victim;
    context.target.targ1.ch_num = 46;
    context.attacker.specials.affected_by = AFF_BLIND;
    set_char_exists(46);

    EXPECT_EQ(olog_hai::is_smash_target_valid(&context.attacker, &context.target), nullptr)
        << "Expected smash validation to reject targets the attacker cannot currently see.";
}

TEST(OlogHaiHelpers, RejectsSmashTargetWhenVictimLeftTheRoom) {
    OlogHaiTestContext context;
    context.original_victim.abs_number = 43;
    context.target.targ1.type = TARGET_CHAR;
    context.target.targ1.ptr.ch = &context.original_victim;
    context.target.targ1.ch_num = 43;
    context.original_victim.in_room = 8;
    set_char_exists(43);

    EXPECT_EQ(olog_hai::is_smash_target_valid(&context.attacker, &context.target), nullptr)
        << "Expected smash validation to fail when the chosen victim is no longer in the attacker's room.";
}

TEST(OlogHaiHelpers, RejectsSelfAsSmashTarget) {
    OlogHaiTestContext context;
    context.attacker.abs_number = 44;
    context.target.targ1.type = TARGET_CHAR;
    context.target.targ1.ptr.ch = &context.attacker;
    context.target.targ1.ch_num = 44;
    set_char_exists(44);

    EXPECT_EQ(olog_hai::is_smash_target_valid(&context.attacker, &context.target), nullptr)
        << "Expected smash validation to reject self-targeting.";
}

TEST(OlogHaiHelpers, ComputesBaseSkillDamageFromWarriorLevelProbabilityAndTactics) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;

    EXPECT_EQ(olog_hai::get_base_skill_damage(context.attacker, 50), 13)
        << "Expected base skill damage to scale with warrior level, success probability, and tactics.";
}

TEST(OlogHaiHelpers, TwoHandedStyleAppliesCurrentBaseDamageMultiplier) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;
    context.attacker.specials.affected_by = AFF_TWOHANDED;

    EXPECT_EQ(olog_hai::get_base_skill_damage(context.attacker, 50), 19)
        << "Expected two-handed style to apply the current 3/2 integer damage multiplier to base olog-hai skill damage.";
}

TEST(OlogHaiHelpers, FrenzyAffectAppliesItsCurrentIntegerScaledDamageBonus) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;
    context.frenzy.type = SKILL_FRENZY;
    context.attacker.affected = &context.frenzy;

    EXPECT_EQ(olog_hai::get_base_skill_damage(context.attacker, 50), 14)
        << "Expected frenzy to increase base skill damage according to the current integer-scaled multiplier path.";
}

TEST(OlogHaiHelpers, HeavyFightingAndRidingAdjustOverrunDamage) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.profs.specialization = static_cast<int>(game_types::PS_HeavyFighting);
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;
    context.attacker.mount_data.mount = &context.extra_target;
    context.attacker.mount_data.mount_number = 12;
    context.extra_target.abs_number = 12;
    set_char_exists(12);

    EXPECT_EQ(olog_hai::calculate_overrun_damage(context.attacker, 50), 14)
        << "Expected overrun damage to use the current heavy-fighting and riding bonuses.";
}

TEST(OlogHaiHelpers, WildFightingAddsFlatBonusToSmashDamage) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.profs.specialization = static_cast<int>(game_types::PS_WildFighting);
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;

    EXPECT_EQ(olog_hai::calculate_smash_damage(context.attacker, 50), 18)
        << "Expected smash damage to gain the flat wild-fighting bonus.";
}

TEST(OlogHaiHelpers, HeavyFightingAddsFlatBonusToCleaveDamage) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.profs.specialization = static_cast<int>(game_types::PS_HeavyFighting);
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;

    EXPECT_EQ(olog_hai::calculate_cleave_damage(context.attacker, 50), 18)
        << "Expected cleave damage to gain the flat heavy-fighting bonus.";
}

TEST(OlogHaiHelpers, StompDamageUsesHalfOfBaseSkillDamage) {
    OlogHaiTestContext context;
    context.profs.prof_level[PROF_WARRIOR] = 20;
    context.attacker.specials.tactics = TACTICS_AGGRESSIVE;

    EXPECT_EQ(olog_hai::calculate_stomp_damage(context.attacker, 50), 6)
        << "Expected stomp damage to use half of the base olog-hai skill damage.";
}

TEST(OlogHaiHelpers, ApplyFrenzyAffectAddsSkillAffectAndSetsBerserkTactics) {
    OlogHaiTestContext context;
    context.attacker.specials.tactics = TACTICS_NORMAL;

    olog_hai::apply_frenzy_affect(&context.attacker);

    ASSERT_NE(context.attacker.affected, nullptr) << "Expected frenzy to attach an affect to the attacker.";
    EXPECT_EQ(context.attacker.affected->type, SKILL_FRENZY);
    EXPECT_EQ(context.attacker.affected->duration, 20);
    EXPECT_EQ(context.attacker.affected->modifier, 20);
    EXPECT_EQ(context.attacker.specials.tactics, TACTICS_BERSERK)
        << "Expected frenzy to switch the character into berserk tactics.";

    affect_remove(&context.attacker, context.attacker.affected);
}

TEST(OlogHaiHelpers, VictimDelaySkipsNobashNpcs) {
    OlogHaiTestContext context;
    context.original_victim.specials2.act = MOB_ISNPC | MOB_NOBASH;
    waiting_list = nullptr;

    olog_hai::apply_victim_delay(&context.original_victim, 9);

    EXPECT_EQ(context.original_victim.delay.wait_value, 0)
        << "Expected apply_victim_delay to leave no delay state on NPCs flagged as nobash targets.";
    EXPECT_EQ(waiting_list, nullptr);
}

TEST(OlogHaiHelpers, VictimDelaySkipsTargetsAlreadyBashed) {
    OlogHaiTestContext context;
    context.original_victim.specials.affected_by = AFF_BASH;
    waiting_list = nullptr;

    olog_hai::apply_victim_delay(&context.original_victim, 9);

    EXPECT_EQ(context.original_victim.delay.wait_value, 0)
        << "Expected apply_victim_delay to leave existing bash victims untouched.";
    EXPECT_EQ(waiting_list, nullptr);
}

TEST(OlogHaiHelpers, VictimDelayAddsWaitStateForEligibleTargets) {
    OlogHaiTestContext context;
    waiting_list = nullptr;

    olog_hai::apply_victim_delay(&context.original_victim, 9);

    EXPECT_EQ(context.original_victim.delay.wait_value, 9);
    EXPECT_EQ(context.original_victim.delay.cmd, CMD_BASH);
    EXPECT_EQ(context.original_victim.delay.subcmd, 2);
    EXPECT_EQ(context.original_victim.delay.priority, 80);
    EXPECT_TRUE(IS_SET(context.original_victim.specials.affected_by, AFF_WAITING | AFF_BASH))
        << "Expected apply_victim_delay to put eligible victims into the bash wait state.";
    EXPECT_EQ(waiting_list, &context.original_victim);

    waiting_list = nullptr;
    context.original_victim.delay.wait_value = 0;
    context.original_victim.specials.affected_by = 0;
}

TEST(OlogHaiHelpers, RoomTargetSkipsAttackerAndMountedCreature) {
    OlogHaiTestContext context;
    clear_targeted_victims();
    context.attacker.mount_data.mount = &context.mount;
    context.attacker.mount_data.mount_number = 45;
    context.mount.abs_number = 45;
    set_char_exists(45);

    context.extra_target.next_in_room = &context.mount;
    context.mount.next_in_room = nullptr;

    olog_hai::room_target(&context.attacker, &record_target);

    ASSERT_EQ(targeted_victim_count, 2)
        << "Expected room targeting to visit each non-attacker, non-mount room occupant exactly once.";
    EXPECT_EQ(targeted_victims[0], &context.original_victim);
    EXPECT_EQ(targeted_victims[1], &context.extra_target);
}

TEST(OlogHaiHelpers, DirectionValidationRejectsMissingExit) {
    OlogHaiTestContext context;
    auto* original_exit = world[context.attacker.in_room].dir_option[NORTH];
    world[context.attacker.in_room].dir_option[NORTH] = nullptr;

    EXPECT_FALSE(is_direction_valid(&context.attacker, NORTH))
        << "Expected direction validation to fail when no exit exists in the requested direction.";

    world[context.attacker.in_room].dir_option[NORTH] = original_exit;
}

TEST(OlogHaiHelpers, DirectionValidationRejectsNowhereExit) {
    OlogHaiTestContext context;
    room_direction_data north_exit{};
    auto* original_exit = world[context.attacker.in_room].dir_option[NORTH];
    north_exit.to_room = NOWHERE;
    world[context.attacker.in_room].dir_option[NORTH] = &north_exit;

    EXPECT_FALSE(is_direction_valid(&context.attacker, NORTH))
        << "Expected direction validation to fail when the exit leads nowhere.";

    world[context.attacker.in_room].dir_option[NORTH] = original_exit;
}

TEST(OlogHaiHelpers, DirectionValidationRejectsHiddenClosedExitWithoutHolylight) {
    OlogHaiTestContext context;
    room_direction_data north_exit{};
    auto* original_exit = world[context.attacker.in_room].dir_option[NORTH];
    north_exit.to_room = 8;
    north_exit.exit_info = EX_CLOSED | EX_ISHIDDEN;
    world[context.attacker.in_room].dir_option[NORTH] = &north_exit;

    EXPECT_FALSE(is_direction_valid(&context.attacker, NORTH))
        << "Expected hidden closed exits to be treated as unavailable for characters without holylight.";

    world[context.attacker.in_room].dir_option[NORTH] = original_exit;
}

TEST(OlogHaiHelpers, DirectionValidationReportsClosedKeywordForVisibleDoor) {
    OlogHaiTestContext context;
    room_direction_data north_exit{};
    auto* original_exit = world[context.attacker.in_room].dir_option[NORTH];
    char door_keyword[] = "oak door";
    north_exit.to_room = 8;
    north_exit.exit_info = EX_CLOSED;
    north_exit.keyword = door_keyword;
    world[context.attacker.in_room].dir_option[NORTH] = &north_exit;

    EXPECT_FALSE(is_direction_valid(&context.attacker, NORTH))
        << "Expected visible closed exits with keywords to be rejected by direction validation.";

    world[context.attacker.in_room].dir_option[NORTH] = original_exit;
}

TEST(OlogHaiHelpers, DirectionValidationAllowsOpenExitToReachableRoom) {
    OlogHaiTestContext context;
    room_direction_data north_exit{};
    auto* original_exit = world[context.attacker.in_room].dir_option[NORTH];
    north_exit.to_room = 8;
    north_exit.exit_info = 0;
    world[context.attacker.in_room].dir_option[NORTH] = &north_exit;

    EXPECT_TRUE(is_direction_valid(&context.attacker, NORTH))
        << "Expected direction validation to accept an open exit leading to a real room.";

    world[context.attacker.in_room].dir_option[NORTH] = original_exit;
}
