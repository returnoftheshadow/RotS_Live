// TASK-021 port, Task 10. room_affect_tick() replaces affect_update_room()'s
// self-re-cast arm -- the historical tick that ran
// `(skills[loc].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`, i.e.
// re-cast the room's spell with the OCCUPANT standing in for the caster. Every
// formula input therefore came from the victim: a level-1 mob walking into a
// grandmaster's blaze took a level-1 blaze, and nobody was ever credited with
// the kill.
//
// This depot has no extract_char test seam and no rots::entity hook system
// (see the test adaptation policy in global-constraints.md), so this file
// reuses the depot's own established idioms rather than porting the modern
// suite's fixture-by-fixture: the heap-NPC/register_npc_char()/ScopedMobIndex
// death-path idiom (fight_credit_tests.cpp), the gear-move oracle for "who was
// credited without a kill-contributor ledger to read back"
// (fight_credit_tests.cpp), the (room, spell) caster-store fixture
// (room_affect_caster_tests.cpp's RoomAffectGuard), and the descriptor output
// capture used to assert on act()/send_to_char() text (spell_pa_tests.cpp).
//
// WHAT THESE TESTS PROVE. A "the tick matches a live re-cast" equivalence test
// would be vacuous: the live helper forms are one-line forwarders onto the
// snapshot forms (Tasks 6/7), so both sides run the same body no matter which
// fields the tick reads. The blaze pin below therefore varies the RECORDED
// snapshot away from the caster's current (live, wrecked) stats and shows the
// tick still follows the recording. The poison/haze/mist pins pick caster
// fields (willpower/perception for saves_poison(), multiples of 25 for the
// mage/mystic level remainder roll) that make the formula's OWN outcome
// deterministic without needing to predict every draw inside damage_credited()
// -- so the assertions read the tick's own state changes (affect duration,
// resolve_poisoner(), room_affect_caster()) rather than an exact hit-point
// count wrung out of the whole damage() pipeline.

#include "../caster_snapshot.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../room_affect_tick.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_random_utils.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <string>

extern struct room_data world;
extern int top_of_world;
extern struct char_data* character_list;
extern struct obj_data* object_list;
extern struct index_data* mob_index;
extern struct skill_data skills[];

void affect_update_room(struct room_data* room);

