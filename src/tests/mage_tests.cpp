#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "../zone.h"
#include "test_random_utils.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>

// get_mage_caster_level/get_magic_power/should_apply_spell_penetration/
// get_spell_pen_value/get_victim_saving_throw/get_save_bonus/
// is_friendly_taget are declared (both the live and caster_snapshot forms,
// TASK-021) by spells.h, included above.
bool different_zone(int was_in, int to_room);
int random_exit(int room);
bool is_teleportation_room_valid(room_data *room);
void apply_chilled_effect(char_data *caster, char_data *victim);

struct loclife_coord {
    int number;
    signed char n;
    signed char e;
    signed char u;
};

int loclife_add_rooms(loclife_coord room, loclife_coord *roomlist, int *roomnum, int room_not);

extern room_data world;
extern int top_of_world;
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct obj_data* object_list;

namespace {

void ensure_test_world(int minimum_room_number) {
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

struct ZoneGuard {
    int room_a;
    int room_b;
    int original_zone_a;
    int original_zone_b;

    ZoneGuard(int first_room, int second_room)
        : room_a(first_room), room_b(second_room), original_zone_a(0), original_zone_b(0) {
        ensure_test_world(std::max(first_room, second_room));
        original_zone_a = world[first_room].zone;
        original_zone_b = world[second_room].zone;
    }

    ~ZoneGuard() {
        world[room_a].zone = original_zone_a;
        world[room_b].zone = original_zone_b;
    }
};

struct RoomExitGuard {
    int room_number;
    room_direction_data *original_exits[NUM_OF_DIRS]{};
    long original_room_flags = 0;
    char_data *original_people = nullptr;

    explicit RoomExitGuard(int room)
        : room_number(room), original_room_flags(0), original_people(nullptr) {
        ensure_test_world(room);
        original_room_flags = world[room].room_flags;
        original_people = world[room].people;
        for (int i = 0; i < NUM_OF_DIRS; ++i) {
            original_exits[i] = world[room].dir_option[i];
        }
    }

    ~RoomExitGuard() {
        for (int i = 0; i < NUM_OF_DIRS; ++i) {
            world[room_number].dir_option[i] = original_exits[i];
        }
        world[room_number].room_flags = original_room_flags;
        world[room_number].people = original_people;
    }
};

struct MageTestContext {
    char_data caster{};
    char_data victim{};
    char_data master{};
    char_prof_data caster_profs{};
    char_prof_data victim_profs{};
    char_prof_data master_profs{};
    char caster_name[16] = "test_mage";
    char victim_short_descr[16] = "test_target";
    char master_name[16] = "test_master";

    MageTestContext() {
        caster.profs = &caster_profs;
        victim.profs = &victim_profs;
        master.profs = &master_profs;

        caster.player.name = caster_name;
        victim.player.short_descr = victim_short_descr;
        master.player.name = master_name;

        caster.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        master.player.race = RACE_HUMAN;

        caster.player.level = 30;
        victim.player.level = 30;
        master.player.level = 30;

        caster.tmpabilities.intel = 20;
        victim.tmpabilities.intel = 20;
        caster.points.spell_power = 0;
        victim.specials2.saving_throw = 0;
        caster.abilities.hit = 500;
        victim.abilities.hit = 500;
        caster.tmpabilities.hit = 500;
        victim.tmpabilities.hit = 500;
        caster.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        caster.in_room = 7;
        victim.in_room = 7;
    }

    void prepare_for_spell_damage() {
        victim.specials2.act = MOB_ISNPC;
        victim.player.level = 0;
        victim.tmpabilities.intel = 8;
        victim.specials2.saving_throw = 0;
        victim.tmpabilities.hit = 500;
        victim.abilities.hit = 500;
        caster.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
    }

    void force_spell_save() {
        victim.specials2.act = MOB_ISNPC;
        victim.player.level = 90;
        victim.tmpabilities.intel = 25;
        victim.specials2.saving_throw = 0;
        victim.tmpabilities.hit = 500;
        victim.abilities.hit = 500;
        caster.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
    }
};

loclife_coord *find_loclife_room(loclife_coord *roomlist, int roomnum, int target_room) {
    for (int i = 0; i < roomnum; ++i) {
        if (roomlist[i].number == target_room) {
            return &roomlist[i];
        }
    }
    return nullptr;
}

// spell_summon() unconditionally indexes zone_table -- both for its own
// caster/victim distance term and, via the real char_from_room()/
// char_to_room() it dispatches on the success arm, for the zone
// goodness/evilness power counters. This shared test binary never boots a
// real zone_table (see FireballSplashesTheRoomBeforeASelfFumbleKillsTheCaster's
// comment on the same constraint), so a real spell_summon() body test needs
// its own stub table installed for the scope of the test. Restores whatever
// zone_table pointed at (normally nullptr) on destruction.
struct ZoneTableGuard {
    struct zone_data *previous_table;
    int previous_top;
    struct zone_data stub[2]{};

    ZoneTableGuard() : previous_table(zone_table), previous_top(top_of_zone_table) {
        zone_table = stub;
        top_of_zone_table = 1;
    }

    ~ZoneTableGuard() {
        zone_table = previous_table;
        top_of_zone_table = previous_top;
    }
};

// Matches spell_pa_tests.cpp's make_descriptor(): a descriptor whose output
// buffer is the small inline buffer rather than a real socket, so act()/
// send_to_char() output can be asserted on directly.
descriptor_data make_descriptor() {
    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

} // namespace

class MageProcTest : public ::testing::Test {
  protected:
    void SetUp() override { ensure_test_world(32); }

    void TearDown() override { clear_test_random_values(); }
};

TEST_F(MageProcTest, MageCasterLevelUsesCurrentIntelRoundingPath) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 18;
    context.caster.tmpabilities.intel = 19;

    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(&context.caster), 21)
        << "Expected low queued rolls to keep the current partial-intelligence bonus unrounded.";

    push_test_random_value(0.99);
    EXPECT_EQ(get_mage_caster_level(&context.caster), 22)
        << "Expected high queued rolls to trigger the current partial-intelligence rounding bonus.";
}

TEST_F(MageProcTest, MagicPowerUsesBattleMageBonusLevelModifierAndIntelRounding) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 24;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    context.caster.specials.tactics = TACTICS_AGGRESSIVE;
    context.caster.points.spell_power = 60;
    context.caster.tmpabilities.intel = 19;

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    EXPECT_EQ(get_magic_power(&context.caster), 124)
        << "Expected magic power to combine mage level, battle-mage bonus, level modifier, and the "
           "current low-roll intel contribution.";

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    EXPECT_EQ(get_magic_power(&context.caster), 126)
        << "Expected magic power to increase by one when the queued intel-rounding roll succeeds.";
}

