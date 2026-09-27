// How apply_poison() merges a new poison with the one already running: strength decides first,
// then an equal poison either replaces (it outlasts the running poison's initial duration) or
// extends by half its duration, capped at that initial duration. The poisoner record follows the
// poison that runs, except that an extension keeps a poisoner who still resolves.
#include "../handler.h"
#include "../poison.h"
#include "../poison_origin.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "scoped_combat_list.h"
#include "scoped_room_occupants.h"
#include "test_affect_support.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>

using test_support::add_filler_affects;
using test_support::clear_captured_output;
using test_support::count_affects_of_type;
using test_support::prepare_capture_descriptor;
using test_support::ScopedAffectCleanup;
using test_support::ScopedCharExists;
using test_support::ScopedCombatList;
using test_support::ScopedRoomOccupants;

namespace {

// The abs_number slots of the two poisoners, which no other suite claims.
constexpr int kFirstPoisonerSlot = MAX_CHARACTERS - 1301;
constexpr int kSecondPoisonerSlot = MAX_CHARACTERS - 1302;

// A world[] index no other suite claims. Poisoners stand here so resolve_poisoner() accepts them;
// the tick and message tests also light it and list its occupants. A poison tick engages its
// victim with itself, so those tests scope the combat list too.
constexpr int kPoisonRoom = 941;

// A sturdy stack NPC standing in kPoisonRoom: in the game, so resolve_poisoner() accepts it.
void make_npc_in_poison_room(char_data& character, char_prof_data& profs) {
    test_support::fill_sturdy_stack_npc(character, profs);
    character.in_room = kPoisonRoom;
}

// A SPELL_POISON template with the given strength malus (0 means no malus, like food).
[[nodiscard]] affected_type poison_of(int strength, int duration) {
    affected_type poison{};
    poison.type = SPELL_POISON;
    poison.duration = duration;
    poison.modifier = static_cast<sh_int>(-strength);
    if (strength > 0) {
        poison.location = APPLY_STR;
    } else {
        poison.location = APPLY_NONE;
    }
    poison.bitvector = AFF_POISON;
    return poison;
}

// Puts a running poison on `victim` without apply_poison(): `initial_duration` in its counter,
// `remaining` ticks left, and `poisoner` recorded as its source; a null `poisoner` means nobody.
void give_running_poison(char_data& victim, int strength, int initial_duration, int remaining,
                         char_data* poisoner) {
    affected_type running = poison_of(strength, remaining);
    running.counter = static_cast<sh_int>(initial_duration);
    affect_to_char(&victim, &running);
    record_poison_origin(&victim, poisoner);
}

// The single running poison, which a test must already have asserted exists.
[[nodiscard]] const affected_type& running_poison(const char_data& victim) {
    return *get_affect_unbounded(&victim, SPELL_POISON);
}

} // namespace

TEST(PoisonRules, PoisonStrengthIsTheStrengthMalusOrZero) {
    EXPECT_EQ(poison_strength(poison_of(4, 10)), 4) << "a -4 STR poison has strength 4";
    EXPECT_EQ(poison_strength(poison_of(2, 10)), 2) << "a -2 STR poison has strength 2";
    EXPECT_EQ(poison_strength(poison_of(0, 10)), 0) << "an APPLY_NONE poison has strength 0";

    affected_type strengthening = poison_of(2, 10);
    strengthening.modifier = 3;
    EXPECT_EQ(poison_strength(strengthening), 0) << "a +3 STR modifier is no malus, so strength 0";

    affected_type weakening_elsewhere = poison_of(2, 10);
    weakening_elsewhere.location = APPLY_CON;
    EXPECT_EQ(poison_strength(weakening_elsewhere), 0)
        << "a -2 CON modifier is no strength malus, so strength 0";

    const affected_type pale_lady_poison = pale_lady_poison_affect();
    const affected_type mystic_poison = poison_victim_affect_at_level(30);
    const affected_type food_poison = consumed_poison_affect(10);
    const int pale_lady = poison_strength(pale_lady_poison);
    const int mystic = poison_strength(mystic_poison);
    const int food = poison_strength(food_poison);
    EXPECT_GT(pale_lady, mystic) << "the Pale Lady (" << pale_lady
                                 << ") must outrank mystic poison (" << mystic << ")";
    EXPECT_GT(mystic, food) << "mystic poison (" << mystic << ") must outrank food (" << food
                            << ")";
}

TEST(PoisonRules, AFreshPoisonAppliesAndRecordsItsSource) {
    char_data poisoner{};
    char_prof_data poisoner_profs{};
    make_npc_in_poison_room(poisoner, poisoner_profs);
    ScopedCharExists poisoner_exists{poisoner, kFirstPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 0)
        << "precondition: the victim starts unpoisoned";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 10), &poisoner), poison_outcome::applied);

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1) << "exactly one poison runs";
    EXPECT_EQ(running_poison(victim).duration, 10);
    EXPECT_EQ(running_poison(victim).counter, 10) << "the counter keeps the initial duration, 10";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "the source is recorded as the poisoner";
}

