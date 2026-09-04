#include "../char_utils.h"
#include "../handler.h"
#include "../object_utils.h"
#include "../spells.h"
#include "../utils.h"
#include <gtest/gtest.h>

namespace utils {
int get_race_perception(const char_data& character);
}

namespace {

struct CharUtilsTestContext {
    char_data character{};
    char_prof_data profs{};
    byte skills[MAX_SKILLS]{};
    byte knowledge[MAX_SKILLS]{};

    CharUtilsTestContext()
    {
        character.profs = &profs;
        character.skills = skills;
        character.knowledge = knowledge;
        character.player.name = const_cast<char*>("player-name");
        character.player.short_descr = const_cast<char*>("mob-name");
    }
};

affected_type make_affect(int type, int duration, affected_type* next = nullptr)
{
    affected_type affect{};
    affect.type = type;
    affect.duration = duration;
    affect.next = next;
    return affect;
}

obj_data make_object(int type_flag, int weight = 0, int bulk = 0)
{
    obj_data object{};
    object.obj_flags.type_flag = type_flag;
    object.obj_flags.weight = weight;
    object.obj_flags.value[2] = bulk;
    return object;
}

} // namespace

TEST(CharUtils, ReturnsNeutralPronounsForSexlessCharacters) {
    CharUtilsTestContext context;
    context.character.player.sex = 0;

    EXPECT_STREQ(utils::his_or_her(context.character), "its");
    EXPECT_STREQ(utils::he_or_she(context.character), "it");
    EXPECT_STREQ(utils::him_or_her(context.character), "it");
}

TEST(CharUtils, ReturnsFemalePronounsForFemaleCharacters) {
    CharUtilsTestContext context;
    context.character.player.sex = 2;

    EXPECT_STREQ(utils::his_or_her(context.character), "her");
    EXPECT_STREQ(utils::he_or_she(context.character), "she");
    EXPECT_STREQ(utils::him_or_her(context.character), "her");
}

TEST(CharUtils, ReturnsSafeDefaultPronounsForUnexpectedSexValues) {
    CharUtilsTestContext context;
    context.character.player.sex = 9;

    EXPECT_STREQ(utils::his_or_her(context.character), "its");
    EXPECT_STREQ(utils::he_or_she(context.character), "it");
    EXPECT_STREQ(utils::him_or_her(context.character), "it");
}

TEST(CharUtils, UsesDefaultCombatModesWhenCallersPassNonPositiveValues) {
    CharUtilsTestContext context;

    utils::set_tactics(context.character, 0);
    utils::set_shooting(context.character, -2);
    utils::set_casting(context.character, 0);

    EXPECT_EQ(utils::get_tactics(context.character), TACTICS_NORMAL)
        << "Expected invalid tactics values to fall back to normal tactics.";
    EXPECT_EQ(utils::get_shooting(context.character), SHOOTING_NORMAL)
        << "Expected invalid shooting values to fall back to normal shooting.";
    EXPECT_EQ(utils::get_casting(context.character), CASTING_NORMAL)
        << "Expected invalid casting values to fall back to normal casting.";
}

TEST(CharUtils, IgnoresCombatModeUpdatesForNpcCharacters) {
    CharUtilsTestContext context;
    context.character.specials2.act = MOB_ISNPC;

    utils::set_tactics(context.character, TACTICS_BERSERK);
    utils::set_shooting(context.character, SHOOTING_FAST);
    utils::set_casting(context.character, CASTING_FAST);

    EXPECT_EQ(utils::get_tactics(context.character), 0)
        << "Expected NPCs to report no player tactics state.";
    EXPECT_EQ(utils::get_shooting(context.character), 0)
        << "Expected NPCs to report no player shooting state.";
    EXPECT_EQ(utils::get_casting(context.character), 0)
        << "Expected NPCs to report no player casting state.";
}

TEST(CharUtils, ReadsAndWritesValidConditionSlotsOnly) {
    CharUtilsTestContext context;

    utils::set_condition(context.character, 1, 42);
    utils::set_condition(context.character, 4, 99);

    EXPECT_EQ(utils::get_condition(context.character, 1), 42)
        << "Expected valid condition slots to store the written value.";
    EXPECT_EQ(utils::get_condition(context.character, 4), 0)
        << "Expected out-of-range condition lookups to stay at zero.";
}