TEST(MageHelpers, SpellPenetrationAppliesForPlayersAndEligibleCharmedOrcFriends) {
    MageTestContext context;

    EXPECT_TRUE(should_apply_spell_penetration(&context.caster))
        << "Expected player casters to always apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected ordinary NPC casters not to apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    EXPECT_TRUE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs with a player master to apply spell penetration.";
}

TEST(MageHelpers, SpellPenetrationRejectsCharmedOrcFriendsWithoutPlayerMaster) {
    MageTestContext context;
    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;

    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs without a master to skip spell penetration.";

    context.master.specials2.act = MOB_ISNPC;
    context.caster.master = &context.master;
    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs with a non-player master to skip spell penetration.";
}

TEST(MageHelpers, SpellPenValueUsesCasterAndMasterMageLevelsForCharmedNpcs) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(&context.caster), 4.0)
        << "Expected player spell penetration to use one fifth of the caster's mage level.";

    context.caster.specials2.act = MOB_ISNPC;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    context.caster.player.level = 20;
    context.master_profs.prof_level[PROF_MAGE] = 15;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(&context.caster), 5.0)
        << "Expected charmed NPC spell penetration to include one third of the master's mage "
           "level.";
}

TEST(MageHelpers, VictimSavingThrowUsesSpellPenetrationAndPlayerLevelAdjustment) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;
    context.victim.specials2.saving_throw = 10;
    context.victim.player.level = 25;

    EXPECT_DOUBLE_EQ(get_victim_saving_throw(&context.caster, &context.victim), 11.0)
        << "Expected player victims to offset spell penetration with the current level-based "
           "saving-throw adjustment.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_DOUBLE_EQ(get_victim_saving_throw(&context.caster, &context.victim), 10.0)
        << "Expected NPC casters without spell penetration eligibility to leave the victim saving "
           "throw unchanged.";
}

TEST(MageHelpers, DifferentZoneReflectsCurrentWorldZoneNumbers) {
    ZoneGuard zone_guard(7, 8);

    world[7].zone = 12;
    world[8].zone = 12;
    EXPECT_FALSE(different_zone(7, 8))
        << "Expected rooms in the same zone to report that they are not in different zones.";

    world[8].zone = 13;
    EXPECT_TRUE(different_zone(7, 8))
        << "Expected rooms with different zone numbers to report that they are in different zones.";
}

TEST_F(MageProcTest, RandomExitReturnsNowhereForInvalidRoomNumbers) {
    EXPECT_EQ(random_exit(-1), NOWHERE);
    EXPECT_EQ(random_exit(999999), NOWHERE);
}

TEST_F(MageProcTest, RandomExitFallsBackToSameRoomWhenNoBlinkableExitsExist) {
    RoomExitGuard room_guard(7);
    RoomExitGuard destination_guard(8);
    room_direction_data blocked_exit{};
    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        world[7].dir_option[i] = nullptr;
    }
    blocked_exit.to_room = 8;
    blocked_exit.exit_info = EX_NOBLINK;
    world[7].dir_option[NORTH] = &blocked_exit;
    world[8].room_flags = 0;

    EXPECT_EQ(random_exit(7), 7) << "Expected random_exit to leave the caster in place when every "
                                    "exit is excluded from blinking.";
}

TEST_F(MageProcTest, RandomExitChoosesAmongEligibleExitsUsingQueuedRandomRolls) {
    RoomExitGuard room_guard(7);
    RoomExitGuard north_guard(8);
    RoomExitGuard east_guard(9);
    room_direction_data north_exit{};
    room_direction_data east_exit{};

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        world[7].dir_option[i] = nullptr;
    }
    north_exit.to_room = 8;
    east_exit.to_room = 9;
    world[7].dir_option[NORTH] = &north_exit;
    world[7].dir_option[EAST] = &east_exit;
    world[8].room_flags = 0;
    world[9].room_flags = 0;

    push_test_random_value(0.0);
    EXPECT_EQ(random_exit(7), 8)
        << "Expected the lowest queued roll to choose the first eligible blink exit.";

    push_test_random_value(0.99);
    EXPECT_EQ(random_exit(7), 9)
        << "Expected the highest queued roll to choose the last eligible blink exit.";
}

TEST(MageHelpers, TeleportationRoomValidationRejectsOccupiedAndRestrictedRooms) {
    room_data test_room{};
    char_data occupant{};

    test_room.people = &occupant;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected occupied rooms to be invalid teleportation destinations.";

    test_room.people = nullptr;
    test_room.room_flags = DEATH;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected death rooms to be invalid teleportation destinations.";

    test_room.room_flags = SECURITYROOM;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected security rooms to be invalid teleportation destinations.";

    test_room.room_flags = NO_TELEPORT;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected no-teleport rooms to be invalid teleportation destinations.";

    test_room.room_flags = GODROOM;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected god rooms to be invalid teleportation destinations.";
}

TEST(MageHelpers, TeleportationRoomValidationAcceptsEmptyOrdinaryRooms) {
    room_data test_room{};
    test_room.room_flags = 0;
    test_room.people = nullptr;

    EXPECT_TRUE(is_teleportation_room_valid(&test_room))
        << "Expected empty rooms without teleport restrictions to be valid teleportation "
           "destinations.";
}

TEST(MageHelpers, SaveBonusUsesCasterAndVictimSpecializationMatchups) {
    MageTestContext context;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Fire);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Cold);

    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected matching caster specialization and opposing victim specialization to stack "
           "the current save-bonus reductions.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Fire);
    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold), 4)
        << "Expected opposing caster specialization and matching victim specialization to stack "
           "the current save-bonus increases.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected arcane specialization to count as primary for the caster and opposing for the "
           "victim in the current implementation.";
}

