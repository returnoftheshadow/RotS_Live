// room_affect_tick() replaces affect_update_room()'s
// self-re-cast arm -- the historical tick that ran
// `(skills[loc].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`, i.e.
// re-cast the room's spell with the OCCUPANT standing in for the caster. Every
// formula input therefore came from the victim: a level-1 mob walking into a
// grandmaster's blaze took a level-1 blaze, and nobody was ever credited with
// the kill.
//
// This depot has no extract_char test seam and no rots::entity hook system, so this file
// reuses the depot's own established idioms rather than porting the modern
// suite's fixture-by-fixture: the heap-NPC/register_npc_char()/ScopedMobIndex
// death-path idiom (fight_credit_tests.cpp), the gear-move oracle for "who was
// credited without a kill-contributor ledger to read back"
// (fight_credit_tests.cpp), the (room, spell) caster-store fixture
// (room_affect_caster_tests.cpp's RoomAffectGuard), and the descriptor output
// capture used to assert on act()/send_to_char() text (spell_pa_tests.cpp).
//
// WHAT THESE TESTS PROVE. A "the tick matches a live re-cast" equivalence test
// would be vacuous: a re-cast and the tick both run the snapshot-form
// helpers, so the two agree no matter which fields the tick reads. The blaze pin below therefore varies the RECORDED
// snapshot away from the caster's current (live, wrecked) stats and shows the
// tick still follows the recording. The poison/haze/mist pins pick caster
// fields (willpower/perception for saves_poison(), multiples of 25 for the
// mage/mystic level remainder roll) that make the formula's OWN outcome
// deterministic without needing to predict every draw inside damage_credited()
// -- so the assertions read the tick's own state changes (affect duration,
// resolve_poisoner(), room_affect_caster()) rather than an exact hit-point
// count wrung out of the whole damage() pipeline.

#include "../big_brother.h"
#include "../caster_snapshot.h"
#include "../character_identity.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../poison.h"
#include "../poison_origin.h"
#include "../room_affect_tick.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "carried_gear.h"
#include "scoped_character_list.h"
#include "scoped_combat_list.h"
#include "scoped_exploit_type_capture.h"
#include "scoped_flee_world.h"
#include "scoped_mob_index.h"
#include "scoped_player_death_sandbox.h"
#include "scoped_room_occupants.h"
#include "scoped_waiting_list.h"
#include "test_character_support.h"
#include "test_descriptor_support.h"
#include "test_flee_support.h"
#include "test_random_utils.h"
#include "test_spell_support.h"
#include "test_world_support.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using test_support::CarriedGear;
using test_support::ensure_test_world;
using test_support::prepare_capture_descriptor;
using test_support::ScopedCharExists;
using test_support::ScopedCombatList;
using test_support::ScopedMobIndex;

extern struct room_data world;
extern struct weather_data weather_info;
extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct skill_data skills[];

void affect_update_room(struct room_data* room);

namespace {

// Rooms this suite claims within the shared test-binary world[] -- high,
// out-of-band values whose caster-store keys (2000 + N) are distinct from every
// other suite's (affect_update_tests: 27/28; mage_tests: up to 32;
// fight_credit_tests: 900-908; room_affect_caster_tests: 950-953;
// interpre_account_menu/spell_pa/db_loader_tests: 1200/3001/3002). The in_room
// values 991 and 1000-1001 overlap act_wiz_tests.cpp's world[991] and
// summon_targeting_tests.cpp's world[1000-1001]; those suites restore their rooms.
constexpr int kBlazeRoomA = 960;
constexpr int kBlazeRoomB = 961;
constexpr int kPoisonRoom = 962;
constexpr int kPoisonNoneRoom = 963;
constexpr int kPoisonNoRecordRoom = 964;
constexpr int kPoisonSavedPresentRoom = 965;
constexpr int kPoisonSavedAwayRoom = 966;
constexpr int kPoisonSavedNoCasterRoom = 967;
constexpr int kHazeRoom = 968;
constexpr int kMistMainRoom = 969;
constexpr int kMistAdjacentRoom = 970;
constexpr int kMistStrongerAdjacentRoom = 971;
constexpr int kUnknownSpellRoom = 972;
constexpr int kFallbackRoom = 973;
constexpr int kNoFallbackRoom = 974;
constexpr int kMistMoveSourceRoom = 975;
constexpr int kMistMoveDestRoom = 976;
constexpr int kAwayRoom = 977; // a caster's "somewhere else" room for the presence pins

// Rooms exercising the CASTING arms (spell_blaze,
// spell_haze, spell_poison's room arm, spell_mist_of_baazunga) directly,
// rather than room_affect_tick() -- continuing this suite's own room band.
constexpr int kBlazeCastRoom = 978;
constexpr int kBlazeWeakerRecastRoom = 979;
constexpr int kBlazeStrongerRecastRoom = 980;
constexpr int kHazeCastRoom = 981;
constexpr int kHazeWeakerRecastRoom = 982;
constexpr int kHazeStrongerRecastRoom = 983;
constexpr int kPoisonCastRoom = 984;
constexpr int kPoisonWeakerRecastRoom = 985;
constexpr int kPoisonStrongerRecastRoom = 986;
constexpr int kMistCastMainRoom = 987;
constexpr int kMistCastAdjacentRoom = 988;
constexpr int kMistRenewMainRoom = 989;
constexpr int kMistRenewAdjacentRoom = 990;
constexpr int kPoisonSavedBlindRoom = 991;
// Rooms for the mist's spread-falloff pins (generation carried in `counter`).
constexpr int kMistFalloffMainRoom = 992;
constexpr int kMistFalloffSeedRoom = 993;
constexpr int kMistFalloffDeepRoom = 994;
constexpr int kMistFalloffDeepSeedRoom = 995;
constexpr int kMistFalloffRenewMainRoom = 996;
constexpr int kMistFalloffRenewNeighbourRoom = 997;
// Rooms for the cast arm's spread-falloff pins.
constexpr int kMistCastWeakMainRoom = 998;
constexpr int kMistCastWeakAdjacentRoom = 999;
constexpr int kMistCastGenerationMainRoom = 1000;
constexpr int kMistCastGenerationAdjacentRoom = 1001;
// Rooms for the drift-then-spread pin: the mist drifts from the first into the second, then
// spreads from there into the third. 1002 is skipped: its key, 3002, is a room spell_pa_tests.cpp uses.
constexpr int kMistDriftSourceRoom = 1003;
constexpr int kMistDriftDestRoom = 1004;
constexpr int kMistDriftSpreadRoom = 1005;

// Real world[] indices, not RoomFixture slots, for the pin where the recorded caster dies in its
// own blaze: a player's death respawns it in a world[] index, and moving a player needs the zone
// ScopedFleeWorld publishes. Beside fast_update_tests.cpp's 1021-1022 and below the 1024 rooms
// gtest_main.cpp allocates; the caster-store key is the index itself (ScopedFleeWorld stamps it).
constexpr int kSelfBlazeRoom = 1013;
constexpr int kSelfBlazeRespawnRoom = 1014;

// abs_number slots this suite registers, in a band no sibling suite in the
// monolithic runner uses (affect_update_tests: MAX_CHARACTERS - 201/-202;
// caster_snapshot_tests: -401; char_utils_tests: -17/-18; poison_origin_tests:
// -601; fight_credit_tests: -801).
constexpr int kCasterASlot = MAX_CHARACTERS - 1001;
constexpr int kCasterBSlot = MAX_CHARACTERS - 1002;

// Every queued draw answers the same normalized value, so number(from, to)
// returns from + (to - from + 1) / 2 (integer truncation) at every call site.
// 0.5 is a dyadic fraction: 0.5 * N is exactly representable for every
// integer N, so this is safe under both x87 excess precision and SSE2 with no
// platform-divergence risk -- x87's rounding error only lands a value a hair
// BELOW an integer boundary; an exact 0.5 * N never does.
constexpr double kMidRoll = 0.5;

void queue_mid_rolls(int count = 60)
{
    for (int roll_index = 0; roll_index < count; ++roll_index) {
        push_test_random_value(kMidRoll);
    }
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// The real world[] array indices RoomFixture uses, independent of the
// disambiguating "room number" (2000 + 960-1005) baked into room->number for the
// (room, spell) caster-store map key. world[]'s backing storage can only
// ever be sized ONCE for the whole process -- room_data::create_bulk()
// hard-exits (`exit(0)`) if BASE_WORLD is already set (db.cpp:4025-4032) --
// and whichever suite in this shared, monolithic test binary happens to
// call ensure_test_world()/create_bulk() first controls how many rooms
// every other suite gets for the rest of the run. The smallest known
// allocator in this binary is mage_tests.cpp's `ensure_test_world(32)`
// (34 rooms). Indexing world[] directly by this file's logical room
// numbers (960+) reads past that allocation whenever some other suite wins
// the race to create the world first -- room_data::operator[] catches it
// ("room_data called for a room outside the world") and serves a fallback
// room instead of the real fixture, silently breaking the test. Stay in a
// small, low index range instead, safely inside even the smallest known
// allocation, mirroring room_affect_caster_tests.cpp's own
// `world[0].number = room_number` precedent (which this file should have
// followed originally) rather than reusing the logical room number as the
// array index. A fixture holds its slot until it is destroyed, so two live
// fixtures never share a world[] index; eight is well above the most (two)
// any test keeps alive at once.
constexpr int kRoomSlotCount = 8;
bool g_room_slot_in_use[kRoomSlotCount] = {}; // true while a live RoomFixture holds that index

// Claims the lowest free world[] slot for a RoomFixture.
int acquire_room_slot()
{
    for (int slot = 0; slot < kRoomSlotCount; ++slot) {
        if (!g_room_slot_in_use[slot]) {
            g_room_slot_in_use[slot] = true;
            return slot;
        }
    }
    ADD_FAILURE() << "more than " << kRoomSlotCount << " RoomFixtures alive at once";
    return 0;
}

void release_room_slot(int slot)
{
    g_room_slot_in_use[slot] = false;
}

// Erases the (room, spell) caster record a test recorded without an affect. The store has
// no direct erase; affect_remove_room() is the one path that drops a record, so a
// throwaway affect for `spell` is added (the two-argument affect_to_room() never replaces
// an existing record) and removed again.
void erase_room_affect_record(room_data* room, int spell)
{
    if (room_affect_caster(room, spell) == nullptr) {
        return;
    }
    affected_type throwaway {};
    throwaway.type = ROOMAFF_SPELL;
    throwaway.duration = 1;
    throwaway.location = spell;
    affect_to_room(room, &throwaway);
    affect_remove_room(room, room_affected_by_spell(room, spell));
}

// Saves/restores everything a test in this file might touch on one room:
// occupants, corpse contents, exits, flags, and the room's own affect list
// (which also empties the (room, spell) caster store for every affect this
// scope leaves behind, since affect_remove_room() is what erases it, and for
// any record a test set without an affect). Stamps
// a distinct room->number, since the caster store is keyed on it and
// dummy/default rooms in the shared world[] all carry -1.
class RoomFixture {
public:
    explicit RoomFixture(int room_number)
        : m_slot(acquire_room_slot())
    {
        ensure_test_world(kRoomSlotCount);
        room_data& room = world[m_slot];
        m_original_people = room.people;
        m_original_contents = room.contents;
        m_original_number = room.number;
        m_original_room_flags = room.room_flags;
        m_original_light = room.light;
        // Lit, unconditionally: CAN_SEE() (utility.cpp) refuses a TO_VICT/TO_CHAR
        // act() line whenever the ADDRESSEE's room has no light source and the
        // RECIPIENT lacks infrared -- the poison saved-arm pins below need
        // act()'s output, not a silently-dropped line because a bare test room
        // defaults to dark.
        room.light = 1;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            m_original_exits[direction] = room.dir_option[direction];
            // Every exit starts cleared: dir_option[] normally points at
            // stack-local room_direction_data owned by whichever test set it
            // last, and the shared test-binary world[] carries that pointer
            // across tests -- leaving a prior test's exit in place here would
            // read a dangling pointer the moment this room's own test walks
            // its exits (mist_tick()'s spread/move code does exactly that).
            room.dir_option[direction] = nullptr;
        }
        room.number = 2000 + room_number;
        room.people = nullptr;
        room.contents = nullptr;
    }
    ~RoomFixture()
    {
        room_data& room = world[m_slot];
        while (room.affected) {
            affect_remove_room(&room, room.affected);
        }
        for (int spell : { SPELL_BLAZE, SPELL_POISON, SPELL_HAZE, SPELL_MIST_OF_BAAZUNGA }) {
            erase_room_affect_record(&room, spell);
        }
        room.people = m_original_people;
        room.contents = m_original_contents;
        room.number = m_original_number;
        room.room_flags = m_original_room_flags;
        room.light = m_original_light;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            room.dir_option[direction] = m_original_exits[direction];
        }
        release_room_slot(m_slot);
    }
    RoomFixture(const RoomFixture&) = delete;
    RoomFixture& operator=(const RoomFixture&) = delete;

