#include "../spells.h"
#include "../utils.h"
#include "test_random_utils.h"
#include "test_world_support.h"
#include <gtest/gtest.h>

int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);

extern room_data world;
extern char_data* combat_list;
extern char_data* combat_next_dude;

namespace {

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
        test_support::ensure_test_world(room_number);
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

namespace {

// A player orc stands idle in a room while a recruited pet trades blows with a mob. damage() used
// to give the mob a one-in-eleven chance per hit to drop the pet and silently engage the player.
struct CharmedPetTestContext {
    static constexpr int room_number = 1;

    char_data mob{};
    char_data pet{};
    char_data master{};
    char_prof_data master_profs{};
    byte master_skills[MAX_SKILLS]{};
    byte master_knowledge[MAX_SKILLS]{};
    char mob_name[16] = "test_mob";
    char pet_name[16] = "test_pet";
    char master_name[16] = "test_master";
    char_data* original_people = nullptr;

    static void give_stats(char_data& who, int level, int ob, int parry, int dodge, int stat)
    {
        who.player.level = level;
        who.points.OB = ob;
        who.points.parry = parry;
        who.points.dodge = dodge;
        who.points.damage = 5;
        who.abilities.str = stat;
        who.abilities.lea = stat;
        who.abilities.intel = stat;
        who.abilities.wil = stat;
        who.abilities.dex = stat;
        who.abilities.con = stat;
        who.tmpabilities = who.abilities;
        who.abilities.hit = 500;
        who.tmpabilities.hit = 500;
        who.abilities.mana = 100;
        who.tmpabilities.mana = 100;
    }

    CharmedPetTestContext()
    {
        test_support::ensure_test_world(room_number);
        original_people = world[room_number].people;

        mob.specials2.act = MOB_ISNPC;
        pet.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND | MOB_PET;
        master.specials2.act = 0; /* a player character */

        mob.player.short_descr = mob_name;
        pet.player.short_descr = pet_name;
        master.player.name = master_name;
        master.player.short_descr = master_name;

        mob.player.race = RACE_HUMAN;
        pet.player.race = RACE_ORC;
        master.player.race = RACE_ORC;

        /* Roughly the live numbers: a level 19 scout, a level 12 war orc, a level 50 player. */
        give_stats(mob, 19, 76, 38, 19, 16);
        give_stats(pet, 12, 48, 24, 12, 13);
        give_stats(master, 50, 100, 50, 25, 20);

        master.profs = &master_profs;
        master.skills = master_skills;
        master.knowledge = master_knowledge;
        master_profs.prof_level[PROF_WARRIOR] = 30;

        for (char_data* who : { &mob, &pet, &master }) {
            who->in_room = room_number;
        }

        /* The pet is charmed and following the player, as do_recruit leaves it. */
        pet.specials.affected_by |= AFF_CHARM;
        pet.master = &master;

        /* The mob and the pet are already trading blows; the player is idle. */
        mob.specials.position = POSITION_FIGHTING;
        pet.specials.position = POSITION_FIGHTING;
        master.specials.position = POSITION_STANDING;
        mob.specials.fighting = &pet;
        pet.specials.fighting = &mob;
        master.specials.fighting = nullptr;

        /* The mob has just spent its energy swinging at the pet. */
        mob.specials.ENERGY = 0;

        mob.next_in_room = &pet;
        pet.next_in_room = &master;
        master.next_in_room = nullptr;
        world[room_number].people = &mob;
    }

    ~CharmedPetTestContext()
    {
        world[room_number].people = original_people;
        for (char_data* who : { &mob, &pet, &master }) {
            who->next_in_room = nullptr;
            who->specials.fighting = nullptr;
            who->in_room = NOWHERE;
        }
        pet.master = nullptr;
        master.profs = nullptr;
        master.skills = nullptr;
        master.knowledge = nullptr;
    }
};

} // namespace

class CharmedPetMasterSwitchTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }
};

TEST_F(CharmedPetMasterSwitchTest, DoesNotDragTheIdleMasterIntoTheFightWhenTheMobHitsThePet) {
    CharmedPetTestContext context;

    /* The value that made the old one-in-eleven switch-to-master roll fire. */
    push_test_random_value(0.0);

    damage(&context.mob, &context.pet, 10, TYPE_HIT, 0);

    EXPECT_EQ(context.master.specials.fighting, nullptr)
        << "Expected a player standing idle beside their pet to stay out of the fight when the pet's opponent damages the pet.";
    EXPECT_EQ(context.master.specials.position, POSITION_STANDING)
        << "Expected an idle player's position to be left alone when their pet's opponent damages the pet.";
    EXPECT_EQ(context.mob.specials.fighting, &context.pet)
        << "Expected the mob to keep fighting the pet it was already engaged with.";
    EXPECT_EQ(context.pet.tmpabilities.hit, 490)
        << "Expected the mob's swing to land on the pet it was fighting.";
}
