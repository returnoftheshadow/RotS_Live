#include "../spells.h"
#include "../utils.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);

extern room_data world;
extern int top_of_world;
extern char_data* combat_list;
extern char_data* combat_next_dude;

namespace {

void ensure_test_world(int minimum_room_number)
{
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

struct DamageTestContext {
    static constexpr int room_number = 1;

    char_data attacker{};
    char_data victim{};
    affected_type victim_primary_affect{};
    affected_type victim_secondary_affect{};
    char attacker_name[16] = "test_attacker";
    char victim_name[16] = "test_victim";
    char_data* original_people = nullptr;

    DamageTestContext()
    {
        ensure_test_world(room_number);
        original_people = world[room_number].people;

        attacker.specials2.act = MOB_ISNPC;
        victim.specials2.act = MOB_ISNPC;
        attacker.player.short_descr = attacker_name;
        victim.player.short_descr = victim_name;

        attacker.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        attacker.player.level = 20;
        victim.player.level = 20;

        attacker.tmpabilities.con = 20;
        victim.tmpabilities.con = 20;
        attacker.abilities.hit = 500;
        victim.abilities.hit = 500;
        attacker.tmpabilities.hit = 500;
        victim.tmpabilities.hit = 500;
        attacker.tmpabilities.mana = 100;
        victim.tmpabilities.mana = 100;

        attacker.specials.position = POSITION_FIGHTING;
        victim.specials.position = POSITION_FIGHTING;
        attacker.specials.fighting = &victim;
        victim.specials.fighting = &attacker;

        attacker.in_room = room_number;
        victim.in_room = room_number;
        attacker.next_in_room = &victim;
        victim.next_in_room = nullptr;
        world[room_number].people = &attacker;
    }

    ~DamageTestContext()
    {
        world[room_number].people = original_people;
        attacker.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        attacker.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
        attacker.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }

    void add_victim_affect(affected_type& affect, int type, int duration, int modifier = 0,
        int location = APPLY_NONE, long bitvector = 0)
    {
        affect = {};
        affect.type = type;
        affect.duration = duration;
        affect.modifier = modifier;
        affect.location = location;
        affect.bitvector = bitvector;
        affect.next = victim.affected;
        victim.affected = &affect;

        if (bitvector != 0) {
            victim.specials.affected_by |= bitvector;
        }
    }
};

} // namespace

class DamageMethodTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }
};

TEST_F(DamageMethodTest, ClampsNonAmbushOverflowDamageBeforeApplyingIt) {
    DamageTestContext context;

    EXPECT_EQ(damage(&context.attacker, &context.victim, 250, TYPE_HIT, 3), 0)
        << "Expected overflow-sized damage to be clamped and applied without killing the high-health test victim.";
    EXPECT_EQ(context.victim.tmpabilities.hit, 300)
        << "Expected damage() to clamp non-ambush hits above 200 before subtracting them from the victim's hit points.";
}

TEST_F(DamageMethodTest, AppliesBeorningPhysicalDamageReductionBeforeFinalDamageCapture) {
    DamageTestContext context;
    context.victim.player.race = RACE_BEORNING;

    damage(&context.attacker, &context.victim, 10, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 491)
        << "Expected beorning victims to reduce incoming physical damage before damage() subtracts it from hit points.";
}

TEST_F(DamageMethodTest, AppliesWildResistanceWhenThePhysicalResistanceRollAllowsIt) {
    DamageTestContext context;
    context.victim.specials.resistance = (1 << PLRSPEC_WILD);

    push_test_random_value(0.50);

    damage(&context.attacker, &context.victim, 30, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 480)
        << "Expected wild resistance to reduce physical damage to two thirds when the one-in-three bypass roll does not clear it.";
}

TEST_F(DamageMethodTest, AppliesWildVulnerabilityToIncreasePhysicalDamage) {
    DamageTestContext context;
    context.victim.specials.vulnerability = (1 << PLRSPEC_WILD);

    push_test_random_value(0.50);

    damage(&context.attacker, &context.victim, 30, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 455)
        << "Expected wild vulnerability to increase incoming physical damage by half before damage() subtracts it from hit points.";
}

TEST_F(DamageMethodTest, ShieldAbsorptionConsumesManaAndReducesDamage) {
    DamageTestContext context;
    context.add_victim_affect(context.victim_primary_affect, SPELL_SHIELD, 5);

    push_test_random_value(0.0);

    damage(&context.attacker, &context.victim, 50, SPELL_MAGIC_MISSILE, 2);

    EXPECT_EQ(context.victim.tmpabilities.hit, 470)
        << "Expected shield to absorb 40 percent of the incoming spell damage before the remaining damage is applied.";
    EXPECT_EQ(context.victim.tmpabilities.mana, 84)
        << "Expected the current shield absorption math to spend 16 mana after its rounding step for this absorbed-damage case.";
    EXPECT_EQ(context.victim_primary_affect.duration, 2)
        << "Expected shield duration to be shortened to the near-expiry value once it absorbs damage and still has mana remaining.";
}
