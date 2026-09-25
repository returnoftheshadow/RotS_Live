// get_affect_unbounded() and the merge in affect_join() that uses it. A character's affect list
// has no length limit, while affected_by_spell() stops after MAX_AFFECT entries; these tests bury
// a poison under inert filler affects to show the difference.
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_character_support.h"
#include <gtest/gtest.h>

namespace {

// A stack-local NPC with just enough state for affect_to_char() and affect_remove().
void make_npc(char_data& character, char_prof_data& profs)
{
    character.profs = &profs;
    character.specials2.act = MOB_ISNPC;
    character.nr = -1;
    character.player.race = RACE_HUMAN;
    character.player.level = 10;
    character.specials.position = POSITION_STANDING;
    character.specials.fighting = nullptr;
}

// An affect of `affect_type` that changes no stat and sets no flag.
affected_type inert_affect(int affect_type, int duration)
{
    affected_type affect {};
    affect.type = affect_type;
    affect.duration = duration;
    affect.modifier = 0;
    affect.location = APPLY_NONE;
    affect.bitvector = 0;
    return affect;
}

// Adds `filler_count` inert armor affects ahead of everything already on the character.
void add_filler_affects(char_data& character, int filler_count)
{
    for (int filler = 0; filler < filler_count; ++filler) {
        affected_type armor = inert_affect(SPELL_ARMOR, 10);
        affect_to_char(&character, &armor);
    }
}

int count_affects_of_type(const char_data& character, int affect_type)
{
    int matches = 0;
    for (const affected_type* affect = character.affected; affect != nullptr; affect = affect->next) {
        if (affect->type == affect_type) {
            ++matches;
        }
    }
    return matches;
}

} // namespace

TEST(GetAffectUnbounded, FindsAnAffectBuriedPastMaxAffectEntries)
{
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &poison);
    const affected_type* buried_poison = victim.affected;
    add_filler_affects(victim, 2 * MAX_AFFECT);

    ASSERT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr) << "the capped finder stops before the poison";
    EXPECT_EQ(get_affect_unbounded(&victim, SPELL_POISON), buried_poison);
}

TEST(GetAffectUnbounded, ReturnsTheNewestMatchOrNullptr)
{
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    add_filler_affects(victim, 3);

    EXPECT_EQ(get_affect_unbounded(&victim, SPELL_POISON), nullptr);

    affected_type older_poison = inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &older_poison);
    affected_type newer_poison = inert_affect(SPELL_POISON, 5);
    affect_to_char(&victim, &newer_poison);

    const affected_type* found = get_affect_unbounded(&victim, SPELL_POISON);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, victim.affected);
    EXPECT_EQ(found->duration, 5);
}

// MAX_AFFECT fillers put the poison one entry past affected_by_spell()'s reach but still within
// affect_remove()'s, so the merge can complete.
TEST(AffectJoin, MergesIntoAPoisonJustPastTheCappedFinder)
{
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type first_poison = inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &first_poison);
    add_filler_affects(victim, MAX_AFFECT);
    ASSERT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr) << "the capped finder must miss the first poison";

    affected_type second_poison = inert_affect(SPELL_POISON, 5);
    affect_join(&victim, &second_poison, FALSE, FALSE);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_POISON), 1) << "the second poison must merge, not sit beside the first";
    ASSERT_EQ(victim.affected->type, SPELL_POISON);
    EXPECT_EQ(victim.affected->duration, 25) << "a shorter new poison adds the old duration";
}

TEST(AffectJoin, AddsTheAffectWhenNoneOfItsTypeIsPresent)
{
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    add_filler_affects(victim, 2);

    affected_type poison = inert_affect(SPELL_POISON, 5);
    affect_join(&victim, &poison, FALSE, FALSE);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_POISON), 1);
    ASSERT_EQ(victim.affected->type, SPELL_POISON);
    EXPECT_EQ(victim.affected->duration, 5);
}
