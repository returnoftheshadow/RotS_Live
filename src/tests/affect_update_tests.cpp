// affect_update()'s walk over the process-global affected_list must survive
// a death that the walk itself triggers.
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
// run for real, following the fireball-fumble precedent at
// src/tests/mage_tests.cpp (make_fireball_caster / test_support::ScopedMobIndex /
// release_fireball_corpse). The fixture reproduces the freed-node order
// literally: [blaze room, dying occupant, sentinel]. Against the unfixed
// walk this is a use-after-free (ASan reports it; the plain build reads
// garbage). Against the fixed walk it asserts the tick completed, the
// occupant died, the sentinel's node is intact, and nothing resolved the
// dead occupant.
#include "../character_identity.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../pkill.h"
#include "../poison.h"
#include "../spells.h"
#include "../test_harness.h"
#include "../utils.h"
#include "character_affect_list_printer.h"
#include "raw_kill_declaration.h"
#include "scoped_character_list.h"
#include "scoped_combat_list.h"
#include "scoped_flee_world.h"
#include "scoped_forced_affect_phase.h"
#include "scoped_mob_index.h"
#include "scoped_player_death_sandbox.h"
#include "scoped_room_occupants.h"
#include "scoped_room_special.h"
#include "scoped_waiting_list.h"
#include "test_affect_support.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"
#include "test_flee_support.h"
#include "test_world_support.h"
#include "test_random_utils.h"
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <initializer_list>
#include <string>
#include <vector>

extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern universal_list* affected_list;
extern universal_list* affected_list_pool;
extern struct skill_data skills[];
ACMD(do_rehash); // act_wiz.cpp: rebuilds affected_list from character_list and the world's rooms
extern struct char_data* combat_list;
extern struct char_data* combat_next_dude;
extern struct player_index_element* player_table;
extern int top_of_p_table;

void do_fame_war_bonuses(struct char_data* ch); // limits.cpp: not declared in limits.h

void affect_update();
void affect_update_person(struct char_data* i, int mode);
void affect_update_room(struct room_data* room);
ASPELL(spell_blaze);

namespace {

// Saves/restores a room's occupant chain so a test's fixture leaves the
// shared world[] state exactly as it found it for later tests in this binary.
struct RoomOccupantGuard {
    int room_number; // which world[] slot this guard owns for the scope
    char_data* original_people; // whatever was linked there before the test