namespace {

void ensure_test_world(int minimum_room_number)
{
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

// Rooms this suite claims within the shared test-binary world[] -- high,
// out-of-band values distinct from every other suite (affect_update_tests:
// 27/28; mage_tests: up to 32; fight_credit_tests: 900-903;
// room_affect_caster_tests: 950-953; interpre_account_menu/spell_pa/
// db_loader_tests: 1200/3001/3002).
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

// TASK-021 port, Task 11: rooms exercising the CASTING arms (spell_blaze,
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
// platform-divergence risk (global-constraints.md's RNG-pinning policy is
// about values that land a hair BELOW an integer boundary from rounding
// error; an exact 0.5 * N never does).
constexpr double kMidRoll = 0.5;

void queue_mid_rolls(int count = 60)
{
    for (int i = 0; i < count; ++i)
        push_test_random_value(kMidRoll);
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Saves/restores everything a test in this file might touch on one room:
// occupants, corpse contents, exits, flags, and the room's own affect list
// (which also empties the (room, spell) caster store for every affect this
// scope leaves behind, since affect_remove_room() is what erases it). Stamps
// a distinct room->number, since the caster store is keyed on it and
// dummy/default rooms in the shared world[] all carry -1.
class RoomFixture {
public:
    explicit RoomFixture(int room_number)
        : m_room_number(room_number)
    {
        ensure_test_world(room_number);
        room_data& room = world[room_number];
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
        for (int i = 0; i < NUM_OF_DIRS; ++i) {
            m_original_exits[i] = room.dir_option[i];
            // Every exit starts cleared: dir_option[] normally points at
            // stack-local room_direction_data owned by whichever test set it
            // last, and the shared test-binary world[] carries that pointer
            // across tests -- leaving a prior test's exit in place here would
            // read a dangling pointer the moment this room's own test walks
            // its exits (mist_tick()'s spread/move code does exactly that).
            room.dir_option[i] = nullptr;
        }
        room.number = 2000 + room_number;
        room.people = nullptr;
        room.contents = nullptr;
    }
    ~RoomFixture()
    {
        room_data& room = world[m_room_number];
        while (room.affected)
            affect_remove_room(&room, room.affected);
        room.people = m_original_people;
        room.contents = m_original_contents;
        room.number = m_original_number;
        room.room_flags = m_original_room_flags;
        room.light = m_original_light;
        for (int i = 0; i < NUM_OF_DIRS; ++i)
            room.dir_option[i] = m_original_exits[i];
    }
    RoomFixture(const RoomFixture&) = delete;
    RoomFixture& operator=(const RoomFixture&) = delete;

    room_data* room() const { return &world[m_room_number]; }

private:
    int m_room_number;
    char_data* m_original_people;
    obj_data* m_original_contents;
    int m_original_number;
    long m_original_room_flags;
    byte m_original_light;
    room_direction_data* m_original_exits[NUM_OF_DIRS];
};

// The death pipeline reads an NPC's prototype twice (raw_kill()'s
// SPECIAL_DEATH probe and make_physical_corpse()'s corpse-owner id); publish
// a one-entry table with no spec-proc for the scope. Mirrors
// fight_credit_tests.cpp's ScopedMobIndex.
class ScopedMobIndex {
public:
    ScopedMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedMobIndex() { mob_index = m_previous; }
    ScopedMobIndex(const ScopedMobIndex&) = delete;
    ScopedMobIndex& operator=(const ScopedMobIndex&) = delete;

private:
    index_data* m_previous;
    index_data m_entry {};
};

// Registers an abs_number AND records the pointer, the way
// register_npc_char() does -- caster_snapshot::resolve() needs both halves.
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
    char_data& m_ch;
};

// Installs a spell pointer into skills[slot] for the scope and restores
// whatever was there -- gtest_main does not run assign_spell_pointers().
class ScopedSpellPointer {
public:
    ScopedSpellPointer(int slot, void (*fn)(char_data*, char*, int, char_data*, obj_data*, int, int))
        : m_slot(slot)
        , m_previous(skills[slot].spell_pointer)
    {
        skills[slot].spell_pointer = fn;
    }
    ~ScopedSpellPointer() { skills[m_slot].spell_pointer = m_previous; }
    ScopedSpellPointer(const ScopedSpellPointer&) = delete;
    ScopedSpellPointer& operator=(const ScopedSpellPointer&) = delete;

private:
    int m_slot;
    void (*m_previous)(char_data*, char*, int, char_data*, obj_data*, int, int);
};

// A one-item container carried (not worn) by a corpse's owner:
// make_physical_corpse() always moves carried objects into the corpse intact,
// but only recurses into containers -- pulling the wearable out to sit
// directly in the corpse -- when move_wearables_to_corpse() runs, which is
// exactly the `!IS_NPC(killer)` (or SPELL_POISON) branch. Mirrors
// fight_credit_tests.cpp's CarriedGear.
struct CarriedGear {
    obj_data container {};
    obj_data item {};

    void attach_to(char_data& owner)
    {
        container.obj_flags.type_flag = ITEM_CONTAINER;
        item.obj_flags.type_flag = ITEM_ARMOR;
        container.contains = &item;
        item.in_obj = &container;
        owner.carrying = &container;
    }
};

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents
// and object_list; take both back out. Mirrors fight_credit_tests.cpp's
// release_corpse().
void release_corpse(room_data& room, obj_data* previous_object_list)
{
    obj_data* corpse = room.contents;
    if (corpse == nullptr)
        return;
    obj_from_room(corpse);
    if (object_list == corpse)
        object_list = corpse->next;
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    RELEASE(corpse);
    object_list = previous_object_list;
}

// Points a descriptor's output at its OWN small_outbuf so act()/send_to_char()
// output can be read back instead of going to a socket. Mirrors
// spell_pa_tests.cpp's make_descriptor().
descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
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
    char_data* occupant = new char_data {};
    clear_char(occupant, MOB_ISNPC);
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
    char_data ch {};
    char_prof_data profs {};
    char name[24] = "tick_caster";

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
// blaze: a lethal tick credits the recorded caster and never engages it
// ---------------------------------------------------------------------------
TEST(RoomAffectTick, BlazeTickCreditsTheRecordedCasterWithoutEngagingIt)
{
    ScopedMobIndex prototype_table;
    RoomFixture occupant_room(kBlazeRoomA);
    RoomFixture caster_room(kAwayRoom);

    char occupant_short_descr[] = "a testing blaze victim";
    char_data* occupant = make_heap_occupant(kBlazeRoomA, occupant_short_descr, 1); // any blaze tick is lethal
    character_list = occupant;
    occupant->next = nullptr;
    occupant_room.room()->people = occupant;
    occupant->next_in_room = nullptr;

    CasterFixture caster(25, 0, game_types::PS_None, kAwayRoom); // standing elsewhere entirely
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
    EXPECT_EQ(caster.ch.specials.fighting, nullptr)
        << "the credited caster must never be engaged by the tick that kills through it -- "
           "damage_credited() only ever engages the occupant itself";

    obj_data* const corpse = occupant_room.room()->contents;
    ASSERT_NE(corpse, nullptr) << "raw_kill() must have created a corpse in the death room";
    EXPECT_EQ(gear.container.contains, nullptr)
        << "move_wearables_to_corpse() only runs for a non-NPC killer -- the recorded (PC) caster "
           "-- so the wearable being pulled out of its container is this test's signature that "
           "the caster, not the (NPC) occupant, was credited with the kill";
    EXPECT_EQ(gear.item.in_obj, corpse);

    release_corpse(*occupant_room.room(), previous_object_list);
}

// ---------------------------------------------------------------------------
// poison
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, PoisonTickRecordsTheResolvedCasterAsPoisoner)
{
    RoomFixture room(kPoisonRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);

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

    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
}

TEST(RoomAffectTick, PoisonTickWithNoRecordedCasterFallsBackToOccupantStatsAndRecordsNoPoisoner)
{
    RoomFixture room(kPoisonNoRecordRoom);
    // No set_room_affect_caster() call at all: room_affect_caster() answers nullptr.

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);