TEST(CharUtils, ReturnsPlayerIndexForPlayersAndSentinelForNpcs) {
    CharUtilsTestContext player_context;
    player_context.character.player_index = 77;

    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;
    npc_context.character.player_index = 88;

    EXPECT_EQ(utils::get_index(player_context.character), 77);
    EXPECT_EQ(utils::get_index(npc_context.character), -1)
        << "Expected NPCs to report the sentinel player-index value.";
}

TEST(CharUtils, ReturnsPlayerNameForPlayersAndShortDescriptionForNpcs) {
    CharUtilsTestContext player_context;
    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;

    EXPECT_STREQ(utils::get_name(player_context.character), "player-name");
    EXPECT_STREQ(utils::get_name(npc_context.character), "mob-name");
}

TEST(CharUtils, ReturnsCharacterLevelForGeneralOrInvalidProfessionQueries) {
    CharUtilsTestContext context;
    context.character.player.level = 31;
    context.profs.prof_level[PROF_MAGE] = 18;

    EXPECT_EQ(utils::get_prof_level(PROF_GENERAL, context.character), 31);
    EXPECT_EQ(utils::get_prof_level(MAX_PROFS + 1, context.character), 31)
        << "Expected invalid profession lookups to fall back to character level.";
}

TEST(CharUtils, StoresProfessionLevelsForPlayerCharactersOnly) {
    CharUtilsTestContext player_context;
    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;

    utils::set_prof_level(PROF_MAGE, player_context.character, 22);
    utils::set_prof_level(PROF_MAGE, npc_context.character, 29);

    EXPECT_EQ(utils::get_prof_level(PROF_MAGE, player_context.character), 22);
    EXPECT_EQ(utils::get_prof_level(PROF_MAGE, npc_context.character), 0)
        << "Expected NPC profession writes to be ignored by the player-only setter.";
}

TEST(CharUtils, ReturnsRaceSpecificProfessionCaps) {
    CharUtilsTestContext orc_context;
    orc_context.character.player.race = RACE_ORC;

    CharUtilsTestContext uruk_context;
    uruk_context.character.player.race = RACE_URUK;

    CharUtilsTestContext default_context;
    default_context.character.player.race = RACE_HUMAN;

    EXPECT_EQ(utils::get_max_race_prof_level(PROF_WARRIOR, orc_context.character), 20);
    EXPECT_EQ(utils::get_max_race_prof_level(PROF_MAGE, uruk_context.character), 27);
    EXPECT_EQ(utils::get_max_race_prof_level(PROF_CLERIC, default_context.character), 30);
}

TEST(CharUtils, ComputesCarryLimitsFromCurrentStatsAndLevel) {
    CharUtilsTestContext context;
    context.character.tmpabilities.str = 12;
    context.character.tmpabilities.dex = 9;
    context.character.player.level = 14;

    EXPECT_EQ(utils::get_carry_weight_limit(context.character), 14000);
    EXPECT_EQ(utils::get_carry_item_limit(context.character), 16);
}

TEST(CharUtils, RecognizesTwoHandedFlagFromAffectedBits) {
    CharUtilsTestContext context;
    context.character.specials.affected_by = AFF_TWOHANDED;

    EXPECT_TRUE(utils::is_twohanded(context.character))
        << "Expected the two-handed helper to mirror the affected flag bit.";
}

TEST(CharUtils, AppliesConfusePenaltyToKnowledgeAndSkillLookups) {
    CharUtilsTestContext context;
    context.character.specials.affected_by = AFF_CONFUSE;
    context.knowledge[SKILL_SWIPE] = 80;

    affected_type confuse = make_affect(SPELL_CONFUSE, 8);
    context.character.affected = &confuse;

    EXPECT_EQ(utils::get_knowledge(context.character, SKILL_SWIPE), 74)
        << "Expected confusion to reduce knowledge by the affect-derived modifier.";
    EXPECT_EQ(utils::get_skill(context.character, SKILL_SWIPE), 68)
        << "Expected skill lookup to apply confusion on top of the raw-skill source.";
}