TEST(PoisonRules, AWeakerPoisonIsBlockedAndChangesNothing) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 4, 24, 24, &first);
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 30), &second), poison_outcome::blocked_by_stronger)
        << "a strength-2 poison cannot touch a strength-4 one, however long it lasts";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 24) << "the running poison keeps its 24 ticks";
    EXPECT_EQ(running_poison(victim).modifier, -4) << "the running poison stays -4 STR";
    EXPECT_EQ(resolve_poisoner(victim), &first) << "the first poisoner keeps the record";
}

TEST(PoisonRules, FoodCannotTouchASpellPoison) {
    char_data poisoner{};
    char_prof_data poisoner_profs{};
    make_npc_in_poison_room(poisoner, poisoner_profs);
    ScopedCharExists poisoner_exists{poisoner, kFirstPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 10, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "precondition: the poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(0, 40), nullptr), poison_outcome::blocked_by_stronger)
        << "food (strength 0) is weaker than a strength-2 spell poison";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 10) << "the spell poison keeps its 10 ticks";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "food must not clear the poisoner";
}

TEST(PoisonRules, AStrongerPoisonReplacesAndTakesTheRecord) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 30, 30, &first);
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(4, 24), &second), poison_outcome::replaced);

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1)
        << "the weaker poison is gone, not stacked";
    EXPECT_EQ(running_poison(victim).modifier, -4);
    EXPECT_EQ(running_poison(victim).duration, 24)
        << "the stronger poison runs its own 24 ticks, even though the weaker had 30 left";
    EXPECT_EQ(running_poison(victim).counter, 24);
    EXPECT_EQ(resolve_poisoner(victim), &second) << "the new source takes the record";
}

TEST(PoisonRules, AnEqualPoisonOutlastingTheInitialDurationReplaces) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, &first);
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 12), &second), poison_outcome::replaced)
        << "12 ticks outlast the running poison's initial 10";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 12);
    EXPECT_EQ(running_poison(victim).counter, 12);
    EXPECT_EQ(resolve_poisoner(victim), &second) << "the new source takes the record";
}

TEST(PoisonRules, AnEqualPoisonMatchingTheInitialDurationExtends) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, &first);
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 10), &second), poison_outcome::extended)
        << "10 ticks only match the initial 10, so the poison extends";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 9) << "4 remaining + 10 / 2 = 9";
    EXPECT_EQ(running_poison(victim).counter, 10) << "the initial duration stays 10";
    EXPECT_EQ(resolve_poisoner(victim), &first) << "the resolving first poisoner keeps the record";
}

TEST(PoisonRules, AnExtensionIsCappedAtTheInitialDuration) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 8, nullptr);
    ASSERT_EQ(running_poison(victim).duration, 8) << "precondition: 8 ticks remain";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 9), nullptr), poison_outcome::extended);

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 10)
        << "8 remaining + 9 / 2 = 12, capped at the initial duration 10";
}

TEST(PoisonRules, AnExtensionRoundsHalfTheDurationDown) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, nullptr);
    ASSERT_EQ(running_poison(victim).duration, 4) << "precondition: 4 ticks remain";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 7), nullptr), poison_outcome::extended);
    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 7) << "4 remaining + 7 / 2 (3) = 7";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 1), nullptr), poison_outcome::extended)
        << "a 1-tick equal poison is still an extension";
    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 7) << "1 / 2 rounds down to 0 extra ticks";
}

TEST(PoisonRules, AnExtensionKeepsAPoisonerThatStillResolves) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, &first);
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    ASSERT_EQ(apply_poison(&victim, poison_of(2, 6), &second), poison_outcome::extended);

    EXPECT_EQ(resolve_poisoner(victim), &first)
        << "an extension must not take the record from a poisoner who still resolves";
}

TEST(PoisonRules, AnExtensionHandsTheRecordToTheNewSourceWhenThePoisonerIsGone) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, &first);
    remove_char_exists(kFirstPoisonerSlot); // what extract_char() does for the first poisoner
    ASSERT_EQ(resolve_poisoner(victim), nullptr)
        << "precondition: the first poisoner no longer resolves";

    ASSERT_EQ(apply_poison(&victim, poison_of(2, 6), &second), poison_outcome::extended);

    EXPECT_EQ(resolve_poisoner(victim), &second)
        << "with the recorded poisoner gone, the extending source takes the record";
}