    EXPECT_EQ(resolve_poisoner(occupant), nullptr)
        << "a builder-placed affect with no recorded caster must credit nobody as the poisoner";
    affected_type* poison = affected_by_spell(&occupant, SPELL_POISON);
    ASSERT_NE(poison, nullptr);
    // duration = get_mystic_caster_level(capture(occupant)) + 1 = (0 + 0/5) + 1 = 1, i.e. the
    // OCCUPANT's own (weak) stats stand in, matching the pre-TASK-021 self-re-cast shape.
    EXPECT_EQ(poison->duration, 1);

    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
}

TEST(RoomAffectTick, PoisonTickWithAnExplicitNoneRecordRecordsNoPoisoner)
{
    RoomFixture room(kPoisonNoneRoom);
    set_room_affect_caster(room.room(), SPELL_POISON, caster_snapshot::none());

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);

    EXPECT_EQ(resolve_poisoner(occupant), nullptr)
        << "an explicit caster_snapshot::none() record must credit nobody, exactly like no record "
           "at all";

    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
}

TEST(RoomAffectTick, PoisonTickSavedArmMessagesTheOccupantAndThePresentCaster)
{
    RoomFixture room(kPoisonSavedPresentRoom);

    char_data occupant {};
    char_prof_data occupant_profs {};
    make_weak_occupant(occupant, occupant_profs, 500);
    occupant.tmpabilities.con = 100; // defense = 500, so saves_poison()'s comparison is never zero
    occupant.in_room = kPoisonSavedPresentRoom;
    descriptor_data occupant_descriptor = make_descriptor();
    occupant.desc = &occupant_descriptor;

    CasterFixture caster(0, 10, game_types::PS_None, kPoisonSavedPresentRoom); // same room: "present"
    ScopedCharExists caster_registration(caster.ch, kCasterASlot);
    descriptor_data caster_descriptor = make_descriptor();
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
    descriptor_data occupant_descriptor = make_descriptor();
    occupant.desc = &occupant_descriptor;

    CasterFixture caster(0, 10, game_types::PS_None, kAwayRoom); // recorded in the poison room, standing elsewhere
    ScopedCharExists caster_registration(caster.ch, kCasterBSlot);
    descriptor_data caster_descriptor = make_descriptor();
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
    descriptor_data occupant_descriptor = make_descriptor();
    occupant.desc = &occupant_descriptor;

    affected_type affect = dummy_affect();
    queue_mid_rolls();
    room_affect_tick(SPELL_POISON, room.room(), &occupant, affect);
    clear_test_random_values();

    const std::string occupant_output = occupant_descriptor.output;
    EXPECT_NE(occupant_output.find("fend off the poison"), std::string::npos)
        << "with no caster to anchor act() on, the victim-facing line must still be delivered "
           "directly: "
        << occupant_output;
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

    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
}