TEST(MageHelpers, FriendlyTargetTreatsSelfFollowersAndSameSideCharactersAsFriendly) {
    MageTestContext context;
    char_data follower{};
    follower.master = &context.caster;

    EXPECT_TRUE(is_friendly_taget(&context.caster, &context.caster))
        << "Expected a caster to always count as a friendly target to themselves.";
    EXPECT_TRUE(is_friendly_taget(&context.caster, &follower))
        << "Expected follower chains ending at the caster to count as friendly targets.";
    EXPECT_TRUE(is_friendly_taget(&context.caster, &context.victim))
        << "Expected same-side characters to count as friendly targets.";

    context.victim.player.race = RACE_ORC;
    EXPECT_FALSE(is_friendly_taget(&context.caster, &context.victim))
        << "Expected characters on the opposing side to count as non-friendly targets.";
}

TEST(MageHelpers, ChilledEffectUsesVictimEnergyAndTracksColdSpecDrain) {
    MageTestContext context;
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;

    apply_chilled_effect(&context.caster, &context.victim);

    EXPECT_EQ(context.victim.specials.ENERGY, 48)
        << "Expected chilled effect to remove half the victim's energy plus four rounds of current "
           "energy regeneration.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;

    apply_chilled_effect(&context.caster, &context.victim);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72)
        << "Expected cold specialization bookkeeping to track the exact energy drained by chilled "
           "effect.";
}

TEST_F(MageProcTest, MagicMissileHalvesDamageWhenSaveIsForced) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.force_spell_save();
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_magic_missile(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 494)
        << "Expected strong-saving victims to halve magic missile's minimum deterministic damage "
           "on the real damage path.";
}

TEST_F(MageProcTest, ChillRayAppliesChilledEffectAndTracksColdSpecOnFailedSave) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_chill_ray(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 480);
    EXPECT_EQ(context.victim.specials.ENERGY, 48);
    EXPECT_EQ(cold_data->get_successful_chills(), 1);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72);
}

TEST_F(MageProcTest, ChillRayTracksColdSpecFailureOnSavedCast) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_chill_ray(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 490);
    EXPECT_EQ(cold_data->get_saved_chills(), 1);
}

TEST_F(MageProcTest, LightningBoltUsesSpecializationBonusAndSaveReduction) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Lightning);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_lightning_bolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 485)
        << "Expected lightning specialization to boost indoor lightning bolt damage before the "
           "strong victim save halves it on the real damage path.";
}

TEST_F(MageProcTest, DarkBoltUsesSpecializationBonusWithoutSunPenalty) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Darkness);
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_dark_bolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 469)
        << "Expected darkness specialization to apply its current 10% raw-damage bonus when "
           "sunlight is not weakening the spell.";
}

TEST_F(MageProcTest, FireboltUsesFireSpecMinimumDamageAndSaveReduction) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Fire);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_firebolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 498)
        << "Expected firebolt's strong-save path to halve the specialization-clamped minimum "
           "damage on the real damage path.";
}

TEST_F(MageProcTest, ConeOfColdAppliesChilledEffectAndColdSpecTrackingOnFailedSave) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_cone_of_cold(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 465);
    EXPECT_EQ(context.victim.specials.ENERGY, 48);
    EXPECT_EQ(cold_data->get_successful_cones(), 1);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72);
}

TEST_F(MageProcTest, LocateLifeAddsReachableRoomsWithUpdatedCoordinates) {
    RoomExitGuard room_guard(7);
    room_direction_data north_exit{};
    room_direction_data east_exit{};
    room_direction_data down_exit{};
    loclife_coord origin{7, 2, -1, 3};
    loclife_coord roomlist[8]{};
    int roomnum = 0;

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        world[7].dir_option[i] = nullptr;
    }

    north_exit.to_room = 8;
    east_exit.to_room = 9;
    down_exit.to_room = 10;
    world[7].dir_option[NORTH] = &north_exit;
    world[7].dir_option[EAST] = &east_exit;
    world[7].dir_option[DOWN] = &down_exit;

    EXPECT_EQ(loclife_add_rooms(origin, roomlist, &roomnum, NOWHERE), 3)
        << "Expected locate-life room expansion to add each reachable adjacent room once.";
    EXPECT_EQ(roomnum, 3);

    loclife_coord *north_room = find_loclife_room(roomlist, roomnum, 8);
    loclife_coord *east_room = find_loclife_room(roomlist, roomnum, 9);
    loclife_coord *down_room = find_loclife_room(roomlist, roomnum, 10);

    ASSERT_NE(north_room, nullptr);
    ASSERT_NE(east_room, nullptr);
    ASSERT_NE(down_room, nullptr);

    EXPECT_EQ(north_room->n, 3);
    EXPECT_EQ(north_room->e, -1);
    EXPECT_EQ(north_room->u, 3);

    EXPECT_EQ(east_room->n, 2);
    EXPECT_EQ(east_room->e, 0);
    EXPECT_EQ(east_room->u, 3);

    EXPECT_EQ(down_room->n, 2);
    EXPECT_EQ(down_room->e, -1);
    EXPECT_EQ(down_room->u, 2);
}

TEST_F(MageProcTest, LocateLifeSkipsBlockedDuplicateAndExcludedRooms) {
    RoomExitGuard room_guard(7);
    room_direction_data north_exit{};
    room_direction_data east_exit{};
    room_direction_data south_exit{};
    room_direction_data west_exit{};
    loclife_coord origin{7, 0, 0, 0};
    loclife_coord roomlist[8]{};
    int roomnum = 1;

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        world[7].dir_option[i] = nullptr;
    }

    roomlist[0].number = 8;
    north_exit.to_room = 8;
    east_exit.to_room = 11;
    east_exit.exit_info = EX_CLOSED | EX_DOORISHEAVY;
    south_exit.to_room = 12;
    west_exit.to_room = 13;

    world[7].dir_option[NORTH] = &north_exit; // duplicate
    world[7].dir_option[EAST] = &east_exit;   // blocked
    world[7].dir_option[SOUTH] = &south_exit; // excluded
    world[7].dir_option[WEST] = &west_exit;   // valid

    EXPECT_EQ(loclife_add_rooms(origin, roomlist, &roomnum, 12), 1)
        << "Expected locate-life room expansion to skip duplicates, excluded rooms, and heavy "
           "closed exits.";
    EXPECT_EQ(roomnum, 2);

    loclife_coord *west_room = find_loclife_room(roomlist, roomnum, 13);
    ASSERT_NE(west_room, nullptr);
    EXPECT_EQ(west_room->n, 0);
    EXPECT_EQ(west_room->e, -1);
    EXPECT_EQ(west_room->u, 0);
}