TEST(PoisonRules, AnExtensionFromNobodyKeepsAPoisonerThatStillResolves) {
    char_data poisoner{};
    char_prof_data poisoner_profs{};
    make_npc_in_poison_room(poisoner, poisoner_profs);
    ScopedCharExists poisoner_exists{poisoner, kFirstPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "precondition: the poisoner resolves";

    ASSERT_EQ(apply_poison(&victim, poison_of(2, 6), nullptr), poison_outcome::extended);

    EXPECT_EQ(resolve_poisoner(victim), &poisoner)
        << "an extension with no source must not erase a poisoner who still resolves";
}

TEST(PoisonRules, ReplacementByNobodyClearsTheRecord) {
    char_data poisoner{};
    char_prof_data poisoner_profs{};
    make_npc_in_poison_room(poisoner, poisoner_profs);
    ScopedCharExists poisoner_exists{poisoner, kFirstPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 0, 5, 5, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "precondition: the poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(0, 20), nullptr), poison_outcome::replaced)
        << "20 ticks of food outlast the running food's initial 5";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 20);
    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "a poison from nobody replaces the record with nobody";
}

TEST(PoisonRules, AZeroCounterFallsBackToTheRemainingDuration) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    affected_type saved = poison_of(2, 6);
    saved.counter = 0;
    affect_to_char(&victim, &saved);
    ASSERT_EQ(running_poison(victim).counter, 0) << "precondition: the counter is zero";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 6), nullptr), poison_outcome::extended)
        << "the initial duration falls back to the remaining 6, which 6 ticks only match";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 6) << "6 + 6 / 2 = 9, capped at 6";
    EXPECT_EQ(running_poison(victim).counter, 6) << "the fallback initial duration is stored";
}

TEST(PoisonRules, ACounterBelowTheRemainingDurationFallsBackToIt) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    affected_type saved = poison_of(2, 6);
    saved.counter = 3;
    affect_to_char(&victim, &saved);
    ASSERT_EQ(running_poison(victim).counter, 3) << "precondition: the counter is below 6";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 6), nullptr), poison_outcome::extended)
        << "the initial duration is the remaining 6, not the counter's 3, and 6 only matches it";

    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, 6) << "6 + 6 / 2 = 9, capped at 6, not at 3";
    EXPECT_EQ(running_poison(victim).counter, 6) << "the fallback initial duration is stored";
}

TEST(PoisonRules, ReplacementRemovesEveryPoisonAffect) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    affected_type buried = poison_of(2, 20);
    affect_to_char(&victim, &buried);
    add_filler_affects(victim, 2 * MAX_AFFECT);
    affected_type newest = poison_of(2, 20);
    affect_to_char(&victim, &newest);
    ASSERT_EQ(count_affects_of_type(victim, SPELL_POISON), 2)
        << "precondition: two poisons, one under " << 2 * MAX_AFFECT << " fillers";

    EXPECT_EQ(apply_poison(&victim, poison_of(4, 24), nullptr), poison_outcome::replaced);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_POISON), 1)
        << "replacement must remove every poison, including the buried one";
    ASSERT_NE(get_affect_unbounded(&victim, SPELL_POISON), nullptr);
    EXPECT_EQ(running_poison(victim).modifier, -4) << "the survivor is the new -4 STR poison";
}

TEST(PoisonRules, APermanentPoisonOfEqualStrengthIsLeftAlone) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 0, -1, &first);
    ASSERT_EQ(running_poison(victim).duration, -1) << "precondition: the poison is permanent";
    ASSERT_EQ(resolve_poisoner(victim), &first) << "precondition: the first poisoner resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 50), &second), poison_outcome::extended);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, -1)
        << "an equal poison must neither shorten nor replace a permanent one";
    EXPECT_EQ(resolve_poisoner(victim), &first)
        << "extending a permanent poison keeps a poisoner who still resolves";
}

TEST(PoisonRules, APermanentPoisonHandsTheRecordToTheNewSourceWhenThePoisonerIsGone) {
    char_data first{};
    char_prof_data first_profs{};
    make_npc_in_poison_room(first, first_profs);
    ScopedCharExists first_exists{first, kFirstPoisonerSlot};
    char_data second{};
    char_prof_data second_profs{};
    make_npc_in_poison_room(second, second_profs);
    ScopedCharExists second_exists{second, kSecondPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 0, -1, &first);
    remove_char_exists(kFirstPoisonerSlot); // what extract_char() does for the first poisoner
    ASSERT_EQ(running_poison(victim).duration, -1) << "precondition: the poison is permanent";
    ASSERT_EQ(resolve_poisoner(victim), nullptr)
        << "precondition: the first poisoner no longer resolves";

    EXPECT_EQ(apply_poison(&victim, poison_of(2, 50), &second), poison_outcome::extended);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    EXPECT_EQ(running_poison(victim).duration, -1) << "the permanent poison stays permanent";
    EXPECT_EQ(resolve_poisoner(victim), &second)
        << "with the recorded poisoner gone, the extending source takes the record";
}