TEST(CharUtils, FindsMatchingSpellAffectAndReturnsNullWhenMissing) {
    CharUtilsTestContext context;
    affected_type tail = make_affect(SPELL_ARMOR, 3);
    affected_type head = make_affect(SPELL_CONFUSE, 5, &tail);
    context.character.affected = &head;

    EXPECT_EQ(utils::is_affected_by_spell(context.character, SPELL_CONFUSE), &head);
    EXPECT_EQ(utils::is_affected_by_spell(context.character, SPELL_HAZE), nullptr)
        << "Expected spell-affect lookup to return null when the spell is absent.";
}

TEST(CharUtils, CapsBalancedStrengthByRaceAndLeavesUnknownRacesUntouched) {
    CharUtilsTestContext hobbit_context;
    hobbit_context.character.player.race = RACE_HOBBIT;
    hobbit_context.character.tmpabilities.str = 25;

    CharUtilsTestContext unknown_context;
    unknown_context.character.player.race = 99;
    unknown_context.character.tmpabilities.str = 25;

    EXPECT_EQ(utils::get_bal_strength(hobbit_context.character), 22);
    EXPECT_DOUBLE_EQ(utils::get_bal_strength_d(hobbit_context.character), 22.5);
    EXPECT_EQ(utils::get_bal_strength(unknown_context.character), 25);
}

TEST(CharUtils, RecognizesEvilRaceFamilies) {
    CharUtilsTestContext magus_context;
    magus_context.character.player.race = RACE_MAGUS;

    CharUtilsTestContext human_context;
    human_context.character.player.race = RACE_HUMAN;

    EXPECT_TRUE(utils::is_evil_race(magus_context.character));
    EXPECT_FALSE(utils::is_evil_race(human_context.character));
}

TEST(CharUtils, UsesStoredEncumbranceAndWeightsForNonSpecialists) {
    CharUtilsTestContext context;
    context.character.specials.encumb_weight = 321;
    context.character.specials.worn_weight = 654;
    context.character.points.encumb = 7;
    context.character.specials2.leg_encumb = 8;

    EXPECT_EQ(utils::get_encumbrance_weight(context.character), 321);
    EXPECT_EQ(utils::get_worn_weight(context.character), 654);
    EXPECT_EQ(utils::get_encumbrance(context.character), 7);
    EXPECT_EQ(utils::get_leg_encumbrance(context.character), 8);
}

TEST(CharUtils, RecalculatesHeavyFightingEncumbranceAndWeightsFromEquipment) {
    CharUtilsTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_HeavyFighting);

    obj_data body = make_object(ITEM_ARMOR, 1200, 4);
    obj_data weapon = make_object(ITEM_WEAPON, 400, 5);
    context.character.equipment[WEAR_BODY] = &body;
    context.character.equipment[WIELD] = &weapon;

    EXPECT_EQ(utils::get_encumbrance_weight(context.character), 1350);
    EXPECT_EQ(utils::get_worn_weight(context.character), 1350);
    EXPECT_EQ(utils::get_encumbrance(context.character), 5);
}

TEST(CharUtils, RecalculatesLightFightingEncumbranceAndWeightsFromEquipment) {
    CharUtilsTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);

    obj_data body = make_object(ITEM_ARMOR, 300, 3);
    obj_data weapon = make_object(ITEM_WEAPON, 250, 4);
    context.character.equipment[WEAR_BODY] = &body;
    context.character.equipment[WIELD] = &weapon;

    EXPECT_EQ(utils::get_encumbrance_weight(context.character), 160);
    EXPECT_EQ(utils::get_worn_weight(context.character), 160);
    EXPECT_EQ(utils::get_encumbrance(context.character), 4);
}