// TASK-018 -- spell_fireball's orc self-fumble arm (mage.cpp, `victim = caster;`) hands the
// caster to apply_spell_damage() as its own victim. When that hit is lethal, fight.cpp's
// damage() runs die() -> raw_kill() -> extract_char(), whose NPC arm unlinks AND free_char()s
// the caster -- and the body used to continue straight into world[caster->in_room].people (the
// splash loop) and is_friendly_taget(caster, victim) using the now-freed caster.
//
// This depot has no extract_char test seam, so the fixture drives the real death pipeline instead
// of stubbing it: the caster is heap-allocated and registered the way the game builds an NPC
// (register_npc_char, linked into character_list and a real room), so free_char() is legal to call
// on it, following the extract_char precedent at src/tests/interpre_account_menu_tests.cpp:3779.
// A one-entry
// mob_index[] is installed because raw_kill()'s SPECIAL_DEATH probe (activate_char_special) and
// make_physical_corpse() both dereference the caster's mob prototype slot unconditionally for any
// IS_NPC() character.
//
// char_from_room() sets a departing character's ch->in_room to NOWHERE (-1) before extract_char's
// NPC arm frees it, and nothing allocates between that free() and the old body's next read of
// `caster->in_room` -- so under the pre-fix ordering, world[caster->in_room] reliably resolves to
// world[-1], which room_data::operator[] reports via mudlog("world[] called for negative room
// number.", ..., TRUE) to stderr (db.cpp:4062). That gives a deterministic, non-ASan witness for
// the UAF, captured via CaptureStderr.
namespace {

constexpr int kFireballRoom = 7;

// Pinned at the midpoint: this container's x87 arithmetic truncates products that land just below
// an integer boundary (e.g. 0.60 * 5 evaluates a hair under 3.0), so an integer roll r in [from,
// to] must be pinned at the MIDPOINT (r - from + 0.5) / range rather than at r's own fraction, or
// the pinned roll can come out one low under x87 while landing correctly under SSE2 (e.g. real CI
// hardware) -- midpoint values truncate identically under both. Both fireball
// tests reuse the same pinned value for every draw in the call (get_magic_power()'s internal
// rolls, the fumble check, the save rolls, and the splash target roll): the fumble check
// (number(0, 9)) is the only draw whose outcome the test's control flow depends on, and these are
// exactly the range-10 midpoints for r = 0 and r = 9. Every other draw only needs to be
// comfortably low (so the caster's one-hit-point body dies, and splash/save comparisons keep the
// same generous margins regardless of a rounding wobble on an unrelated draw) -- this suite
// asserts *behavior* (who died, who was hit, ordering) rather than exact damage values, so it does
// not depend on any of those other draws being precise.
constexpr double kForceFumbleRoll = 0.05; // (0 - 0 + 0.5) / 10 -- number(0, 9) == 0, fumble fires
constexpr double kForceNoFumbleRoll = 0.95; // (9 - 0 + 0.5) / 10 -- number(0, 9) == 9, no fumble

void queue_fireball_rolls(double roll, int count = 60)
{
    for (int roll_index = 0; roll_index < count; ++roll_index) {
        push_test_random_value(roll);
    }
}

// raw_kill()'s SPECIAL_DEATH probe (activate_char_special -> IS_MOB()) and
// make_physical_corpse() both read mob_index[character->nr] unconditionally for any IS_NPC()
// character; this suite has no mob table, so publish a one-entry one (matching the caster's
// nr = 0 below) for the test's scope and restore whatever was installed before.
class ScopedFireballMobIndex {
public:
    ScopedFireballMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedFireballMobIndex() { mob_index = m_previous; }
    ScopedFireballMobIndex(const ScopedFireballMobIndex &) = delete;
    ScopedFireballMobIndex &operator=(const ScopedFireballMobIndex &) = delete;

private:
    index_data *m_previous; // whatever this suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot the caster's nr = 0 names
};

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents and object_list; take
// both back out so a fireball test leaves no residue for later tests in this binary.
void release_fireball_corpse(int room_number, obj_data *previous_object_list)
{
    obj_data *corpse = world[room_number].contents;
    if (corpse == nullptr) {
        return;
    }
    obj_from_room(corpse);
    if (object_list == corpse) {
        object_list = corpse->next;
    }
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    RELEASE(corpse);
    object_list = previous_object_list;
}

// Builds the heap-allocated, registered orc NPC caster the death pipeline needs (see the file
// comment above): clear_char() + register_npc_char() the way the game constructs an NPC, with
// nr = 0 naming the ScopedFireballMobIndex slot above.
char_data *make_fireball_caster(int hit_points, char *short_descr)
{
    char_data *caster = new char_data {};
    clear_char(caster, MOB_ISNPC);
    caster->specials2.act = MOB_ISNPC;
    caster->nr = 0; // prototype slot 0 of the scoped one-entry mob_index above
    caster->player.race = RACE_ORC;
    caster->player.short_descr = short_descr; // make_physical_corpse() reads GET_NAME() for the corpse text
    caster->player.level = 30;
    caster->profs->prof_level[PROF_MAGE] = 30; // wide save-DC margin so the pinned rolls above are decisive either way
    caster->tmpabilities.intel = 20;
    caster->tmpabilities.hit = hit_points;
    caster->abilities.hit = std::max(hit_points, 1);
    caster->specials.position = POSITION_STANDING;
    caster->in_room = kFireballRoom;
    register_npc_char(caster);
    return caster;
}

} // namespace

