// The poisoner record's in-memory lifecycle: recording and resolving it, resolution failing safely
// once the poisoner is extracted, parked or its slot reused, a null poisoner clearing it, and
// affect_remove() and clear_char() clearing it. The record is never persisted. The do_drink(),
// do_eat() and vampire_huntress call sites are not driven here: they need a far heavier
// interpreter and world fixture.
#include "../db.h"
#include "../handler.h"
#include "../poison.h"
#include "../poison_origin.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_affect_support.h"
#include "test_character_support.h"

#include <gtest/gtest.h>

using test_support::fill_stack_npc;
using test_support::inert_affect;
using test_support::ScopedAffectCleanup;
using test_support::ScopedCharExists;

namespace {

// An out-of-band abs_number slot no other suite claims.
constexpr int kPoisonerSlot = MAX_CHARACTERS - 601;

// The room a poisoner stands in while it is in the game.
constexpr int kPoisonerRoom = 7;

} // namespace

TEST(PoisonOrigin, ResolveReturnsTheRecordedPoisoner) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);

    EXPECT_EQ(victim.specials.poisoned_by.abs_number, kPoisonerSlot);
    EXPECT_EQ(victim.specials.poisoned_by.identity, &poisoner);
    EXPECT_EQ(resolve_poisoner(victim), &poisoner);
}

// A poisoner parked at the character menu after a quit keeps its registration until the
// socket closes, but extract_char() has already taken it out of its room. It is not in the
// game, so it must not be credited for the poison meanwhile.
TEST(PoisonOrigin, ResolveRejectsAPoisonerParkedOutsideAnyRoom) {
    char_data poisoner{};
    poisoner.in_room = kPoisonerRoom;
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    poisoner.in_room = NOWHERE;
    EXPECT_EQ(resolve_poisoner(victim), nullptr) << "a parked poisoner is out of the game";

    poisoner.in_room = kPoisonerRoom;
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "back in a room, it resolves again";
}

// A poisoner whose link dropped has no descriptor, but its body still stands in its room: it is
// in the game, so it is still credited for the poison.
TEST(PoisonOrigin, ResolveAcceptsALinkDeadPoisonerStillStandingInARoom) {
    char_data poisoner{};
    poisoner.in_room = kPoisonerRoom;
    poisoner.desc = nullptr;
    ASSERT_FALSE(IS_NPC(&poisoner)) << "precondition: the poisoner is a player";
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);

    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "a link-dead body in a room is in the game";
}

TEST(PoisonOrigin, ResolveReturnsNullptrAfterThePoisonerIsExtracted) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // The poisoner was extracted: remove_char_exists() is what extract_char() does for a mob
    // and for a player whose socket is gone. A player parked at the menu keeps its slot and is
    // rejected by character_in_game() instead.
    remove_char_exists(kPoisonerSlot);

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "an extracted poisoner must resolve to nobody, never a dangling pointer";
}

TEST(PoisonOrigin, ResolveReturnsNullptrAfterTheSlotIsRecycledByADifferentCharacter) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    remove_char_exists(kPoisonerSlot);

    // register_npc_char()'s cursor hands the freed slot to a new mob: the same abs_number at a
    // different address. resolve_poisoner() only compares the recorded address against
    // char_by_abs_number()'s current owner; it never dereferences the stale one.
    char_data imposter{};
    ScopedCharExists imposter_exists{imposter, kPoisonerSlot};
    ASSERT_EQ(char_by_abs_number(kPoisonerSlot), &imposter);

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "the recycled slot's new owner must not be mistaken for the recorded poisoner";
}

// The slot is recycled to a character at the same address, so number and pointer both still
// match the record; the registration serial stamped by the new set_char_exists() is what makes
// resolve_poisoner() refuse it.
TEST(PoisonOrigin, ResolveReturnsNullptrAfterTheSlotIsReRegisteredAtTheSameAddress) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    remove_char_exists(kPoisonerSlot);
    set_char_exists(kPoisonerSlot, &poisoner);
    ASSERT_EQ(char_by_abs_number(kPoisonerSlot), &poisoner)
        << "slot and address both match the record";

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "a re-registration at the old address is a new character and must not inherit the "
           "poison credit";
}

TEST(PoisonOrigin, RecordWithNullptrPoisonerClearsAnExistingRecord) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // A poisoned meal or drink has no poisoner behind it, so do_drink() and do_eat() pass a null
    // source.
    record_poison_origin(&victim, nullptr);

    EXPECT_EQ(victim.specials.poisoned_by.abs_number, -1);
    EXPECT_EQ(victim.specials.poisoned_by.identity, nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr);
}

