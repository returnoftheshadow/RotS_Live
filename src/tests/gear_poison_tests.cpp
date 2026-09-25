// A worn item can set AFF_POISON through an APPLY_BITVECTOR affect line with no SPELL_POISON
// affect behind it. The bit belongs to the item: a timed poison that expires or is cured clears
// the bit in affect_remove() (handler.cpp), whose closing affect_total() re-applies every worn
// item's affects, so the wearer stays poisoned until the item comes off.
#include "../db.h"
#include "../handler.h"
#include "../poison.h"
#include "../spells.h"
#include "../structs.h"
#include "../test_harness.h"
#include "../utils.h"
#include "test_character_support.h"

#include <gtest/gtest.h>

extern struct room_data world;
extern int top_of_world;
extern struct char_data* combat_list;

void affect_update_person(struct char_data* i, int mode);

namespace {

// A world[] index no other suite claims; the wearer stands here so the poison damage's room
// messages have a valid occupant list to walk.
constexpr int kWearerRoom = 29;

// The APPLY_BITVECTOR modifier that names AFF_POISON: affect_modify() sets bit 1 << modifier.
constexpr int kPoisonBitNumber = 11;

// Bound on affect_update_person() calls for a 1-tick poison: one tick spends the duration, the
// next removes the affect.
constexpr int kExpiryTickBudget = 3;

void ensure_test_world(int minimum_room_number) {
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

// Saves and restores the wearer's room occupant list and the global combat list: the poison
// damage engages the wearer with itself, which puts it on combat_list.
class ScopedWearerRoom {
public:
    explicit ScopedWearerRoom(char_data& wearer)
        : m_previous_people(nullptr), m_previous_combat_list(combat_list) {
        ensure_test_world(kWearerRoom);
        m_previous_people = world[kWearerRoom].people;
        world[kWearerRoom].people = &wearer;
        wearer.next_in_room = nullptr;
        wearer.in_room = kWearerRoom;
    }
    ~ScopedWearerRoom() {
        world[kWearerRoom].people = m_previous_people;
        combat_list = m_previous_combat_list;
    }
    ScopedWearerRoom(const ScopedWearerRoom&) = delete;
    ScopedWearerRoom& operator=(const ScopedWearerRoom&) = delete;

private:
    char_data* m_previous_people; // the room's occupant list before the test
    char_data* m_previous_combat_list; // combat_list before the test
};

// Forces every slow affect to tick on each affect_update_person() call for the scope.
class ScopedForcedAffectPhase {
public:
    ScopedForcedAffectPhase() : m_previous(harness_force_affect_phase) {
        harness_force_affect_phase = 1;
    }
    ~ScopedForcedAffectPhase() { harness_force_affect_phase = m_previous; }
    ScopedForcedAffectPhase(const ScopedForcedAffectPhase&) = delete;
    ScopedForcedAffectPhase& operator=(const ScopedForcedAffectPhase&) = delete;

private:
    int m_previous; // the flag's value before the scope
};

// A neck item whose one affect line sets AFF_POISON, the unit-test twin of the harness world's
// "sickly amulet". Worn through equip_char() on construction and taken off on scope exit if the
// test has not already removed it.
class WornPoisonAmulet {
public:
    explicit WornPoisonAmulet(char_data& wearer) : m_wearer(wearer), m_amulet() {
        m_amulet.in_room = NOWHERE; // equip_char() refuses an item lying in a room
        m_amulet.obj_flags.type_flag = ITEM_WORN;
        m_amulet.obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_NECK;
        m_amulet.affected[0].location = APPLY_BITVECTOR;
        m_amulet.affected[0].modifier = kPoisonBitNumber;
        equip_char(&m_wearer, &m_amulet, WEAR_NECK_1);
    }
    ~WornPoisonAmulet() {
        if (m_wearer.equipment[WEAR_NECK_1] == &m_amulet) {
            unequip_char(&m_wearer, WEAR_NECK_1);
        }
    }
    WornPoisonAmulet(const WornPoisonAmulet&) = delete;
    WornPoisonAmulet& operator=(const WornPoisonAmulet&) = delete;

    bool is_worn() const { return m_wearer.equipment[WEAR_NECK_1] == &m_amulet; }

private:
    char_data& m_wearer; // the character wearing the amulet
    obj_data m_amulet; // the item itself, owned by this scope
};

void make_wearer(char_data& wearer, char_prof_data& profs) {
    wearer.profs = &profs;
    wearer.specials2.act = MOB_ISNPC;
    wearer.nr = -1;
    wearer.player.race = RACE_HUMAN;
    wearer.player.level = 10;
    wearer.abilities.hit = 500;
    wearer.tmpabilities.hit = 500; // far above the 5 a poison tick or a wear/remove deals
    wearer.specials.position = POSITION_STANDING;
    wearer.specials.fighting = nullptr;
}

// The mystic poison's shape (poison_victim_affect_at_level(), poison.cpp) with a 1-tick duration.
affected_type one_tick_poison() {
    affected_type poison {};
    poison.type = SPELL_POISON;
    poison.duration = 1;
    poison.modifier = -2;
    poison.location = APPLY_STR;
    poison.bitvector = AFF_POISON;
    return poison;
}

// Runs forced affect ticks until the wearer carries no SPELL_POISON affect or the budget is spent.
void tick_until_the_poison_expires(char_data& wearer) {
    ScopedForcedAffectPhase forced_phase;
    for (int tick = 0; tick < kExpiryTickBudget && affected_by_spell(&wearer, SPELL_POISON);
         ++tick) {
        affect_update_person(&wearer, 0);
    }
}

bool is_poisoned(const char_data& character) { return IS_AFFECTED(&character, AFF_POISON); }

} // namespace

TEST(GearPoison, AWornPoisonItemSetsThePoisonFlag) {
    char_data wearer {};
    char_prof_data wearer_profs {};
    make_wearer(wearer, wearer_profs);
    ScopedWearerRoom room(wearer);
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    ASSERT_FALSE(is_poisoned(wearer)) << "precondition: the wearer starts unpoisoned";

    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    EXPECT_TRUE(is_poisoned(wearer)) << "a worn APPLY_BITVECTOR 11 item must set AFF_POISON";
    EXPECT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "the item's poison is a bare flag, with no SPELL_POISON affect behind it";
}

TEST(GearPoison, TheWearerStaysPoisonedAfterATimedPoisonExpires) {
    char_data wearer {};
    char_prof_data wearer_profs {};
    make_wearer(wearer, wearer_profs);
    ScopedWearerRoom room(wearer);
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    affected_type poison = one_tick_poison();
    affect_to_char(&wearer, &poison);
    ASSERT_NE(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the poison is on";

    tick_until_the_poison_expires(wearer);

    ASSERT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the 1-tick poison expired within " << kExpiryTickBudget << " ticks";
    EXPECT_TRUE(is_poisoned(wearer))
        << "affect_remove()'s affect_total() must re-apply the worn item's AFF_POISON after the "
           "timed poison clears it";
}

TEST(GearPoison, WithoutTheItemTheFlagEndsWithThePoison) {
    char_data wearer {};
    char_prof_data wearer_profs {};
    make_wearer(wearer, wearer_profs);
    ScopedWearerRoom room(wearer);
    test_support::ScopedAffectCleanup wearer_affects(wearer);

    affected_type poison = one_tick_poison();
    affect_to_char(&wearer, &poison);
    ASSERT_TRUE(is_poisoned(wearer)) << "precondition: the poison affect sets AFF_POISON";

    tick_until_the_poison_expires(wearer);

    ASSERT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the 1-tick poison expired within " << kExpiryTickBudget << " ticks";
    EXPECT_FALSE(is_poisoned(wearer)) << "with nothing worn, AFF_POISON must end with the poison";
}

TEST(GearPoison, RemovingTheItemClearsTheFlag) {
    char_data wearer {};
    char_prof_data wearer_profs {};
    make_wearer(wearer, wearer_profs);
    ScopedWearerRoom room(wearer);
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(is_poisoned(wearer)) << "precondition: the worn amulet sets AFF_POISON";
    ASSERT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: no poison affect is running";

    unequip_char(&wearer, WEAR_NECK_1);

    ASSERT_FALSE(amulet.is_worn()) << "precondition: unequip_char() took the amulet off";
    EXPECT_FALSE(is_poisoned(wearer)) << "taking the item off must clear the AFF_POISON it set";
}

TEST(GearPoison, CuringTheTimedPoisonLeavesTheItemsFlag) {
    char_data wearer {};
    char_prof_data wearer_profs {};
    make_wearer(wearer, wearer_profs);
    ScopedWearerRoom room(wearer);
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    affected_type poison = one_tick_poison();
    poison.duration = 20;
    affect_to_char(&wearer, &poison);
    ASSERT_NE(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the poison is on";

    cure_poison(&wearer);

    EXPECT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "the cure removes the poison affect";
    EXPECT_TRUE(is_poisoned(wearer)) << "the cure must leave the worn item's AFF_POISON in place";
}