    explicit RoomOccupantGuard(int room)
        : room_number(room), original_people(nullptr) {
        test_support::ensure_test_world(room);
        original_people = world[room].people;
    }
    ~RoomOccupantGuard() { world[room_number].people = original_people; }
    RoomOccupantGuard(const RoomOccupantGuard&) = delete;
    RoomOccupantGuard& operator=(const RoomOccupantGuard&) = delete;
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
    spell_function m_previous; // the cell's prior value
};

// Registers a unique abs_number the way register_npc_char() would for a real
// mob, through the TWO-argument set_char_exists() overload, which
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

// do_fame_war_bonuses()'s pkill_get_rank_by_character() reads
// player_table[player_index]; publish a one-entry table for the scope and
// restore whatever was installed before, mirroring test_support::ScopedMobIndex.
// The destructor runs even if a test assertion fails partway through,
// so a failure here cannot leak the table or leave the process-global
// player_table dangling for later tests in this binary.
class ScopedFameWarPlayerTable {
public:
    explicit ScopedFameWarPlayerTable(int totalrank)
        : m_previous_table(player_table), m_previous_top(top_of_p_table) {
        player_table = new player_index_element[1] {};
        player_table[0].totalrank = totalrank;
        top_of_p_table = 0;
    }
    ~ScopedFameWarPlayerTable() {
        delete[] player_table;
        player_table = m_previous_table;
        top_of_p_table = m_previous_top;
    }
    ScopedFameWarPlayerTable(const ScopedFameWarPlayerTable&) = delete;
    ScopedFameWarPlayerTable& operator=(const ScopedFameWarPlayerTable&) = delete;

private:
    player_index_element* m_previous_table; // whatever this suite found installed
    int m_previous_top; // whatever top_of_p_table held before this scope
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

// A stack-local NPC that never dies in these tests (the sentinel/imposter/victim
// roles below): fill_stack_npc() with intelligence 20 and `hit` as both its
// current and maximum hit points.
void make_npc_with_hit_points(char_data& ch, char_prof_data& profs, int hit) {
    test_support::fill_stack_npc(ch, profs);
    ch.tmpabilities.intel = 20;
    ch.abilities.hit = hit;
    ch.tmpabilities.hit = hit;
}

// Builds the heap-allocated, registered NPC occupant the death pipeline
// needs (see the file comment above): clear_char() + register_npc_char() the
// way the game constructs an NPC, with nr = 0 naming the
// test_support::ScopedMobIndex slot.
char_data* make_blaze_occupant(int hit_points, char* short_descr, int room) {
    char_data* occupant = test_support::allocate_test_character(MOB_ISNPC);
    occupant->specials2.act = MOB_ISNPC;
    occupant->nr = 0; // prototype slot 0 of the scoped one-entry mob_index
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
constexpr int kReRegisteredSlot = MAX_CHARACTERS - 203; // the same-address re-registration pin below
constexpr int kRehashSlot = MAX_CHARACTERS - 204; // the do_rehash rebuild pin below
constexpr int kStaleNodeSlot = MAX_CHARACTERS - 205; // the stale-entry housekeeping pin below

} // namespace

TEST(AffectUpdateWalk, SurvivesAnOccupantDyingToTheBlazeTickItIsProcessing) {
    test_support::ScopedMobIndex prototype_table;
    ScopedBlazeSpellPointer blaze_cell;
    RoomOccupantGuard blaze_room_guard(kBlazeRoom);
    RoomOccupantGuard quiet_room_guard(kQuietRoom);

    char_data sentinel {};
    char_prof_data sentinel_profs {};
    make_npc_with_hit_points(sentinel, sentinel_profs, 500);
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
    // Infravision is inert for the damage path; only each affect's
    // affected_list node matters here and in the pins below.
    affected_type sentinel_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
    affect_to_char(&sentinel, &sentinel_af);
    affected_type occupant_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
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
// The recycled abs_number slot
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
    make_npc_with_hit_points(victim, victim_profs, 500);
    victim.abs_number = kRecycledSlot;
    set_char_exists(kRecycledSlot, &victim);

    affected_type victim_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
    affect_to_char(&victim, &victim_af);
    ASSERT_TRUE(affected_list_holds(&victim)) << "the victim must be on the walk";
    ASSERT_NE(victim.affected, nullptr);

    // The victim was extracted, and register_npc_char()'s cursor handed its
    // number to a brand-new mob. The bit is set again -- for somebody else.
    char_data imposter {};
    char_prof_data imposter_profs {};
    make_npc_with_hit_points(imposter, imposter_profs, 500);
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

// Pin: the slot is recycled to a character allocated at the SAME address, so
// both halves of the pointer-plus-number identity still match the snapshotted
// entry; the registration serial recorded on the node is what retires it.
TEST(AffectUpdateWalk, DoesNotUpdateACharacterWhoseSlotWasReRegisteredAtTheSameAddress) {
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc_with_hit_points(victim, victim_profs, 500);
    victim.abs_number = kReRegisteredSlot;
    set_char_exists(kReRegisteredSlot, &victim);

    affected_type victim_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
    affect_to_char(&victim, &victim_af);
    ASSERT_TRUE(affected_list_holds(&victim)) << "the victim must be on the walk";
    ASSERT_NE(victim.affected, nullptr);

    // Extracted, then a new character registered in the same slot at the
    // same address (the same object stands in for the reused block).
    remove_char_exists(kReRegisteredSlot);
    set_char_exists(kReRegisteredSlot, &victim);
    ASSERT_EQ(char_by_abs_number(kReRegisteredSlot), &victim) << "slot and address both match the entry";

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();

    const int duration_after = victim.affected ? victim.affected->duration : -1;
    const bool victim_node_gone = !affected_list_holds(&victim);
    while (victim.affected) {
        affect_remove(&victim, victim.affected);
    }
    remove_char_exists(kReRegisteredSlot);

    EXPECT_EQ(duration_after, 50)
        << "an entry recorded for an earlier registration must not tick the slot's new owner, "
           "even at the same address; stderr was: "
        << captured;
    EXPECT_TRUE(victim_node_gone) << "and the stale entry is retired";
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "the housekeeping arm must treat the entry as a stranger; stderr was: " << captured;
}

// Pin: the housekeeping arm retires only the node its stale entry was
// captured from. Here a new registration at the same address and slot owns
// its own node, which sits ahead of the stale one. Matching on pointer and
// number alone would retire the live node and leave the character's affects
// unticked for good.
TEST(AffectUpdateWalk, RetiringAStaleEntryKeepsTheSameAddressNewRegistrationsNode) {
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc_with_hit_points(victim, victim_profs, 500);
    victim.abs_number = kStaleNodeSlot;
    set_char_exists(kStaleNodeSlot, &victim);
    const long stale_serial = victim.registration_serial;

    affected_type victim_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
    affect_to_char(&victim, &victim_af);

    remove_char_exists(kStaleNodeSlot);
    set_char_exists(kStaleNodeSlot, &victim);
    const long live_serial = victim.registration_serial;
    universal_list* const live_node = pool_to_list(&affected_list, &affected_list_pool);
    live_node->type = TARGET_CHAR;
    live_node->number = kStaleNodeSlot;
    live_node->ptr.ch = &victim;
    live_node->serial = live_serial;
    ASSERT_EQ(affected_list, live_node) << "the live node must precede the stale one";

    const auto nodes_with_serial = [&victim](long serial) -> int {
        int count = 0;
        for (universal_list* node = affected_list; node; node = node->next) {
            if (node->type == TARGET_CHAR && node->ptr.ch == &victim && node->serial == serial) {
                ++count;
            }
        }
        return count;
    };

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();

    const int live_nodes_after = nodes_with_serial(live_serial);
    const int stale_nodes_after = nodes_with_serial(stale_serial);
    const int duration_after = victim.affected ? victim.affected->duration : -1;
    while (victim.affected) {
        affect_remove(&victim, victim.affected);
    }
    remove_char_exists(kStaleNodeSlot);

    EXPECT_EQ(live_nodes_after, 1) << "the new registration's node must survive; stderr was: " << captured;
    EXPECT_EQ(stale_nodes_after, 0) << "and the stale entry's own node is the one retired";
    EXPECT_EQ(duration_after, 49) << "the live entry still ticked the character once";
}

TEST(AffectUpdateWalk, DoesNotDereferenceAFreedCharacterThroughARecycledSlot) {
    // A character that has already been extracted and freed, with its
    // affected_list node still in place -- the node is fabricated directly
    // (rather than through affect_to_char()) so that freeing the character
    // leaks no pooled affected_type behind it.
    // Deliberately new/delete: free_char would unregister kRecycledSlot, the slot this test then reuses.
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
    make_npc_with_hit_points(imposter, imposter_profs, 500);
    ScopedCharExists imposter_exists { imposter, kRecycledSlot };

    testing::internal::CaptureStderr();
    affect_update(); // reads `freed->affected` if the guard is only char_exists()
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(affected_list_holds(freed_address))
        << "the freed character's entry must be retired, not walked; stderr was: " << captured;
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "and reported without naming it; stderr was: " << captured;
}

// Pin: `rehash` throws the whole affected_list away and rebuilds it from
// character_list; every rebuilt character node must carry that character's
// registration serial, or affect_update() would treat every affected
// character in the game as a stranger and evict them on the next tick.
// top_of_world is parked at -1 for the call so the rebuild's room walk does
// not read rooms other suites in this binary may have left behind.
TEST(AffectUpdateWalk, RehashRebuildsCharacterEntriesWithTheirRegistrationSerial) {
    char_data victim {};
    char_prof_data victim_profs {};
    make_npc_with_hit_points(victim, victim_profs, 500);
    ScopedCharExists victim_exists { victim, kRehashSlot };
    ASSERT_NE(victim.registration_serial, 0L) << "registration must have stamped a serial";

    affected_type victim_af = test_support::inert_affect(SPELL_INFRAVISION, 50);
    affect_to_char(&victim, &victim_af);
    ASSERT_TRUE(affected_list_holds(&victim));

    char immortal_name[] = "test_rehasher";
    char_data immortal {};
    immortal.player.name = immortal_name;
    immortal.player.level = LEVEL_GRGOD;

    char_data* const previous_character_list = character_list;
    const int previous_top_of_world = top_of_world;
    character_list = &victim;
    victim.next = nullptr;
    top_of_world = -1;
    char empty_argument[] = "";
    testing::internal::CaptureStderr();
    do_rehash(&immortal, empty_argument, nullptr, 0, 0);
    testing::internal::GetCapturedStderr();
    top_of_world = previous_top_of_world;
    character_list = previous_character_list;

    universal_list* rebuilt = nullptr;
    for (universal_list* node = affected_list; node; node = node->next) {
        if (node->type == TARGET_CHAR && node->ptr.ch == &victim) {
            rebuilt = node;
        }
    }
    ASSERT_NE(rebuilt, nullptr) << "rehash must have rebuilt the victim's entry";
    EXPECT_EQ(rebuilt->number, victim.abs_number);
    EXPECT_EQ(rebuilt->serial, victim.registration_serial) << "the rebuilt node must carry the live serial";

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();

    const int duration_after = victim.affected ? victim.affected->duration : -1;
    const bool still_listed = affected_list_holds(&victim);
    while (victim.affected) {
        affect_remove(&victim, victim.affected);
    }

    EXPECT_EQ(duration_after, 49) << "the rebuilt entry must tick the character, not evict it; stderr was: " << captured;
    EXPECT_TRUE(still_listed);
    EXPECT_EQ(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "a rebuilt entry must not be treated as a stranger; stderr was: " << captured;
}

// affect_update_person()'s expiry branch used to read af->type after
// affect_remove_notify()/affect_remove() had already freed that node
// (put_to_affected_type_pool() -- handler.cpp -- calls free()), a
// heap-use-after-free ASan caught on the SPELL_ANGER check that follows
// the removal. It fires whenever any affect expires on a person tick; CI
// run 35556262343 caught it via
// test_poisoner_who_quits_before_the_lethal_tick_is_credited_with_nothing,
// which happens to expire a SPELL_ANGER affect along the way. This test
// expires a SPELL_ANGER affect directly and checks the post-removal state
// the freed read used to corrupt: attacked_level reset and an empty
// affected list.
TEST(AffectUpdatePerson, ExpiringAngerResetsAttackedLevelWithoutTouchingTheFreedNode) {
    char_data* character = test_support::allocate_test_character(MOB_VOID);
    character->player.name = strdup("test_anger_target");
    character->player.level = 30;
    character->specials.attacked_level = 5;

    affected_type anger_affect {};
    anger_affect.type = SPELL_ANGER;
    anger_affect.duration = 0; // already expired: the loop's else/removal branch runs on the first tick
    anger_affect.modifier = 0;
    anger_affect.location = APPLY_NONE;
    anger_affect.bitvector = 0;
    affect_to_char(character, &anger_affect);

    {
        // The forced phase expires this on the very first call regardless of SPELL_ANGER's own
        // is_fast/time_phase configuration.
        const test_support::ScopedForcedAffectPhase forced_phase;
        affect_update_person(character, 0);
    }

    EXPECT_EQ(character->specials.attacked_level, 0);
    EXPECT_EQ(character->affected, nullptr);

    test_support::release_test_character(character);
}

// do_fame_war_bonuses() (limits.cpp) used to write pkaff->duration, read and
// write pkaff->modifier, and test `if (!pkaff)` -- all after
// affect_remove(ch, pkaff) had already freed that node via
// put_to_affected_type_pool() (handler.cpp). With the pointer nulled instead,
// the same "dropped bonuses" branch fell through to the re-creation branch and
// handed the player a fresh affect for the invalid rank on every fast-update pass.
// The branch now records the rank and returns. Both invalid ranks are driven:
// unranked (ranking 0, tier 0) and below the table (ranking 11, tier 4).
void expect_dropped_rank_removes_fame_war(int totalrank, int expected_ranking) {
    char_data* character = test_support::allocate_test_character(MOB_VOID);
    character->player.name = strdup("test_fame_war_target");
    character->player.level = 30;
    character->player_index = 0;
    character->player.ranking = 3; // the valid rank the affect below was earned at

    // pkill_get_rank_by_character() reads player_table[player_index]; give it
    // a one-entry table whose totalrank makes the recomputed ranking invalid.
    // The guard restores the previous table on scope exit even if an
    // assertion below fails.
    ScopedFameWarPlayerTable player_table_guard(totalrank);

    affected_type old_fame_war {};
    old_fame_war.type = SPELL_FAME_WAR;
    old_fame_war.duration = 50;
    old_fame_war.modifier = 3; // the tier for the rank the player has since lost
    old_fame_war.location = APPLY_NONE;
    old_fame_war.bitvector = 0;
    affect_to_char(character, &old_fame_war);

    do_fame_war_bonuses(character);

    // EXPECT (not ASSERT) so release_test_character below always runs.
    EXPECT_EQ(character->affected, nullptr) << "the dropped affect must not be re-created";
    EXPECT_EQ(character->player.ranking, expected_ranking);

    test_support::release_test_character(character);
}

TEST(DoFameWarBonuses, UnrankedPlayerLosesTheAffectAndGetsNoReplacement) {
    expect_dropped_rank_removes_fame_war(PKILL_UNRANKED, 0);
}

TEST(DoFameWarBonuses, RankBelowTheTableLosesTheAffectInsteadOfTierFour) {
    expect_dropped_rank_removes_fame_war(10, 11); // totalrank 10 -> ranking 11 > MAX_RANK
}

namespace {

// Every stat a fame-war bonus can change, in each set it writes.
struct fame_war_stats {
    char_ability_data constant; // constabilities: con, hit, mana and move bonuses land here
    char_ability_data maximum;  // abilities: the same bonuses, and recalc_abilities()'s rebuild
    char_ability_data current;  // tmpabilities: the hit, mana and move bonuses
    int offensive_bonus;        // points.OB: the warrior and ranger bonus
    int spell_penetration;      // points.spell_pen: the mage bonus
    int damage;                 // points.damage
};

fame_war_stats capture_fame_war_stats(const char_data& character) {
    return fame_war_stats{character.constabilities, character.abilities, character.tmpabilities,
                          character.points.OB, character.points.spell_pen,
                          character.points.damage};
}

void expect_same_ability_set(const char_ability_data& before, const char_ability_data& after,
                             const char* set_name) {
    EXPECT_EQ(after.con, before.con) << set_name;
    EXPECT_EQ(after.hit, before.hit) << set_name;
    EXPECT_EQ(after.mana, before.mana) << set_name;
    EXPECT_EQ(after.move, before.move) << set_name;
}

} // namespace

// Losing a rank takes the bonuses off exactly once: a tier-3 warrior's +1 constitution, +5 hit
// points and +5 offensive bonus are applied by the real path and then removed, leaving every
// stat where it stood before the bonus. Removing them twice would leave the character 1
// constitution, 5 hit points and 5 offensive bonus short.
TEST(DoFameWarBonuses, ADroppedRankReturnsEveryStatToItsPreBonusValue) {
    char_data* character = test_support::allocate_test_character(MOB_VOID);
    character->player.name = strdup("test_fame_war_warrior");
    character->player.level = 30;
    character->player_index = 0;
    character->player.ranking = 0;
    character->profs->prof_level[PROF_WARRIOR] = 30; // the bonus follows the highest profession
    for (char_ability_data* stats :
         {&character->constabilities, &character->abilities, &character->tmpabilities}) {
        stats->con = 15;
        stats->hit = 20;
        stats->mana = 20;
        stats->move = 20;
    }
    // Start from a recalculated state, as the drop's own recalc_abilities() leaves it, and
    // wounded: at full health, recalc_abilities() caps the granted current hit points at the
    // new maximum, which grows by less than the +5 (constant hit points scale by con / 20).
    recalc_abilities(character);
    character->tmpabilities.hit = character->abilities.hit - 10;
    const fame_war_stats before = capture_fame_war_stats(*character);

    ScopedFameWarPlayerTable player_table_guard(2); // totalrank 2 -> ranking 3 -> tier 3
    do_fame_war_bonuses(character);
    const affected_type* const fame_war = affected_by_spell(character, SPELL_FAME_WAR);
    ASSERT_NE(fame_war, nullptr) << "precondition: a valid rank grants the fame-war affect";
    EXPECT_EQ(fame_war->modifier, 3) << "precondition: ranking 3 is tier 3";
    EXPECT_EQ(character->constabilities.con, before.constant.con + 1) << "precondition: tier 3 grants +1 con";
    EXPECT_EQ(character->points.OB, before.offensive_bonus + 5) << "precondition: tier 3 grants +5 OB";

    player_table[0].totalrank = PKILL_UNRANKED;
    do_fame_war_bonuses(character);

    EXPECT_EQ(affected_by_spell(character, SPELL_FAME_WAR), nullptr) << "the rank was lost";
    const fame_war_stats after = capture_fame_war_stats(*character);
    expect_same_ability_set(before.constant, after.constant, "constabilities");
    expect_same_ability_set(before.maximum, after.maximum, "abilities");
    expect_same_ability_set(before.current, after.current, "tmpabilities");
    EXPECT_EQ(after.offensive_bonus, before.offensive_bonus);
    EXPECT_EQ(after.spell_penetration, before.spell_penetration);
    EXPECT_EQ(after.damage, before.damage);

    test_support::release_test_character(character);
}

// ---------------------------------------------------------------------------
// Tick bodies that run game code
// ---------------------------------------------------------------------------
//
// Fear flees, activity runs the mob's own special, and asphyxiation and poison deal damage, which
// can set off a wimpy flee or break a sanctuary. That code can kill the character or remove any of
// its affects, the ticking one and the one the walk saved as next included, so
// affect_update_person() stops the pass once the character no longer resolves or has lost an
// affect. Each character below carries a second affect behind the ticking one, so the saved next
// node is a real node. The characters are heap characters registered the way the game registers
// them: a death frees them for real, where AddressSanitizer sees any later use, and a survivor
// keeps resolving.

namespace {

// Rooms these tests own in the shared test world, beside flee_path_tests.cpp's 1015 and 1016 and
// below the 1024 rooms gtest_main.cpp allocates. A flee leaves the origin east.
constexpr int kTickOriginRoom = 1017;
constexpr int kTickDestinationRoom = 1018;
constexpr int kTickFleeDirection = EAST;

// Long enough that the ticking affect does not expire on the one tick each test runs.
constexpr int kTickingAffectDuration = 10;

// The affect behind the ticking one. Its arm does nothing, so a tick only counts it down.
constexpr int kSecondAffectType = SPELL_INFRAVISION;
constexpr int kSecondAffectDuration = 50;

// The affect an entry special adds in the pin that an addition does not stop the pass.
constexpr int kAddedAffectType = SPELL_ARMOR;
constexpr int kAddedAffectDuration = 30;

// Above 20, so the asphyxiation tick deals modifier / 5 damage, and not above 40, the death
// threshold outside the underwater zone, so it does not kill outright.
constexpr int kSuffocationModifier = 30;
// Hit points the suffocation damage (kSuffocationModifier / 5) takes below a fifth of
// kFleeTestHitPoints, where a MOB_WIMPY NPC flees, alive and awake.
constexpr int kSuffocatingHitPoints = 25;

// A sanctuary modifier below 0: check_sanctuary() breaks such a sanctuary on any hit by an
// attacker of alignment 0, which takes nothing off it.
constexpr int kBrokenSanctuaryModifier = -1;

// Adds an inert affect of `affect_type` at the front of `character`'s list.
void add_inert_affect(char_data& character, int affect_type, int duration) {
    affected_type affect = test_support::inert_affect(affect_type, duration);
    affect_to_char(&character, &affect);
}

// Removes `character`'s affect of `affect_type`, as a special that dispels it would. Reports a test
// failure when it has none.
void remove_affect_of_type(char_data& character, int affect_type) {
    affected_type* const affect = affected_by_spell(&character, affect_type);
    if (affect == nullptr) {
        ADD_FAILURE() << "remove_affect_of_type: no affect of type " << affect_type;
        return;
    }
    affect_remove(&character, affect);
}

// The types on `character`'s affect list, head first.
std::vector<int> affect_types_of(const char_data& character) {
    std::vector<int> types;
    for (const affected_type* affect = character.affected; affect != nullptr;
         affect = affect->next) {
        types.push_back(affect->type);
    }
    return types;
}

// A registered MOB_WIMPY NPC with kSuffocatingHitPoints, an asphyxiation of kSuffocationModifier at
// the head of its list and the second affect behind it. It suffocates only in a room it cannot
// breathe in.
[[nodiscard]] char_data* make_suffocating_wimpy_npc(char* short_description) {
    char_data* const npc = test_support::make_registered_test_npc(short_description);
    SET_BIT(npc->specials2.act, MOB_WIMPY);
    npc->tmpabilities.hit = kSuffocatingHitPoints;
    add_inert_affect(*npc, kSecondAffectType, kSecondAffectDuration);
    affected_type asphyxiation =
        test_support::inert_affect(SPELL_ASPHYXIATION, kTickingAffectDuration);
    asphyxiation.modifier = kSuffocationModifier;
    affect_to_char(npc, &asphyxiation);
    return npc;
}

// How many times lethal_activity_special() has killed its host.
int g_activity_special_kills = 0;

// A mob special that kills its host when mob activity runs it (SPECIAL_SELF) and reports that
// handled. Every other call, such as the death's own SPECIAL_DEATH probe, passes through. A null
// host is reported as a test failure.
int lethal_activity_special(char_data* host, char_data* /*character*/, int /*command*/,
                            char* /*argument*/, int callflag, waiting_type* /*wait_data*/) {
    if (callflag != SPECIAL_SELF) {
        return 0;
    }
    if (host == nullptr) {
        ADD_FAILURE() << "lethal_activity_special: null host";
        return 0;
    }
    ++g_activity_special_kills;
    raw_kill(host, nullptr, 0);
    return 1;
}

} // namespace

// Each test ticks one character once, with slow affects forced to tick too, in two rooms a flee
// can cross. The fixture holds what a death needs and releases what it leaves behind.
class AffectUpdateTickGuard : public ::testing::Test {
  protected:
    AffectUpdateTickGuard()
        : m_flee_world(kTickOriginRoom, kTickDestinationRoom, kTickFleeDirection) {}

    void TearDown() override {
        clear_test_random_values();
        test_support::release_room_objects(kTickOriginRoom);
        test_support::release_room_objects(kTickDestinationRoom);
    }

    // mob_index[0], the test NPCs' prototype.
    test_support::ScopedMobIndex m_prototype_table;
    // Keeps the tests' fights and other suites' stale fighters apart.
    test_support::ScopedCombatList m_combat_list;
    // Keeps other suites' stale entries away from extract_char()'s walk.
    test_support::ScopedWaitingList m_waiting_list;
    // The origin, the destination, one exit east between them, and zone 0.
    test_support::ScopedFleeWorld m_flee_world;
    // Lets every slow affect tick on the test's affect_update_person() call.
    test_support::ScopedForcedAffectPhase m_forced_phase;
};

// Pins the fear arm's check: the flee ends in a room whose entry special kills the NPC.
TEST_F(AffectUpdateTickGuard, AnNpcKilledDuringItsFearFleeEndsTheTick) {
    char name[] = "a frightened test orc";
    char_data* const npc = test_support::make_registered_test_npc(name);
    add_inert_affect(*npc, kSecondAffectType, kSecondAffectDuration);
    add_inert_affect(*npc, SPELL_FEAR, kTickingAffectDuration);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kTickDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     raw_kill(character, nullptr, 0);
                                                 });
    test_support::queue_successful_flee_rolls(kTickFleeDirection);

    affect_update_person(npc, 0);
    // The NPC has been freed: below it is only resolved, never read.

    EXPECT_EQ(entries, 1);
    EXPECT_EQ(npc_identity.resolve(), nullptr);
    EXPECT_EQ(world[kTickOriginRoom].people, nullptr);
    EXPECT_EQ(world[kTickDestinationRoom].people, nullptr);
    test_support::release_survivor(npc_identity);
}

// The same flee by a player with a descriptor: it dies, respawns and still resolves, but its
// death stripped the fear and the affect behind it, so the tick stops.
TEST_F(AffectUpdateTickGuard, APlayerKilledDuringItsFearFleeRespawnsAndItsTickEnds) {
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kTickOriginRoom);
    descriptor_data descriptor{};
    char_data* const player = test_support::make_linked_test_player(descriptor, "Frightened");
    add_inert_affect(*player, kSecondAffectType, kSecondAffectDuration);
    add_inert_affect(*player, SPELL_FEAR, kTickingAffectDuration);
    const character_identity player_identity = character_identity::capture(*player);
    test_support::ScopedCharacterList characters({player});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {player});
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kTickDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     raw_kill(character, nullptr, 0);
                                                 });
    test_support::queue_successful_flee_rolls(kTickFleeDirection);

    affect_update_person(player, 0);

    EXPECT_EQ(entries, 1);
    char_data* const respawned = player_identity.resolve();
    EXPECT_EQ(respawned, player) << "a respawned player keeps its registration";
    if (respawned != nullptr) {
        EXPECT_EQ(respawned->in_room, kTickOriginRoom) << "the player respawns in its start room";
        EXPECT_EQ(affect_types_of(*respawned), std::vector<int>{}) << "death strips every affect";
    }
    test_support::release_survivor(player_identity);
    test_support::release_large_output(descriptor);
}

