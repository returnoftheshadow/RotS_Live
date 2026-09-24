#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "../zone.h"
#include "test_character_support.h"
#include "test_random_utils.h"
#include "test_spell_support.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <utility>

// get_mage_caster_level/get_magic_power/should_apply_spell_penetration/
// get_spell_pen_value/get_victim_saving_throw/get_save_bonus/
// is_spared_by_room_blast are declared by spells.h, included above.
bool different_zone(int was_in, int to_room);
int random_exit(int room);
bool is_teleportation_room_valid(room_data *room);
void apply_chilled_effect(char_data *caster, char_data *victim);
int get_character_saving_throw(const char_data* victim);

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
extern struct char_data* combat_list;
extern struct char_data* combat_next_dude;
extern short spllog_save;

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

// caster, victim and master are stack objects. No test may let damage() kill one of them:
// a lethal hit routes through extract_char() and free_char(), which would free a stack
// address. Tests that need a killable body allocate it with make_fireball_caster() or
// test_support::allocate_test_character() instead.
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
    struct zone_data *previous_table; // real zone_table found before the test; restored on scope exit
    int previous_top; // real top_of_zone_table found before the test; restored on scope exit
    struct zone_data stub[2]{}; // the one-entry-per-zone stub table installed for the scope

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

    // The spell pins engage stack-local casters and victims through damage()'s
    // set_fighting() and never stop_fighting() them, so the global combat_list
    // would keep dangling stack addresses for every later suite that walks it
    // (extract_char() -> stop_fighting_him()). Reset it the way damage_tests.cpp does.
    void TearDown() override {
        clear_test_random_values();
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }
};

TEST_F(MageProcTest, MageCasterLevelUsesCurrentIntelRoundingPath) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 18;
    context.caster.tmpabilities.intel = 19;

    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(caster_snapshot::capture(context.caster)), 21)
        << "Expected low queued rolls to keep the current partial-intelligence bonus unrounded.";

    push_test_random_value(0.99);
    EXPECT_EQ(get_mage_caster_level(caster_snapshot::capture(context.caster)), 22)
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
    EXPECT_EQ(get_magic_power(caster_snapshot::capture(context.caster)), 124)
        << "Expected magic power to combine mage level, battle-mage bonus, level modifier, and the "
           "current low-roll intel contribution.";

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    EXPECT_EQ(get_magic_power(caster_snapshot::capture(context.caster)), 126)
        << "Expected magic power to increase by one when the queued intel-rounding roll succeeds.";
}

TEST(MageHelpers, SpellPenetrationAppliesForPlayersAndEligibleCharmedOrcFriends) {
    MageTestContext context;

    EXPECT_TRUE(should_apply_spell_penetration(caster_snapshot::capture(context.caster)))
        << "Expected player casters to always apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_FALSE(should_apply_spell_penetration(caster_snapshot::capture(context.caster)))
        << "Expected ordinary NPC casters not to apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    EXPECT_TRUE(should_apply_spell_penetration(caster_snapshot::capture(context.caster)))
        << "Expected charmed orc-friend NPCs with a player master to apply spell penetration.";
}

TEST(MageHelpers, SpellPenetrationRejectsCharmedOrcFriendsWithoutPlayerMaster) {
    MageTestContext context;
    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;

    EXPECT_FALSE(should_apply_spell_penetration(caster_snapshot::capture(context.caster)))
        << "Expected charmed orc-friend NPCs without a master to skip spell penetration.";

    context.master.specials2.act = MOB_ISNPC;
    context.caster.master = &context.master;
    EXPECT_FALSE(should_apply_spell_penetration(caster_snapshot::capture(context.caster)))
        << "Expected charmed orc-friend NPCs with a non-player master to skip spell penetration.";
}

TEST(MageHelpers, SpellPenValueUsesCasterAndMasterMageLevelsForCharmedNpcs) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(caster_snapshot::capture(context.caster)), 4.0)
        << "Expected player spell penetration to use one fifth of the caster's mage level.";

    context.caster.specials2.act = MOB_ISNPC;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    context.caster.player.level = 20;
    context.master_profs.prof_level[PROF_MAGE] = 15;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(caster_snapshot::capture(context.caster)), 5.0)
        << "Expected charmed NPC spell penetration to include one third of the master's mage "
           "level.";
}