    // The real world[] array index this fixture occupies -- what a
    // room_direction_data::to_room pointing AT this room must hold (mist_tick()
    // dereferences to_room as a raw world[] index, never room->number).
    int slot() const { return m_slot; }

    room_data* room() const { return &world[m_slot]; }

private:
    int m_slot; // the real world[] array index this fixture occupies (see slot() above)
    char_data* m_original_people; // occupant chain found before the test; restored on scope exit
    obj_data* m_original_contents; // corpse/content list found before the test; restored on scope exit
    int m_original_number; // room->number found before the test; restored on scope exit
    long m_original_room_flags; // room->room_flags found before the test; restored on scope exit
    byte m_original_light; // room->light found before the test; restored on scope exit
    room_direction_data* m_original_exits[NUM_OF_DIRS]; // per-direction exits found before the test; restored on scope exit
};

// Installs a spell pointer into skills[slot] for the scope and restores
// whatever was there -- gtest_main does not run assign_spell_pointers().
class ScopedSpellPointer {
public:
    ScopedSpellPointer(int slot, spell_function fn)
        : m_slot(slot)
        , m_previous(skills[slot].spell_pointer)
    {
        skills[slot].spell_pointer = fn;
    }
    ~ScopedSpellPointer() { skills[m_slot].spell_pointer = m_previous; }
    ScopedSpellPointer(const ScopedSpellPointer&) = delete;
    ScopedSpellPointer& operator=(const ScopedSpellPointer&) = delete;

private:
    int m_slot; // the skills[] cell this scope owns
    spell_function m_previous; // the cell's prior value; restored on scope exit
};

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents
// and object_list; take both back out. Mirrors fight_credit_tests.cpp's
// release_corpse().
void release_corpse(room_data& room, obj_data* previous_object_list)
{
    obj_data* corpse = room.contents;
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

// The room affect's victim: a plain NPC with no specialization and no mage
// levels, so its own formulas are all zero unless a test says otherwise --
// any nonzero contribution the tick shows has to come from the recorded
// caster.
void make_weak_occupant(char_data& ch, char_prof_data& profs, int hit_points)
{
    ch.profs = &profs;
    ch.specials2.act = MOB_ISNPC;
    ch.nr = -1;
    ch.player.race = RACE_HUMAN;
    ch.player.level = 1;
    ch.tmpabilities.intel = 8;
    ch.tmpabilities.con = 0;
    ch.tmpabilities.wil = 0;
    ch.points.willpower = 0;
    ch.specials2.perception = 0;
    ch.specials2.saving_throw = 0;
    ch.abilities.hit = hit_points;
    ch.tmpabilities.hit = hit_points;
    ch.specials.position = POSITION_STANDING;
    ch.specials.fighting = nullptr;
}

// Builds the heap-allocated, registered NPC occupant the death pipeline needs
// (see the file comment above): clear_char() + register_npc_char() the way
// the game constructs an NPC, with nr = 0 naming the ScopedMobIndex slot.
char_data* make_heap_occupant(int room, char* short_descr, int hit_points)
{
    char_data* occupant = test_support::allocate_test_character(MOB_ISNPC);
    occupant->specials2.act = MOB_ISNPC;
    occupant->nr = 0;
    occupant->player.race = RACE_HUMAN;
    occupant->player.short_descr = short_descr;
    occupant->player.level = 1;
    occupant->tmpabilities.intel = 8;
    occupant->tmpabilities.hit = hit_points;
    occupant->abilities.hit = std::max(hit_points, 1);
    occupant->specials.position = POSITION_STANDING;
    occupant->specials.fighting = nullptr;
    occupant->in_room = room;
    register_npc_char(occupant);
    return occupant;
}

// A recorded room-affect caster: a PC (act == 0), so should_apply_spell_penetration()
// is true and the blaze/haze/poison formulas actually read the snapshot's
// PROF_MAGE/PROF_CLERIC levels. mage_prof/cleric_prof are chosen as multiples
// of 25 by callers that need get_mage_caster_level()/get_mystic_caster_level()
// to draw nothing from the RNG queue: (25 / 5) % 5 == 0 makes the rounding
// roll a number(0, 0), which this depot's wrapped number() returns from
// `from` without consuming the test queue at all.
struct CasterFixture {
    char_data ch {}; // the recorded room-affect caster; captured into a caster_snapshot by each pin
    char_prof_data profs {}; // backs ch.profs; holds the mage/cleric prof levels the formulas read
    char name[24] = "tick_caster"; // backs ch.player.name (GET_NAME()); owned for the fixture's lifetime

    CasterFixture(int mage_prof, int cleric_prof, game_types::player_specs spec, int in_room)
    {
        ch.profs = &profs;
        ch.player.name = name;
        ch.specials2.act = 0; // a PC, not IS_NPC
        ch.player.race = RACE_HUMAN;
        ch.player.level = 30;
        profs.prof_level[PROF_MAGE] = mage_prof;
        profs.prof_level[PROF_CLERIC] = cleric_prof;
        profs.specialization = static_cast<int>(spec);
        ch.tmpabilities.intel = 25;
        ch.tmpabilities.wil = 25;
        ch.points.spell_power = 0;
        ch.points.spell_pen = 0;
        ch.points.willpower = 0; // saves_poison() offence: willpower * 8 * perception / 100
        ch.specials2.perception = 0;
        ch.specials.tactics = 0;
        ch.abilities.hit = 500;
        ch.tmpabilities.hit = 500;
        ch.specials.position = POSITION_STANDING;
        ch.specials.fighting = nullptr;
        ch.in_room = in_room;
    }
};

affected_type dummy_affect()
{
    // room_affect_tick() never reads its `affect` parameter (see
    // room_affect_tick.h) -- every test passes an empty node.
    return affected_type {};
}

} // namespace

// ---------------------------------------------------------------------------
// blaze: damage from the RECORDED snapshot, not the caster's current stats
// ---------------------------------------------------------------------------
//
// Rather than predicting every draw damage_credited() makes internally
// (check_resistances(), the shield-spell arm, etc.), this pin is
// DIFFERENTIAL: two identical occupants, two casters recorded with the SAME
// RNG sequence -- one caster's snapshot was captured strong and then the
// LIVE character was wrecked down to a caster-B-shaped husk; the other
// caster (never wrecked) starts already caster-B-shaped. If the tick read
// live stats, both occupants would take the same (small) hit; reading the
// snapshot instead makes the first occupant take much more.
TEST(RoomAffectTick, BlazeTickDamageComesFromTheSnapshotNotTheCastersCurrentStats)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room_a(kBlazeRoomA);
    RoomFixture room_b(kBlazeRoomB);

    char_data occupant_a {};
    char_prof_data occupant_a_profs {};
    make_weak_occupant(occupant_a, occupant_a_profs, 500);
    char_data occupant_b {};
    char_prof_data occupant_b_profs {};
    make_weak_occupant(occupant_b, occupant_b_profs, 500);

    CasterFixture strong_caster(25, 0, game_types::PS_None, kBlazeRoomA);
    set_room_affect_caster(room_a.room(), SPELL_BLAZE, caster_snapshot::capture(strong_caster.ch));
    // Wreck the LIVE caster AFTER the snapshot was captured -- a level-down,
    // a re-spec, or simply a much weaker future recast of this room affect.
    strong_caster.profs.prof_level[PROF_MAGE] = 1;
    strong_caster.ch.tmpabilities.intel = 3;

    CasterFixture already_weak_caster(1, 0, game_types::PS_None, kBlazeRoomB);
    already_weak_caster.ch.tmpabilities.intel = 3;
    set_room_affect_caster(room_b.room(), SPELL_BLAZE, caster_snapshot::capture(already_weak_caster.ch));

    affected_type affect = dummy_affect();

    clear_test_random_values();
    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, room_a.room(), &occupant_a, affect);
    const int damage_from_recorded_strong_snapshot = 500 - occupant_a.tmpabilities.hit;

    clear_test_random_values();
    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, room_b.room(), &occupant_b, affect);
    const int damage_from_weak_caster = 500 - occupant_b.tmpabilities.hit;
    clear_test_random_values();

    EXPECT_GT(damage_from_recorded_strong_snapshot, damage_from_weak_caster + 10)
        << "the tick must have followed the strong snapshot recorded at cast time, not the "
           "caster's current (wrecked) live stats -- had it read the live caster, occupant_a's "
           "damage would have matched occupant_b's";
}