// A fear flee the NPC survives, while the destination's entry special removes the affect behind
// the fear: the tick stops before the fear's save, which would lower the fear's modifier.
TEST_F(AffectUpdateTickGuard, AFearFleeThatRemovesTheNextAffectEndsTheTick) {
    char name[] = "a frightened test orc";
    char_data* const npc = test_support::make_registered_test_npc(name);
    add_inert_affect(*npc, kSecondAffectType, kSecondAffectDuration);
    add_inert_affect(*npc, SPELL_FEAR, kTickingAffectDuration);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});
    int entries = 0;
    test_support::ScopedRoomSpecial dispelling_entry(
        kTickDestinationRoom, SPECIAL_ENTER, [&entries](char_data* character) -> void {
            ++entries;
            remove_affect_of_type(*character, kSecondAffectType);
        });
    test_support::queue_successful_flee_rolls(kTickFleeDirection);

    affect_update_person(npc, 0);

    EXPECT_EQ(entries, 1);
    ASSERT_EQ(npc_identity.resolve(), npc) << "the NPC survives its flee";
    EXPECT_EQ(npc->in_room, kTickDestinationRoom);
    EXPECT_EQ(affect_types_of(*npc), std::vector<int>{SPELL_FEAR});
    const affected_type* const fear = affected_by_spell(npc, SPELL_FEAR);
    ASSERT_NE(fear, nullptr);
    EXPECT_EQ(fear->duration, kTickingAffectDuration - 1);
    EXPECT_EQ(fear->modifier, 0) << "the tick must stop before the fear's save";
    test_support::release_survivor(npc_identity);
}