TEST_F(MageProcTest, FireballSplashesTheRoomBeforeASelfFumbleKillsTheCaster) {
    ScopedFireballMobIndex prototype_table;
    RoomExitGuard room_guard(kFireballRoom);

    char caster_short_descr[] = "a testing fireball orc";
    char_data *caster = make_fireball_caster(1, caster_short_descr); // any fireball hit -- primary or splash -- is lethal
    character_list = caster;
    caster->next = nullptr;

    MageTestContext context; // supplies the named target and the bystander
    context.prepare_for_spell_damage();
    context.victim.in_room = kFireballRoom;
    context.master.in_room = kFireballRoom;
    // MageTestContext only sets these up for caster/victim; the bystander needs them too so it
    // is a normal, alive, undamaged occupant rather than the zero-initialized default (which
    // reads as POSITION_DEAD -- damage() would refuse to touch a "corpse"). It also needs the
    // NPC flag: without it, damage_credited()'s ordinary engagement bookkeeping (set_fighting())
    // during the splash hit links the bystander into a "fight" with the caster, and when the
    // caster's deferred self-hit kills it, group_gain()'s room walk sees a non-NPC "fighting"
    // the dead caster and treats it as a player killer -- routing it into exp_with_modifiers(),
    // which dereferences a zone_table this unit-test binary never boots. Flagging the bystander
    // NPC (an ordinary orc-room occupant, matching the named victim) keeps it out of that path,
    // which was never this test's concern.
    context.master.specials2.act = MOB_ISNPC;
    context.master.abilities.hit = 500;
    context.master.tmpabilities.hit = 500;
    context.master.specials.position = POSITION_STANDING;
    context.master.specials.fighting = nullptr;

    world[kFireballRoom].people = caster;
    caster->next_in_room = &context.victim;
    context.victim.next_in_room = &context.master;
    context.master.next_in_room = nullptr;

    const int bystander_hit_before = context.master.tmpabilities.hit;
    obj_data *const previous_object_list = object_list;

    queue_fireball_rolls(kForceFumbleRoll);

    testing::internal::CaptureStderr();
    spell_fireball(caster, nullptr, 0, &context.victim, nullptr, 0, 0);
    const std::string captured = testing::internal::GetCapturedStderr();
    // caster is freed at this point; nothing below may dereference it -- only compare the
    // pointer value or read state through survivors (character_list, world[]'s room lists).

    release_fireball_corpse(kFireballRoom, previous_object_list);

    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "Expected the splash to resolve world[caster->in_room] while the caster was still "
           "alive, never after extract_char() unlinked it; stderr was: "
        << captured;
    EXPECT_LT(context.master.tmpabilities.hit, bystander_hit_before)
        << "the fumbled fireball must still splash the room before the caster's own hit lands";
    EXPECT_EQ(character_list, nullptr)
        << "extract_char()'s NPC arm must have unlinked the caster from character_list";
    EXPECT_EQ(world[kFireballRoom].people, &context.victim)
        << "extract_char()'s NPC arm must have unlinked the caster from the room's occupant list";
}