TEST(MageHelpers, VictimSavingThrowUsesSpellPenetrationAndPlayerLevelAdjustment) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;
    context.victim.specials2.saving_throw = 10;
    context.victim.player.level = 25;

    EXPECT_DOUBLE_EQ(get_victim_saving_throw(caster_snapshot::capture(context.caster), &context.victim), 11.0)
        << "Expected player victims to offset spell penetration with the current level-based "
           "saving-throw adjustment.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_DOUBLE_EQ(get_victim_saving_throw(caster_snapshot::capture(context.caster), &context.victim), 10.0)
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
        get_save_bonus(caster_snapshot::capture(context.caster), context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected matching caster specialization and opposing victim specialization to stack "
           "the current save-bonus reductions.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Fire);
    EXPECT_EQ(
        get_save_bonus(caster_snapshot::capture(context.caster), context.victim, game_types::PS_Fire, game_types::PS_Cold), 4)
        << "Expected opposing caster specialization and matching victim specialization to stack "
           "the current save-bonus increases.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    EXPECT_EQ(
        get_save_bonus(caster_snapshot::capture(context.caster), context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected arcane specialization to count as primary for the caster and opposing for the "
           "victim in the current implementation.";
}

// A mist loses 3 levels on its first hop and 3 more on each further hop (3, 9, 18, 30
// in total), and never goes below zero.
TEST(MageHelpers, MistEffectiveLevelFallsOffTriangularlyPerHop) {
    EXPECT_EQ(mist_spread_level_loss(0), 0);
    EXPECT_EQ(mist_spread_level_loss(1), 3);
    EXPECT_EQ(mist_spread_level_loss(2), 9);
    EXPECT_EQ(mist_spread_level_loss(3), 18);
    EXPECT_EQ(mist_spread_level_loss(4), 30);

    EXPECT_EQ(mist_effective_level(30, 0), 30);
    EXPECT_EQ(mist_effective_level(30, 1), 27);
    EXPECT_EQ(mist_effective_level(30, 2), 21);
    EXPECT_EQ(mist_effective_level(30, 3), 12);
    EXPECT_EQ(mist_effective_level(30, 4), 0) << "clamped, never negative";
    EXPECT_EQ(mist_effective_level(20, 3), 2);
    EXPECT_EQ(mist_effective_level(20, 4), 0);
    EXPECT_EQ(mist_effective_level(30, -1), 30) << "a negative generation is treated as the cast room";
}

// One blaze burn is number(8, level) + 10, halved on a save. A mid roll makes number(8, 30)
// return 8 + 23 / 2 = 19.
TEST(MageHelpers, BlazeBurnDamageIsEightToLevelPlusTenHalvedOnASave) {
    push_test_random_value(0.5);
    EXPECT_EQ(blaze_burn_damage(30, false), 29);
    push_test_random_value(0.5);
    EXPECT_EQ(blaze_burn_damage(30, true), 14) << "halved by a right shift, as the spell always did";
    clear_test_random_values();
}

namespace {

// Stamps an uncharmed mob of `race` and `alignment` onto a zeroed char_data.
void make_room_blast_mob(char_data &mob, int race, int alignment) {
    mob.specials2.act = MOB_ISNPC;
    mob.player.race = race;
    mob.specials2.alignment = alignment;
}

// Stamps a charmed pet following `master`.
void make_room_blast_pet(char_data &pet, char_data *master) {
    pet.specials2.act = MOB_ISNPC | MOB_PET;
    SET_BIT(pet.specials.affected_by, AFF_CHARM);
    pet.master = master;
}

} // namespace

// The player half of is_spared_by_room_blast(): the caster, same-side players and anyone whose
// follow chain leads to one of them are spared; the other side is not.
TEST(MageHelpers, RoomBlastSparesTheCasterSameSidePlayersAndTheirFollowers) {
    MageTestContext context; // caster and master are same-side human players
    const caster_snapshot caster_at_cast = caster_snapshot::capture(context.caster);

    char_data pet{};
    make_room_blast_pet(pet, &context.caster);
    char_data pets_pet{};
    make_room_blast_pet(pets_pet, &pet);
    char_data ally_pet{};
    make_room_blast_pet(ally_pet, &context.master);
    char_data enemy{};
    enemy.player.race = RACE_ORC;
    char_data enemy_pet{};
    make_room_blast_pet(enemy_pet, &enemy);

    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &context.caster)) << "the caster";
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &pet)) << "the caster's pet";
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &pets_pet))
        << "a follow chain that ends at the caster";
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &context.master)) << "a same-side player";
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &ally_pet)) << "a same-side player's pet";
    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &enemy)) << "an other-side player";
    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &enemy_pet)) << "an other-side player's pet";
}

TEST(MageHelpers, RoomBlastSelfTestUsesSameCharacterAs) {
    MageTestContext context;
    const caster_snapshot snap = caster_snapshot::capture(context.caster);

    EXPECT_TRUE(is_spared_by_room_blast(snap, context.caster, &context.caster))
        << "a caster snapshot must spare the character it was captured from";
    EXPECT_TRUE(snap.same_character_as(context.caster));
    EXPECT_FALSE(snap.same_character_as(context.victim))
        << "same_character_as() must not treat an unrelated character as the captured caster";
}

// Rule 1: race 0 (RACE_GOD) is what builders leave animals, golems and unfinished mobs at, and
// other_side_race() puts it on every side. Such a mob has no side, so it is never spared.
TEST(MageHelpers, RoomBlastNeverSparesAMobWithoutARace) {
    MageTestContext context;
    const caster_snapshot caster_at_cast = caster_snapshot::capture(context.caster);

    char_data wolf{};
    make_room_blast_mob(wolf, RACE_GOD, 0);
    char_data kindly_golem{};
    make_room_blast_mob(kindly_golem, RACE_GOD, 1000);

    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &wolf));
    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &kindly_golem))
        << "a good alignment does not give a raceless mob a side";
}

// Rule 2: a mob with a race is spared only when that race is on the caster's side of the race war,
// by the same rule that places players (other_side_race()).
TEST(MageHelpers, RoomBlastSparesOnlyMobsWhoseRaceIsOnTheCastersSide) {
    MageTestContext context; // a human caster: the good side
    const caster_snapshot caster_at_cast = caster_snapshot::capture(context.caster);

    for (const int good_race : { RACE_HUMAN, RACE_DWARF, RACE_WOOD, RACE_HOBBIT, RACE_HIGH, RACE_BEORNING }) {
        char_data mob{};
        make_room_blast_mob(mob, good_race, 0);
        EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &mob)) << "race " << good_race;
    }

    constexpr int kHalfOrcRace = 19; // no named constant; the mob files use it for half-orcs
    for (const int evil_race : { RACE_URUK, RACE_HARAD, RACE_ORC, RACE_EASTERLING, RACE_MAGUS, RACE_UNDEAD,
             RACE_OLOGHAI, RACE_HARADRIM, kHalfOrcRace, RACE_TROLL }) {
        char_data mob{};
        make_room_blast_mob(mob, evil_race, 0);
        EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &mob)) << "race " << evil_race;
    }
}