TEST(PoisonOrigin, AffectRemoveOfTheLastSpellPoisonAffectClearsTheRecord) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    char_prof_data victim_profs{};
    fill_stack_npc(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);

    affected_type only_poison = inert_affect(SPELL_POISON, 10);
    affect_to_char(&victim, &only_poison);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);
    ASSERT_NE(affected_by_spell(&victim, SPELL_POISON), nullptr);

    // Removes the only, and therefore last, SPELL_POISON affect.
    affect_remove(&victim, victim.affected);

    EXPECT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr);
    EXPECT_EQ(victim.specials.poisoned_by.abs_number, -1);
    EXPECT_EQ(victim.specials.poisoned_by.identity, nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr);
}

TEST(PoisonOrigin, AffectRemoveKeepsTheRecordWhileAConcurrentSpellPoisonAffectRemains) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};

    char_data victim{};
    char_prof_data victim_profs{};
    fill_stack_npc(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);

    // affect_to_char(), unlike affect_join(), never merges same-type entries, so this reproduces
    // two poisonings landing at once.
    affected_type first = inert_affect(SPELL_POISON, 10);
    affect_to_char(&victim, &first);
    affected_type second = inert_affect(SPELL_POISON, 5);
    affect_to_char(&victim, &second);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // Removes one of the two; one SPELL_POISON affect remains.
    affect_remove(&victim, victim.affected);

    ASSERT_NE(affected_by_spell(&victim, SPELL_POISON), nullptr)
        << "a concurrent SPELL_POISON affect must still be standing";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner)
        << "the record must survive while a SPELL_POISON affect it could belong to remains";

    // Removes the last SPELL_POISON affect.
    affect_remove(&victim, victim.affected);

    EXPECT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "once the last SPELL_POISON affect is gone the record must be cleared too";
}

TEST(PoisonOrigin, ClearCharBlanksThePoisonRecord) {
    char_data poisoner{};

    char_data character{};
    character.specials.poisoned_by.abs_number = 777;
    character.specials.poisoned_by.identity = &poisoner;

    clear_char(&character, MOB_VOID);

    EXPECT_EQ(character.specials.poisoned_by.abs_number, -1);
    EXPECT_EQ(character.specials.poisoned_by.identity, nullptr);
}

// Poisoned food or drink has no strength malus, so it cannot touch any spell poison.
TEST(PoisonOrigin, ConsumedPoisonCannotTouchASpellPoison) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    fill_stack_npc(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    affected_type spell_poison = poison_victim_affect_at_level(19);
    affect_to_char(&victim, &spell_poison);
    record_poison_origin(&victim, &poisoner);

    const affected_type consumed = consumed_poison_affect(30);
    EXPECT_EQ(apply_poison(&victim, consumed, nullptr), poison_outcome::blocked_by_stronger);

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 20) << "a 30-tick consumed poison leaves the 20-tick spell poison";
    EXPECT_EQ(poison->modifier, -2) << "the spell poison's -2 STR malus stays";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "the spell poisoner stays recorded";
}

// An equal consumed poison extends the running one by half its duration, and keeps a poisoner
// that still resolves.
TEST(PoisonOrigin, AnEqualConsumedPoisonExtendsAndKeepsAResolvablePoisoner) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    fill_stack_npc(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);
    affected_type running = consumed_poison_affect(10);
    running.counter = 10;
    running.duration = 4;
    affect_to_char(&victim, &running);
    record_poison_origin(&victim, &poisoner);
    const affected_type* before = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->counter, 10) << "precondition: initial duration 10";
    ASSERT_EQ(before->duration, 4) << "precondition: 4 ticks remain";

    const affected_type consumed = consumed_poison_affect(10);
    EXPECT_EQ(apply_poison(&victim, consumed, nullptr), poison_outcome::extended);

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 9) << "4 remaining plus half of 10 is 9, under the cap of 10";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "an extension keeps the poisoner";
}

TEST(PoisonOrigin, ConsumedPoisonOnAnUnpoisonedCharacterAppliesAndRecordsNobody) {
    char_data victim{};
    char_prof_data victim_profs{};
    fill_stack_npc(victim, victim_profs);
    ScopedAffectCleanup victim_affects(victim);

    const affected_type consumed = consumed_poison_affect(8);
    EXPECT_EQ(apply_poison(&victim, consumed, nullptr), poison_outcome::applied);

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 8);
    EXPECT_EQ(poison->counter, 8) << "the initial duration is the applied duration";
    EXPECT_EQ(resolve_poisoner(victim), nullptr);
}

// The null-victim paths below log a SYSERR line to stderr; that output is expected.

TEST(PoisonOrigin, RecordingForANullVictimChangesNothing) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    record_poison_origin(&victim, &poisoner);

    record_poison_origin(nullptr, &poisoner);

    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "an unrelated record is untouched";
}

TEST(PoisonOrigin, ClearingANullVictimChangesNothing) {
    char_data poisoner{};
    ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    record_poison_origin(&victim, &poisoner);

    clear_poison_origin(nullptr);

    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "an unrelated record is untouched";
}