// Suffocation's damage leaves a MOB_WIMPY NPC wounded enough to flee, and the flee ends in a room
// whose entry special kills it. damage() then reports the victim gone, so the arm's existing
// return after a lethal damage() stops the tick; damage_credited()'s check after the flee is what
// this pins.
TEST_F(AffectUpdateTickGuard, AWimpyNpcKilledFleeingItsOwnSuffocationEndsTheTick) {
    char name[] = "a suffocating test orc";
    char_data* const npc = make_suffocating_wimpy_npc(name);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});
    world[kTickOriginRoom].sector_type = SECT_UNDERWATER; // the flee world restores the room
    int entries = 0;
    test_support::ScopedRoomSpecial lethal_entry(kTickDestinationRoom, SPECIAL_ENTER,
                                                 [&entries](char_data* character) -> void {
                                                     ++entries;
                                                     raw_kill(character, nullptr, 0);
                                                 });
    test_support::queue_east_everywhere_rolls();

    affect_update_person(npc, 0);

    EXPECT_EQ(entries, 1);
    EXPECT_EQ(npc_identity.resolve(), nullptr);
    EXPECT_EQ(world[kTickOriginRoom].people, nullptr);
    EXPECT_EQ(world[kTickDestinationRoom].people, nullptr);
    test_support::release_survivor(npc_identity);
}

