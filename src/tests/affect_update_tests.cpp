// TASK-020 -- affect_update()'s walk over the process-global affected_list
// must survive a death that the walk itself triggers.
//
// affect_update() (limits.cpp) used to walk affected_list with a pre-saved
// `tmplist2 = tmplist->next`. affect_update_room() re-casts a room's blaze on
// each occupant with caster == victim == the occupant; a lethal tick runs
// damage() -> die() -> raw_kill(), which strips every affect of the dead
// character, and affect_remove()'s tail then removes the character's own
// affected_list node through from_list_to_pool() -- which free()s it. When
// that node is the one the walk already saved as next, the next iteration
// dereferences freed memory. pool_to_list() inserts at the head, so the node
// right behind a freshly-cast blaze room's is whoever acquired their first
// affect just before the cast -- exactly the people standing in the blaze.
//
// This depot has no extract_char test seam, so the dying occupant is built the way the game builds
// an NPC -- heap-allocated, clear_char()'d, and register_npc_char()'d
// into character_list and a real room -- so extract_char()/free_char() can
// run for real, following the TASK-018 fireball-fumble precedent at
// src/tests/mage_tests.cpp (make_fireball_caster / ScopedFireballMobIndex /
// release_fireball_corpse). The fixture reproduces the freed-node order
// literally: [blaze room, dying occupant, sentinel]. Against the unfixed
// walk this is a use-after-free (ASan reports it; the plain build reads
// garbage). Against the fixed walk it asserts the tick completed, the
// occupant died, the sentinel's node is intact, and nothing resolved the
// dead occupant.
#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "test_random_utils.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>

extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern struct index_data* mob_index;
extern universal_list* affected_list;
extern universal_list* affected_list_pool;
extern struct skill_data skills[];
extern struct char_data* combat_list;
extern struct char_data* combat_next_dude;

void affect_update();
void affect_update_room(struct room_data* room);
ASPELL(spell_blaze);