// ---------------------------------------------------------------------------
// mist
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, MistTickRenewsFromTheSnapshotLevelAndNeverShortensAStrongerMist)
{
    RoomFixture main_room(kMistMainRoom);
    RoomFixture stronger_adjacent(kMistStrongerAdjacentRoom);
    room_direction_data east_exit {};
    east_exit.to_room = kMistStrongerAdjacentRoom;
    main_room.room()->dir_option[EAST] = &east_exit;

    // level = get_mage_caster_level(who) = 25 + 25/5 = 30 -> level/5 = 6, level/6 = 5.
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
    north_exit.to_room = kMistAdjacentRoom;
    main_room.room()->dir_option[NORTH] = &north_exit;

    // level = 25 + 25/5 = 30 -> the fresh adjacent seed carries level/6 = 5.
    CasterFixture caster(25, 0, game_types::PS_None, kMistMainRoom);
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);
    set_room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA, recorded);

    affected_type affect = dummy_affect();
    room_affect_tick(SPELL_MIST_OF_BAAZUNGA, main_room.room(), main_room.room()->people, affect);

    affected_type* seeded = room_affected_by_spell(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded, nullptr) << "an empty adjacent room must be freshly seeded";
    EXPECT_EQ(seeded->duration, 5);

    const caster_snapshot* seeded_caster = room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded_caster, nullptr)
        << "the fresh seed must carry the SAME caster the main room's mist was ticked from";
    EXPECT_STREQ(seeded_caster->name, recorded.name);
    EXPECT_EQ(seeded_caster->mage_prof_level, recorded.mage_prof_level);
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
    char_data* tmpch = nullptr;
    int calls = 0;
};

// What the (stubbed) fallback spell_pointer saw -- this suite's witness that
// affect_update_room()'s historical re-cast arm fired (or did not).
RecordedFallback* g_recorded_fallback_slot = nullptr;