// The same wimpy flee, survived, while the destination's entry special removes the affect behind
// the asphyxiation: damage() returns 0, and the tick stops before the arm's move loss and before
// it reaches the removed node. The special runs after the flee has paid its own move cost.
TEST_F(AffectUpdateTickGuard, AWimpyNpcThatFleesItsOwnSuffocationAndLosesTheNextAffectEndsTheTick) {
    char name[] = "a suffocating test orc";
    char_data* const npc = make_suffocating_wimpy_npc(name);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});
    world[kTickOriginRoom].sector_type = SECT_UNDERWATER; // the flee world restores the room
    int entries = 0;
    int move_points_on_entry = -1;
    test_support::ScopedRoomSpecial dispelling_entry(
        kTickDestinationRoom, SPECIAL_ENTER,
        [&entries, &move_points_on_entry](char_data* character) -> void {
            ++entries;
            move_points_on_entry = GET_MOVE(character);
            remove_affect_of_type(*character, kSecondAffectType);
        });
    test_support::queue_east_everywhere_rolls();

    affect_update_person(npc, 0);

    EXPECT_EQ(entries, 1);
    ASSERT_EQ(npc_identity.resolve(), npc) << "the NPC survives its flee";
    EXPECT_EQ(npc->in_room, kTickDestinationRoom) << "the wimpy flee went through";
    EXPECT_EQ(affect_types_of(*npc), std::vector<int>{SPELL_ASPHYXIATION});
    EXPECT_EQ(GET_MOVE(npc), move_points_on_entry) << "the tick must stop before the move loss";
    test_support::release_survivor(npc_identity);
}