TEST_F(MageProcTest, FireballWithoutAFumbleStillDamagesTheVictimAndKeepsTheCaster) {
    ScopedFireballMobIndex prototype_table;
    RoomExitGuard room_guard(kFireballRoom);

    char caster_short_descr[] = "a testing fireball orc";
    char_data *caster = make_fireball_caster(500, caster_short_descr); // no fumble expected: the caster must survive
    character_list = caster;
    caster->next = nullptr;

    MageTestContext context;
    context.prepare_for_spell_damage();
    context.victim.in_room = kFireballRoom;
    context.master.in_room = kFireballRoom;
    // MageTestContext only sets these up for caster/victim; the bystander needs them too so it
    // is a normal, alive, undamaged occupant rather than the zero-initialized default (which
    // reads as POSITION_DEAD -- damage() would refuse to touch a "corpse"). Also flagged NPC for
    // the same reason as the fumble test above: without it, a splash hit that links the bystander
    // into set_fighting() bookkeeping would make it look like a player if the caster ever died
    // (not exercised by this no-fumble test, but kept consistent with the fumble test's fixture).
    context.master.specials2.act = MOB_ISNPC;
    context.master.abilities.hit = 500;
    context.master.tmpabilities.hit = 500;
    context.master.specials.position = POSITION_STANDING;
    context.master.specials.fighting = nullptr;

    world[kFireballRoom].people = caster;
    caster->next_in_room = &context.victim;
    context.victim.next_in_room = &context.master;
    context.master.next_in_room = nullptr;

    const int victim_hit_before = context.victim.tmpabilities.hit;
    obj_data *const previous_object_list = object_list;

    queue_fireball_rolls(kForceNoFumbleRoll);

    spell_fireball(caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    release_fireball_corpse(kFireballRoom, previous_object_list);

    EXPECT_LT(context.victim.tmpabilities.hit, victim_hit_before)
        << "the primary hit must still land on the named victim";
    EXPECT_EQ(caster->in_room, kFireballRoom)
        << "no fumble, no self-kill: the caster must still be standing where it cast from";
    EXPECT_EQ(character_list, caster)
        << "a caster that never fumbled must not have been extracted";

    // The caster survives this test, so tear it down the way extract_char() would.
    character_list = nullptr;
    remove_char_exists(caster->abs_number);
    free_char(caster);
}

// TASK-019 -- spell_earthquake's crack/fall loop (mage.cpp). The damage loop above it
// excludes the caster (`if (tmpch != caster)`), but the fall loop does not: on the coin
// flip the caster itself is moved into the crevice and takes fall damage INSIDE the
// occupant loop. A lethal fall runs apply_spell_damage() -> damage() -> die() ->
// raw_kill() -> extract_char(), which frees an NPC caster -- and the loop then keeps
// calling new_saves_spell(caster, tmpch, ...) for every later occupant, reading the
// freed caster's profs/tmpabilities/points fields. Same defect class and same fix shape
// as TASK-018: every other occupant falls first, the caster falls last, as the spell's
// final act.
//
// The caster's own lethal self-fall is the same self-damage shape TASK-018's fireball
// test already exercises (apply_spell_damage(caster, caster, ...) -> damage() -> die()
// -> raw_kill() -> extract_char()), so this test reuses that fixture wholesale
// (make_fireball_caster, ScopedFireballMobIndex, release_fireball_corpse,
// queue_fireball_rolls, kFireballRoom as the quake room): this depot has no
// extract_char test seam, so the fix is proven by driving the real death pipeline
// rather than stubbing it.
//
// RNG draw sequence (all pinned to the same value, see kEarthquakeSafeRoll below):
//   1. get_mage_caster_level(): one number(0, intel_factor % 5) roll (magnitude only).
//   2. dam_value = number(1, 30) + level (magnitude only).
//   3. First (non-fall) damage loop: one new_saves_spell() roll for the bystander --
//      the caster is skipped entirely by that loop's own `tmpch != caster` guard, so it
//      draws nothing here (magnitude only; damage()'s internal draws, if any, follow).
//   4. Fall loop, iteration 1 (caster -- head of the room's chain, so the unfixed loop
//      reaches it first): a new_saves_spell() roll (magnitude only, since `tmpch !=
//      caster` is always false for the caster, so `saved` never gates the branch);
//      then a CRITICAL number(0, 1) roll -- pinned to 0 so `!number(0, 1)` is true and
//      the caster falls; then the landing-save new_saves_spell() roll (magnitude only).
//   5. Fall loop, iteration 2 (bystander): a new_saves_spell() roll (may itself
//      short-circuit the OR before any number(0, 1) roll is drawn, depending on the
//      forced-low DC margin); if drawn, another CRITICAL number(0, 1) roll -- also
//      pinned to 0 so the bystander falls regardless; then its own landing-save
//      new_saves_spell() roll, then apply_spell_damage() -> damage() (non-lethal;
//      internal draws, if any, follow).
//   6. Deferred caster fall (after the loop, fixed code only): fall(caster, ...) ->
//      apply_spell_damage(caster, caster, ...) -> damage() -> die() -> raw_kill() ->
//      extract_char() -> free_char() / make_physical_corpse() -- the same death
//      pipeline TASK-018's fireball test exercises, unspecified internal draw count.
// Every draw above uses the SAME pinned value, so the exact count needs no bookkeeping
// beyond covering the two CRITICAL number(0, 1) rolls (steps 4 and 5): a generous
// uniform buffer suffices, exactly as TASK-018's fireball test does.
namespace {

// 0.25 is a dyadic fraction (exactly representable in IEEE double), so value * range is
// exact under BOTH x87 extended precision and SSE2 -- unlike a decimal fraction such as
// 0.60, it never lands a hair below an integer boundary and truncates one low on one
// platform but not the other. At range
// 2 (number(0, 1)) it yields exactly 0 (0.25 * 2 = 0.5, truncated -> 0), which is the
// one outcome this test's control flow depends on: every occupant must fall. At every
// other range used by this call (get_mage_caster_level's rounding roll, number(1, 30),
// number(1, 20) inside new_saves_spell), it yields a fixed, low-but-nonzero result that
// only affects damage magnitude, never which branch is taken.
constexpr double kEarthquakeSafeRoll = 0.25;
constexpr int kEarthquakeCrackRoom = 8;

} // namespace

TEST_F(MageProcTest, EarthquakeLetsEveryOtherOccupantFallBeforeTheCastersOwnFall) {
    ScopedFireballMobIndex prototype_table;
    RoomExitGuard quake_room_guard(kFireballRoom);
    RoomExitGuard crack_room_guard(kEarthquakeCrackRoom);

    char caster_short_descr[] = "a testing earthquake orc";
    char_data *caster = make_fireball_caster(1, caster_short_descr); // any quake fall is lethal
    character_list = caster;
    caster->next = nullptr;

    MageTestContext context; // supplies the bystander (context.victim)
    context.prepare_for_spell_damage();
    context.victim.in_room = kFireballRoom;

    // A door-less way down makes crack_chance certain (mage.cpp: `dir_option[DOWN] &&
    // !exit_info`), and an existing destination (kEarthquakeCrackRoom) takes the
    // "existing way down" branch rather than world.create_room().
    room_direction_data way_down{};
    way_down.to_room = kEarthquakeCrackRoom;
    way_down.exit_info = 0;
    world[kFireballRoom].dir_option[DOWN] = &way_down;

    // Caster is the HEAD of the room's chain: the unfixed loop reaches -- and frees --
    // it before the bystander is ever processed.
    world[kFireballRoom].people = caster;
    caster->next_in_room = &context.victim;
    context.victim.next_in_room = nullptr;

    obj_data *const previous_object_list = object_list;

    queue_fireball_rolls(kEarthquakeSafeRoll, 100);

    testing::internal::CaptureStderr();
    spell_earthquake(caster, nullptr, 0, nullptr, nullptr, 0, 0);
    const std::string captured = testing::internal::GetCapturedStderr();
    // caster is freed at this point (its own fall was lethal); nothing below may
    // dereference it -- only compare the pointer value or read state through survivors
    // (character_list, world[]'s room lists, the bystander).

    const int victim_location_after = context.victim.in_room;

    release_fireball_corpse(kEarthquakeCrackRoom, previous_object_list);
    release_fireball_corpse(kFireballRoom, previous_object_list);

    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "nothing may resolve the dead caster through a stale room number; stderr was: "
        << captured;
    EXPECT_EQ(victim_location_after, kEarthquakeCrackRoom)
        << "the bystander must still fall into the crack";
    EXPECT_EQ(world[kEarthquakeCrackRoom].people, &context.victim)
        << "the bystander must be the survivor left linked into the crack room's occupant chain "
           "once the caster's own deferred fall has been extracted back out of it";
    EXPECT_EQ(world[kFireballRoom].people, nullptr)
        << "both occupants must have left the quake room -- nothing remains there to be reached "
           "through a freed caster pointer on a later, hypothetical occupant";
    EXPECT_EQ(character_list, nullptr)
        << "extract_char()'s NPC arm must have unlinked the caster from character_list";
}

// ---------------------------------------------------------------------------
// TASK-021 Task 6: every mage formula helper gains a caster_snapshot overload
// that owns the body; the live const char_data* form is a one-line forwarder
// onto it. These tests pin the per-call RNG rolls (which stay inside the
// snapshot bodies) so the live and snapshot forms can be proven equivalent
// rather than merely both compiling.
// ---------------------------------------------------------------------------

TEST_F(MageProcTest, MageCasterLevelSnapshotFormMatchesLiveFormUnderPinnedRng) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 18;
    context.caster.tmpabilities.intel = 22; // intel_factor 4, remainder 4 -> a real number(0, 4) draw

    const caster_snapshot snap = caster_snapshot::capture(context.caster);

    push_test_random_value(0.5);
    const int live_level = get_mage_caster_level(&context.caster);
    push_test_random_value(0.5);
    EXPECT_EQ(get_mage_caster_level(snap), live_level)
        << "Expected the snapshot form to reproduce the live form's mage caster level under the "
           "same pinned remainder roll.";
}

TEST_F(MageProcTest, MagicPowerSnapshotFormMatchesLiveFormUnderPinnedRng) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 24;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    context.caster.specials.tactics = TACTICS_AGGRESSIVE;
    context.caster.points.spell_power = 60;
    context.caster.tmpabilities.intel = 22; // two remainder draws per call (own + get_mage_caster_level's)

    const caster_snapshot snap = caster_snapshot::capture(context.caster);

    push_test_random_value(0.5);
    push_test_random_value(0.5);
    const int live_power = get_magic_power(&context.caster);
    push_test_random_value(0.5);
    push_test_random_value(0.5);
    EXPECT_EQ(get_magic_power(snap), live_power)
        << "Expected the snapshot form to reproduce the live form's magic power, including the "
           "battle-mage bonus and the race-capped level modifier.";
}