TEST(CharUtils, ComputesSkillAndDodgePenaltiesFromEncumbranceAndStrength) {
    CharUtilsTestContext context;
    context.character.tmpabilities.str = 20;
    context.character.specials.encumb_weight = 400;
    context.character.points.encumb = 5;
    context.character.specials.worn_weight = 300;
    context.character.specials2.leg_encumb = 4;

    EXPECT_EQ(utils::get_skill_penalty(context.character), 2);
    EXPECT_EQ(utils::get_dodge_penalty(context.character), 4);
}

TEST(CharUtils, ReturnsPlayerIdnumAndNpcSentinel) {
    CharUtilsTestContext player_context;
    player_context.character.specials2.idnum = 4444;

    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;
    npc_context.character.specials2.idnum = 9999;

    EXPECT_EQ(utils::get_idnum(player_context.character), 4444);
    EXPECT_EQ(utils::get_idnum(npc_context.character), -1);
}

TEST(CharUtils, DistinguishesAwakeAndSleepingCharacters) {
    CharUtilsTestContext awake_context;
    awake_context.character.specials.position = POSITION_STANDING;

    CharUtilsTestContext sleeping_context;
    sleeping_context.character.specials.position = POSITION_SLEEPING;

    EXPECT_TRUE(utils::is_awake(awake_context.character));
    EXPECT_FALSE(utils::is_awake(sleeping_context.character));
}

TEST(CharUtils, CapsRankingTierAtFour) {
    EXPECT_EQ(utils::get_ranking_tier(3), 3);
    EXPECT_EQ(utils::get_ranking_tier(9), 4);
}

TEST(CharUtils, UsesKnowledgeFallbackForRawKnowledgeAndRawSkill) {
    CharUtilsTestContext context;
    context.character.player.bodytype = 1;
    context.knowledge[SKILL_SWIPE] = 61;

    EXPECT_EQ(utils::get_raw_knowledge(context.character, SKILL_SWIPE), 61);
    EXPECT_EQ(utils::get_raw_skill(context.character, SKILL_SWIPE), 61);

    context.character.player.bodytype = 2;
    EXPECT_EQ(utils::get_raw_skill(context.character, SKILL_SWIPE), 0)
        << "Expected bodytype 2 to suppress raw skill access.";
}

TEST(CharUtils, SupportsSettingSkillsAndKnowledgeWhenArraysExist) {
    CharUtilsTestContext context;

    utils::set_skill(context.character, SKILL_SWIPE, 33);
    utils::set_knowledge(context.character, SKILL_SWIPE, 66);

    EXPECT_EQ(context.skills[SKILL_SWIPE], 33);
    EXPECT_EQ(context.knowledge[SKILL_SWIPE], 66);
}

TEST(CharUtils, ComputesCarryCapacityChecksFromWeightAndItems) {
    CharUtilsTestContext context;
    context.character.tmpabilities.str = 10;
    context.character.tmpabilities.dex = 8;
    context.character.player.level = 10;
    context.character.specials.carry_weight = 100;
    context.character.specials.carry_items = 3;

    obj_data light_object = make_object(ITEM_ARMOR, 50, 0);
    obj_data heavy_object = make_object(ITEM_ARMOR, 20000, 0);

    EXPECT_TRUE(utils::can_carry_object(context.character, light_object));
    EXPECT_FALSE(utils::can_carry_object(context.character, heavy_object));
}

TEST(CharUtils, BlocksVisionWhenCharacterIsNowhereBlindOrWriting) {
    weather_data weather{};
    room_data room{};
    room.sector_type = SECT_CITY;

    CharUtilsTestContext nowhere_context;
    nowhere_context.character.in_room = NOWHERE;

    CharUtilsTestContext blind_context;
    blind_context.character.in_room = 1;
    blind_context.character.specials.affected_by = AFF_BLIND;

    CharUtilsTestContext writing_context;
    writing_context.character.in_room = 1;
    writing_context.character.specials2.act = PLR_WRITING;

    EXPECT_FALSE(utils::can_see(nowhere_context.character, weather, room));
    EXPECT_FALSE(utils::can_see(blind_context.character, weather, room));
    EXPECT_FALSE(utils::can_see(writing_context.character, weather, room));
}

