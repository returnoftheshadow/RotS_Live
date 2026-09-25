// The poison library's templates, cure, poisoner-record upkeep, resist-poison start and tick.
// Affect lists here are built past MAX_AFFECT entries on purpose: the cure and the record upkeep
// must see a poison at any depth, where affected_by_spell() stops after MAX_AFFECT entries.
#include "../handler.h"
#include "../poison.h"
#include "../poison_origin.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_character_support.h"

#include <gtest/gtest.h>

extern struct room_data world;
extern int top_of_world;
extern struct char_data* combat_list;

namespace {

// A world[] index no other suite claims; the victim of a poison tick stands here so the damage's
// room messages have a valid occupant list to walk.
constexpr int kVictimRoom = 940;

// An abs_number slot no other suite claims, for the recorded poisoner.
constexpr int kPoisonerSlot = MAX_CHARACTERS - 611;

void ensure_test_world(int minimum_room_number) {
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

// Saves and restores the victim's room occupant list and the global combat list: the poison
// damage engages the victim with itself, which puts it on combat_list.
class ScopedVictimRoom {
  public:
    explicit ScopedVictimRoom(char_data& victim)
        : m_previous_people(nullptr), m_previous_combat_list(combat_list) {
        ensure_test_world(kVictimRoom);
        m_previous_people = world[kVictimRoom].people;
        world[kVictimRoom].people = &victim;
        victim.next_in_room = nullptr;
        victim.in_room = kVictimRoom;
    }
    ~ScopedVictimRoom() {
        world[kVictimRoom].people = m_previous_people;
        combat_list = m_previous_combat_list;
    }
    ScopedVictimRoom(const ScopedVictimRoom&) = delete;
    ScopedVictimRoom& operator=(const ScopedVictimRoom&) = delete;

  private:
    char_data* m_previous_people;      // the room's occupant list before the test
    char_data* m_previous_combat_list; // combat_list before the test
};

// A stack-local NPC with enough state for affect_to_char(), affect_remove() and a poison tick.
void make_npc(char_data& character, char_prof_data& profs) {
    character.profs = &profs;
    character.specials2.act = MOB_ISNPC;
    character.nr = -1;
    character.player.race = RACE_HUMAN;
    character.player.level = 10;
    character.abilities.hit = 500;
    character.tmpabilities.hit = 500; // far above the 5 a poison tick deals
    character.specials.position = POSITION_STANDING;
    character.specials.fighting = nullptr;
}

// An affect of `affect_type` that changes no stat and sets no flag.
affected_type inert_affect(int affect_type, int duration) {
    affected_type affect{};
    affect.type = affect_type;
    affect.duration = duration;
    affect.modifier = 0;
    affect.location = APPLY_NONE;
    affect.bitvector = 0;
    return affect;
}

// Adds `filler_count` inert armor affects ahead of everything already on the character.
void add_filler_affects(char_data& character, int filler_count) {
    for (int filler = 0; filler < filler_count; ++filler) {
        affected_type armor = inert_affect(SPELL_ARMOR, 10);
        affect_to_char(&character, &armor);
    }
}

int count_affects_of_type(const char_data& character, int affect_type) {
    int matches = 0;
    for (const affected_type* affect = character.affected; affect != nullptr;
         affect = affect->next) {
        if (affect->type == affect_type) {
            ++matches;
        }
    }
    return matches;
}

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
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &poison);
    add_filler_affects(victim, 2 * MAX_AFFECT);
    ASSERT_EQ(affected_by_spell(&victim, SPELL_POISON), nullptr)
        << "precondition: the poison sits past affected_by_spell()'s " << MAX_AFFECT << " entries";