// Rule 3: builders gave a side's outlaws that side's race, so an alignment opposed to the
// caster's side overrides the race. Zero opposes neither side.
TEST(MageHelpers, RoomBlastBurnsAMobWhoseAlignmentOpposesTheCastersSide) {
    MageTestContext context;
    const caster_snapshot good_caster = caster_snapshot::capture(context.caster);

    char_data bandit{};
    make_room_blast_mob(bandit, RACE_HUMAN, -30);
    char_data neutral_villager{};
    make_room_blast_mob(neutral_villager, RACE_HUMAN, 0);
    EXPECT_FALSE(is_spared_by_room_blast(good_caster, context.caster, &bandit))
        << "a good-side caster burns a human mob with evil alignment";
    EXPECT_TRUE(is_spared_by_room_blast(good_caster, context.caster, &neutral_villager))
        << "alignment 0 does not oppose the good side";

    context.caster.player.race = RACE_ORC;
    const caster_snapshot evil_caster = caster_snapshot::capture(context.caster);

    char_data orc_warrior{};
    make_room_blast_mob(orc_warrior, RACE_ORC, -500);
    char_data orc_slave{};
    make_room_blast_mob(orc_slave, RACE_ORC, 150);
    char_data neutral_orc{};
    make_room_blast_mob(neutral_orc, RACE_ORC, 0);
    EXPECT_TRUE(is_spared_by_room_blast(evil_caster, context.caster, &orc_warrior));
    EXPECT_FALSE(is_spared_by_room_blast(evil_caster, context.caster, &orc_slave))
        << "an evil-side caster burns an orc mob with good alignment";
    EXPECT_TRUE(is_spared_by_room_blast(evil_caster, context.caster, &neutral_orc))
        << "alignment 0 does not oppose the evil side";
}

// Rule 4: a mob already fighting the caster's party (the caster, a group member, or anyone
// following one of them) is burned whatever its race, alignment or master.
TEST(MageHelpers, RoomBlastBurnsASameSideMobFightingTheCastersParty) {
    MageTestContext context; // master is the caster's group-mate here
    group_data party(&context.caster);
    party.add_member(&context.master);
    const caster_snapshot caster_at_cast = caster_snapshot::capture(context.caster);

    char_data pet{};
    make_room_blast_pet(pet, &context.caster);
    char_data mates_pet{};
    make_room_blast_pet(mates_pet, &context.master);
    char_data stranger{};
    stranger.player.race = RACE_HUMAN; // a same-side player outside the party
    char_data orc{};
    make_room_blast_mob(orc, RACE_ORC, -500);

    char_data elf{};
    make_room_blast_mob(elf, RACE_WOOD, 500);
    ASSERT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf)) << "a peaceful elf is spared";

    const std::pair<char_data *, const char *> party_opponents[] = {
        { &context.caster, "fighting the caster" },
        { &context.master, "fighting a group-mate" },
        { &pet, "fighting the caster's pet" },
        { &mates_pet, "fighting a group-mate's pet" },
    };
    for (const auto &opponent : party_opponents) {
        elf.specials.fighting = opponent.first;
        EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf)) << opponent.second;
    }

    elf.specials.fighting = &orc;
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf))
        << "fighting someone outside the party does not count";
    elf.specials.fighting = &stranger;
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf))
        << "the party is the caster's group, not the whole side";

    char_data elf_follower{};
    make_room_blast_mob(elf_follower, RACE_WOOD, 500);
    elf_follower.master = &context.caster;
    elf_follower.specials.fighting = &context.caster;
    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf_follower))
        << "fighting the party outranks following the caster";
}

// Two characters without a group are not group-mates, so an ungrouped caster's party is only
// itself and its followers: a same-side mob fighting an ungrouped stranger is spared, and one
// fighting the caster itself still burns.
TEST(MageHelpers, RoomBlastUnderAnUngroupedCasterSparesAMobFightingAnUngroupedStranger) {
    MageTestContext context;
    ASSERT_EQ(context.caster.group, nullptr) << "precondition: the fixture caster is ungrouped";
    const caster_snapshot caster_at_cast = caster_snapshot::capture(context.caster);

    char_data stranger{};
    stranger.player.race = RACE_HUMAN; // an ungrouped same-side player outside the party
    char_data elf{};
    make_room_blast_mob(elf, RACE_WOOD, 500);

    elf.specials.fighting = &stranger;
    EXPECT_TRUE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf))
        << "an ungrouped caster and an ungrouped stranger must not count as one party";

    elf.specials.fighting = &context.caster;
    EXPECT_FALSE(is_spared_by_room_blast(caster_at_cast, context.caster, &elf))
        << "a mob fighting the ungrouped caster itself is still burned";
}

// An immortal player caster (race 0) belongs to neither side, so it shares a side with no mob.
// Players stay under other_side(), which puts RACE_GOD on every player's side.
TEST(MageHelpers, RoomBlastUnderAnImmortalCasterSparesNoMob) {
    MageTestContext context;
    char_data elf{};
    make_room_blast_mob(elf, RACE_WOOD, 500);

    context.caster.player.race = RACE_GOD;
    const caster_snapshot immortal = caster_snapshot::capture(context.caster);
    EXPECT_FALSE(is_spared_by_room_blast(immortal, context.caster, &elf)) << "an immortal's blast spares no mob";
    EXPECT_TRUE(is_spared_by_room_blast(immortal, context.caster, &context.master))
        << "other_side() puts RACE_GOD on every player's side";
}

// An uncharmed mob caster judges players by race, as it judges mobs. other_side() alone would
// put every player on its side. A mob caster with no side (race 0) spares nobody.
TEST(MageHelpers, RoomBlastUnderAMobCasterJudgesPlayersByRace) {
    MageTestContext context;
    context.caster.specials2.act = MOB_ISNPC;
    char_data human_player{};
    human_player.player.race = RACE_HUMAN;
    char_data orc_player{};
    orc_player.player.race = RACE_ORC;
    char_data elf{};
    make_room_blast_mob(elf, RACE_WOOD, 500);
    char_data orc{};
    make_room_blast_mob(orc, RACE_ORC, -500);

    context.caster.player.race = RACE_HUMAN;
    const caster_snapshot human_mob = caster_snapshot::capture(context.caster);
    EXPECT_TRUE(is_spared_by_room_blast(human_mob, context.caster, &human_player));
    EXPECT_FALSE(is_spared_by_room_blast(human_mob, context.caster, &orc_player));
    EXPECT_TRUE(is_spared_by_room_blast(human_mob, context.caster, &elf));
    EXPECT_FALSE(is_spared_by_room_blast(human_mob, context.caster, &orc));

    context.caster.player.race = RACE_ORC;
    const caster_snapshot orc_mob = caster_snapshot::capture(context.caster);
    EXPECT_FALSE(is_spared_by_room_blast(orc_mob, context.caster, &human_player));
    EXPECT_TRUE(is_spared_by_room_blast(orc_mob, context.caster, &orc_player));
    EXPECT_FALSE(is_spared_by_room_blast(orc_mob, context.caster, &elf));
    EXPECT_TRUE(is_spared_by_room_blast(orc_mob, context.caster, &orc));

    context.caster.player.race = RACE_GOD;
    const caster_snapshot raceless_mob = caster_snapshot::capture(context.caster);
    EXPECT_FALSE(is_spared_by_room_blast(raceless_mob, context.caster, &human_player));
    EXPECT_FALSE(is_spared_by_room_blast(raceless_mob, context.caster, &orc_player));
    EXPECT_FALSE(is_spared_by_room_blast(raceless_mob, context.caster, &elf));
}