TEST(CharUtils, ShadowCharactersCanSeeButOnlyRecognizeMagicObjects) {
    weather_data weather{};
    room_data room{};
    room.sector_type = SECT_CITY;

    CharUtilsTestContext context;
    context.character.in_room = 1;
    context.character.specials2.act = PLR_ISSHADOW;

    obj_data mundane = make_object(ITEM_ARMOR);
    mundane.short_description = const_cast<char*>("mundane item");
    mundane.name = const_cast<char*>("mundane item");

    obj_data magic = make_object(ITEM_ARMOR);
    magic.obj_flags.extra_flags = ITEM_MAGIC;
    magic.short_description = const_cast<char*>("magic item");
    magic.name = const_cast<char*>("magic item");

    EXPECT_TRUE(utils::can_see(context.character, weather, room));
    EXPECT_FALSE(utils::can_see_object(context.character, mundane, weather, room));
    EXPECT_TRUE(utils::can_see_object(context.character, magic, weather, room));
    EXPECT_STREQ(utils::get_object_string(context.character, mundane, weather, room), "something");
    EXPECT_STREQ(utils::get_object_string(context.character, magic, weather, room), "magic item");
}

TEST(CharUtils, DetectInvisibleAllowsCharactersToSeeInvisibleObjects) {
    weather_data weather{};
    room_data room{};
    room.sector_type = SECT_CITY;

    CharUtilsTestContext context;
    context.character.in_room = 1;
    context.character.specials.affected_by = AFF_DETECT_INVISIBLE;

    obj_data invisible = make_object(ITEM_ARMOR);
    invisible.obj_flags.extra_flags = ITEM_INVISIBLE;
    invisible.short_description = const_cast<char*>("hidden blade");
    char object_name[] = "hidden blade";
    invisible.name = object_name;

    EXPECT_TRUE(utils::can_see_object(context.character, invisible, weather, room));
    EXPECT_STREQ(utils::get_object_name(context.character, invisible, weather, room), "hidden");
}

TEST(CharUtils, CanGetObjectRequiresTakeFlagCarryCapacityAndVisibility) {
    weather_data weather{};
    room_data room{};
    room.sector_type = SECT_CITY;

    CharUtilsTestContext context;
    context.character.in_room = 1;
    context.character.tmpabilities.str = 10;
    context.character.tmpabilities.dex = 8;
    context.character.player.level = 10;

    obj_data takeable = make_object(ITEM_ARMOR, 50, 0);
    takeable.obj_flags.wear_flags = ITEM_TAKE;

    obj_data not_takeable = make_object(ITEM_ARMOR, 50, 0);

    EXPECT_TRUE(utils::can_get_object(context.character, takeable, weather, room));
    EXPECT_FALSE(utils::can_get_object(context.character, not_takeable, weather, room));
}

TEST(CharUtils, DistinguishesShadowFlagsForPlayersAndNpcs) {
    CharUtilsTestContext player_context;
    player_context.character.specials2.act = PLR_ISSHADOW;

    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    EXPECT_TRUE(utils::is_shadow(player_context.character));
    EXPECT_TRUE(utils::is_shadow(npc_context.character));
}

TEST(CharUtils, ClassifiesRacesAndAlignmentHelpers) {
    CharUtilsTestContext good_context;
    good_context.character.player.race = RACE_HUMAN;
    good_context.character.specials2.alignment = 150;

    CharUtilsTestContext evil_context;
    evil_context.character.player.race = RACE_ORC;
    evil_context.character.specials2.alignment = -150;

    CharUtilsTestContext neutral_context;
    neutral_context.character.player.race = RACE_HARADRIM;

    EXPECT_TRUE(utils::is_race_good(good_context.character));
    EXPECT_TRUE(utils::is_race_evil(evil_context.character));
    EXPECT_TRUE(utils::is_race_haradrim(neutral_context.character));
    EXPECT_TRUE(utils::is_good(good_context.character));
    EXPECT_TRUE(utils::is_evil(evil_context.character));
    EXPECT_TRUE(utils::is_neutral(neutral_context.character));
}