    EXPECT_TRUE(cure_poison(&victim)) << "the victim carried a poison, so the cure reports one";
    EXPECT_EQ(get_affect_unbounded(&victim, SPELL_POISON), nullptr)
        << "the cure must remove a poison buried under " << 2 * MAX_AFFECT << " fillers";
    EXPECT_EQ(count_affects_of_type(victim, SPELL_ARMOR), 2 * MAX_AFFECT)
        << "the cure removes only poison affects";
}

TEST(PoisonLibrary, CurePoisonOnAnUnpoisonedCharacterReturnsFalse) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    add_filler_affects(victim, 3);

    EXPECT_FALSE(cure_poison(&victim)) << "an unpoisoned character has nothing to cure";
    EXPECT_EQ(count_affects_of_type(victim, SPELL_ARMOR), 3) << "the other affects stay";
}

TEST(PoisonLibrary, ForgettingTheOriginKeepsTheRecordWhileABuriedSecondPoisonRemains) {
    char_data poisoner{};
    test_support::ScopedCharExists poisoner_exists{poisoner, kPoisonerSlot};
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type buried_poison = inert_affect(SPELL_POISON, 20);
    affect_to_char(&victim, &buried_poison);
    add_filler_affects(victim, 2 * MAX_AFFECT);
    affected_type newest_poison = inert_affect(SPELL_POISON, 5);
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
    make_npc(victim, victim_profs);
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
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);

    EXPECT_EQ(start_poison_resistance(&victim, 17), poison_resistance_outcome::not_poisoned);
    EXPECT_EQ(victim.affected, nullptr) << "no resist-poison affect starts without a poison";
}

TEST(PoisonLibrary, ResistingARunningPoisonMatchesItsDurationAtTheClericLevel) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = inert_affect(SPELL_POISON, 12);
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
    make_npc(victim, victim_profs);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = inert_affect(SPELL_POISON, 12);
    affect_to_char(&victim, &poison);
    ASSERT_EQ(start_poison_resistance(&victim, 17), poison_resistance_outcome::started)
        << "precondition: the first attempt starts resisting";

    EXPECT_EQ(start_poison_resistance(&victim, 25), poison_resistance_outcome::already_resisting);

    EXPECT_EQ(count_affects_of_type(victim, SPELL_RESIST_POISON), 1)
        << "a second attempt adds no resist-poison affect";
    const affected_type* const resistance = get_affect_unbounded(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(resistance, nullptr) << "the first resistance is still running";
    EXPECT_EQ(resistance->modifier, 17) << "the running resistance keeps its cleric level";
}

TEST(PoisonLibrary, AResistedTickShortensThePoisonByTheModifierAndSyncsTheResistance) {
    char_data victim{};
    char_prof_data victim_profs{};
    make_npc(victim, victim_profs);
    ScopedVictimRoom room(victim);
    test_support::ScopedAffectCleanup victim_affects(victim);
    affected_type poison = inert_affect(SPELL_POISON, 9);
    affect_to_char(&victim, &poison);
    affected_type resistance = inert_affect(SPELL_RESIST_POISON, 9);
    resistance.modifier = 5;
    affect_to_char(&victim, &resistance);
    affected_type* const running_poison = get_affect_unbounded(&victim, SPELL_POISON);
    ASSERT_NE(running_poison, nullptr) << "precondition: the poison is on";
    const int starting_hit = GET_HIT(&victim);

    EXPECT_EQ(tick_poison_affect(&victim, running_poison), 0) << "a 500-hit victim survives a tick";

    EXPECT_EQ(running_poison->duration, 4) << "a remaining 9 less the resist modifier 5 is 4";
    const affected_type* const running_resistance =
        get_affect_unbounded(&victim, SPELL_RESIST_POISON);
    ASSERT_NE(running_resistance, nullptr) << "a resisted tick keeps the resist-poison affect";
    EXPECT_EQ(running_resistance->duration, 4)
        << "the resist-poison affect follows the poison's duration";
    EXPECT_EQ(GET_HIT(&victim), starting_hit - 5) << "every poison tick deals 5 damage";
}