// A charmed caster (an orc follower ordered to cast) is judged as the head of its follow chain;
// every other caster is judged as itself.
TEST(MageHelpers, RoomBlastOwnerIsTheHeadOfACharmedCastersFollowChain) {
    MageTestContext context; // caster is a player
    char_data follower{};
    make_room_blast_pet(follower, &context.caster);
    char_data followers_follower{};
    make_room_blast_pet(followers_follower, &follower);
    char_data mob_leader{};
    make_room_blast_mob(mob_leader, RACE_ORC, -500);
    char_data mob_follower{};
    make_room_blast_mob(mob_follower, RACE_ORC, -500);
    mob_follower.master = &mob_leader;
    char_data masterless_charmed{};
    make_room_blast_pet(masterless_charmed, nullptr);

    EXPECT_EQ(room_blast_owner(&context.caster), &context.caster) << "a player casts for itself";
    EXPECT_EQ(room_blast_owner(&follower), &context.caster) << "a charmed follower casts for its master";
    EXPECT_EQ(room_blast_owner(&followers_follower), &context.caster) << "the head of a longer chain";
    EXPECT_EQ(room_blast_owner(&mob_follower), &mob_follower) << "an uncharmed mob casts for itself";
    EXPECT_EQ(room_blast_owner(&masterless_charmed), &masterless_charmed) << "no master to cast for";
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

    test_support::cast_spell(spell_magic_missile, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_chill_ray, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_chill_ray, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_lightning_bolt, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_dark_bolt, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_firebolt, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

    test_support::cast_spell(spell_cone_of_cold, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

// spell_fireball's orc self-fumble arm (mage.cpp, `victim = caster;`) hands the
// caster to apply_spell_damage() as its own victim. When that hit is lethal, fight.cpp's
// damage() runs die() -> raw_kill() -> extract_char(), whose NPC arm unlinks AND free_char()s
// the caster -- and the body used to continue straight into world[caster->in_room].people (the
// splash loop) and its friendly-target check using the now-freed caster.
//
// This depot has no extract_char test seam, so the fixture drives the real death pipeline instead
// of stubbing it: the caster is heap-allocated and registered the way the game builds an NPC
// (register_npc_char, linked into character_list and a real room), so free_char() is legal to call
// on it. A one-entry mob_index[] is installed because raw_kill()'s SPECIAL_DEATH probe
// (activate_char_special) and make_physical_corpse() both dereference the caster's mob
// prototype slot unconditionally for any IS_NPC() character.
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
    char_data *caster = test_support::allocate_test_character(MOB_ISNPC);
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
    // NPC flag: a non-NPC bystander that dies in the splash would be extracted and freed as a
    // player body through extract_char() and free_char(), and master is a stack object (see
    // the struct comment above).
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
    test_support::cast_spell(spell_fireball, caster, nullptr, 0, &context.victim, nullptr, 0, 0);
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

    testing::internal::CaptureStderr();
    test_support::cast_spell(spell_fireball, caster, nullptr, 0, &context.victim, nullptr, 0, 0);
    const std::string captured = testing::internal::GetCapturedStderr();
    // CaptureStderr dup2s a temp file over fd 2, which is also where AddressSanitizer
    // reports; the window must close (GetCapturedStderr, above) before any call that
    // could fault under ASan, so release_fireball_corpse() runs outside it, matching the
    // fumble test above.
    release_fireball_corpse(kFireballRoom, previous_object_list);

    // The capture window above covers only spell_fireball() itself (release_fireball_corpse()
    // runs after GetCapturedStderr, per the comment above it); this assertion pins that the
    // cast's own obj_from_room() calls log no SYSERR.
    EXPECT_EQ(captured.find("obj_from_room: object is not in its room's contents list."), std::string::npos)
        << "the cast's own obj_from_room() calls must log no SYSERR; stderr was: "
        << captured;
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

namespace {

// Stamped on kFireballRoom for a scene's scope: blaze records its caster keyed on the room's
// number, and the shared test world's rooms all default to -1.
constexpr int kRoomBlastRoomNumber = 3007;

// kFireballRoom set up for a room-blast cast: occupants, exits and flags restored on exit
// (RoomExitGuard), the room number stamped, and any room affect a blaze leaves (with its caster
// record) removed.
struct ScopedBlastRoom {
    RoomExitGuard room_guard{kFireballRoom}; // restores the room's occupants, exits and flags
    int original_room_number = 0; // world[kFireballRoom].number before the scope

    ScopedBlastRoom() {
        original_room_number = world[kFireballRoom].number;
        world[kFireballRoom].number = kRoomBlastRoomNumber;
    }

    ~ScopedBlastRoom() {
        while (world[kFireballRoom].affected) {
            affect_remove_room(&world[kFireballRoom], world[kFireballRoom].affected);
        }
        world[kFireballRoom].number = original_room_number;
    }

    // Links `occupants` into the room in order.
    template <std::size_t kCount>
    void seat(char_data *const (&occupants)[kCount]) {
        world[kFireballRoom].people = occupants[0];
        for (std::size_t index = 0; index + 1 < kCount; ++index) {
            occupants[index]->next_in_room = occupants[index + 1];
        }
        occupants[kCount - 1]->next_in_room = nullptr;
    }
};

// A stack occupant of kFireballRoom with more hit points than one blast can take.
void prepare_blast_occupant(char_data &occupant, int race, int alignment) {
    occupant.player.race = race;
    occupant.specials2.alignment = alignment;
    occupant.player.level = 10;
    occupant.abilities.hit = 500;
    occupant.tmpabilities.hit = 500;
    occupant.specials.position = POSITION_STANDING;
    occupant.in_room = kFireballRoom;
}

void queue_blaze_rolls() {
    queue_fireball_rolls(0.05, 200);
}

// A room-blast scene in kFireballRoom: a good-side human player caster with one bystander per
// sparing rule, plus an orc mob as fireball's named target. Every character is a stack object with
// more hit points than one blast can take, so nothing dies.
struct RoomBlastScene {
    MageTestContext context; // the caster (a human player) and the named target (an orc mob)
    char_data pet{}; // the caster's charmed pet: spared
    char_data elf{}; // a peaceful wood-elf mob: spared (same-side race)
    char_data wolf{}; // a race-0 mob: burned (rule 1, no race)
    char_data orc{}; // an orc mob: burned (rule 2, other-side race)
    char_data bandit{}; // a human mob with evil alignment: burned (rule 3)
    char_data hostile_elf{}; // a wood-elf mob fighting the caster: burned (rule 4)
    ScopedBlastRoom room; // the room, restored and cleaned on exit

    explicit RoomBlastScene(game_types::player_specs specialization) {
        context.caster_profs.specialization = static_cast<int>(specialization);
        context.caster_profs.prof_level[PROF_MAGE] = 30; // enough power that every hit does damage
        context.caster.in_room = kFireballRoom;
        context.prepare_for_spell_damage();
        context.victim.player.race = RACE_ORC;
        context.victim.specials2.alignment = -500;
        context.victim.in_room = kFireballRoom;

        prepare_bystander(pet, RACE_ORC, -500); // a pet's own race and alignment never matter
        pet.specials2.act |= MOB_PET;
        SET_BIT(pet.specials.affected_by, AFF_CHARM);
        prepare_bystander(elf, RACE_WOOD, 500);
        prepare_bystander(wolf, RACE_GOD, 0);
        prepare_bystander(orc, RACE_ORC, -500);
        prepare_bystander(bandit, RACE_HUMAN, -30);
        prepare_bystander(hostile_elf, RACE_WOOD, 500);
        hostile_elf.specials.fighting = &context.caster;

        char_data *const occupants[] = { &context.caster, &context.victim, &pet, &elf, &wolf, &orc, &bandit, &hostile_elf };
        room.seat(occupants);

        // After the occupant list: add_follower()'s act() lines walk it.
        add_follower(&pet, &context.caster, FOLLOW_MOVE);
    }

    ~RoomBlastScene() {
        // Returns the follow node to its pool if the cast left the pet following.
        stop_follower(&pet, FOLLOW_MOVE);
    }

    void cast_fireball() {
        // A human caster draws no fumble roll; every other draw is low, so each splash roll
        // (number() <= 0.2) lands.
        queue_blaze_rolls();
        test_support::cast_spell(spell_fireball, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);
    }

    void cast_blaze() {
        queue_blaze_rolls();
        test_support::cast_spell(spell_blaze, &context.caster, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    }

    static void prepare_bystander(char_data &bystander, int race, int alignment) {
        bystander.specials2.act = MOB_ISNPC;
        prepare_blast_occupant(bystander, race, alignment);
    }

    // The spared/burned split both blasts must produce for a good-side caster. The caster's own
    // hit points are not a usable witness: the cast recalculates this bare stack caster's
    // maximum (500 to 10). RoomBlastSelfTestUsesSameCharacterAs pins that the caster is spared.
    void expect_only_the_casters_side_spared() const {
        EXPECT_EQ(pet.tmpabilities.hit, 500) << "the caster's pet";
        EXPECT_EQ(pet.master, &context.caster) << "a spared pet keeps following its master";
        EXPECT_EQ(elf.tmpabilities.hit, 500) << "a peaceful wood-elf mob";
        EXPECT_LT(wolf.tmpabilities.hit, 500) << "rule 1: a mob with no race";
        EXPECT_LT(orc.tmpabilities.hit, 500) << "rule 2: an other-side race";
        EXPECT_LT(bandit.tmpabilities.hit, 500) << "rule 3: a human mob with evil alignment";
        EXPECT_LT(hostile_elf.tmpabilities.hit, 500) << "rule 4: an elf fighting the caster";
    }
};

} // namespace

// A fire-spec mage's splash spares the caster's side and burns everyone else. The old check asked
// about the named target instead: aimed at a mob, which other_side() counts as friendly, it
// cancelled the whole splash.
TEST_F(MageProcTest, FireSpecFireballSplashSparesOnlyTheCastersSide) {
    RoomBlastScene scene(game_types::PS_Fire);

    scene.cast_fireball();

    EXPECT_LT(scene.context.victim.tmpabilities.hit, 500) << "the named target takes the primary hit";
    scene.expect_only_the_casters_side_spared();
}

// The control for the test above: without fire specialization the splash spares no one, so the
// pet and the peaceful elf really are in its path and "spared" above is not vacuous.
TEST_F(MageProcTest, FireballWithoutFireSpecSplashesEveryone) {
    RoomBlastScene scene(game_types::PS_None);

    scene.cast_fireball();

    EXPECT_LT(scene.pet.tmpabilities.hit, 500) << "the caster's pet";
    EXPECT_EQ(scene.pet.master, nullptr) << "a pet its master burns stops following";
    EXPECT_LT(scene.elf.tmpabilities.hit, 500) << "a peaceful wood-elf mob";
    EXPECT_LT(scene.wolf.tmpabilities.hit, 500);
    EXPECT_LT(scene.orc.tmpabilities.hit, 500);
    EXPECT_LT(scene.bandit.tmpabilities.hit, 500);
    EXPECT_LT(scene.hostile_elf.tmpabilities.hit, 500);
}

// Blaze's first burst applies the same rules for every caster, specialized or not. The old check
// spared every uncharmed mob, so the burst burned only other-side players.
TEST_F(MageProcTest, BlazeBurstSparesOnlyTheCastersSide) {
    RoomBlastScene scene(game_types::PS_None);

    scene.cast_blaze();

    EXPECT_LT(scene.context.victim.tmpabilities.hit, 500) << "an orc mob";
    scene.expect_only_the_casters_side_spared();
    EXPECT_NE(room_affected_by_spell(&world[kFireballRoom], SPELL_BLAZE), nullptr)
        << "the burst still leaves the blaze burning";
}

// A mob casting blaze judges players by race: an orc mob's burst burns a human player and spares
// an orc player. Before, other_side() put every player on a mob caster's side.
TEST_F(MageProcTest, BlazeBurstFromAMobCasterBurnsOnlyOtherSidePlayers) {
    ScopedBlastRoom room;
    MageTestContext context; // the caster becomes an orc mob; master is the human player
    context.caster.specials2.act = MOB_ISNPC;
    context.caster.player.race = RACE_ORC;
    context.caster.in_room = kFireballRoom;
    prepare_blast_occupant(context.master, RACE_HUMAN, 0);
    char_data orc_player{};
    prepare_blast_occupant(orc_player, RACE_ORC, 0);

    char_data *const occupants[] = { &context.caster, &context.master, &orc_player };
    room.seat(occupants);

    queue_blaze_rolls();
    test_support::cast_spell(spell_blaze, &context.caster, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_LT(context.master.tmpabilities.hit, 500) << "a human player, on the other side";
    EXPECT_EQ(orc_player.tmpabilities.hit, 500) << "an orc player, on the orc mob's side";
}

// The burst's saving throw carries the fire/cold specialization bonus, as every later
// blaze tick and every other mage spell does. new_saves_spell() records the save value it
// used in spllog_save; the last occupant the burst rolls for is hostile_elf.
TEST_F(MageProcTest, BlazeBurstSavePassesTheSpecializationBonus) {
    RoomBlastScene scene(game_types::PS_Fire);

    scene.cast_blaze();

    const caster_snapshot caster_at_cast = caster_snapshot::capture(scene.context.caster);
    const int specialization_bonus = get_save_bonus(caster_at_cast, scene.hostile_elf, game_types::PS_Fire, game_types::PS_Cold);
    ASSERT_EQ(specialization_bonus, -2) << "a fire-spec caster against an unspecialized victim";
    EXPECT_EQ(spllog_save, get_character_saving_throw(&scene.hostile_elf) + specialization_bonus)
        << "the burst must roll the victim's save with the specialization bonus applied";
}

// Black arrow's poison lasts the MAGE caster level + 1: it shares poison_victim_affect_at_level()
// with the mystic poison but applies it at its own level. Every draw is queued at 0.0: the
// victim's save roll is 1 against a DC of 23 (10 + 30 / 3 + (20 - 8) / 4), and the poison roll
// number(1, 50) is 1, under the level, so the poison always lands.
TEST_F(MageProcTest, BlackArrowPoisonLastsTheMageLevelPlusOne) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 30;
    context.prepare_for_spell_damage();
    test_support::ScopedAffectCleanup victim_affects(context.victim);

    push_test_random_value(0.0); // get_mage_caster_level()'s intel-rounding draw: 30 + 20 / 5
    const int mage_level = get_mage_caster_level(caster_snapshot::capture(context.caster));
    ASSERT_EQ(mage_level, 34);

    queue_fireball_rolls(0.0, 60);
    test_support::cast_spell(spell_black_arrow, &context.caster, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);

    const affected_type* poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "with every roll at its floor the poison must land";
    EXPECT_EQ(poison->duration, mage_level + 1);
    EXPECT_EQ(poison->modifier, -2);
    EXPECT_EQ(poison->location, APPLY_STR);
}

namespace {

// abs_number slots the poisoner pins below register. Nothing else in this file claims them, and
// they were chosen clear of the slots the other suites in the monolithic runner claim.
constexpr int kBlackArrowCasterSlot = MAX_CHARACTERS - 1201;
constexpr int kMysticPoisonCasterSlot = MAX_CHARACTERS - 1202;
constexpr int kMysticPoisonSecondCasterSlot = MAX_CHARACTERS - 1203;

} // namespace

// Black arrow names its mage as the poisoner, and still does when it lands on a victim who is
// already poisoned: the join replaces the running poison, which clears the record, so the
// record must be written after it. Rolls as in the duration test above. Before the second
// arrow the running poison is cut to 1 tick, so an arrow that poisons shows as a renewed duration.
TEST_F(MageProcTest, BlackArrowRecordsTheMageAsThePoisonerEvenOnAnAlreadyPoisonedVictim) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 30;
    context.prepare_for_spell_damage();
    test_support::ScopedAffectCleanup victim_affects(context.victim);
    test_support::ScopedCharExists caster_registration(context.caster, kBlackArrowCasterSlot);

    queue_fireball_rolls(0.0, 60);
    test_support::cast_spell(spell_black_arrow, &context.caster, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);
    affected_type* poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the first arrow poisons";
    EXPECT_EQ(resolve_poisoner(context.victim), &context.caster) << "the first arrow must record its mage";

    poison->duration = 1;
    queue_fireball_rolls(0.0, 60);
    test_support::cast_spell(spell_black_arrow, &context.caster, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);
    poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the victim is still poisoned after the second arrow";
    ASSERT_GT(poison->duration, 1) << "precondition: the second arrow poisons";
    EXPECT_EQ(resolve_poisoner(context.victim), &context.caster)
        << "a second arrow joins the running poison and must keep its mage recorded";
    clear_test_random_values();
}

// Mystic poison names its caster, keeps that record when the same caster poisons again, and a
// different caster poisoning the same victim becomes the recorded poisoner. Before each later
// cast the running poison is cut to 1 tick, so a poison that lands shows as a renewed duration.
TEST_F(MageProcTest, MysticPoisonKeepsItsPoisonerAndASecondPoisonerTakesOver) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_CLERIC] = 10;
    context.master_profs.prof_level[PROF_CLERIC] = 10;
    context.prepare_for_spell_damage();
    context.master.in_room = context.victim.in_room; // the second poisoner stands with the victim
    context.master.specials.position = POSITION_STANDING;
    // saves_poison()'s defense is then 0, so the poison always lands. affect_total() recomputes an
    // NPC's willpower as level + tmpabilities.wil - confusion / 10, which stays 0 here: the level
    // is 0 (prepare_for_spell_damage()), wil is 0 and the victim is not confused.
    context.victim.tmpabilities.con = 0;
    context.victim.points.willpower = 0;
    test_support::ScopedAffectCleanup victim_affects(context.victim);
    test_support::ScopedCharExists caster_registration(context.caster, kMysticPoisonCasterSlot);
    test_support::ScopedCharExists master_registration(context.master, kMysticPoisonSecondCasterSlot);

    queue_fireball_rolls(0.5, 60);
    test_support::cast_spell(spell_poison, &context.caster, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);
    affected_type* poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the poison lands";
    EXPECT_EQ(resolve_poisoner(context.victim), &context.caster) << "the first poison must record its caster";

    poison->duration = 1;
    queue_fireball_rolls(0.5, 60);
    test_support::cast_spell(spell_poison, &context.caster, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);
    poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the victim is still poisoned after the second poison";
    ASSERT_GT(poison->duration, 1) << "precondition: the second poison lands";
    EXPECT_EQ(resolve_poisoner(context.victim), &context.caster)
        << "re-poisoning an already-poisoned victim must keep the caster recorded";

    poison->duration = 1;
    queue_fireball_rolls(0.5, 60);
    test_support::cast_spell(spell_poison, &context.master, nullptr, SPELL_TYPE_SPELL, &context.victim, nullptr, 0, 0);
    poison = affected_by_spell(&context.victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the victim is still poisoned after the second poisoner's cast";
    ASSERT_GT(poison->duration, 1) << "precondition: the second poisoner's poison lands";
    EXPECT_EQ(resolve_poisoner(context.victim), &context.master)
        << "a second poisoner's landed poison must become the recorded origin";
    clear_test_random_values();
}

namespace {

// A player master with an orc follower (a charmed pet and orc-friend) that casts blaze on its
// behalf in kFireballRoom. The master is MageTestContext's caster; its group-mate is master.
struct OrcFollowerBlazeScene {
    ScopedBlastRoom room; // the room, restored and cleaned on exit
    MageTestContext context; // caster: the follower's master; master: the master's group-mate
    char_prof_data follower_profs{}; // the follower's professions, for its cast-time snapshot
    char_data follower{}; // the casting orc follower

    explicit OrcFollowerBlazeScene(int master_race) {
        prepare_blast_occupant(context.caster, master_race, 0);
        prepare_blast_occupant(context.master, master_race, 0);
        prepare_blast_occupant(follower, RACE_ORC, -500);
        follower.profs = &follower_profs;
        follower.player.level = 30;
        follower.specials2.act = MOB_ISNPC | MOB_PET | MOB_ORC_FRIEND;
        SET_BIT(follower.specials.affected_by, AFF_CHARM);
    }

    ~OrcFollowerBlazeScene() {
        stop_follower(&follower, FOLLOW_MOVE);
    }

    // Seats `occupants` (which must include the follower and its master), then attaches the
    // follower; add_follower()'s act() lines walk the occupant list.
    template <std::size_t kCount>
    void seat_and_follow(char_data *const (&occupants)[kCount]) {
        room.seat(occupants);
        add_follower(&follower, &context.caster, FOLLOW_MOVE);
    }

    void cast_blaze() {
        queue_blaze_rolls();
        test_support::cast_spell(spell_blaze, &follower, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    }
};

} // namespace

// An orc follower's blaze spares its Magus master and the master's group. Judged by its own orc
// race, it burned them: other_side_race() puts orcs and the Magi on opposite sides.
TEST_F(MageProcTest, BlazeFromAnOrcFollowerSparesItsMagusMasterAndGroup) {
    OrcFollowerBlazeScene scene(RACE_MAGUS);
    group_data party(&scene.context.caster);
    party.add_member(&scene.context.master);
    char_data elf{};
    prepare_blast_occupant(elf, RACE_WOOD, 500);
    elf.specials2.act = MOB_ISNPC;

    char_data *const occupants[] = { &scene.follower, &scene.context.caster, &scene.context.master, &elf };
    scene.seat_and_follow(occupants);
    scene.cast_blaze();

    EXPECT_EQ(scene.context.caster.tmpabilities.hit, 500) << "the follower's Magus master";
    EXPECT_EQ(scene.context.master.tmpabilities.hit, 500) << "the master's group-mate";
    EXPECT_EQ(scene.follower.master, &scene.context.caster) << "the follower still follows its master";
    EXPECT_LT(elf.tmpabilities.hit, 500) << "an elf mob, on the other side from the Magus";
}

// Rule 4 through a follower: a mob fighting the follower's master is the master's enemy, so the
// follower's blaze burns it. Judged as its own party, the follower saw no one fighting it.
TEST_F(MageProcTest, BlazeFromAnOrcFollowerBurnsAMobFightingItsMaster) {
    OrcFollowerBlazeScene scene(RACE_URUK);
    char_data peaceful_orc{};
    prepare_blast_occupant(peaceful_orc, RACE_ORC, -500);
    peaceful_orc.specials2.act = MOB_ISNPC;
    char_data hostile_orc{};
    prepare_blast_occupant(hostile_orc, RACE_ORC, -500);
    hostile_orc.specials2.act = MOB_ISNPC;
    hostile_orc.specials.fighting = &scene.context.caster;

    char_data *const occupants[] = { &scene.follower, &scene.context.caster, &peaceful_orc, &hostile_orc };
    scene.seat_and_follow(occupants);
    scene.cast_blaze();

    EXPECT_EQ(scene.context.caster.tmpabilities.hit, 500) << "the follower's Uruk master";
    EXPECT_EQ(peaceful_orc.tmpabilities.hit, 500) << "an orc mob on the master's side";
    EXPECT_LT(hostile_orc.tmpabilities.hit, 500) << "an orc mob fighting the master";
}

// spell_earthquake's crack/fall loop (mage.cpp). The damage loop above it
// excludes the caster (`if (tmpch != caster)`), but the fall loop does not: on the coin
// flip the caster itself is moved into the crevice and takes fall damage INSIDE the
// occupant loop. A lethal fall runs apply_spell_damage() -> damage() -> die() ->
// raw_kill() -> extract_char(), which frees an NPC caster -- and the loop then keeps
// calling new_saves_spell(caster, tmpch, ...) for every later occupant, reading the
// freed caster's profs/tmpabilities/points fields. Same defect class and same fix shape
// as the fireball self-fumble test above: every other occupant falls first, the caster
// falls last, as the spell's final act.
//
// The caster's own lethal self-fall is the same self-damage shape the fireball test
// above already exercises (apply_spell_damage(caster, caster, ...) -> damage() -> die()
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
//      pipeline the fireball test above exercises, unspecified internal draw count.
// Every draw above uses the SAME pinned value, so the exact count needs no bookkeeping
// beyond covering the two CRITICAL number(0, 1) rolls (steps 4 and 5): a generous
// uniform buffer suffices, exactly as the fireball test above does.
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
    test_support::cast_spell(spell_earthquake, caster, nullptr, 0, nullptr, nullptr, 0, 0);
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
// Snapshot-specific behaviour of the formula helpers.
// ---------------------------------------------------------------------------

// Covers get_spell_pen_value()'s charmed-NPC-without-master arm, which the
// snapshot form reaches through master_mage_prof_level == 0 (capture()'s
// guard for "no master" -- see
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
}

// ---------------------------------------------------------------------------
// spell_summon body coverage (the spell had no body test anywhere in the
// tree). The body itself has NO sight check by design; the
// dark-room targeting fix lives in consts.cpp's mask and is pinned by
// summon_targeting_tests.cpp's SummonTargeting suite. This exercises the
// success arm end to end: a willing (PRF_SUMMONABLE clear -- the flag is
// inverted: set == NOT summonable), non-fighting, mortal player victim who
// fails the save is moved into the caster's room through the real
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
    // level) + (20-8)/4 = 3. spell_summon's dist is the straight-line distance
    // between the caster's and victim's zone map coordinates: both rooms are
    // zone 0 on the zero-initialized zone_data stub, so ch_x/ch_y/v_x/v_y are
    // all 0 and dist == 0, giving save_value = 3 + 0 = 3. The single draw
    // below (the save roll, number(1,20)) is a queued midpoint of the
    // roll==1 bucket, so roll == 1 and 1+3=4, which is not > 13 --
    // new_saves_spell() returns false (the victim fails to save) and the
    // success arm runs.
    push_test_random_value(0.025); // (1 - 1 + 0.5) / 20 -- midpoint of the roll==1 bucket

    test_support::cast_spell(spell_summon, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

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

namespace {

// Casts summon from zone 0 at map square (0, 0) at a victim in zone 1 at
// (victim_x, victim_y), with the save roll forced to save_roll. The caster
// and victim have zero mage levels and 20 intel, so new_saves_spell()'s DC is
// 13 and the victim's base save is 3: the victim saves when
// save_roll + 3 + distance bonus > 13. Returns true when the victim arrived.
bool summon_across_zones(int victim_x, int victim_y, int save_roll) {
    MageTestContext context;
    char summon_victim_name[16] = "test_target";
    context.victim.player.name = summon_victim_name;

    ZoneTableGuard zone_table_guard;
    zone_table_guard.stub[1].x = victim_x;
    zone_table_guard.stub[1].y = victim_y;
    ZoneGuard zone_guard(7, 8);
    world[7].zone = 0;
    world[8].zone = 1;

    RoomExitGuard caster_room_guard(7);
    RoomExitGuard victim_room_guard(8);
    world[7].room_flags = 0; // clear any NO_TELEPORT leftover from an earlier suite in this room
    world[7].people = nullptr;
    world[8].people = nullptr;
    char_to_room(&context.caster, 7);
    char_to_room(&context.victim, 8);

    descriptor_data caster_descriptor = make_descriptor();
    context.caster.desc = &caster_descriptor;

    push_test_random_value((save_roll - 0.5) / 20); // midpoint of the save_roll bucket
    test_support::cast_spell(spell_summon, &context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);
    const bool summoned = context.victim.in_room == 7;

    char_from_room(&context.victim);
    char_from_room(&context.caster);
    return summoned;
}

} // namespace

// A (3, 4) offset is 5 squares in a straight line. With roll 1 the victim
// needs a bonus of 10 to save, so 5 lets the summon through; the squared
// formula's 25 would have forced the save.
TEST_F(MageProcTest, SummonDistanceBonusIsStraightLineNotSquared) {
    EXPECT_TRUE(summon_across_zones(3, 4, 1))
        << "Expected a 5-square summon on a roll of 1 to land.";
}

// With roll 6 the victim needs a bonus of 5, so the straight-line 5 saves.
// The live XOR formula gave this offset a bonus of -3 and would have let the
// summon through.
TEST_F(MageProcTest, SummonDistanceBonusIsNotTheLiveXorFormula) {
    EXPECT_FALSE(summon_across_zones(3, 4, 6))
        << "Expected a 5-square summon on a roll of 6 to be saved.";
}

// A (12, 16) offset is exactly 20 squares, the bonus at which
// new_saves_spell() forces the save whatever the roll.
TEST_F(MageProcTest, SummonAtTwentySquaresAlwaysFails) {
    EXPECT_FALSE(summon_across_zones(12, 16, 1))
        << "Expected a 20-square summon to be saved even on a roll of 1.";
}