TEST(CharUtils, NpcHostilityAndRpChecksUseRaceAndPreferenceFlags) {
    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;
    npc_context.character.specials2.pref = 1 << RACE_HUMAN;
    npc_context.character.specials2.rp_flag = 1 << RACE_HUMAN;

    CharUtilsTestContext victim_context;
    victim_context.character.player.race = RACE_HUMAN;

    EXPECT_TRUE(utils::is_hostile_to(npc_context.character, victim_context.character));
    EXPECT_TRUE(utils::is_rp_race_check(npc_context.character, victim_context.character));
}

TEST(CharUtils, RidingHelpersDependOnMountedPointersAndRegisteredCharacters) {
    CharUtilsTestContext context;
    char_data mount{};
    char_data rider{};

    context.character.mount_data.mount = &mount;
    context.character.mount_data.mount_number = 8;
    context.character.mount_data.rider = &rider;
    context.character.mount_data.rider_number = 9;

    set_char_exists(8);
    set_char_exists(9);

    EXPECT_TRUE(utils::is_riding(context.character));
    EXPECT_TRUE(utils::is_ridden(context.character));

    remove_char_exists(8);
    remove_char_exists(9);
}

TEST(CharUtils, ReturnsProfileAndRaceAbbreviationsForPlayersAndNpcFallback) {
    CharUtilsTestContext player_context;
    player_context.character.player.prof = PROF_WARRIOR;
    player_context.character.player.race = RACE_DWARF;

    CharUtilsTestContext npc_context;
    npc_context.character.specials2.act = MOB_ISNPC;

    EXPECT_STREQ(utils::get_prof_abbrev(player_context.character), "Wa");
    EXPECT_STREQ(utils::get_race_abbrev(player_context.character), "Dwf");
    EXPECT_STREQ(utils::get_prof_abbrev(npc_context.character), "--");
    EXPECT_STREQ(utils::get_race_abbrev(npc_context.character), "--");
}

TEST(CharUtils, ReturnsRaceAndPerceptionValuesWithClampingAndShadowOverride) {
    CharUtilsTestContext context;
    context.character.player.race = RACE_WOOD;
    context.character.specials2.perception = 140;

    EXPECT_EQ(utils::get_race(context.character), RACE_WOOD);
    EXPECT_EQ(utils::get_minimum_insight_perception(context.character), 30);
    EXPECT_EQ(utils::get_race_perception(context.character), 50);
    EXPECT_EQ(utils::get_perception(context.character), 100);

    context.character.specials2.perception = -1;
    EXPECT_EQ(utils::get_perception(context.character), 50);

    context.character.specials2.act = PLR_ISSHADOW;
    EXPECT_EQ(utils::get_perception(context.character), 100);
}

TEST(CharUtils, RecognizesMentalFlagAndSpecializationAccessors) {
    CharUtilsTestContext mental_context;
    mental_context.character.specials2.pref = PRF_MENTAL;

    CharUtilsTestContext spec_context;
    spec_context.profs.specialization = static_cast<int>(game_types::PS_WeaponMaster);

    EXPECT_TRUE(utils::is_mental(mental_context.character));
    EXPECT_EQ(utils::get_specialization(spec_context.character), game_types::PS_WeaponMaster);
}

TEST(CharUtils, TracksResistanceVulnerabilityGuardianAndEnergyRegen) {
    CharUtilsTestContext context;
    context.character.specials.resistance = 1 << 3;
    context.character.specials.vulnerability = 1 << 4;
    context.character.specials.affected_by = AFF_CHARM;
    context.character.specials2.act = MOB_ISNPC | MOB_GUARDIAN;
    context.character.master = &context.character;
    context.character.points.ENE_regen = 12;

    EXPECT_TRUE(utils::is_resistant(context.character, 3));
    EXPECT_TRUE(utils::is_vulnerable(context.character, 4));
    EXPECT_TRUE(utils::is_guardian(context.character));
    EXPECT_EQ(utils::get_energy_regen(context.character), 12);
}