TEST(MageHelpers, SpellPenetrationSnapshotFormMatchesLiveForm) {
    MageTestContext context;

    caster_snapshot snap = caster_snapshot::capture(context.caster);
    EXPECT_EQ(should_apply_spell_penetration(snap), should_apply_spell_penetration(&context.caster))
        << "Expected a player caster to agree between the live and snapshot forms.";

    context.caster.specials2.act = MOB_ISNPC;
    snap = caster_snapshot::capture(context.caster);
    EXPECT_EQ(should_apply_spell_penetration(snap), should_apply_spell_penetration(&context.caster))
        << "Expected an ordinary NPC caster to agree between the live and snapshot forms.";

    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    snap = caster_snapshot::capture(context.caster);
    EXPECT_EQ(should_apply_spell_penetration(snap), should_apply_spell_penetration(&context.caster))
        << "Expected a charmed orc-friend NPC with a player master to agree between the live and "
           "snapshot forms.";
}

TEST(MageHelpers, SpellPenValueSnapshotFormMatchesLiveFormForCharmedNpc) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;
    context.caster.specials2.act = MOB_ISNPC;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    context.caster.player.level = 20;
    context.master_profs.prof_level[PROF_MAGE] = 15;

    const caster_snapshot snap = caster_snapshot::capture(context.caster);
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), get_spell_pen_value(&context.caster))
        << "Expected the snapshot form to reproduce the live form's charmed-NPC master bonus.";
}

// Brief-mandated coverage: get_spell_pen_value()'s charmed-NPC-without-master
// arm, which the snapshot form reaches through master_mage_prof_level == 0
// (capture()'s guard for "no master" -- see
// CaptureDerivesTheCharmedOrcFriendSpellPenetrationPair in
// caster_snapshot_tests.cpp for the field itself).
TEST(MageHelpers, SpellPenValueSnapshotFormHandlesCharmedNpcWithoutMaster) {
    MageTestContext context;
    context.caster.specials2.act = MOB_ISNPC;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = nullptr;
    context.caster.player.level = 25;

    const caster_snapshot snap = caster_snapshot::capture(context.caster);
    EXPECT_TRUE(snap.is_npc);
    EXPECT_TRUE(snap.is_charmed);
    EXPECT_EQ(snap.master_mage_prof_level, 0)
        << "Expected a masterless charmed NPC to carry no master mage level.";
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), 5.0)
        << "Expected the master_mage_prof_level == 0 arm to add nothing on top of the NPC's own "
           "mage level (25 / 5).";
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), get_spell_pen_value(&context.caster))
        << "Expected the snapshot form to agree with the live form on the masterless arm.";
}

TEST(MageHelpers, VictimSavingThrowSnapshotFormMatchesLiveForm) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;
    context.victim.specials2.saving_throw = 10;
    context.victim.player.level = 25;

    caster_snapshot snap = caster_snapshot::capture(context.caster);
    EXPECT_DOUBLE_EQ(get_victim_saving_throw(snap, &context.victim),
        get_victim_saving_throw(&context.caster, &context.victim))
        << "Expected the snapshot form to agree with the live form for a spell-penetrating player "
           "caster.";

    context.caster.specials2.act = MOB_ISNPC;
    snap = caster_snapshot::capture(context.caster);
    EXPECT_DOUBLE_EQ(get_victim_saving_throw(snap, &context.victim),
        get_victim_saving_throw(&context.caster, &context.victim))
        << "Expected the snapshot form to agree with the live form for a non-penetrating NPC caster.";
}

TEST(MageHelpers, SaveBonusSnapshotFormMatchesLiveForm) {
    MageTestContext context;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Fire);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Cold);

    const caster_snapshot snap = caster_snapshot::capture(context.caster);
    EXPECT_EQ(get_save_bonus(snap, context.victim, game_types::PS_Fire, game_types::PS_Cold),
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold))
        << "Expected the snapshot form to agree with the live form's specialization matchup.";
}

TEST(MageHelpers, FriendlyTargetSnapshotFormAgreesWithLiveFormAndSelfTestUsesSameCharacterAs) {
    MageTestContext context;
    char_data follower{};
    follower.master = &context.caster;

    const caster_snapshot snap = caster_snapshot::capture(context.caster);

    // The self test: caster.same_character_as(*victim) must hold only for the
    // very character the snapshot was captured from, matching the live form's
    // `victim == caster` identity test.
    EXPECT_TRUE(is_friendly_taget(snap, &context.caster))
        << "Expected a caster snapshot to count as a friendly target to the character it was "
           "captured from.";
    EXPECT_TRUE(snap.same_character_as(context.caster));
    EXPECT_FALSE(snap.same_character_as(context.victim))
        << "same_character_as() must not treat an unrelated character as the captured caster.";

    EXPECT_EQ(is_friendly_taget(snap, &context.caster), is_friendly_taget(&context.caster, &context.caster));
    EXPECT_EQ(is_friendly_taget(snap, &follower), is_friendly_taget(&context.caster, &follower))
        << "Expected the snapshot form to agree with the live form across a follower chain.";
    EXPECT_EQ(is_friendly_taget(snap, &context.victim), is_friendly_taget(&context.caster, &context.victim))
        << "Expected the snapshot form to agree with the live form for a same-side character.";

    context.victim.player.race = RACE_ORC;
    EXPECT_EQ(is_friendly_taget(snap, &context.victim), is_friendly_taget(&context.caster, &context.victim))
        << "Expected the snapshot form to agree with the live form for an opposing-side character.";
}

// ---------------------------------------------------------------------------
// TASK-025: spell_summon body coverage (the spell had no body test anywhere
// in the tree before this port). The body itself has NO sight check by
// design; the dark-room targeting fix lives in consts.cpp's mask and is
// pinned by summon_targeting_tests.cpp's SummonTargeting suite. This
// exercises the success arm end to end: a willing (PRF_SUMMONABLE clear --
// the flag is inverted: set == NOT summonable), non-fighting, mortal player
// victim who fails the save is moved into the caster's room through the real
// char_from_room()/char_to_room() pair.
//
// The victim deliberately has NO descriptor: a linkdead player is a legal
// summon target, and this pins act_move.cpp's existing null-desc guard in
// msdp_room_update() (`if (!ch->desc) return;`, act_move.cpp:599-601) --
// spell_summon() calls msdp_room_update(victim) on its success arm, and a
// desc-less victim exercises that guard on every run of this test.
// ---------------------------------------------------------------------------