// ---------------------------------------------------------------------------
// blaze: a lethal tick credits the recorded caster
// ---------------------------------------------------------------------------
// Who was credited is read back through Big Brother: raw_kill() hands every
// death to on_character_died(), which for an orc-friend NPC victim with a
// non-empty corpse records the killer, and is_corpse_protected() then answers
// true only when that recorded killer was a player. The corpse's gear moving
// out of its container is NOT a credit signature -- make_physical_corpse()
// moves it for a null killer too -- which the null-credit control below pins.
// Boot creates the Big Brother singleton (db.cpp); gtest_main does not, and
// raw_kill() reports every death to it. A plain NPC corpse never touches its
// members, but an orc-friend corpse is recorded, so the pins below need the
// real instance. create() keeps one function-local static, so repeating it
// is harmless.
void ensure_big_brother()
{
    game_rules::big_brother::create(weather_info, &world);
}

TEST(RoomAffectTick, LethalBlazeTickCreditsTheRecordedCaster)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    ScopedMobIndex prototype_table;
    RoomFixture occupant_room(kBlazeRoomA);
    RoomFixture caster_room(kAwayRoom);

    char occupant_short_descr[] = "a testing blaze victim";
    char_data* occupant = make_heap_occupant(occupant_room.slot(), occupant_short_descr, 1); // any blaze tick is lethal
    occupant->specials2.act |= MOB_ORC_FRIEND; // so Big Brother records this corpse's killer
    character_list = occupant;
    occupant->next = nullptr;
    occupant_room.room()->people = occupant;
    occupant->next_in_room = nullptr;
    ASSERT_TRUE(IS_NPC(occupant)) << "precondition: the victim is a mob, so the caster is the only player in scope";

    CasterFixture caster(25, 0, game_types::PS_None, kAwayRoom); // standing elsewhere entirely
    ASSERT_FALSE(IS_NPC(&caster.ch)) << "precondition: the recorded caster is a player";
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    set_room_affect_caster(occupant_room.room(), SPELL_BLAZE, caster_snapshot::capture(caster.ch));

    CarriedGear gear;
    gear.attach_to(*occupant);

    affected_type affect = dummy_affect();
    obj_data* const previous_object_list = object_list;

    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, occupant_room.room(), occupant, affect);
    clear_test_random_values();
    // occupant is freed at this point; nothing below may dereference it --
    // only compare the pointer value or read state through survivors.

    EXPECT_EQ(character_list, nullptr)
        << "extract_char()'s NPC arm must have unlinked the dead occupant from character_list";
    EXPECT_EQ(occupant_room.room()->people, nullptr)
        << "extract_char()'s NPC arm must have unlinked the dead occupant from the room's occupant list";
    // The death's stop_fighting() clears any engagement, so whether the caster was engaged is
    // pinned on a surviving occupant in BlazeTickNeverEngagesACasterStandingInTheRoom.

    obj_data* const corpse = occupant_room.room()->contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.item.in_obj, corpse) << "the corpse must hold the victim's gear (Big Brother ignores empty corpses)";

    // Big Brother exposes only whether the recorded killer was a player, so
    // the identity claim rests on this scenario holding exactly one player:
    // the occupant is a mob (asserted before the tick) and the caster is the
    // only other character in scope.
    game_rules::big_brother& big_brother = game_rules::big_brother::instance();
    EXPECT_TRUE(big_brother.is_corpse_protected(&caster.ch, corpse))
        << "Big Brother must have been told a PLAYER killed this orc-friend: the recorded caster "
           "is the only player in this scenario, so this is the credited identity";

    big_brother.on_corpse_decayed(corpse);
    release_corpse(*occupant_room.room(), previous_object_list);
}

// The credit follows the recorded caster even when the victim is on the caster's own side and
// in the caster's group: nothing on the tick path exempts a groupmate from being burned or the
// caster from being credited with the kill. Read back through Big Brother as in the pin above.
TEST(RoomAffectTick, LethalBlazeTickOnASameSideGroupmateCreditsTheCaster)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    ScopedMobIndex prototype_table;
    RoomFixture occupant_room(kBlazeRoomA);
    RoomFixture caster_room(kAwayRoom);

    char occupant_short_descr[] = "a testing blaze groupmate";
    char_data* occupant = make_heap_occupant(occupant_room.slot(), occupant_short_descr, 1); // any blaze tick is lethal
    occupant->specials2.act |= MOB_ORC_FRIEND; // so Big Brother records this corpse's killer
    character_list = occupant;
    occupant->next = nullptr;
    occupant_room.room()->people = occupant;
    occupant->next_in_room = nullptr;

    CasterFixture caster(25, 0, game_types::PS_None, kAwayRoom);
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    ASSERT_EQ(occupant->player.race, caster.ch.player.race) << "precondition: the victim is on the caster's side";
    ASSERT_EQ(GET_ALIGNMENT(occupant), GET_ALIGNMENT(&caster.ch)) << "precondition: the victim shares the caster's alignment";
    // The victim's death removes it from the group, and remove_character_from_group() deletes a
    // group left with only its leader, so the group lives on the heap.
    group_data* const party = new group_data(&caster.ch);
    party->add_member(occupant);
    ASSERT_TRUE(party->is_member(occupant)) << "precondition: the victim is in the caster's group";
    set_room_affect_caster(occupant_room.room(), SPELL_BLAZE, caster_snapshot::capture(caster.ch));

    CarriedGear gear;
    gear.attach_to(*occupant);

    affected_type affect = dummy_affect();
    obj_data* const previous_object_list = object_list;

    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, occupant_room.room(), occupant, affect);
    clear_test_random_values();
    // occupant is freed at this point; nothing below may dereference it.

    EXPECT_EQ(character_list, nullptr) << "the groupmate still dies";
    EXPECT_EQ(caster.ch.group, nullptr) << "the death left the caster alone, which disbanded the group";
    if (caster.ch.group != nullptr) {
        delete caster.ch.group;
        caster.ch.group = nullptr;
    }

    obj_data* const corpse = occupant_room.room()->contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    ASSERT_EQ(gear.item.in_obj, corpse) << "precondition: the corpse holds the victim's gear (Big Brother ignores empty corpses)";

    game_rules::big_brother& big_brother = game_rules::big_brother::instance();
    EXPECT_TRUE(big_brother.is_corpse_protected(&caster.ch, corpse))
        << "the lethal tick must credit the recorded caster, the only player in scope, even though "
           "the victim was a same-side groupmate";

    big_brother.on_corpse_decayed(corpse);
    release_corpse(*occupant_room.room(), previous_object_list);
}

// Control for the pin above: the same tick with a recorded caster who can no
// longer be resolved (never registered, as an extracted caster would be)
// credits nobody. The gear still moves -- proving it cannot stand in for a
// credit assertion -- while Big Brother, told of a null killer, leaves a
// same-side looter unprotected.
TEST(RoomAffectTick, BlazeTickWithAnUnresolvableCasterCreditsNobody)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    ScopedMobIndex prototype_table;
    RoomFixture occupant_room(kBlazeRoomA);
    RoomFixture caster_room(kAwayRoom);

    char occupant_short_descr[] = "a testing blaze victim";
    char_data* occupant = make_heap_occupant(occupant_room.slot(), occupant_short_descr, 1);
    occupant->specials2.act |= MOB_ORC_FRIEND;
    character_list = occupant;
    occupant->next = nullptr;
    occupant_room.room()->people = occupant;
    occupant->next_in_room = nullptr;

    CasterFixture caster(25, 0, game_types::PS_None, kAwayRoom);
    caster.ch.abs_number = kCasterASlot;
    remove_char_exists(kCasterASlot); // deliberately NOT registered: the snapshot must not resolve
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);
    ASSERT_EQ(recorded.resolve(), nullptr) << "precondition: the recorded caster is unresolvable";
    set_room_affect_caster(occupant_room.room(), SPELL_BLAZE, recorded);

    CarriedGear gear;
    gear.attach_to(*occupant);

    affected_type affect = dummy_affect();
    obj_data* const previous_object_list = object_list;

    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, occupant_room.room(), occupant, affect);
    clear_test_random_values();

    EXPECT_EQ(character_list, nullptr) << "the occupant still dies";

    obj_data* const corpse = occupant_room.room()->contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.container.contains, nullptr)
        << "the gear moves for a null killer exactly as for a player killer, so gear movement "
           "proves nothing about credit";
    EXPECT_EQ(gear.item.in_obj, corpse);

    game_rules::big_brother& big_brother = game_rules::big_brother::instance();
    EXPECT_FALSE(big_brother.is_corpse_protected(&caster.ch, corpse))
        << "with nobody credited, Big Brother recorded no player killer: a same-side (human) "
           "looter is not kept off this human orc-friend's corpse";

    big_brother.on_corpse_decayed(corpse);
    release_corpse(*occupant_room.room(), previous_object_list);
}

// A blaze tick burns the occupant through itself, so a caster standing in the room is never
// pulled into a fight. The tick here is not lethal: a death runs stop_fighting() on every
// opponent, which would hide an engagement.
TEST(RoomAffectTick, BlazeTickNeverEngagesACasterStandingInTheRoom)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    RoomFixture room(kBlazeRoomA);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.in_room = room.slot();

    CasterFixture caster(25, 0, game_types::PS_None, room.slot());
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    room.room()->people = &occupant;
    occupant.next_in_room = &caster.ch;
    caster.ch.next_in_room = nullptr;
    set_room_affect_caster(room.room(), SPELL_BLAZE, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_BLAZE, room.room(), &occupant, affect);
    clear_test_random_values();

    ASSERT_LT(occupant.tmpabilities.hit, 500) << "precondition: the blaze tick burned the occupant";
    ASSERT_GT(occupant.tmpabilities.hit, 0) << "precondition: the occupant survives the tick";
    EXPECT_EQ(caster.ch.specials.fighting, nullptr)
        << "a blaze tick burns the occupant through itself and must never start a fight with "
           "its caster, who stands in the room";
    EXPECT_EQ(occupant.specials.fighting, nullptr)
        << "a blaze tick burns the occupant through itself and must never engage the occupant "
           "with its caster";
}