void recording_fallback_spell(char_data* caster, char* /*arg*/, int /*type*/,
    char_data* /*victim*/, obj_data* /*obj*/, int /*digit*/, int /*is_object*/)
{
    if (g_recorded_fallback_slot != nullptr) {
        g_recorded_fallback_slot->tmpch = caster;
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

    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
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
    north_exit.to_room = kMistMoveDestRoom;
    source_room.room()->dir_option[NORTH] = &north_exit;
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction)
        if (direction != NORTH)
            source_room.room()->dir_option[direction] = nullptr;

    CasterFixture caster(25, 0, game_types::PS_None, kMistMoveSourceRoom);
    const caster_snapshot recorded = caster_snapshot::capture(caster.ch);

    affected_type mist_affect {};
    mist_affect.type = ROOMAFF_SPELL;
    mist_affect.duration = 5;
    mist_affect.modifier = 0;
    mist_affect.location = SPELL_MIST_OF_BAAZUNGA;
    mist_affect.bitvector = 0;
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

    EXPECT_EQ(room_affected_by_spell(source_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "the source room's mist affect must have been removed by the move";
    EXPECT_EQ(room_affect_caster(source_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr)
        << "affect_remove_room() must have erased the source room's caster record along with it";
}

// ---------------------------------------------------------------------------
// TASK-021 port, Task 11: the CASTING arms themselves (spell_blaze,
// spell_haze, spell_poison's room arm, spell_mist_of_baazunga) record their
// caster's snapshot when they create or strengthen a room affect. These
// tests drive the live ASPELLs directly, unlike the room_affect_tick()
// suite above -- Task 10 covers what a room affect READS back; this covers
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
// iterates (is_friendly_taget() would skip the caster as friendly to
// itself either way -- see mage.cpp's spell_blaze comment). No test in
// this section needs push_test_random_value()/queue_mid_rolls().
// ---------------------------------------------------------------------------

TEST(RoomAffectCasting, BlazeCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kBlazeCastRoom);
    CasterFixture caster(25, 0, game_types::PS_None, kBlazeCastRoom);

    clear_test_random_values();
    spell_blaze(&caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
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
    CasterFixture strong_caster(30, 0, game_types::PS_None, kBlazeWeakerRecastRoom); // level 35
    CasterFixture weak_caster(1, 0, game_types::PS_None, kBlazeWeakerRecastRoom); // level 6

    spell_blaze(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_blaze(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

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
    CasterFixture weak_caster(1, 0, game_types::PS_None, kBlazeStrongerRecastRoom); // level 6
    CasterFixture strong_caster(30, 0, game_types::PS_None, kBlazeStrongerRecastRoom); // level 35

    spell_blaze(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_blaze(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a stronger recast raised oldaf->modifier, so it must take the room over";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, HazeCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kHazeCastRoom);
    CasterFixture caster(0, 10, game_types::PS_None, kHazeCastRoom);

    spell_haze(&caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* haze = room_affected_by_spell(room.room(), SPELL_HAZE);
    ASSERT_NE(haze, nullptr) << "a fresh cast must have created the room affect";

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_HAZE);
    ASSERT_NE(recorded, nullptr) << "the fresh 3-arg affect_to_room() call must have recorded a caster";
    EXPECT_TRUE(recorded->same_character_as(caster.ch));
}

TEST(RoomAffectCasting, HazeWeakerRecastLeavesThePreviousRecord)
{
    RoomFixture room(kHazeWeakerRecastRoom);
    CasterFixture strong_caster(0, 30, game_types::PS_None, kHazeWeakerRecastRoom); // level 35
    CasterFixture weak_caster(0, 1, game_types::PS_None, kHazeWeakerRecastRoom); // level 6

    spell_haze(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_haze(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

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
    CasterFixture weak_caster(0, 1, game_types::PS_None, kHazeStrongerRecastRoom); // level 6
    CasterFixture strong_caster(0, 30, game_types::PS_None, kHazeStrongerRecastRoom); // level 35

    spell_haze(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_haze(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_HAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->same_character_as(strong_caster.ch))
        << "a stronger recast raised oldaf->modifier, so it must take the room over";
    EXPECT_FALSE(recorded->same_character_as(weak_caster.ch));
}

TEST(RoomAffectCasting, PoisonRoomArmCastRecordsTheCasterSnapshot)
{
    RoomFixture room(kPoisonCastRoom);
    CasterFixture caster(0, 10, game_types::PS_None, kPoisonCastRoom);

    spell_poison(&caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    affected_type* poison = room_affected_by_spell(room.room(), SPELL_POISON);
    ASSERT_NE(poison, nullptr) << "a fresh cast must have created the room affect";

    const caster_snapshot* recorded = room_affect_caster(room.room(), SPELL_POISON);
    ASSERT_NE(recorded, nullptr) << "the fresh 3-arg affect_to_room() call must have recorded a caster";
    EXPECT_TRUE(recorded->same_character_as(caster.ch));
}

TEST(RoomAffectCasting, PoisonRoomArmWeakerRecastLeavesThePreviousRecord)
{
    RoomFixture room(kPoisonWeakerRecastRoom);
    CasterFixture strong_caster(0, 30, game_types::PS_None, kPoisonWeakerRecastRoom); // level 35
    CasterFixture weak_caster(0, 1, game_types::PS_None, kPoisonWeakerRecastRoom); // level 6

    spell_poison(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_poison(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

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
    CasterFixture weak_caster(0, 1, game_types::PS_None, kPoisonStrongerRecastRoom); // level 6
    CasterFixture strong_caster(0, 30, game_types::PS_None, kPoisonStrongerRecastRoom); // level 35

    spell_poison(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    spell_poison(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

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
    north_exit.to_room = kMistCastAdjacentRoom;
    main_room.room()->dir_option[NORTH] = &north_exit;

    CasterFixture caster(25, 0, game_types::PS_None, kMistCastMainRoom); // level 30

    spell_mist_of_baazunga(&caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    const caster_snapshot* main_recorded = room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(main_recorded, nullptr) << "the fresh main-room seed must have recorded a caster";
    EXPECT_TRUE(main_recorded->same_character_as(caster.ch));

    const caster_snapshot* adjacent_recorded = room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(adjacent_recorded, nullptr)
        << "an empty adjacent room must be freshly seeded carrying the SAME caster";
    EXPECT_TRUE(adjacent_recorded->same_character_as(caster.ch));
}

TEST(RoomAffectCasting, MistCastLongerDurationRenewalReplacesTheRecordInMainAndAdjacent)
{
    RoomFixture main_room(kMistRenewMainRoom);
    RoomFixture adjacent(kMistRenewAdjacentRoom);
    room_direction_data east_exit {};
    east_exit.to_room = kMistRenewAdjacentRoom;
    main_room.room()->dir_option[EAST] = &east_exit;

    CasterFixture weak_caster(1, 0, game_types::PS_None, kMistRenewMainRoom); // level 6: main dur 1, adj dur 1
    CasterFixture strong_caster(25, 0, game_types::PS_None, kMistRenewMainRoom); // level 30: main dur 6, adj dur 5

    // Seed both rooms weakly first, recording weak_caster in each.
    spell_mist_of_baazunga(&weak_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);
    ASSERT_NE(room_affect_caster(main_room.room(), SPELL_MIST_OF_BAAZUNGA), nullptr);
    ASSERT_NE(room_affect_caster(adjacent.room(), SPELL_MIST_OF_BAAZUNGA), nullptr);

    // A stronger recast: main's duration (1) is raised to 6, and the adjacent
    // room's own (quirky) comparison is against the MAIN room's new af.duration
    // (6) too -- see mage.cpp's spell_mist_of_baazunga comment -- so both
    // renewals fire and both records must move to strong_caster.
    spell_mist_of_baazunga(&strong_caster.ch, nullptr, SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

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
}
