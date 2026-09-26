// The poison library's templates, cure, poisoner-record upkeep, resist-poison start and tick.
// Affect lists here are built past MAX_AFFECT entries on purpose: the cure and the record upkeep
// must see a poison at any depth, where affected_by_spell() stops after MAX_AFFECT entries.
#include "../caster_snapshot.h"
#include "../handler.h"
#include "../poison.h"
#include "../poison_origin.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "character_affect_list_printer.h"
#include "scoped_combat_list.h"
#include "scoped_room_occupants.h"
#include "test_affect_support.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"

#include <gtest/gtest.h>

namespace {

// A world[] index no other suite claims; the victim of a poison tick stands here so the damage's
// room messages have a valid occupant list to walk.
constexpr int kVictimRoom = 940;

// An abs_number slot no other suite claims, for the recorded poisoner.
constexpr int kPoisonerSlot = MAX_CHARACTERS - 611;

} // namespace

TEST(PoisonLibrary, ThePaleLadysPoisonIsMinusFourStrengthForTwentyFourTicks) {
    const affected_type poison = pale_lady_poison_affect();

    EXPECT_EQ(poison.type, SPELL_POISON);
    EXPECT_EQ(poison.location, APPLY_STR);
    EXPECT_EQ(poison.modifier, -4) << "the Pale Lady's bite is the strongest poison, -4 STR";
    EXPECT_EQ(poison.duration, 24);
    EXPECT_EQ(poison.bitvector, AFF_POISON);
}

TEST(PoisonLibrary, AConsumedPoisonHasNoStrengthMalusAndTheGivenDuration) {
    const affected_type poison = consumed_poison_affect(7);

    EXPECT_EQ(poison.type, SPELL_POISON);
    EXPECT_EQ(poison.location, APPLY_NONE);
    EXPECT_EQ(poison.modifier, 0) << "poisoned food or drink carries no strength malus";
    EXPECT_EQ(poison.duration, 7);
    EXPECT_EQ(poison.bitvector, AFF_POISON);
}

TEST(PoisonLibrary, CurePoisonRemovesAPoisonBuriedPastMaxAffectEntries) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = test_support::inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &poison);
    test_support::add_filler_affects(victim, 2 * MAX_AFFECT);
    ASSERT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr)
        << "precondition: the poison sits past affected_by_spell()'s " << MAX_AFFECT << " entries";

    EXPECT_TRUE(cure_poison(&victim)) << "the victim carried a poison, so the cure reports one";
    EXPECT_EQ(get_affect_unbounded(&victim, SPELL_POISON), nullptr)
        << "the cure must remove a poison buried under " << 2 * MAX_AFFECT << " fillers";
    EXPECT_EQ(test_support::count_affects_of_type(victim, SPELL_ARMOR), 2 * MAX_AFFECT)
        << "the cure removes only poison affects";
}

TEST(PoisonLibrary, CurePoisonOnAnUnpoisonedCharacterReturnsFalse) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    test_support::add_filler_affects(victim, 3);

    EXPECT_FALSE(cure_poison(&victim)) << "an unpoisoned character has nothing to cure";
    EXPECT_EQ(test_support::count_affects_of_type(victim, SPELL_ARMOR), 3)
        << "the other affects stay";
}

TEST(PoisonLibrary, ForgettingTheOriginKeepsTheRecordWhileABuriedSecondPoisonRemains) {
    char_data poisoner{};
    test_support::ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type buried_poison = test_support::inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &buried_poison);
    test_support::add_filler_affects(victim, 2 * MAX_AFFECT);
    affected_type newest_poison = test_support::inert_affect(SPELL_POISON, 5);
    affect_to_char(&victim, &newest_poison);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "precondition: the poisoner resolves";

    affect_remove(&victim, victim.affected); // the newest poison, at the head of the list
    forget_poison_origin_if_cured(&victim);

    ASSERT_NE(get_affect_unbounded(&victim, SPELL_POISON), nullptr)
        << "precondition: the buried poison is still running";
    EXPECT_EQ(resolve_poisoner(victim), &poisoner)
        << "the record must survive while a poison past " << MAX_AFFECT << " entries remains";
}