// A caster who dies to its own room spell is punished as for any lethal hit it dealt itself
// (a fumbled fireball is the in-game case): it is recorded as its own killer, respawns on the
// gentle terms (a quarter of its hit points, no mana, stats whole) rather than the harsh ones
// (1 hit point, two thirds of every stat), and no kill or death record is written. The blaze
// tick gets there by crediting the resolved caster, which is the victim itself.
TEST(RoomAffectTick, ACasterKilledByItsOwnBlazeTickIsPunishedLikeAnyHitItDealtItself)
{
    ScopedCombatList combat_list_guard;
    test_support::ScopedWaitingList waiting_list_guard;
    ensure_big_brother();
    test_support::ScopedFleeWorld rooms(kSelfBlazeRoom, kSelfBlazeRespawnRoom, EAST);
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kSelfBlazeRespawnRoom);
    test_support::ScopedExploitTypeCapture exploits;

    descriptor_data descriptor {};
    char_data* const caster = test_support::make_linked_test_player(descriptor, "Pyro");
    caster->profs->prof_level[PROF_MAGE] = 25;
    caster->tmpabilities.intel = 25;
    caster->tmpabilities.hit = 1; // constitution 18 dies at -9; a mid-roll level-30 tick burns far more
    // Loading a player resets its damage report, which constructs the calloc'd map inside it;
    // damage() records into that map for a player attacker, here the occupant itself.
    caster->damage_details.reset();
    const character_identity caster_identity = character_identity::capture(*caster);
    test_support::ScopedCharacterList characters({ caster });
    test_support::ScopedRoomOccupants occupants(kSelfBlazeRoom, { caster });
    room_data* const blaze_room = &world[kSelfBlazeRoom];
    set_room_affect_caster(blaze_room, SPELL_BLAZE, caster_snapshot::capture(*caster));
    ASSERT_EQ(room_affect_caster(blaze_room, SPELL_BLAZE)->resolve(), caster)
        << "precondition: the recorded caster resolves to the occupant itself";

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    testing::internal::CaptureStderr();
    room_affect_tick(SPELL_BLAZE, blaze_room, caster, affect);
    const std::string captured = testing::internal::GetCapturedStderr();
    clear_test_random_values();
    erase_room_affect_record(blaze_room, SPELL_BLAZE);

    EXPECT_NE(captured.find("Pyro killed by Pyro"), std::string::npos)
        << "die() must have been told the caster killed itself; stderr was: " << captured;
    EXPECT_TRUE(exploits.types.empty())
        << "a death whose only credited killer is the victim writes no kill or death record";

    char_data* const survivor = caster_identity.resolve();
    EXPECT_EQ(survivor, caster) << "a connected player respawns and keeps its registration";
    if (survivor != nullptr) {
        EXPECT_EQ(survivor->in_room, kSelfBlazeRespawnRoom) << "the tick killed the caster, which respawned";
        EXPECT_EQ(survivor->tmpabilities.hit, survivor->abilities.hit / 4)
            << "the gentle arm restores a quarter of the hit points; the harsh arm leaves 1";
        EXPECT_EQ(survivor->tmpabilities.mana, 0);
        EXPECT_EQ(survivor->tmpabilities.str, survivor->abilities.str)
            << "the gentle arm leaves the stats whole; the harsh arm cuts them to two thirds";
    }

    test_support::release_survivor(caster_identity);
    test_support::release_room_objects(kSelfBlazeRoom);
    test_support::release_room_objects(kSelfBlazeRespawnRoom);
    test_support::release_large_output(descriptor);
}

// ---------------------------------------------------------------------------
// poison
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, PoisonTickRecordsTheResolvedCasterAsPoisoner)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room(kPoisonRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    CasterFixture caster(0, 10, game_types::PS_None, kPoisonRoom);
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);

    EXPECT_EQ(resolve_poisoner(occupant), &caster.ch)
        << "the poison tick must record the RESOLVED recorded caster as the poisoner";
    affected_type* poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "with offence == defense == 0 the poison must apply deterministically";
    // duration = get_mystic_caster_level(who) + 1 = (cleric_prof 10 + wil 25/5) + 1 = 16, entirely
    // from the RECORDED caster -- the occupant's own cleric_prof/wil are both 0.
    EXPECT_EQ(poison->duration, 16);
}

// A room-poison tick on an occupant already running an equal poison extends it by half the tick's
// duration and keeps the recorded caster, even after the room passes to another caster of the
// same level. Both casters are cleric level 10, so each tick's poison lasts 16 ticks.
TEST(RoomAffectTick, PoisonTickExtendsAnEqualPoisonAndKeepsItsRecordedCaster)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room(kPoisonRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    // Once the first poison lands, affect_total() recomputes an NPC's willpower as level +
    // tmpabilities.wil - confusion / 10. With level 0 (wil is already 0) it stays 0, so
    // saves_poison()'s defense stays 0 and every tick lands.
    occupant.player.level = 0;
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    CasterFixture first_caster(0, 10, game_types::PS_None, kPoisonRoom);
    ScopedCharExists first_registration(first_caster.ch, kCasterASlot);
    CasterFixture second_caster(0, 10, game_types::PS_None, kPoisonRoom);
    ScopedCharExists second_registration(second_caster.ch, kCasterBSlot);
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(first_caster.ch));
    constexpr int kTickPoisonDuration = 16;

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    affected_type* poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the first tick poisons";
    ASSERT_EQ(poison->duration, kTickPoisonDuration) << "precondition: the first tick poisons for 16 ticks";
    ASSERT_EQ(resolve_poisoner(occupant), &first_caster.ch) << "precondition: the first tick records its caster";

    poison->duration = 1;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(second_caster.ch));
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "the occupant must still be poisoned after the new caster's tick";
    EXPECT_EQ(poison->duration, 1 + kTickPoisonDuration / 2)
        << "the new caster's 16-tick poison must extend the running 1 tick by 8";
    EXPECT_EQ(resolve_poisoner(occupant), &first_caster.ch)
        << "an extension must keep the recorded caster, who still resolves";
}

// A room-poison tick extending an equal poison whose recorded poisoner has left the game hands the
// record to the tick's own resolved caster.
TEST(RoomAffectTick, PoisonTickHandsTheRecordToItsCasterWhenThePoisonerIsGone)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room(kPoisonRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.player.level = 0; // every tick lands, as in the extension pin above
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    CasterFixture first_caster(0, 10, game_types::PS_None, kPoisonRoom);
    ScopedCharExists first_registration(first_caster.ch, kCasterASlot);
    CasterFixture second_caster(0, 10, game_types::PS_None, kPoisonRoom);
    ScopedCharExists second_registration(second_caster.ch, kCasterBSlot);
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(first_caster.ch));
    constexpr int kTickPoisonDuration = 16;

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    affected_type* poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "precondition: the first tick poisons";
    ASSERT_EQ(poison->duration, kTickPoisonDuration) << "precondition: the first tick poisons for 16 ticks";
    remove_char_exists(kCasterASlot); // what extract_char() does for the departed first caster
    ASSERT_EQ(resolve_poisoner(occupant), nullptr) << "precondition: the first caster no longer resolves";

    poison->duration = 1;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(second_caster.ch));
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "the occupant must still be poisoned after the new caster's tick";
    EXPECT_EQ(poison->duration, 1 + kTickPoisonDuration / 2)
        << "the new caster's 16-tick poison must extend the running 1 tick by 8, not replace it";
    EXPECT_EQ(resolve_poisoner(occupant), &second_caster.ch)
        << "with the recorded poisoner gone, the tick's resolved caster must take the record";
}

// A poison tick that kills credits the recorded caster, read back through Big Brother as in
// the blaze credit pin above. Only the landed arm deals damage, so the occupant's death shows
// the poison landed.
TEST(RoomAffectTick, LethalPoisonTickCreditsTheRecordedCaster)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    ScopedMobIndex prototype_table;
    RoomFixture occupant_room(kPoisonRoom);
    RoomFixture caster_room(kAwayRoom);

    char occupant_short_descr[] = "a testing poison victim";
    char_data* occupant = make_heap_occupant(occupant_room.slot(), occupant_short_descr, 1); // the tick deals 5
    occupant->specials2.act |= MOB_ORC_FRIEND; // so Big Brother records this corpse's killer
    character_list = occupant;
    occupant->next = nullptr;
    occupant_room.room()->people = occupant;
    occupant->next_in_room = nullptr;
    ASSERT_TRUE(IS_NPC(occupant)) << "precondition: the victim is a mob, so the caster is the only player in scope";
    // clear_char() leaves constitution and willpower at 0, so saves_poison()'s defense is 0 and
    // the poison lands; constitution 0 also makes any hit point at or below 0 fatal.
    ASSERT_EQ(GET_CON(occupant), 0) << "precondition: the occupant cannot save and dies at 0 hit points";
    ASSERT_EQ(GET_WILLPOWER(occupant), 0) << "precondition: the occupant cannot save";

    CasterFixture caster(0, 10, game_types::PS_None, kAwayRoom); // standing elsewhere entirely
    ASSERT_FALSE(IS_NPC(&caster.ch)) << "precondition: the recorded caster is a player";
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    set_room_affect_caster(occupant_room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    CarriedGear gear;
    gear.attach_to(*occupant);

    affected_type affect = dummy_affect();
    obj_data* const previous_object_list = object_list;

    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, occupant_room.room(), occupant, affect);
    clear_test_random_values();
    // occupant is freed at this point; nothing below may dereference it.

    ASSERT_EQ(character_list, nullptr) << "precondition: the poison landed and its 5-point tick killed the 1-hit occupant";
    obj_data* const corpse = occupant_room.room()->contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    ASSERT_EQ(gear.item.in_obj, corpse) << "precondition: the corpse holds the victim's gear (Big Brother ignores empty corpses)";

    game_rules::big_brother& big_brother = game_rules::big_brother::instance();
    EXPECT_TRUE(big_brother.is_corpse_protected(&caster.ch, corpse))
        << "the lethal poison tick must credit the recorded caster, the only player in scope";

    big_brother.on_corpse_decayed(corpse);
    release_corpse(*occupant_room.room(), previous_object_list);
}

