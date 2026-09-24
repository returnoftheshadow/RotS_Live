// Poison origin tracking. record_poison_origin()
// (fight.cpp) is the only sanctioned writer of char_special_data's
// poisoned_by_abs_number/poisoned_by pair; resolve_poisoner() is the only
// sanctioned reader. The pair is never persisted (char_special_data does not
// appear in char_file_u), so these tests exercise only the in-memory
// lifecycle: record+resolve round-trip, resolution safely failing once the
// recorded poisoner is gone (extracted, or its abs_number slot recycled by a
// different character -- never dereferencing the stale pointer to find out),
// record_poison_origin(victim, nullptr) clearing an existing record, and the
// two production sites that clear the pair without going through
// record_poison_origin: affect_remove() (handler.cpp, once the last
// SPELL_POISON affect is gone -- but not while a second concurrent
// SPELL_POISON affect remains) and clear_char() (db.cpp).
//
// The do_drink()/do_eat() (act_obj2.cpp) and vampire_huntress (spec_pro.cpp)
// call sites are exercised only by code review, not by a driven test here:
// they call record_poison_origin() with the same two argument shapes already
// covered directly (a live poisoner pointer, and nullptr) inside command/
// special-procedure bodies that need a much heavier world/interpreter
// fixture to drive end-to-end. Nothing about their record_poison_origin()
// call is untested in isolation -- only the surrounding ACMD/SPECIAL plumbing
// is not re-driven here.
#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include "test_character_support.h"
#include <gtest/gtest.h>

using test_support::ScopedCharExists;

namespace {

// The abs_number slots this file's tests hand between characters. High,
// out-of-band slots so register_npc_char() (which allocates from slot 0
// upward) is very unlikely to reach them, and distinct from the ranges other
// suites already claim (affect_update_tests.cpp: MAX_CHARACTERS - 201/-202;
// caster_snapshot_tests.cpp: MAX_CHARACTERS - 401; char_utils_tests.cpp:
// MAX_CHARACTERS - 17/-18).
constexpr int kPoisonerSlot = MAX_CHARACTERS - 601;

// A minimal, stack-local NPC good enough to run affect_to_char()/
// affect_remove() -- profs pointer, race, and position, mirroring
// affect_update_tests.cpp's make_npc().
void make_npc(char_data& ch, char_prof_data& profs)
{
    ch.profs = &profs;
    ch.specials2.act = MOB_ISNPC;
    ch.nr = -1;
    ch.player.race = RACE_HUMAN;
    ch.player.level = 10;
    ch.specials.position = POSITION_STANDING;
    ch.specials.fighting = nullptr;
}

// A SPELL_POISON affected_type with an inert location/bitvector (APPLY_NONE,
// 0), so affect_modify()'s stat-apply switch has nothing to do beyond the
// type-independent affected_by/race_affect bookkeeping already proven safe by
// affect_update_tests.cpp's own inert_affect(). Only the `type` field matters
// to affect_remove()'s poison-clearing check.
affected_type inert_poison_affect(int duration)
{
    affected_type af {};
    af.type = SPELL_POISON;
    af.duration = duration;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = 0;
    return af;
}

} // namespace

TEST(PoisonOrigin, RecordAndResolveRoundTrip)
{
    char_data poisoner {};
    ScopedCharExists poisoner_exists { poisoner, kPoisonerSlot };

    char_data victim {};
    record_poison_origin(&victim, &poisoner);

    EXPECT_EQ(victim.specials.poisoned_by_abs_number, kPoisonerSlot);
    EXPECT_EQ(victim.specials.poisoned_by, &poisoner);
    EXPECT_EQ(resolve_poisoner(victim), &poisoner);
}

// A poisoner parked at the character menu after a quit keeps its registration until the
// socket closes, but extract_char() has already taken it out of its room. It is not in the
// game, so it must not be credited for the poison meanwhile.
TEST(PoisonOrigin, ResolveRejectsAPoisonerParkedOutsideAnyRoom)
{
    char_data poisoner {};
    poisoner.in_room = 7;
    ScopedCharExists poisoner_exists { poisoner, kPoisonerSlot };

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    poisoner.in_room = NOWHERE;
    EXPECT_EQ(resolve_poisoner(victim), nullptr) << "a parked poisoner is out of the game";

    poisoner.in_room = 7;
    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "back in a room, it resolves again";
}

TEST(PoisonOrigin, ResolveReturnsNullptrAfterThePoisonerIsExtracted)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // The poisoner was extracted: remove_char_exists() is what extract_char() does for a mob
    // and for a player whose socket is gone. A player parked at the menu keeps its slot and is
    // rejected by character_in_game() instead.
    remove_char_exists(kPoisonerSlot);

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "an extracted poisoner must resolve to nobody, never a dangling pointer";
}

TEST(PoisonOrigin, ResolveReturnsNullptrAfterTheSlotIsRecycledByADifferentCharacter)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    remove_char_exists(kPoisonerSlot);

    // register_npc_char()'s cursor hands the freed slot to a brand-new mob --
    // the SAME abs_number, a DIFFERENT char_data*. resolve_poisoner() must
    // recognize the mismatch by pointer identity without ever dereferencing
    // the victim's own (now-stale) poisoned_by -- it only compares that
    // value against char_by_abs_number()'s report of the CURRENT owner.
    char_data imposter {};
    imposter.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &imposter);
    ASSERT_EQ(char_by_abs_number(kPoisonerSlot), &imposter);

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "the recycled slot's new owner must not be mistaken for the recorded poisoner";

    remove_char_exists(kPoisonerSlot);
}