namespace {

void ensure_test_world(int minimum_room_number) {
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

// Saves/restores a room's occupant chain so a test's fixture leaves the
// shared world[] state exactly as it found it for later tests in this binary.
struct RoomOccupantGuard {
    int room_number; // which world[] slot this guard owns for the scope
    char_data* original_people; // whatever was linked there before the test

    explicit RoomOccupantGuard(int room)
        : room_number(room), original_people(nullptr) {
        ensure_test_world(room);
        original_people = world[room].people;
    }
    ~RoomOccupantGuard() { world[room_number].people = original_people; }
    RoomOccupantGuard(const RoomOccupantGuard&) = delete;
    RoomOccupantGuard& operator=(const RoomOccupantGuard&) = delete;
};

// The death pipeline reads the NPC's prototype (raw_kill()'s SPECIAL_DEATH
// probe via activate_char_special, and make_corpse()'s corpse-owner lookup)
// unconditionally for any IS_NPC() character; publish a one-entry table for
// the scope, matching src/tests/mage_tests.cpp's ScopedFireballMobIndex.
class ScopedAffectUpdateMobIndex {
public:
    ScopedAffectUpdateMobIndex()
        : m_previous(mob_index) {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedAffectUpdateMobIndex() { mob_index = m_previous; }
    ScopedAffectUpdateMobIndex(const ScopedAffectUpdateMobIndex&) = delete;
    ScopedAffectUpdateMobIndex& operator=(const ScopedAffectUpdateMobIndex&) = delete;

private:
    index_data* m_previous; // whatever this suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot the occupant's nr = 0 names
};

// gtest_main does not run the real spell-pointer assignment pass; install
// blaze's real body in its skills[] cell for the scope so
// affect_update_room() can re-cast it on the room's occupants.
class ScopedBlazeSpellPointer {
public:
    ScopedBlazeSpellPointer()
        : m_previous(skills[SPELL_BLAZE].spell_pointer) {
        skills[SPELL_BLAZE].spell_pointer = spell_blaze;
    }
    ~ScopedBlazeSpellPointer() { skills[SPELL_BLAZE].spell_pointer = m_previous; }
    ScopedBlazeSpellPointer(const ScopedBlazeSpellPointer&) = delete;
    ScopedBlazeSpellPointer& operator=(const ScopedBlazeSpellPointer&) = delete;

private:
    void (*m_previous)(char_data*, char*, int, char_data*, obj_data*, int, int); // the cell's prior value
};

// Registers a unique abs_number the way register_npc_char() would for a real
// mob, through the TWO-argument set_char_exists() (TASK-021 port), which
// also records the pointer char_by_abs_number() hands back. affect_update()
// updates a snapshotted entry only while that lookup still returns the very
// pointer the entry named, so the one-argument overload -- which leaves the
// pointer slot null -- cannot stand in for a real registration here.
class ScopedCharExists {
public:
    explicit ScopedCharExists(char_data& ch, int abs_number)
        : m_ch(ch) {
        ch.abs_number = abs_number;
        set_char_exists(abs_number, &ch);
    }
    ~ScopedCharExists() { remove_char_exists(m_ch.abs_number); }
    ScopedCharExists(const ScopedCharExists&) = delete;
    ScopedCharExists& operator=(const ScopedCharExists&) = delete;

private:
    char_data& m_ch; // the character whose registration this scope owns
};

bool affected_list_holds(const void* ptr) {
    for (universal_list* node = affected_list; node; node = node->next) {
        if (node->ptr.ch == ptr || node->ptr.room == ptr) {
            return true;
        }
    }
    return false;
}

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents
// and object_list; take both back out so a death test leaves no residue for
// later tests in this binary. Mirrors mage_tests.cpp's release_fireball_corpse.
void release_corpse_from_room(int room_number, obj_data* previous_object_list) {
    obj_data* corpse = world[room_number].contents;
    if (corpse == nullptr) {
        return;
    }
    obj_from_room(corpse);
    if (object_list == corpse) {
        object_list = corpse->next;
    }
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    RELEASE(corpse);
    object_list = previous_object_list;
}

// Builds a normal, alive, stack-local NPC that never dies in these tests --
// the sentinel/imposter/victim roles below. Mirrors mage_tests.cpp's
// MageTestContext-style member setup.
void make_npc(char_data& ch, char_prof_data& profs, int hit) {
    ch.profs = &profs;
    ch.specials2.act = MOB_ISNPC;
    ch.nr = -1;
    ch.player.race = RACE_HUMAN;
    ch.player.level = 10;
    ch.tmpabilities.intel = 20;
    ch.abilities.hit = hit;
    ch.tmpabilities.hit = hit;
    ch.specials.position = POSITION_STANDING;
    ch.specials.fighting = nullptr;
}

// Builds the heap-allocated, registered NPC occupant the death pipeline
// needs (see the file comment above): clear_char() + register_npc_char() the
// way the game constructs an NPC, with nr = 0 naming the
// ScopedAffectUpdateMobIndex slot above.
char_data* make_blaze_occupant(int hit_points, char* short_descr, int room) {
    char_data* occupant = new char_data {};
    clear_char(occupant, MOB_ISNPC);
    occupant->specials2.act = MOB_ISNPC;
    occupant->nr = 0; // prototype slot 0 of the scoped one-entry mob_index above
    occupant->player.race = RACE_HUMAN;
    occupant->player.short_descr = short_descr; // make_corpse() reads GET_NAME() for the corpse text
    occupant->player.level = 30;
    occupant->profs->prof_level[PROF_MAGE] = 30;
    occupant->tmpabilities.intel = 20;
    occupant->tmpabilities.hit = hit_points;
    occupant->abilities.hit = std::max(hit_points, 1);
    occupant->specials.position = POSITION_STANDING;
    occupant->specials.fighting = nullptr;
    occupant->in_room = room;
    register_npc_char(occupant);
    return occupant;
}

affected_type inert_affect(int duration) {
    affected_type af {};
    af.type = SPELL_INFRAVISION; // inert for the damage path; only its affected_list node matters
    af.duration = duration;
    af.modifier = 0;
    af.location = 0;
    af.bitvector = 0;
    return af;
}

constexpr int kBlazeRoom = 27;
constexpr int kQuietRoom = 28;

// Pinned at the midpoint: this container's x87 arithmetic truncates products
// that land just below an integer boundary, so an integer roll r in [from,
// to] must be pinned at the MIDPOINT (r - from + 0.5) / range rather than at
// r's own fraction -- midpoint values truncate identically under x87 and
// SSE2. The only draw this test's control flow depends on is affect_update_room()'s
// "1 in 13 chance a room spell does nothing" gate -- number(0, 12) must come
// out 0 so the blaze tick actually fires on the occupant -- so this is the
// range-13 midpoint for r = 0. Every other draw in the call (movechance,
// get_mage_caster_level()'s rounding roll, the damage roll, the save roll,
// and whatever damage()/die()/raw_kill()/extract_char() draw internally)
// only affects magnitude, never which branch is taken, since the occupant's
// one-hit-point body dies from any positive damage regardless of a save.
constexpr double kBlazeSafeRoll = 0.5 / 13.0; // (0 - 0 + 0.5) / 13 -- number(0, 12) == 0, the tick fires

void queue_blaze_rolls(int count = 100) {
    for (int roll_index = 0; roll_index < count; ++roll_index) {
        push_test_random_value(kBlazeSafeRoll);
    }
}

// The abs_number slots the recycled-slot tests below hand from one character
// to another. High, out-of-band slots so register_npc_char() (which
// allocates from slot 0 upward) is very unlikely to reach them in this
// suite; char_utils_tests.cpp's CharRegistry tests separately own
// MAX_CHARACTERS - 17/-18, so this file stays clear of those too.
constexpr int kSentinelSlot = MAX_CHARACTERS - 201;
constexpr int kRecycledSlot = MAX_CHARACTERS - 202;

} // namespace

TEST(AffectUpdateWalk, SurvivesAnOccupantDyingToTheBlazeTickItIsProcessing) {
    ScopedAffectUpdateMobIndex prototype_table;
    ScopedBlazeSpellPointer blaze_cell;
    RoomOccupantGuard blaze_room_guard(kBlazeRoom);
    RoomOccupantGuard quiet_room_guard(kQuietRoom);

    char_data sentinel {};
    char_prof_data sentinel_profs {};
    make_npc(sentinel, sentinel_profs, 500);
    sentinel.in_room = kQuietRoom;
    ScopedCharExists sentinel_exists { sentinel, kSentinelSlot };

    char occupant_short_descr[] = "a testing blaze victim";
    char_data* occupant = make_blaze_occupant(1, occupant_short_descr, kBlazeRoom); // any blaze tick is lethal
    char_data* const original_character_list = character_list;
    character_list = occupant;
    occupant->next = nullptr;

    world[kQuietRoom].people = &sentinel;
    sentinel.next_in_room = nullptr;
    world[kBlazeRoom].people = occupant;
    occupant->next_in_room = nullptr;

    char_data* const original_combat_list = combat_list;
    char_data* const original_combat_next_dude = combat_next_dude;
    obj_data* const previous_object_list = object_list;

    // Creation order decides affected_list order (head insertion):
    // sentinel first (tail), occupant second, the room last (head).
    affected_type sentinel_af = inert_affect(50);
    affect_to_char(&sentinel, &sentinel_af);
    affected_type occupant_af = inert_affect(50);
    affect_to_char(occupant, &occupant_af);
    affected_type blaze {};
    blaze.type = ROOMAFF_SPELL;
    blaze.duration = 5;
    blaze.modifier = 20;
    blaze.location = SPELL_BLAZE;
    blaze.bitvector = 0;
    affect_to_room(&world[kBlazeRoom], &blaze);
    ASSERT_EQ(affected_list->ptr.room, &world[kBlazeRoom]) << "the blaze room must head the walk";
    ASSERT_EQ(affected_list->type, TARGET_ROOM);
    ASSERT_EQ(affected_list->next->type, TARGET_CHAR);
    ASSERT_EQ(affected_list->next->ptr.ch, occupant) << "the dying occupant must be the saved next node";
    ASSERT_TRUE(affected_list_holds(&sentinel));

    const int occupant_abs_number = occupant->abs_number;
    queue_blaze_rolls();

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();
    clear_test_random_values();
    // occupant is freed at this point; nothing below may dereference it --
    // only compare the pointer value or read state through survivors
    // (character_list, world[]'s room lists, char_by_abs_number()).

    release_corpse_from_room(kBlazeRoom, previous_object_list);
    if (affected_type* left = room_affected_by_spell(&world[kBlazeRoom], SPELL_BLAZE)) {
        affect_remove_room(&world[kBlazeRoom], left);
    }
    const bool sentinel_node_intact = affected_list_holds(&sentinel);
    const bool occupant_node_gone = !affected_list_holds(occupant);
    while (sentinel.affected) {
        affect_remove(&sentinel, sentinel.affected);
    }
    combat_list = original_combat_list;
    combat_next_dude = original_combat_next_dude;
    character_list = original_character_list;

    EXPECT_EQ(char_by_abs_number(occupant_abs_number), nullptr)
        << "extract_char()'s NPC arm must have unregistered the dead occupant's slot; stderr was: " << captured;
    EXPECT_EQ(world[kBlazeRoom].people, nullptr)
        << "extract_char()'s NPC arm must have unlinked the occupant from the room's occupant list";
    EXPECT_TRUE(occupant_node_gone) << "raw_kill's affect strip frees the dead occupant's node";
    EXPECT_TRUE(sentinel_node_intact) << "the walk must reach and keep the node behind the freed one";
    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "nothing may resolve the dead occupant through a stale room number; stderr was: " << captured;
}

// ---------------------------------------------------------------------------
// The recycled abs_number slot (M-2)
// ---------------------------------------------------------------------------
//
// affect_update() snapshots (abs_number, char_data*) pairs BEFORE any body
// runs, then revisits them. char_exists() alone cannot validate such a pair:
// it is one bit, and register_npc_char() hands a freed slot straight to the
// next character its cursor reaches -- a death earlier in this very tick is
// enough. The walk therefore resolves the number back to a live pointer
// (char_by_abs_number()) and requires it to be the SAME pointer the snapshot
// named.
//
// The two tests below pin the two halves of that guard: the slot's NEW owner
// must not make the OLD entry look live, and the old entry's pointer must
// never be dereferenced once it has been freed (an ASan witness -- the plain
// build reads the recycled bytes without complaint).

TEST(AffectUpdateWalk, DoesNotUpdateACharacterWhoseAbsNumberSlotWasRecycled) {
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs, 500);
    victim.abs_number = kRecycledSlot;
    set_char_exists(kRecycledSlot, &victim);

    affected_type victim_af = inert_affect(50);
    affect_to_char(&victim, &victim_af);
    ASSERT_TRUE(affected_list_holds(&victim)) << "the victim must be on the walk";
    ASSERT_NE(victim.affected, nullptr);

    // The victim was extracted, and register_npc_char()'s cursor handed its
    // number to a brand-new mob. The bit is set again -- for somebody else.
    char_data imposter {};
    char_prof_data imposter_profs {};
    make_npc(imposter, imposter_profs, 500);
    ScopedCharExists imposter_exists { imposter, kRecycledSlot };
    ASSERT_EQ(char_by_abs_number(kRecycledSlot), &imposter);
    ASSERT_NE(char_exists(kRecycledSlot), 0) << "the slot's bit is set -- for the new owner";

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();

    const int duration_after = victim.affected ? victim.affected->duration : -1;
    const bool victim_node_gone = !affected_list_holds(&victim);
    while (victim.affected) {
        affect_remove(&victim, victim.affected);
    }

    EXPECT_EQ(duration_after, 50)
        << "the old owner of a recycled slot must not be updated (char_exists() alone would "
           "have ticked it down to 49); stderr was: "
        << captured;
    EXPECT_TRUE(victim_node_gone) << "and its entry is retired as stale instead";
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "the housekeeping arm must not name the stale pointer either; stderr was: " << captured;
    EXPECT_EQ(imposter.affected, nullptr) << "and the new owner gains nothing from the old entry";
}

TEST(AffectUpdateWalk, DoesNotDereferenceAFreedCharacterThroughARecycledSlot) {
    // A character that has already been extracted and freed, with its
    // affected_list node still in place -- the node is fabricated directly
    // (rather than through affect_to_char()) so that freeing the character
    // leaks no pooled affected_type behind it.
    char_data* const freed = new char_data {};
    freed->abs_number = kRecycledSlot;
    universal_list* const stale_node = pool_to_list(&affected_list, &affected_list_pool);
    stale_node->type = TARGET_CHAR;
    stale_node->number = kRecycledSlot;
    stale_node->ptr.ch = freed;
    const void* const freed_address = freed;
    delete freed;

    // ...and the brand-new mob that register_npc_char() handed the same slot.
    char_data imposter {};
    char_prof_data imposter_profs {};
    make_npc(imposter, imposter_profs, 500);
    ScopedCharExists imposter_exists { imposter, kRecycledSlot };

    testing::internal::CaptureStderr();
    affect_update(); // reads `freed->affected` if the guard is only char_exists()
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(affected_list_holds(freed_address))
        << "the freed character's entry must be retired, not walked; stderr was: " << captured;
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "and reported without naming it; stderr was: " << captured;
}