// The poison counterpart of BlazeTickNeverEngagesACasterStandingInTheRoom, on a non-lethal tick
// for the same reason.
TEST(RoomAffectTick, PoisonTickNeverEngagesACasterStandingInTheRoom)
{
    ScopedCombatList combat_list_guard;
    ensure_big_brother();
    RoomFixture room(kPoisonRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.in_room = room.slot();
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    CasterFixture caster(0, 10, game_types::PS_None, room.slot());
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    room.room()->people = &occupant;
    occupant.next_in_room = &caster.ch;
    caster.ch.next_in_room = nullptr;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    ASSERT_NE(affected_by_spell(&occupant, SPELL_POISON), nullptr) << "precondition: the poison lands";
    ASSERT_LT(occupant.tmpabilities.hit, 500) << "precondition: the landed poison's tick damaged the occupant";
    ASSERT_GT(occupant.tmpabilities.hit, 0) << "precondition: the occupant survives the tick";
    EXPECT_EQ(caster.ch.specials.fighting, nullptr)
        << "a poison tick damages the occupant through itself and must never start a fight with "
           "its caster, who stands in the room";
    EXPECT_EQ(occupant.specials.fighting, nullptr)
        << "a poison tick damages the occupant through itself and must never engage the occupant "
           "with its caster";
}

TEST(RoomAffectTick, PoisonTickWithNoRecordedCasterFallsBackToOccupantStatsAndRecordsNoPoisoner)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room(kPoisonNoRecordRoom);
    // No set_room_affect_caster() call at all: room_affect_caster() answers nullptr.

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);

    EXPECT_EQ(resolve_poisoner(occupant), nullptr)
        << "a builder-placed affect with no recorded caster must credit nobody as the poisoner";
    affected_type* poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    // duration = get_mystic_caster_level(capture(occupant)) + 1. get_prof_level() (char_utils.cpp)
    // returns player.level -- not profs->prof_level[prof] -- for any IS_NPC() character (its
    // "no real profession track" branch), and make_weak_occupant() flags the occupant NPC with
    // player.level = 1, so the snapshot's cleric_prof_level is 1 (not the raw, unset
    // profs->prof_level[PROF_CLERIC] == 0 a non-NPC caster would read here). will_factor is 0
    // (wil = 0), so get_mystic_caster_level() = 1 + 0 = 1, and duration = 1 + 1 = 2 -- still the
    // OCCUPANT's own (weak) stats standing in, matching the historical self-re-cast shape.
    EXPECT_EQ(poison->duration, 2);
}

TEST(RoomAffectTick, PoisonTickWithAnExplicitNoneRecordRecordsNoPoisoner)
{
    ScopedCombatList combat_list_guard;
    RoomFixture room(kPoisonNoneRoom);
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::none());

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    test_support::ScopedAffectCleanup occupant_affects(occupant);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);

    EXPECT_EQ(resolve_poisoner(occupant), nullptr)
        << "an explicit caster_snapshot::none() record must credit nobody, exactly like no record "
           "at all";
}

TEST(RoomAffectTick, PoisonTickSavedArmMessagesTheOccupantAndThePresentCaster)
{
    RoomFixture room(kPoisonSavedPresentRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.tmpabilities.con = 100; // defense = 500, so saves_poison()'s comparison is never zero
    occupant.in_room = kPoisonSavedPresentRoom;
    descriptor_data occupant_descriptor {};
    prepare_capture_descriptor(occupant_descriptor);
    occupant.desc = &occupant_descriptor;

    CasterFixture caster(0, 10, game_types::PS_None, kPoisonSavedPresentRoom); // same room: "present"
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    descriptor_data caster_descriptor {};
    prepare_capture_descriptor(caster_descriptor);
    caster.ch.desc = &caster_descriptor;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    EXPECT_EQ(affected_by_spell(&occupant, SPELL_POISON), nullptr)
        << "offence(0) < defense(>=250) must always save the occupant";

    const std::string occupant_output = occupant_descriptor.output;
    const std::string caster_output = caster_descriptor.output;
    EXPECT_NE(occupant_output.find("fend off the poison"), std::string::npos) << occupant_output;
    EXPECT_NE(caster_output.find("shrugs off your poison"), std::string::npos) << caster_output;
}

TEST(RoomAffectTick, PoisonTickSavedArmOmitsTheCasterLineWhenTheCasterHasWalkedAway)
{
    RoomFixture room(kPoisonSavedAwayRoom);
    RoomFixture away_room(kAwayRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.tmpabilities.con = 100;
    occupant.in_room = kPoisonSavedAwayRoom;
    descriptor_data occupant_descriptor {};
    prepare_capture_descriptor(occupant_descriptor);
    occupant.desc = &occupant_descriptor;

    CasterFixture caster(0, 10, game_types::PS_None, kAwayRoom); // recorded in the poison room, standing elsewhere
    ScopedCharExists caster_registration(caster.ch, kCasterBSlot);
    descriptor_data caster_descriptor {};
    prepare_capture_descriptor(caster_descriptor);
    caster.ch.desc = &caster_descriptor;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    const std::string occupant_output = occupant_descriptor.output;
    const std::string caster_output = caster_descriptor.output;
    EXPECT_NE(occupant_output.find("fend off the poison"), std::string::npos)
        << "the victim-facing line must still reach the occupant regardless of where the caster "
           "stands: "
        << occupant_output;
    EXPECT_TRUE(caster_output.empty())
        << "a caster who has walked away must be told nothing: " << caster_output;
}

TEST(RoomAffectTick, PoisonTickSavedArmSendsTheVictimLineDirectlyWhenThereIsNoCaster)
{
    RoomFixture room(kPoisonSavedNoCasterRoom);
    // No set_room_affect_caster() call: the tick falls back to occupant self-capture, whose
    // willpower/perception are both 0 by make_weak_occupant() -- so its own offence is 0 too, and
    // a high-CON occupant still always saves against its own (self-captured) poison.

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.tmpabilities.con = 100;
    descriptor_data occupant_descriptor {};
    prepare_capture_descriptor(occupant_descriptor);
    occupant.desc = &occupant_descriptor;

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    const std::string occupant_output = occupant_descriptor.output;
    EXPECT_NE(occupant_output.find("fend off the poison"), std::string::npos)
        << "with no caster recorded, the victim-facing line must still be delivered directly: "
        << occupant_output;
}

// The victim-facing "fend off" line reaches the occupant even when the occupant cannot
// see the present caster: it carries no act() codes, so it must not go through act()'s
// CAN_SEE() gate, which dropped it for a blind occupant (and can render "glances
// directly at you" for an invisible caster).
TEST(RoomAffectTick, PoisonTickSavedArmReachesABlindOccupantWhenTheCasterIsPresent)
{
    RoomFixture room(kPoisonSavedBlindRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.tmpabilities.con = 100; // defense = 500, so saves_poison()'s comparison is never zero
    occupant.in_room = kPoisonSavedBlindRoom;
    SET_BIT(occupant.specials.affected_by, AFF_BLIND);
    descriptor_data occupant_descriptor {};
    prepare_capture_descriptor(occupant_descriptor);
    occupant.desc = &occupant_descriptor;

    CasterFixture caster(0, 10, game_types::PS_None, kPoisonSavedBlindRoom); // same room: "present"
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    descriptor_data caster_descriptor {};
    prepare_capture_descriptor(caster_descriptor);
    caster.ch.desc = &caster_descriptor;
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    const std::string occupant_output = occupant_descriptor.output;
    EXPECT_NE(occupant_output.find("fend off the poison"), std::string::npos)
        << "a blind occupant must still be told the poison was fended off; output was: "
        << occupant_output;
    EXPECT_EQ(occupant_output.find("glances directly at you"), std::string::npos) << occupant_output;
}

// ---------------------------------------------------------------------------
// haze
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, HazeTickAppliesFromTheSnapshotLevelPlusTheIllusionBonus)
{
    RoomFixture room(kHazeRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    test_support::ScopedAffectCleanup occupant_affects(occupant);
    occupant.specials2.perception = 0; // saves_mystic() defense = 0

    CasterFixture caster(0, 10, game_types::PS_Illusion, kHazeRoom);
    set_room_affect_caster(room.room(), SPELL_HAZE, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    push_test_random_value(kMidRoll); // my_duration = number(0, 1)
    push_test_random_value(0.9); // saves_mystic()'s offense roll: must clear defense (0)
    room_affect_tick(SPELL_HAZE, room.room(), &occupant, affect);
    clear_test_random_values();

    affected_type* haze = affected_by_spell(&occupant, SPELL_HAZE);
    ASSERT_NE(haze, nullptr) << "offense(90) > defense(0) must always fail the occupant's save";
    // level = get_mystic_caster_level(who) + 6 (Illusion) = (10 + 25/5) + 6 = 21, entirely from
    // the RECORDED caster -- the occupant carries no mystic levels of its own.
    EXPECT_EQ(haze->modifier, 21);
}

// ---------------------------------------------------------------------------
// mist
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, MistTickRenewsFromTheSnapshotLevelAndNeverShortensAStrongerMist)
{
    RoomFixture main_room(kMistMainRoom);
    RoomFixture stronger_adjacent(kMistStrongerAdjacentRoom);
    room_direction_data east_exit {};
    east_exit.to_room = stronger_adjacent.slot();
    main_room.room()->dir_option[EAST] = &east_exit;

    // level = get_mage_caster_level(who) = 25 + 25/5 = 30 -> level/5 = 6.
    CasterFixture caster(25, 0, game_types::PS_None, kMistMainRoom);
    set_room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA, caster_snapshot::capture(caster.ch));

    affected_type weak_mist {};
    weak_mist.type = ROOMAFF_SPELL;
    weak_mist.duration = 1; // weaker than level/5 = 6 -- must be renewed up
    weak_mist.modifier = 0;
    weak_mist.location = SPELL_MIST_OF_BAAZUNGA;
    weak_mist.bitvector = 0;
    affect_to_room(main_room.room(), &weak_mist);

    affected_type stronger_mist {};
    stronger_mist.type = ROOMAFF_SPELL;
    stronger_mist.duration = 99; // STRONGER than level/5 = 6 -- must never be shortened
    stronger_mist.modifier = 0;
    stronger_mist.location = SPELL_MIST_OF_BAAZUNGA;
    stronger_mist.bitvector = 0;
    affect_to_room(stronger_adjacent.room(), &stronger_mist);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, main_room.room(), main_room.room()->people, affect);

    affected_type* main_after = room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_after, nullptr);
    EXPECT_EQ(main_after->duration, 6) << "a weaker mist must be renewed up to level/5 from the snapshot";

    affected_type* adjacent_after = room_affected_by_spell(stronger_adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(adjacent_after, nullptr);
    EXPECT_EQ(adjacent_after->duration, 99)
        << "a stronger adjacent mist must never be shortened down to level/5";
}

TEST(RoomAffectTick, MistTickSeedsAnEmptyAdjacentRoomCarryingTheCaster)
{
    RoomFixture main_room(kMistMainRoom);
    RoomFixture adjacent(kMistAdjacentRoom);
    room_direction_data north_exit {};
    north_exit.to_room = adjacent.slot();
    main_room.room()->dir_option[NORTH] = &north_exit;

    // level = 25 + 25/5 = 30, falling to 27 one hop out -> the fresh adjacent seed
    // carries 27/6 = 4.
    CasterFixture caster(25, 0, game_types::PS_None, kMistMainRoom);
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);
    set_room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA, recorded);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, main_room.room(), main_room.room()->people, affect);

    affected_type* seeded = room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded, nullptr) << "an empty adjacent room must be freshly seeded";
    EXPECT_EQ(seeded->duration, 4);

    const caster_snapshot* seeded_caster = room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded_caster, nullptr)
        << "the fresh seed must carry the SAME caster the main room's mist was ticked from";
    EXPECT_STREQ(seeded_caster->name, recorded.name);
    EXPECT_EQ(seeded_caster->mage_prof_level, recorded.mage_prof_level);
}