// Pin: the slot is recycled to a character at the SAME address, so number and
// pointer both still match the record; the registration serial stamped by the
// new set_char_exists() is what makes resolve_poisoner() refuse it.
TEST(PoisonOrigin, ResolveReturnsNullptrAfterTheSlotIsReRegisteredAtTheSameAddress)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    remove_char_exists(kPoisonerSlot);
    set_char_exists(kPoisonerSlot, &poisoner);
    ASSERT_EQ(char_by_abs_number(kPoisonerSlot), &poisoner) << "slot and address both match the record";

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "a re-registration at the old address is a new character and must not inherit the poison credit";

    remove_char_exists(kPoisonerSlot);
}

TEST(PoisonOrigin, RecordWithNullptrPoisonerClearsAnExistingRecord)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // A poisoned meal or drink has no poisoner behind it -- do_drink()/
    // do_eat() (act_obj2.cpp) both record nullptr for exactly this reason.
    record_poison_origin(&victim, nullptr);

    EXPECT_EQ(victim.specials.poisoned_by_abs_number, -1);
    EXPECT_EQ(victim.specials.poisoned_by, nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr);

    remove_char_exists(kPoisonerSlot);
}

TEST(PoisonOrigin, AffectRemoveOfTheLastSpellPoisonAffectClearsTheRecord)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);

    affected_type af = inert_poison_affect(10);
    affect_to_char(&victim, &af);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);
    ASSERT_NE(affected_by_spell(&victim, SPELL_POISON), nullptr);

    affect_remove(&victim, victim.affected); // removes the only (and therefore last) SPELL_POISON affect

    EXPECT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr);
    EXPECT_EQ(victim.specials.poisoned_by_abs_number, -1);
    EXPECT_EQ(victim.specials.poisoned_by, nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr);

    remove_char_exists(kPoisonerSlot);
}

TEST(PoisonOrigin, AffectRemoveKeepsTheRecordWhileAConcurrentSpellPoisonAffectRemains)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);

    // Two independent SPELL_POISON affects on the victim at once --
    // affect_to_char() (unlike affect_join()) never merges same-type
    // entries, so this reproduces two poisonings landing concurrently.
    affected_type first = inert_poison_affect(10);
    affect_to_char(&victim, &first);
    affected_type second = inert_poison_affect(5);
    affect_to_char(&victim, &second);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    affect_remove(&victim, victim.affected); // removes one of the two; one SPELL_POISON affect remains

    ASSERT_NE(affected_by_spell(&victim, SPELL_POISON), nullptr)
        << "a concurrent SPELL_POISON affect must still be standing";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner)
        << "the record must survive while a SPELL_POISON affect it could belong to remains";

    affect_remove(&victim, victim.affected); // removes the last SPELL_POISON affect

    EXPECT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr);
    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "once the last SPELL_POISON affect is gone the record must be cleared too";

    remove_char_exists(kPoisonerSlot);
}

TEST(PoisonOrigin, ClearCharBlanksThePoisonRecord)
{
    char_data poisoner {};

    char_data character {};
    character.specials.poisoned_by_abs_number = 777;
    character.specials.poisoned_by = &poisoner;

    clear_char(&character, MOB_VOID);

    EXPECT_EQ(character.specials.poisoned_by_abs_number, -1);
    EXPECT_EQ(character.specials.poisoned_by, nullptr);
}

// Poisoned food or drink has no recorded poisoner. It replaces a weaker poison (and clears the
// record, since nobody owns the new one) and leaves a stronger one, and its poisoner, alone.
TEST(PoisonOrigin, ConsumedPoisonLeavesAStrongerSpellPoisonAndItsPoisoner)
{
    char_data poisoner {};
    ScopedCharExists poisoner_exists { poisoner, kPoisonerSlot };
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type spell_poison = inert_poison_affect(20);
    affect_to_char(&victim, &spell_poison);
    record_poison_origin(&victim, &poisoner);

    apply_consumed_poison(&victim, inert_poison_affect(5));

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 20) << "the weaker consumed poison changes nothing";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner);
}

TEST(PoisonOrigin, ConsumedPoisonReplacesAWeakerSpellPoisonAndClearsThePoisoner)
{
    char_data poisoner {};
    ScopedCharExists poisoner_exists { poisoner, kPoisonerSlot };
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type spell_poison = inert_poison_affect(5);
    affect_to_char(&victim, &spell_poison);
    record_poison_origin(&victim, &poisoner);

    apply_consumed_poison(&victim, inert_poison_affect(20));

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 20) << "the stronger consumed poison takes over";
    EXPECT_EQ(resolve_poisoner(victim), nullptr) << "nobody owns a consumed poison";
}

TEST(PoisonOrigin, ConsumedPoisonOnAnUnpoisonedCharacterAppliesAndRecordsNobody)
{
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);

    apply_consumed_poison(&victim, inert_poison_affect(8));

    const affected_type* poison = affected_by_spell(&victim, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    EXPECT_EQ(poison->duration, 8);
    EXPECT_EQ(resolve_poisoner(victim), nullptr);
}