TEST(PoisonLibrary, ForgettingTheOriginClearsTheRecordOnceNoPoisonRemains) {
    char_data poisoner{};
    test_support::ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    record_poison_origin(&victim, &poisoner);
    ASSERT_EQ(resolve_poisoner(victim), &poisoner) << "precondition: the poisoner resolves";

    forget_poison_origin_if_cured(&victim);

    EXPECT_EQ(resolve_poisoner(victim), nullptr)
        << "with no poison affect left, the record names nobody";
}

TEST(PoisonLibrary, ResistingWithoutAPoisonChangesNothing) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);

    EXPECT_EQ(start_poison_resistance(&victim, 17), poison_resistance_outcome::not_poisoned);
    EXPECT_EQ(victim.affected, nullptr) << "no resist-poison affect starts without a poison";
}

TEST(PoisonLibrary, ResistingARunningPoisonMatchesItsDurationAtTheClericLevel) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = test_support::inert_affect(SPELL_POISON, 12);
    affect_to_char(&victim, &poison);

    EXPECT_EQ(start_poison_resistance(&victim, 17), poison_resistance_outcome::started);

    const affected_type* resistance = get_affect_unbounded(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(resistance, nullptr) << "a resist-poison affect must start";
    EXPECT_EQ(resistance->duration, 12) << "the resistance lasts as long as the running poison";
    EXPECT_EQ(resistance->modifier, 17) << "the modifier is the cleric level passed in";
    EXPECT_EQ(resistance->location, APPLY_NONE);
    EXPECT_EQ(resistance->bitvector, 0);
    EXPECT_EQ(resistance->counter, 0) << "the resist-poison affect is value-initialised";
}

TEST(PoisonLibrary, ResistingTwiceLeavesTheFirstResistanceAlone) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = test_support::inert_affect(SPELL_POISON, 12);
    affect_to_char(&victim, &poison);
    ASSERT_EQ(start_poison_resistance(&victim, 17), poison_resistance_outcome::started)
        << "precondition: the first attempt starts resisting";

    EXPECT_EQ(start_poison_resistance(&victim, 25), poison_resistance_outcome::already_resisting);

    EXPECT_EQ(test_support::count_affects_of_type(victim, SPELL_RESIST_POISON), 1)
        << "a second attempt adds no resist-poison affect";
    const affected_type* const resistance = get_affect_unbounded(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(resistance, nullptr) << "the first resistance is still running";
    EXPECT_EQ(resistance->modifier, 17) << "the running resistance keeps its cleric level";
}

TEST(PoisonLibrary, AResistedTickShortensThePoisonByTheModifierAndSyncsTheResistance) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    test_support::ScopedCombatList combat; // the tick's damage engages the victim with itself
    test_support::ScopedRoomOccupants room(kVictimRoom, {&victim});
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = test_support::inert_affect(SPELL_POISON, 9);
    affect_to_char(&victim, &poison);
    affected_type resistance = test_support::inert_affect(SPELL_RESIST_POISON, 9);
    resistance.modifier = 5;
    affect_to_char(&victim, &resistance);
    affected_type* const running_poison = get_affect_unbounded(&victim, SPELL_POISON);
    ASSERT_NE(running_poison, nullptr) << "precondition: the poison is on";
    const int starting_hit = GET_HIT(&victim);

    EXPECT_FALSE(tick_poison_affect(&victim, running_poison))
        << "a " << test_support::kSturdyNpcHitPoints << "-hit victim survives a tick";

    EXPECT_EQ(running_poison->duration, 4) << "a remaining 9 less the resist modifier 5 is 4";
    const affected_type* const running_resistance =
        get_affect_unbounded(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(running_resistance, nullptr) << "a resisted tick keeps the resist-poison affect";
    EXPECT_EQ(running_resistance->duration, 4)
        << "the resist-poison affect follows the poison's duration";
    EXPECT_EQ(GET_HIT(&victim), starting_hit - 5) << "every poison tick deals 5 damage";
}

