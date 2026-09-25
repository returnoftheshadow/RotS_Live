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
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../pkill.h"
#include "../spells.h"
#include "../test_harness.h"
#include "../utils.h"
#include "scoped_mob_index.h"
#include "test_affect_support.h"
#include "test_character_support.h"
#include "test_world_support.h"
#include "test_random_utils.h"
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>

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
// roles below): make_stack_npc() with intelligence 20 and `hit` as both its
// current and maximum hit points.
void make_npc_with_hit_points(char_data& ch, char_prof_data& profs, int hit) {
    test_support::make_stack_npc(ch, profs);
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

    // harness_force_affect_phase makes a slow affect tick unconditionally
    // (test_harness.h), so this expires on the very first call regardless
    // of SPELL_ANGER's own is_fast/time_phase configuration.
    const int previous_force_phase = harness_force_affect_phase;
    harness_force_affect_phase = 1;
    affect_update_person(character, 0);
    harness_force_affect_phase = previous_force_phase;

    EXPECT_EQ(character->specials.attacked_level, 0);
    EXPECT_EQ(character->affected, nullptr);

    test_support::release_test_character(character);
}

// do_fame_war_bonuses() (limits.cpp) used to write pkaff->duration, read and
// write pkaff->modifier, and test `if (!pkaff)` -- all after
// affect_remove(ch, pkaff) had already freed that node via
// put_to_affected_type_pool() (handler.cpp). With the pointer nulled instead,
// the same "dropped bonuses" branch fell through to the re-creation branch and
// handed the player a fresh affect for the invalid rank on every hourly pass.
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