// The poison tick's damage runs check_sanctuary() with the victim as its own attacker, and the
// sanctuary behind the poison breaks: the tick stops, and nothing else happens to the NPC.
TEST_F(AffectUpdateTickGuard, APoisonTickThatBreaksTheSanctuaryBehindItEndsTheTick) {
    char name[] = "a poisoned test orc";
    char_data* const npc = test_support::make_registered_test_npc(name);
    GET_ALIGNMENT(npc) = 0;
    affected_type sanctuary = test_support::inert_affect(SPELL_SANCTUARY, kSecondAffectDuration);
    sanctuary.modifier = kBrokenSanctuaryModifier;
    sanctuary.bitvector = AFF_SANCTUARY;
    affect_to_char(npc, &sanctuary);
    affected_type poison = consumed_poison_affect(kTickingAffectDuration);
    affect_to_char(npc, &poison);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});

    affect_update_person(npc, 0);

    ASSERT_EQ(npc_identity.resolve(), npc) << "one poison tick is not lethal";
    EXPECT_EQ(npc->in_room, kTickOriginRoom) << "nothing made the NPC flee";
    EXPECT_EQ(affect_types_of(*npc), std::vector<int>{SPELL_POISON}) << "the sanctuary is broken";
    EXPECT_FALSE(IS_AFFECTED(npc, AFF_SANCTUARY));
    test_support::release_survivor(npc_identity);
}