TEST(CharDataMethods, TracksPracticeSpendingAndResetBehavior) {
    CharUtilsTestContext context;
    context.character.player.level = 10;
    context.character.abilities.lea = 15;
    context.skills[1] = 3;
    context.skills[2] = 4;
    context.knowledge[1] = 9;
    context.character.specials2.spells_to_learn = 1;

    EXPECT_EQ(context.character.get_spent_practice_count(), 7);
    EXPECT_EQ(context.character.get_max_practice_count(), 70);

    context.character.update_available_practice_sessions();
    EXPECT_EQ(context.character.specials2.spells_to_learn, 63);

    context.character.reset_skills();
    EXPECT_EQ(context.skills[1], 0);
    EXPECT_EQ(context.knowledge[1], 0);
    EXPECT_EQ(context.character.specials2.spells_to_learn, 70);
    EXPECT_FALSE(context.character.is_affected());
}

TEST(CharDataMethods, ReportsAffectedStateWhenAffectListExists) {
    CharUtilsTestContext context;
    affected_type affect = make_affect(SPELL_ARMOR, 2);
    context.character.affected = &affect;

    EXPECT_TRUE(context.character.is_affected());
}

TEST(SpecializationData, ColdSpecializationTracksCountersAndCreatesReportText) {
    cold_spec_data data;
    CharUtilsTestContext context;

    data.on_chill_applied(25);
    data.on_chill_ray_success(30);
    data.on_chill_ray_fail(10);
    data.on_cone_of_cold_success(40);
    data.on_cone_of_cold_failed(15);

    EXPECT_EQ(data.get_chill_ray_count(), 2);
    EXPECT_EQ(data.get_successful_chills(), 1);
    EXPECT_EQ(data.get_saved_chills(), 1);
    EXPECT_EQ(data.get_cone_count(), 2);
    EXPECT_EQ(data.get_successful_cones(), 1);
    EXPECT_EQ(data.get_saved_cones(), 1);
    EXPECT_EQ(data.get_total_energy_sapped(), 25);
    EXPECT_NE(data.to_string(context.character).find("You are specialized in cold."), std::string::npos);
}

TEST(SpecializationData, CreatesAndResetsConcreteSpecializationInfo) {
    CharUtilsTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);

    context.character.extra_specialization_data.set(context.character);
    EXPECT_EQ(context.character.extra_specialization_data.get_current_spec(), game_types::PS_LightFighting);
    EXPECT_NE(
        context.character.extra_specialization_data.to_string(context.character).find("light fighting"),
        std::string::npos);

    context.character.extra_specialization_data.reset();
    EXPECT_EQ(context.character.extra_specialization_data.get_current_spec(), game_types::PS_None);
    EXPECT_EQ(
        context.character.extra_specialization_data.to_string(context.character),
        std::string("You are not specialized in anything.\r\n"));
}

TEST(GroupData, ManagesMembershipPcCountsAndRoomFiltering) {
    CharUtilsTestContext leader_context;
    CharUtilsTestContext pc_member_context;
    CharUtilsTestContext npc_member_context;

    leader_context.character.in_room = 10;
    pc_member_context.character.in_room = 10;
    npc_member_context.character.in_room = 20;
    npc_member_context.character.specials2.act = MOB_ISNPC;

    group_data group(&leader_context.character);
    group.add_member(&pc_member_context.character);
    group.add_member(&npc_member_context.character);
    group.add_member(&pc_member_context.character);

    EXPECT_EQ(group.size(), 3u);
    EXPECT_EQ(group.get_pc_count(), 2);
    EXPECT_TRUE(group.is_member(&pc_member_context.character));

    char_vector pcs_in_room;
    group.get_pcs_in_room(pcs_in_room, 10);
    EXPECT_EQ(pcs_in_room.size(), 2u);

    EXPECT_FALSE(group.remove_member(&leader_context.character));
    EXPECT_TRUE(group.remove_member(&npc_member_context.character));
    EXPECT_EQ(group.size(), 2u);
}

TEST(DamageReports, ReturnsFriendlyEmptyReports) {
    player_damage_details player_report;
    group_damaga_data group_report;
    CharUtilsTestContext context;

    EXPECT_NE(player_report.get_damage_report(&context.character).find("has not recorded any damage dealt"), std::string::npos);
    EXPECT_NE(group_report.get_damage_report().find("have not recorded any damage dealt"), std::string::npos);
}