TEST_F(MageProcTest, SummonMovesAWillingPlayerVictimToTheCastersRoom) {
    MageTestContext context;
    // The victim is a player here (specials2.act stays 0 -- not MOB_ISNPC),
    // and act()'s $N formatting for the caster's success message reads a
    // player's name (GET_NAME() prefers player.name over short_descr for a PC).
    char summon_victim_name[16] = "test_target";
    context.victim.player.name = summon_victim_name;

    // spell_summon() reads zone_table[room->zone].x/y for its save-bonus
    // distance term, and char_to_room()/char_from_room() bump the zone's
    // goodness/evilness power counters -- both real zone_table dereferences
    // this shared test binary never boots on its own (see the guard's own
    // comment). Both rooms are zone 0 in the shared test world.
    ZoneTableGuard zone_table_guard;
    ZoneGuard zone_guard(7, 8);
    world[7].zone = 0;
    world[8].zone = 0;

    // Place both characters with real occupant chains -- the spell unlinks
    // and relinks the victim through char_from_room()/char_to_room().
    RoomExitGuard caster_room_guard(7);
    RoomExitGuard victim_room_guard(8);
    world[7].room_flags = 0; // clear any NO_TELEPORT leftover from an earlier suite in this room
    world[7].people = nullptr;
    world[8].people = nullptr;
    char_to_room(&context.caster, 7);
    char_to_room(&context.victim, 8);

    descriptor_data caster_descriptor = make_descriptor();
    context.caster.desc = &caster_descriptor;

    // new_saves_spell(): casting_dc = 10 + 0 (zero mage-prof level) +
    // (20-8)/4 = 13. get_character_saving_throw(victim) = 0 (zero mage-prof
    // level) + (20-8)/4 = 3. spell_summon's dist is the squared distance
    // between the caster's and victim's zone map coordinates: both rooms are
    // zone 0 on the zero-initialized zone_data stub, so ch_x/ch_y/v_x/v_y are
    // all 0 and dist == 0, giving save_value = 3 + 0 = 3. The single draw
    // below (the save roll, number(1,20)) is a queued midpoint of the
    // roll==1 bucket, so roll == 1 and 1+3=4, which is not > 13 --
    // new_saves_spell() returns false (the victim fails to save) and the
    // success arm runs.
    push_test_random_value(0.025); // (1 - 1 + 0.5) / 20 -- midpoint of the roll==1 bucket

    spell_summon(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.in_room, 7)
        << "Expected the summoned victim to be moved into the caster's room.";
    const std::string caster_output = caster_descriptor.output;
    EXPECT_NE(caster_output.find("appears in the room."), std::string::npos)
        << "Expected the caster to see the success message; output was: " << caster_output;

    // Fixture hygiene: undo spell_summon's own char_from_room()/char_to_room()
    // move before the guards above unwind, so no stack character survives in
    // world[]'s occupant chains after the test ends.
    char_from_room(&context.victim);
    char_from_room(&context.caster);
}

// spell_summon()'s save bonus is the squared distance between the caster's
// and victim's zone map coordinates (dist = delta_x^2 + delta_y^2), not the
// bitwise XOR of the coordinate deltas. This test puts the two zones 4
// x-units apart and pins a save_value that only the squared arithmetic
// produces: under the fix the victim saves and the summon fails; under the
// pre-fix XOR arithmetic the same setup would have let the summon succeed
// (see the derivation below), so this test is red against the bug and green
// against the fix.
TEST_F(MageProcTest, SummonSaveBonusUsesSquaredZoneDistance) {
    MageTestContext context;
    char summon_victim_name[16] = "test_target";
    context.victim.player.name = summon_victim_name;

    // spell_summon() reads zone_table[room->zone].x/y for its save-bonus
    // distance term (see ZoneTableGuard's comment above). Put the caster's
    // zone (0) at (0, 0) and the victim's zone (1) at (4, 0), 4 x-units away
    // -- y stays 0 for both so the y term of dist is 0.
    ZoneTableGuard zone_table_guard;
    zone_table_guard.stub[0].x = 0;
    zone_table_guard.stub[0].y = 0;
    zone_table_guard.stub[1].x = 4;
    zone_table_guard.stub[1].y = 0;

    ZoneGuard zone_guard(7, 8);
    world[7].zone = 0;
    world[8].zone = 1;

    // Place both characters with real occupant chains -- different_zone()
    // is true here (zone 0 vs zone 1), so spell_summon() also exercises
    // prohibit_item_stay_zone_move() before the save roll; neither character
    // carries or wears anything, so that call is a no-op.
    RoomExitGuard caster_room_guard(7);
    RoomExitGuard victim_room_guard(8);
    world[7].room_flags = 0; // clear any NO_TELEPORT leftover from an earlier suite in this room
    world[7].people = nullptr;
    world[8].people = nullptr;
    char_to_room(&context.caster, 7);
    char_to_room(&context.victim, 8);

    descriptor_data caster_descriptor = make_descriptor();
    context.caster.desc = &caster_descriptor;

    // new_saves_spell(): casting_dc = 13 and get_character_saving_throw(victim)
    // = 3, same as the same-zone test above (identical caster/victim mage
    // levels and intel). dist = (4-0)^2 + (0-0)^2 = 16, so save_value = 3 +
    // 16 = 19. The single draw below (the save roll, number(1,20)) is a
    // queued midpoint of the roll==1 bucket, so roll == 1 and 1+19=20, which
    // IS > 13 -- new_saves_spell() returns true (the victim saves) and the
    // summon fails.
    //
    // Under the pre-fix bitwise-XOR arithmetic, the same coordinates would
    // have produced dist = (4^2) + (0^2) = 6 + 2 = 8 (XOR, not squaring), so
    // save_value = 3 + 8 = 11 and 1+11=12, which is NOT > 13 -- the summon
    // would have incorrectly succeeded.
    push_test_random_value(0.025); // (1 - 1 + 0.5) / 20 -- midpoint of the roll==1 bucket

    spell_summon(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.in_room, 8)
        << "Expected the victim to save against the summon and stay in its own room.";
    const std::string caster_output = caster_descriptor.output;
    EXPECT_NE(caster_output.find("You failed."), std::string::npos)
        << "Expected the caster to see the failure message; output was: " << caster_output;

    // Fixture hygiene: the victim never moved, but char_to_room() above
    // still linked both characters into world[]'s occupant chains.
    char_from_room(&context.victim);
    char_from_room(&context.caster);
}
