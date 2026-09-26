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
#include "scoped_combat_list.h"
#include "scoped_forced_affect_phase.h"
#include "scoped_room_occupants.h"
#include "test_character_support.h"

#include <gtest/gtest.h>

void affect_update_person(char_data* character, int mode);

namespace {

// A world[] index no other suite claims; the wearer stands here so the poison damage's room
// messages have a valid occupant list to walk. That damage also engages the wearer with itself,
// which puts it on combat_list, so each test scopes the combat list too.
constexpr int kWearerRoom = 29;

// The APPLY_BITVECTOR modifier that names AFF_POISON.
constexpr int kPoisonBitNumber = 11;
static_assert((1 << kPoisonBitNumber) == AFF_POISON, "affect_modify() sets bit 1 << modifier");

// Bound on affect_update_person() calls for a 1-tick poison: one tick spends the duration, the
// next removes the affect.
constexpr int kExpiryTickBudget = 3;

// A neck item whose one affect line sets AFF_POISON, the unit-test twin of the harness world's
// "sickly amulet". Worn through equip_char() on construction and taken off on scope exit if the
// test has not already removed it.
class WornPoisonAmulet {
  public:
    explicit WornPoisonAmulet(char_data& wearer) : m_wearer(wearer) {
        m_amulet.in_room = NOWHERE; // equip_char() refuses an item lying in a room
        m_amulet.obj_flags.type_flag = ITEM_WORN;
        m_amulet.obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_NECK;
        m_amulet.affected[0].location = APPLY_BITVECTOR;
        m_amulet.affected[0].modifier = kPoisonBitNumber;
        equip_char(&m_wearer, &m_amulet, WEAR_NECK_1);
    }
    ~WornPoisonAmulet() {
        if (is_worn()) {
            unequip_char(&m_wearer, WEAR_NECK_1);
        }
    }
    WornPoisonAmulet(const WornPoisonAmulet&) = delete;
    WornPoisonAmulet& operator=(const WornPoisonAmulet&) = delete;

    [[nodiscard]] bool is_worn() const { return m_wearer.equipment[WEAR_NECK_1] == &m_amulet; }

  private:
    char_data& m_wearer; // the character wearing the amulet
    obj_data m_amulet{}; // the item itself, owned by this scope
};

// Runs forced affect ticks until the wearer carries no SPELL_POISON affect or the budget is spent.
void tick_until_the_poison_expires(char_data& wearer) {
    test_support::ScopedForcedAffectPhase forced_phase;
    for (int tick = 0; tick < kExpiryTickBudget; ++tick) {
        const affected_type* const poison = affected_by_spell(&wearer, SPELL_POISON);
        if (poison == nullptr) {
            break;
        }
        affect_update_person(&wearer, 0);
    }
}

[[nodiscard]] bool is_poisoned(const char_data& character) {
    return IS_AFFECTED(&character, AFF_POISON);
}

} // namespace

TEST(GearPoison, AWornPoisonItemSetsThePoisonFlag) {
    char_data wearer{};
    char_prof_data wearer_profs{};
    test_support::fill_sturdy_stack_npc(wearer, wearer_profs);
    test_support::ScopedCombatList combat;
    test_support::ScopedRoomOccupants room(kWearerRoom, {&wearer});
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    ASSERT_FALSE(is_poisoned(wearer)) << "precondition: the wearer starts unpoisoned";

    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    EXPECT_TRUE(is_poisoned(wearer))
        << "a worn APPLY_BITVECTOR " << kPoisonBitNumber << " item must set AFF_POISON";
    EXPECT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "the item's poison is a bare flag, with no SPELL_POISON affect behind it";
}

TEST(GearPoison, TheWearerStaysPoisonedAfterATimedPoisonExpires) {
    char_data wearer{};
    char_prof_data wearer_profs{};
    test_support::fill_sturdy_stack_npc(wearer, wearer_profs);
    test_support::ScopedCombatList combat;
    test_support::ScopedRoomOccupants room(kWearerRoom, {&wearer});
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    affected_type poison = poison_victim_affect_at_level(0); // one tick: level 0 + 1
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
    char_data wearer{};
    char_prof_data wearer_profs{};
    test_support::fill_sturdy_stack_npc(wearer, wearer_profs);
    test_support::ScopedCombatList combat;
    test_support::ScopedRoomOccupants room(kWearerRoom, {&wearer});
    test_support::ScopedAffectCleanup wearer_affects(wearer);

    affected_type poison = poison_victim_affect_at_level(0); // one tick: level 0 + 1
    affect_to_char(&wearer, &poison);
    ASSERT_TRUE(is_poisoned(wearer)) << "precondition: the poison affect sets AFF_POISON";

    tick_until_the_poison_expires(wearer);

    ASSERT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the 1-tick poison expired within " << kExpiryTickBudget << " ticks";
    EXPECT_FALSE(is_poisoned(wearer)) << "with nothing worn, AFF_POISON must end with the poison";
}

TEST(GearPoison, RemovingTheItemClearsTheFlag) {
    char_data wearer{};
    char_prof_data wearer_profs{};
    test_support::fill_sturdy_stack_npc(wearer, wearer_profs);
    test_support::ScopedCombatList combat;
    test_support::ScopedRoomOccupants room(kWearerRoom, {&wearer});
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
    char_data wearer{};
    char_prof_data wearer_profs{};
    test_support::fill_sturdy_stack_npc(wearer, wearer_profs);
    test_support::ScopedCombatList combat;
    test_support::ScopedRoomOccupants room(kWearerRoom, {&wearer});
    test_support::ScopedAffectCleanup wearer_affects(wearer);
    WornPoisonAmulet amulet(wearer);
    ASSERT_TRUE(amulet.is_worn()) << "precondition: equip_char() put the amulet on the neck";

    // A 20-tick poison, long enough that nothing but the cure ends it here.
    affected_type poison = poison_victim_affect_at_level(19);
    affect_to_char(&wearer, &poison);
    ASSERT_NE(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "precondition: the poison is on";

    EXPECT_TRUE(cure_poison(&wearer)) << "the cure reports the poison affect it removed";

    EXPECT_EQ(affected_by_spell(&wearer, SPELL_POISON), nullptr)
        << "the cure removes the poison affect";
    EXPECT_TRUE(is_poisoned(wearer)) << "the cure must leave the worn item's AFF_POISON in place";
}