// A mist affect's counter is its spread generation: 0 where it was breathed, one more per
// room it has spread. A tick seeds an empty neighbour one generation out, at the level the
// falloff leaves for that generation.
TEST(RoomAffectTick, MistTickSeedsANeighbourOneGenerationOutAtTheReducedLevel)
{
    RoomFixture main_room(kMistFalloffMainRoom);
    RoomFixture neighbour(kMistFalloffSeedRoom);
    room_direction_data north_exit {};
    north_exit.to_room = neighbour.slot();
    main_room.room()->dir_option[NORTH] = &north_exit;

    CasterFixture caster(25, 0, game_types::PS_None, kMistFalloffMainRoom); // level 30
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type cast_mist {};
    cast_mist.type = ROOMAFF_SPELL;
    cast_mist.duration = 6;
    cast_mist.modifier = 0;
    cast_mist.location = SPELL_MIST_OF_BAAZUNGA;
    cast_mist.bitvector = 0;
    cast_mist.counter = 0;
    affect_to_room(main_room.room(), &cast_mist, recorded);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, main_room.room(), main_room.room()->people, affect);

    affected_type* seeded = room_affected_by_spell(neighbour.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded, nullptr);
    EXPECT_EQ(seeded->counter, 1) << "one room out from the cast room";
    EXPECT_EQ(seeded->duration, 4) << "level 30 falls to 27 one hop out; 27 / 6 = 4";
    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->counter, 0)
        << "the cast room's own generation never changes";
}

// Three rooms out, a level-30 mist is at level 12: it still renews its own room (12 / 5 = 2
// ticks) but the next generation would be level 0, so no seed is placed at all.
TEST(RoomAffectTick, MistTickAtTheThirdGenerationRenewsItselfButSeedsNothing)
{
    RoomFixture deep_room(kMistFalloffDeepRoom);
    RoomFixture beyond(kMistFalloffDeepSeedRoom);
    room_direction_data east_exit {};
    east_exit.to_room = beyond.slot();
    deep_room.room()->dir_option[EAST] = &east_exit;

    CasterFixture caster(25, 0, game_types::PS_None, kMistFalloffDeepRoom); // level 30
    affected_type deep_mist {};
    deep_mist.type = ROOMAFF_SPELL;
    deep_mist.duration = 1;
    deep_mist.modifier = 0;
    deep_mist.location = SPELL_MIST_OF_BAAZUNGA;
    deep_mist.bitvector = 0;
    deep_mist.counter = 3;
    affect_to_room(deep_room.room(), &deep_mist, caster_snapshot::capture(caster.ch));

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, deep_room.room(), deep_room.room()->people, affect);

    EXPECT_EQ(room_affected_by_spell(deep_room.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 2)
        << "renewed from the generation-3 level of 12";
    EXPECT_EQ(room_affected_by_spell(beyond.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "a seed whose level would be 0 is never placed";
    EXPECT_FALSE(IS_SET(beyond.room()->room_flags, SHADOWY)) << "nor does the room darken";
}

// Renewing an existing neighbour from a room closer to the source pulls the neighbour's
// generation in (a mist that reached a room by a long way round is now one hop from the
// cast room) and never pushes it out.
TEST(RoomAffectTick, MistTickRenewsANeighbourAndPullsItsGenerationCloser)
{
    RoomFixture main_room(kMistFalloffRenewMainRoom);
    RoomFixture neighbour(kMistFalloffRenewNeighbourRoom);
    room_direction_data west_exit {};
    west_exit.to_room = neighbour.slot();
    main_room.room()->dir_option[WEST] = &west_exit;
    // The way back, so the reverse tick below reaches the cast room.
    room_direction_data east_exit {};
    east_exit.to_room = main_room.slot();
    neighbour.room()->dir_option[EAST] = &east_exit;

    CasterFixture caster(25, 0, game_types::PS_None, kMistFalloffRenewMainRoom); // level 30
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type cast_mist {};
    cast_mist.type = ROOMAFF_SPELL;
    cast_mist.duration = 6;
    cast_mist.modifier = 0;
    cast_mist.location = SPELL_MIST_OF_BAAZUNGA;
    cast_mist.bitvector = 0;
    cast_mist.counter = 0;
    affect_to_room(main_room.room(), &cast_mist, recorded);

    affected_type far_mist {};
    far_mist.type = ROOMAFF_SPELL;
    far_mist.duration = 1;
    far_mist.modifier = 0;
    far_mist.location = SPELL_MIST_OF_BAAZUNGA;
    far_mist.bitvector = 0;
    far_mist.counter = 3;
    affect_to_room(neighbour.room(), &far_mist, recorded);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, main_room.room(), main_room.room()->people, affect);

    affected_type* renewed = room_affected_by_spell(neighbour.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(renewed, nullptr);
    EXPECT_EQ(renewed->duration, 6) << "renewed against the ticking room's own level / 5, as before";
    EXPECT_EQ(renewed->counter, 1) << "now one hop from the cast room";

    // The reverse never happens: ticking the far room must not push the cast room out.
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, neighbour.room(), neighbour.room()->people, affect);
    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->counter, 0);
}

TEST(RoomAffectTick, UnknownSpellHasNoTickBodyAndReturnsFalse)
{
    RoomFixture room(kUnknownSpellRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);

    affected_type affect = dummy_affect();
    EXPECT_FALSE(room_affect_tick(SPELL_FEAR, room.room(), &occupant, affect))
        << "room_affect_tick() only knows blaze/poison/haze/mist; every other spell must fall "
           "back to affect_update_room()'s historical re-cast";
}

// ---------------------------------------------------------------------------
// affect_update_room() integration
// ---------------------------------------------------------------------------

namespace {

struct RecordedFallback {
    char_data* tmpch = nullptr; // the occupant the stubbed fallback spell_pointer was last called with
    int calls = 0; // how many times the stubbed fallback spell_pointer fired
    bool snapshot_names_caster = false; // whether the last call's caster_at_cast was captured from its caster
};

// What the (stubbed) fallback spell_pointer saw -- this suite's witness that
// affect_update_room()'s historical re-cast arm fired (or did not).
RecordedFallback* g_recorded_fallback_slot = nullptr;

void recording_fallback_spell(char_data* caster, char* /*arg*/, int /*type*/,
    char_data* /*victim*/, obj_data* /*obj*/, int /*digit*/, int /*is_object*/,
    const caster_snapshot& caster_at_cast)
{
    if (g_recorded_fallback_slot != nullptr) {
        g_recorded_fallback_slot->tmpch = caster;
        g_recorded_fallback_slot->snapshot_names_caster = caster_at_cast.same_character_as(*caster);
        ++g_recorded_fallback_slot->calls;
    }
}

} // namespace

// Change #1: a ROOMAFF_SPELL this file has no tick body for must still reach
// the historical self-re-cast fallback.
TEST(RoomAffectTick, AffectUpdateRoomFallsBackToTheHistoricalRecastForAnUnknownSpell)
{
    RoomFixture room(kFallbackRoom);
    ScopedSpellPointer fear_pointer(SPELL_FEAR, recording_fallback_spell);
    RecordedFallback recorded;
    g_recorded_fallback_slot = &recorded;

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.in_room = kFallbackRoom;
    room.room()->people = &occupant;
    occupant.next_in_room = nullptr;

    affected_type unknown_affect {};
    unknown_affect.type = ROOMAFF_SPELL;
    unknown_affect.duration = 5;
    unknown_affect.modifier = 0;
    unknown_affect.location = SPELL_FEAR;
    unknown_affect.bitvector = 0;
    affect_to_room(room.room(), &unknown_affect);

    push_test_random_value(kMidRoll); // movechance = number(1, 100)
    push_test_random_value(0.5 / 13.0); // number(0, 12) == 0 -- the "1 in 13" gate always fires
    affect_update_room(room.room());
    clear_test_random_values();
    g_recorded_fallback_slot = nullptr;

    EXPECT_EQ(recorded.calls, 1)
        << "room_affect_tick() must have returned false for SPELL_FEAR, and the fallback re-cast "
           "must have fired exactly once";
    EXPECT_EQ(recorded.tmpch, &occupant);
    EXPECT_TRUE(recorded.snapshot_names_caster)
        << "run_spell() must hand the body a snapshot of the character it casts for";
}