// Pin: an affect added during a tick body removes nothing, so the pass goes on. The destination's
// entry special adds one ahead of the fear; the fear's save runs, and the affect behind the fear
// still ticks. The save's own draws come after the queued flee and only decide the fear's duration.
TEST_F(AffectUpdateTickGuard, AnAffectAddedDuringAFearFleeDoesNotEndTheTick) {
    char name[] = "a frightened test orc";
    char_data* const npc = test_support::make_registered_test_npc(name);
    add_inert_affect(*npc, kSecondAffectType, kSecondAffectDuration);
    add_inert_affect(*npc, SPELL_FEAR, kTickingAffectDuration);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});
    int entries = 0;
    test_support::ScopedRoomSpecial granting_entry(
        kTickDestinationRoom, SPECIAL_ENTER, [&entries](char_data* character) -> void {
            ++entries;
            add_inert_affect(*character, kAddedAffectType, kAddedAffectDuration);
        });
    test_support::queue_successful_flee_rolls(kTickFleeDirection);

    affect_update_person(npc, 0);

    EXPECT_EQ(entries, 1);
    ASSERT_EQ(npc_identity.resolve(), npc) << "the NPC survives its flee";
    const std::vector<int> expected_types{kAddedAffectType, SPELL_FEAR, kSecondAffectType};
    EXPECT_EQ(affect_types_of(*npc), expected_types);
    const affected_type* const fear = affected_by_spell(npc, SPELL_FEAR);
    const affected_type* const second = affected_by_spell(npc, kSecondAffectType);
    const affected_type* const added = affected_by_spell(npc, kAddedAffectType);
    ASSERT_NE(fear, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(fear->modifier, -2) << "the fear's save ran";
    EXPECT_EQ(second->duration, kSecondAffectDuration - 1) << "the affect behind still ticks";
    EXPECT_EQ(added->duration, kAddedAffectDuration) << "an affect ahead waits for the next tick";
    test_support::release_survivor(npc_identity);
}