// The null-victim paths below log a SYSERR line to stderr; that output is expected.

TEST(PoisonLibrary, ApplyingAPoisonToANullVictimIsNotApplied) {
    const affected_type poison = pale_lady_poison_affect();

    EXPECT_EQ(apply_poison(nullptr, poison, nullptr), poison_outcome::not_applied);
}

TEST(PoisonLibrary, OutcomeMessagesForANullVictimTellTheCasterNothing) {
    char_data caster{};
    char_prof_data caster_profs{};
    test_support::fill_sturdy_stack_npc(caster, caster_profs);
    descriptor_data caster_descriptor{};
    test_support::prepare_capture_descriptor(caster_descriptor);
    caster.desc = &caster_descriptor;

    send_poison_outcome_messages(poison_outcome::blocked_by_stronger, nullptr, &caster,
                                 "fresh line\n\r");

    EXPECT_STREQ(caster_descriptor.small_outbuf, "") << "with no victim, nobody is told anything";
}

TEST(PoisonLibrary, ANotAppliedOutcomeSendsNothing) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    descriptor_data victim_descriptor{};
    test_support::prepare_capture_descriptor(victim_descriptor);
    victim.desc = &victim_descriptor;
    char_data caster{};
    char_prof_data caster_profs{};
    test_support::fill_sturdy_stack_npc(caster, caster_profs);
    descriptor_data caster_descriptor{};
    test_support::prepare_capture_descriptor(caster_descriptor);
    caster.desc = &caster_descriptor;

    send_poison_outcome_messages(poison_outcome::not_applied, &victim, &caster, "fresh line\n\r");

    EXPECT_STREQ(victim_descriptor.small_outbuf, "");
    EXPECT_STREQ(caster_descriptor.small_outbuf, "");
}

TEST(PoisonLibrary, ANullVictimSavesAgainstThePoison) {
    const caster_snapshot caster{};

    EXPECT_TRUE(saves_poison(nullptr, caster)) << "a poison with no victim does not land";
}

TEST(PoisonLibrary, TickingANullVictimReportsNoDeath) {
    affected_type poison = test_support::inert_affect(SPELL_POISON, 9);

    EXPECT_FALSE(tick_poison_affect(nullptr, &poison));
    EXPECT_EQ(poison.duration, 9) << "the poison is left alone";
}

TEST(PoisonLibrary, TickingANullPoisonLeavesTheVictimAlone) {
    char_data victim{};
    char_prof_data victim_profs{};
    test_support::fill_sturdy_stack_npc(victim, victim_profs);
    const int starting_hit = GET_HIT(&victim);

    EXPECT_FALSE(tick_poison_affect(&victim, nullptr));
    EXPECT_EQ(GET_HIT(&victim), starting_hit) << "no poison, so no damage";
}

TEST(PoisonLibrary, PoisonTickDamageToANullVictimIsZero) {
    EXPECT_EQ(deal_poison_tick_damage(nullptr), 0);
}

TEST(PoisonLibrary, CuringANullVictimCuresNothing) { EXPECT_FALSE(cure_poison(nullptr)); }

TEST(PoisonLibrary, ResistingForANullVictimFindsNoPoison) {
    EXPECT_EQ(start_poison_resistance(nullptr, 17), poison_resistance_outcome::not_poisoned);
}

TEST(PoisonLibrary, ForgettingTheOriginOfANullVictimChangesNothing) {
    char_data poisoner{};
    test_support::ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    record_poison_origin(&victim, &poisoner);

    forget_poison_origin_if_cured(nullptr);

    EXPECT_EQ(resolve_poisoner(victim), &poisoner) << "an unrelated record is untouched";
}