// Change #1's other half: a spell room_affect_tick() DOES know must never
// reach the fallback re-cast.
TEST(RoomAffectTick, AffectUpdateRoomNeverFallsBackForASpellItOwns)
{
    RoomFixture room(kNoFallbackRoom);
    ScopedSpellPointer haze_pointer(SPELL_HAZE, recording_fallback_spell);
    RecordedFallback recorded;
    g_recorded_fallback_slot = &recorded;

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    test_support::ScopedAffectCleanup occupant_affects(occupant);
    occupant.specials2.perception = 0;
    occupant.in_room = kNoFallbackRoom;
    room.room()->people = &occupant;
    occupant.next_in_room = nullptr;

    CasterFixture caster(0, 10, game_types::PS_None, kNoFallbackRoom);
    set_room_affect_caster(room.room(), SPELL_HAZE, caster_snapshot::capture(caster.ch));

    affected_type haze_affect {};
    haze_affect.type = ROOMAFF_SPELL;
    haze_affect.duration = 5;
    haze_affect.modifier = 0;
    haze_affect.location = SPELL_HAZE;
    haze_affect.bitvector = 0;
    affect_to_room(room.room(), &haze_affect, caster_snapshot::capture(caster.ch));

    push_test_random_value(kMidRoll); // movechance
    push_test_random_value(0.5 / 13.0); // the "1 in 13" gate
    push_test_random_value(kMidRoll); // haze_tick()'s my_duration
    push_test_random_value(0.9); // saves_mystic()'s offense roll
    affect_update_room(room.room());
    clear_test_random_values();
    g_recorded_fallback_slot = nullptr;

    EXPECT_EQ(recorded.calls, 0)
        << "room_affect_tick() handles SPELL_HAZE, so the fallback re-cast must never fire";
    EXPECT_NE(affected_by_spell(&occupant, SPELL_HAZE), nullptr)
        << "the real tick must still have run and applied haze to the occupant";
}