// Pins the activity arm's check: mob activity runs the NPC's own special, which kills it, and the
// arm must not fall through into confusion's check, which reads the character.
TEST_F(AffectUpdateTickGuard, AnNpcKilledByItsOwnSpecialDuringActivityEndsTheTick) {
    g_activity_special_kills = 0;
    // The fixture's prototype entry, discarded with the fixture.
    mob_index[0].func = lethal_activity_special;
    char name[] = "an active test orc";
    char keywords[] = "orc active";
    char_data* const npc = test_support::make_registered_test_npc(name);
    npc->player.name = keywords; // mob activity's special lookup searches them
    SET_BIT(npc->specials2.act, MOB_SPEC);
    npc->specials.default_pos = POSITION_STANDING; // so mob activity leaves its position alone
    add_inert_affect(*npc, kSecondAffectType, kSecondAffectDuration);
    add_inert_affect(*npc, SPELL_ACTIVITY, kTickingAffectDuration);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants origin_occupants(kTickOriginRoom, {npc});

    affect_update_person(npc, 0);

    EXPECT_EQ(g_activity_special_kills, 1);
    EXPECT_EQ(npc_identity.resolve(), nullptr);
    EXPECT_EQ(world[kTickOriginRoom].people, nullptr);
    test_support::release_survivor(npc_identity);
}
