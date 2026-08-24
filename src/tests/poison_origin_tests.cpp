// TASK-021 port, Task 7: poison origin tracking. record_poison_origin()
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
#include <gtest/gtest.h>

namespace {

// The abs_number slots this file's tests hand between characters. High,
// out-of-band slots so register_npc_char() (which allocates from slot 0
// upward) is very unlikely to reach them, and distinct from the ranges other
// suites already claim (affect_update_tests.cpp: MAX_CHARACTERS - 201/-202;
// caster_snapshot_tests.cpp: MAX_CHARACTERS - 401; char_utils_tests.cpp:
// MAX_CHARACTERS - 17/-18).
constexpr int kPoisonerSlot = MAX_CHARACTERS - 601;

// RAII registration matching affect_update_tests.cpp's ScopedCharExists: the
// two-argument set_char_exists() (TASK-021 port) records the pointer
// char_by_abs_number() -- and therefore resolve_poisoner() -- hands back.
class ScopedCharExists {
public:
    ScopedCharExists(char_data& ch, int abs_number)
        : m_ch(ch)
    {
        ch.abs_number = abs_number;
        set_char_exists(abs_number, &ch);
    }
    ~ScopedCharExists() { remove_char_exists(m_ch.abs_number); }
    ScopedCharExists(const ScopedCharExists&) = delete;
    ScopedCharExists& operator=(const ScopedCharExists&) = delete;

private:
    char_data& m_ch; // the character whose registration this scope owns
};

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

TEST(PoisonOrigin, ResolveReturnsNullptrAfterThePoisonerIsExtracted)
{
    char_data poisoner {};
    poisoner.abs_number = kPoisonerSlot;
    set_char_exists(kPoisonerSlot, &poisoner);

    char_data victim {};
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner);

    // The poisoner was extracted: remove_char_exists() is exactly what
    // extract_char()'s NPC/PC arms do to a freed character's slot.
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