// Change #3: a mist that MOVES keeps its recorded caster, and the (freed)
// source-room affect node is not re-read afterward (the `tmpaf = nullptr`
// fix -- see limits.cpp's affect_update_room()).
TEST(RoomAffectTick, AffectUpdateRoomCarriesTheCasterWhenTheMistMoves)
{
    RoomFixture source_room(kMistMoveSourceRoom);
    RoomFixture dest_room(kMistMoveDestRoom);
    ScopedSpellPointer mist_pointer(SPELL_MIST_OF_BAAZUNGA, recording_fallback_spell);
    room_direction_data north_exit {};
    north_exit.to_room = dest_room.slot();
    source_room.room()->dir_option[NORTH] = &north_exit;
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        if (direction != NORTH) {
            source_room.room()->dir_option[direction] = nullptr;
        }
    }

    CasterFixture caster(25, 0, game_types::PS_None, kMistMoveSourceRoom);
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type mist_affect {};
    mist_affect.type = ROOMAFF_SPELL;
    mist_affect.duration = 5;
    mist_affect.modifier = 0;
    mist_affect.location = SPELL_MIST_OF_BAAZUNGA;
    mist_affect.bitvector = 0;
    mist_affect.counter = 2;
    affect_to_room(source_room.room(), &mist_affect, recorded);

    source_room.room()->room_flags = 0;
    dest_room.room()->room_flags = 0;
    source_room.room()->people = nullptr; // no occupant tick -- only the move code is under test

    const int time_phase_now = get_current_time_phase();
    room_affected_by_spell(source_room.room(), SPELL_MIST_OF_BAAZUNGA)->time_phase = time_phase_now;

    push_test_random_value(0.1); // movechance = number(1, 100) < 75, the mist decides to move
    push_test_random_value(0.0); // direction = number(0, NUM_OF_DIRS - 1) == 0 == NORTH
    testing::internal::CaptureStderr();
    affect_update_room(source_room.room());
    const std::string captured = testing::internal::GetCapturedStderr();
    clear_test_random_values();

    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "nothing may read the freed source-room affect node through a stale reference; stderr "
           "was: "
        << captured;

    const caster_snapshot* moved_caster = room_affect_caster(dest_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(moved_caster, nullptr) << "the mist's destination must carry a recorded caster";
    EXPECT_STREQ(moved_caster->name, recorded.name)
        << "the destination's recorded caster must be the SAME one the source room's mist carried";
    EXPECT_EQ(moved_caster->mage_prof_level, recorded.mage_prof_level);

    affected_type* moved_mist = room_affected_by_spell(dest_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(moved_mist, nullptr);
    EXPECT_EQ(moved_mist->counter, 2) << "a drifting mist keeps its generation";

    EXPECT_EQ(room_affected_by_spell(source_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "the source room's mist affect must have been removed by the move";
    EXPECT_EQ(room_affect_caster(source_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "affect_remove_room() must have erased the source room's caster record along with it";
}

// A mist that drifted spreads from its new room as the generation it carried, not as a fresh
// cast: a level-30 caster's generation-1 mist seeds its new neighbour at generation 2, level
// 30 - 9 = 21, for 21 / 6 = 3 ticks, and the seed carries the same caster.
TEST(RoomAffectTick, AMistThatDriftedSpreadsFromItsNewRoomAtItsCarriedGeneration)
{
    RoomFixture source_room(kMistDriftSourceRoom);
    RoomFixture dest_room(kMistDriftDestRoom);
    RoomFixture spread_room(kMistDriftSpreadRoom);
    ScopedSpellPointer mist_pointer(SPELL_MIST_OF_BAAZUNGA, recording_fallback_spell);
    room_direction_data north_exit {};
    north_exit.to_room = dest_room.slot();
    source_room.room()->dir_option[NORTH] = &north_exit;
    room_direction_data east_exit {};
    east_exit.to_room = spread_room.slot();
    dest_room.room()->dir_option[EAST] = &east_exit;
    source_room.room()->room_flags = 0;
    dest_room.room()->room_flags = 0;
    spread_room.room()->room_flags = 0;

    CasterFixture caster(25, 0, game_types::PS_None, source_room.slot()); // level 30
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type mist_affect {};
    mist_affect.type = ROOMAFF_SPELL;
    mist_affect.duration = 5;
    mist_affect.modifier = 0;
    mist_affect.location = SPELL_MIST_OF_BAAZUNGA;
    mist_affect.bitvector = 0;
    mist_affect.counter = 1;
    affect_to_room(source_room.room(), &mist_affect, recorded);
    room_affected_by_spell(source_room.room(), SPELL_MIST_OF_BAAZUNGA)->time_phase = get_current_time_phase();

    push_test_random_value(0.1); // movechance = number(1, 100) < 75, the mist decides to move
    push_test_random_value(0.0); // direction = number(0, NUM_OF_DIRS - 1) == 0 == NORTH
    affect_update_room(source_room.room());
    clear_test_random_values();
    ASSERT_EQ(room_affected_by_spell(source_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "precondition: the mist drifted out of its source room";
    affected_type* const drifted = room_affected_by_spell(dest_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(drifted, nullptr) << "precondition: the mist drifted into the destination";

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, dest_room.room(), dest_room.room()->people, affect);

    affected_type* const seeded = room_affected_by_spell(spread_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded, nullptr) << "the drifted mist must spread into its new room's empty neighbour";
    EXPECT_EQ(seeded->counter, 2) << "one generation out from the generation-1 mist that drifted";
    EXPECT_EQ(seeded->duration, 3) << "level 30 falls to 21 two hops out; 21 / 6 = 3";
    const caster_snapshot* const seeded_caster = room_affect_caster(spread_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded_caster, nullptr) << "the seed must carry the caster the drifted mist carried";
    EXPECT_TRUE(seeded_caster->same_character_as(caster.ch));
    EXPECT_EQ(room_affected_by_spell(dest_room.room(), SPELL_MIST_OF_BAAZUNGA)->counter, 1)
        << "spreading never changes the ticking room's own generation";
}

// ---------------------------------------------------------------------------
// The CASTING arms themselves (spell_blaze,
// spell_haze, spell_poison's room arm, spell_mist_of_baazunga) record their
// caster's snapshot when they create or strengthen a room affect. These
// tests drive the live ASPELLs directly, unlike the room_affect_tick()
// suite above -- that suite covers what a room affect READS back; this covers
// what casting it WRITES.
//
// All four arms' room-cast path draws nothing from the RNG queue with a
// CasterFixture caster: get_mage_caster_level()/get_mystic_caster_level()'s
// only roll is number(0, intel_factor % 5) / number(0, will_factor % 5),
// and CasterFixture's fixed intel = wil = 25 makes both 25/5 = 5, 5 % 5 ==
// 0 -- a number(0, 0) the wrapped number() returns from `from` without
// consuming the queue (see CasterFixture's own comment above). Blaze's
// occupant damage loop is the one place a draw COULD happen, but every test
// below leaves the cast room's people list empty, so the loop never
// iterates (is_spared_by_room_blast() would spare the caster either way
// -- see mage.cpp's spell_blaze comment). No test in
// this section needs push_test_random_value()/queue_mid_rolls().
// ---------------------------------------------------------------------------

TEST(RoomAffectCasting, BlazeCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kBlazeCastRoom);
    CasterFixture caster(25, 0, game_types::PS_None, room.slot());

    clear_test_random_values();
    test_support::cast_spell(spell_blaze, &caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    clear_test_random_values();

    affected_type* blaze = room_affected_by_spell(room.room(), SPELL_BLAZE);
    ASSERT_NE(blaze, nullptr) << "a fresh cast must have created the room affect";

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr) << "the fresh 3-arg affect_to_room() call must have recorded a caster";
    EXPECT_TRUE(recorded->same_character_as(caster.ch))
        << "the recorded snapshot must resolve back to the casting character";
}

TEST(RoomAffectCasting, BlazeWeakerRecastLeavesThePreviousRecord)
{
    RoomFixture room(kBlazeWeakerRecastRoom);
    CasterFixture strong_caster(30, 0, game_types::PS_None, room.slot()); // level 35
    CasterFixture weak_caster(1, 0, game_types::PS_None, room.slot()); // level 6

    test_support::cast_spell(spell_blaze, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_blaze, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a weaker recast must never raise oldaf->modifier, so it must leave the previously "
           "recorded (stronger) caster in place";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, BlazeStrongerRecastReplacesTheRecord)
{
    RoomFixture room(kBlazeStrongerRecastRoom);
    CasterFixture weak_caster(1, 0, game_types::PS_None, room.slot()); // level 6
    CasterFixture strong_caster(30, 0, game_types::PS_None, room.slot()); // level 35

    test_support::cast_spell(spell_blaze, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_blaze, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a stronger recast raised oldaf->modifier, so it must take the room over";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, HazeCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kHazeCastRoom);
    CasterFixture caster(0, 10, game_types::PS_None, room.slot());

    test_support::cast_spell(spell_haze, &caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* haze = room_affected_by_spell(room.room(), SPELL_HAZE);
    ASSERT_NE(haze, nullptr) << "a fresh cast must have created the room affect";

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_HAZE);
    ASSERT_NE(recorded, nullptr) << "the fresh 3-arg affect_to_room() call must have recorded a caster";
    EXPECT_TRUE(recorded->same_character_as(caster.ch));
}

TEST(RoomAffectCasting, HazeWeakerRecastLeavesThePreviousRecord)
{
    RoomFixture room(kHazeWeakerRecastRoom);
    CasterFixture strong_caster(0, 30, game_types::PS_None, room.slot()); // level 35
    CasterFixture weak_caster(0, 1, game_types::PS_None, room.slot()); // level 6

    test_support::cast_spell(spell_haze, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_haze, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_HAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a weaker recast must never raise oldaf->modifier, so it must leave the recorded caster "
           "in place";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, HazeStrongerRecastReplacesTheRecord)
{
    RoomFixture room(kHazeStrongerRecastRoom);
    CasterFixture weak_caster(0, 1, game_types::PS_None, room.slot()); // level 6
    CasterFixture strong_caster(0, 30, game_types::PS_None, room.slot()); // level 35

    test_support::cast_spell(spell_haze, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_haze, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_HAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a stronger recast raised oldaf->modifier, so it must take the room over";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, PoisonRoomArmCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kPoisonCastRoom);
    CasterFixture caster(0, 10, game_types::PS_None, room.slot());

    test_support::cast_spell(spell_poison, &caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* poison = room_affected_by_spell(room.room(), SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "a fresh cast must have created the room affect";

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_POISON);
    ASSERT_NE(recorded, nullptr) << "the fresh 3-arg affect_to_room() call must have recorded a caster";
    EXPECT_TRUE(recorded->same_character_as(caster.ch));
}

TEST(RoomAffectCasting, PoisonRoomArmWeakerRecastLeavesThePreviousRecord)
{
    RoomFixture room(kPoisonWeakerRecastRoom);
    CasterFixture strong_caster(0, 30, game_types::PS_None, room.slot()); // level 35
    CasterFixture weak_caster(0, 1, game_types::PS_None, room.slot()); // level 6

    test_support::cast_spell(spell_poison, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_poison, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_POISON);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a weaker recast must never raise oldaf->modifier, so it must leave the recorded caster "
           "in place";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, PoisonRoomArmStrongerRecastReplacesTheRecord)
{
    RoomFixture room(kPoisonStrongerRecastRoom);
    CasterFixture weak_caster(0, 1, game_types::PS_None, room.slot()); // level 6
    CasterFixture strong_caster(0, 30, game_types::PS_None, room.slot()); // level 35

    test_support::cast_spell(spell_poison, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    test_support::cast_spell(spell_poison, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_POISON);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a stronger recast raised oldaf->modifier, so it must take the room over";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, MistCastSeedsAFreshAdjacentRoomCarryingTheCaster)
{
    RoomFixture main_room(kMistCastMainRoom);
    RoomFixture adjacent(kMistCastAdjacentRoom);
    room_direction_data north_exit {};
    north_exit.to_room = adjacent.slot();
    main_room.room()->dir_option[NORTH] = &north_exit;

    CasterFixture caster(25, 0, game_types::PS_None, main_room.slot()); // level 30

    test_support::cast_spell(spell_mist_of_baazunga, &caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* main_recorded = room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_recorded, nullptr) << "the fresh main-room seed must have recorded a caster";
    EXPECT_TRUE(main_recorded->same_character_as(caster.ch));

    const caster_snapshot* adjacent_recorded = room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(adjacent_recorded, nullptr)
        << "an empty adjacent room must be freshly seeded carrying the SAME caster";
    EXPECT_TRUE(adjacent_recorded->same_character_as(caster.ch));

    EXPECT_EQ(room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA)->counter, 1)
        << "the adjacent seed is one generation out from the cast room";
    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->counter, 0)
        << "the room the mist was breathed in is generation 0";
    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 6)
        << "the cast room lasts level 30 / 5 = 6";
    EXPECT_EQ(room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 4)
        << "level 30 falls to 27 one hop out; 27 / 6 = 4";
}

// A caster too weak for the one-hop falloff to leave a lasting seed breathes a mist only
// into the room they stand in.
TEST(RoomAffectCasting, MistCastByAWeakCasterSeedsNoAdjacentRoom)
{
    RoomFixture main_room(kMistCastWeakMainRoom);
    RoomFixture adjacent(kMistCastWeakAdjacentRoom);
    room_direction_data south_exit {};
    south_exit.to_room = adjacent.slot();
    main_room.room()->dir_option[SOUTH] = &south_exit;
    main_room.room()->room_flags = 0;
    adjacent.room()->room_flags = 0;

    // level 6: main dur 1; one hop out the level is 3, and 3 / 6 = 0
    CasterFixture weak_caster(1, 0, game_types::PS_None, main_room.slot());

    test_support::cast_spell(spell_mist_of_baazunga, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* main_mist = room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_mist, nullptr) << "the caster's own room is always misted";
    EXPECT_EQ(main_mist->duration, 1) << "level 6 / 5 = 1";

    EXPECT_EQ(room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "a seed of level 3 / 6 = 0 ticks is never placed";
    EXPECT_EQ(room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "nor is a caster recorded for it";
    EXPECT_FALSE(IS_SET(adjacent.room()->room_flags, SHADOWY)) << "nor does the room darken";
}

// A cast that renews its own room makes that room the mist's source (generation 0), and a
// renewed neighbour is pulled in to one hop out, however far its mist had come.
TEST(RoomAffectCasting, MistCastRenewalResetsTheMainGenerationAndPullsTheAdjacentOneIn)
{
    RoomFixture main_room(kMistCastGenerationMainRoom);
    RoomFixture adjacent(kMistCastGenerationAdjacentRoom);
    room_direction_data west_exit {};
    west_exit.to_room = adjacent.slot();
    main_room.room()->dir_option[WEST] = &west_exit;

    CasterFixture caster(25, 0, game_types::PS_None, main_room.slot()); // level 30
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type drifted_mist {};
    drifted_mist.type = ROOMAFF_SPELL;
    drifted_mist.duration = 1;
    drifted_mist.modifier = 0;
    drifted_mist.location = SPELL_MIST_OF_BAAZUNGA;
    drifted_mist.bitvector = 0;
    drifted_mist.counter = 2;
    affect_to_room(main_room.room(), &drifted_mist, recorded);

    affected_type far_mist {};
    far_mist.type = ROOMAFF_SPELL;
    far_mist.duration = 1;
    far_mist.modifier = 0;
    far_mist.location = SPELL_MIST_OF_BAAZUNGA;
    far_mist.bitvector = 0;
    far_mist.counter = 3;
    affect_to_room(adjacent.room(), &far_mist, recorded);

    test_support::cast_spell(spell_mist_of_baazunga, &caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* main_mist = room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_mist, nullptr);
    EXPECT_EQ(main_mist->duration, 6) << "renewed to level 30 / 5 = 6";
    EXPECT_EQ(main_mist->counter, 0) << "a drifted mist the caster renews becomes the cast room";

    affected_type* adjacent_mist = room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(adjacent_mist, nullptr);
    EXPECT_EQ(adjacent_mist->duration, 6)
        << "renewed against the MAIN room's level 30 / 5 = 6, the cast's long-standing quirk";
    EXPECT_EQ(adjacent_mist->counter, 1) << "a generation-3 mist is pulled in to one hop out";
}

TEST(RoomAffectCasting, MistCastLongerDurationRenewalReplacesTheRecordInMainAndAdjacent)
{
    RoomFixture main_room(kMistRenewMainRoom);
    RoomFixture adjacent(kMistRenewAdjacentRoom);
    room_direction_data east_exit {};
    east_exit.to_room = adjacent.slot();
    main_room.room()->dir_option[EAST] = &east_exit;

    // level 12: main dur 2, adjacent dur 1 (level 9 one hop out)
    CasterFixture weak_caster(7, 0, game_types::PS_None, main_room.slot());
    // level 30: main dur 6, adjacent dur 4 (level 27 one hop out)
    CasterFixture strong_caster(25, 0, game_types::PS_None, main_room.slot());

    // Seed both rooms weakly first, recording weak_caster in each.
    test_support::cast_spell(spell_mist_of_baazunga, &weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    ASSERT_NE(room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr);
    ASSERT_NE(room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA), nullptr);
    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 2)
        << "the weak cast lasts level 12 / 5 = 2";
    EXPECT_EQ(room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 1)
        << "the weak seed lasts level 9 / 6 = 1";

    // A stronger recast: main's duration (2) is raised to 6, and the adjacent
    // room's own (quirky) comparison is against the MAIN room's new af.duration
    // (6) too -- see mage.cpp's spell_mist_of_baazunga comment -- so both
    // renewals fire and both records must move to strong_caster.
    test_support::cast_spell(spell_mist_of_baazunga, &strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* main_recorded = room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_recorded, nullptr);
    EXPECT_TRUE(main_recorded->same_character_as(strong_caster.ch))
        << "the main room's longer-duration renewal must replace the recorded caster";
    EXPECT_FALSE(main_recorded->same_character_as(weak_caster.ch));

    const caster_snapshot* adjacent_recorded = room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(adjacent_recorded, nullptr);
    EXPECT_TRUE(adjacent_recorded->same_character_as(strong_caster.ch))
        << "the adjacent room's longer-duration renewal must also replace the recorded caster";
    EXPECT_FALSE(adjacent_recorded->same_character_as(weak_caster.ch));

    EXPECT_EQ(room_affected_by_spell(main_room.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 6)
        << "the strong recast raises main to level 30 / 5 = 6";
    EXPECT_EQ(room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA)->duration, 6)
        << "the adjacent renewal compares against the MAIN room's 6";
}