TEST(PoisonRules, AResistedPoisonKeepsItsResistanceWhenExtended) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    ScopedCombatList combat;
    ScopedRoomOccupants room{kPoisonRoom, {&victim}};
    ScopedAffectCleanup victim_affects(victim);
    give_running_poison(victim, 2, 10, 4, nullptr);
    constexpr int kClericLevel = 2;
    ASSERT_EQ(start_poison_resistance(&victim, kClericLevel), poison_resistance_outcome::started);
    const affected_type* const resistance = affected_by_spell(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(resistance, nullptr) << "precondition: a resist-poison affect runs";
    ASSERT_EQ(resistance->duration, 4) << "precondition: resistance matches the 4 poison ticks";

    ASSERT_EQ(apply_poison(&victim, poison_of(2, 10), nullptr), poison_outcome::extended);

    ASSERT_EQ(affected_by_spell(&victim, SPELL_RESIST_POISON), resistance)
        << "an extension must leave the resist-poison affect in place";
    EXPECT_EQ(resistance->duration, 4) << "apply_poison() leaves the resistance's duration alone";
    affected_type* const poison = get_affect_unbounded(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    ASSERT_EQ(poison->duration, 9) << "precondition: 4 + 10 / 2 = 9 poison ticks";

    const bool victim_died = tick_poison_affect(&victim, poison);

    EXPECT_FALSE(victim_died);

    EXPECT_EQ(poison->duration, 9 - kClericLevel) << "the resistance shortens the extended poison";
    EXPECT_EQ(resistance->duration, poison->duration)
        << "the next tick syncs the resistance to the extended poison";
}

TEST(PoisonRules, EachOutcomeMessagesTheVictimAndOnlyARefusalTellsTheCaster) {
    char victim_short_descr[] = "a poisoned victim";
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc_in_poison_room(victim, victim_profs);
    victim.player.short_descr = victim_short_descr;
    descriptor_data victim_descriptor{};
    prepare_capture_descriptor(victim_descriptor);
    victim.desc = &victim_descriptor;
    char_data caster{};
    char_prof_data caster_profs{};
    make_npc_in_poison_room(caster, caster_profs);
    descriptor_data caster_descriptor{};
    prepare_capture_descriptor(caster_descriptor);
    caster.desc = &caster_descriptor;
    ScopedCombatList combat;
    ScopedRoomOccupants room{kPoisonRoom, {&victim, &caster}};

    send_poison_outcome_messages(poison_outcome::extended, &victim, &caster, "fresh line\n\r");
    EXPECT_STREQ(victim_descriptor.small_outbuf,
                 "You feel sicker as the poison lingers in your blood.\n\r");
    EXPECT_STREQ(caster_descriptor.small_outbuf, "")
        << "the caster of an extension is told nothing";

    clear_captured_output(victim_descriptor);
    send_poison_outcome_messages(poison_outcome::blocked_by_stronger, &victim, &caster,
                                 "fresh line\n\r");
    EXPECT_STREQ(victim_descriptor.small_outbuf,
                 "Your body is already fighting a stronger poison.\n\r");
    EXPECT_NE(std::strstr(caster_descriptor.small_outbuf,
                          "poisoned victim is already suffering from a stronger poison."),
              nullptr)
        << "the caster of a refused poison is told why: " << caster_descriptor.small_outbuf;

    clear_captured_output(victim_descriptor);
    clear_captured_output(caster_descriptor);
    send_poison_outcome_messages(poison_outcome::blocked_by_stronger, &victim, nullptr, nullptr);
    EXPECT_STREQ(victim_descriptor.small_outbuf,
                 "Your body is already fighting a stronger poison.\n\r")
        << "a refused poison with no caster still tells the victim";

    for (const poison_outcome fresh : {poison_outcome::applied, poison_outcome::replaced}) {
        clear_captured_output(victim_descriptor);
        clear_captured_output(caster_descriptor);
        send_poison_outcome_messages(fresh, &victim, &caster, "You feel very sick.\n\r");
        EXPECT_STREQ(victim_descriptor.small_outbuf, "You feel very sick.\n\r")
            << "applied and replaced send the source's fresh line, outcome "
            << static_cast<int>(fresh);
        EXPECT_STREQ(caster_descriptor.small_outbuf, "")
            << "the caster of a fresh poison is told nothing, outcome " << static_cast<int>(fresh);

        clear_captured_output(victim_descriptor);
        send_poison_outcome_messages(fresh, &victim, &caster, nullptr);
        EXPECT_STREQ(victim_descriptor.small_outbuf, "")
            << "a null fresh line sends nothing, outcome " << static_cast<int>(fresh);
    }
}
