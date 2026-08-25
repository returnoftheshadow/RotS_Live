// TASK-021 port, Task 9: the room-affect caster store -- a side map from
// (room number, spell) to the caster_snapshot recorded when a ROOMAFF_SPELL
// affect was cast into that room. Lives beside the room's own affected_type
// list rather than inside it: affected_type is embedded in char_file_u (the
// legacy binary player-file layout) and must not grow.
//
// Mirrors the modern depot's RoomAffectCaster test suite
// (caster_snapshot_tests.cpp:513-540, RotS_Live_Modern), adapted to this
// depot's world fixture idiom (affect_update_tests.cpp's
// ensure_test_world()/RoomOccupantGuard) since this depot has no
// ScopedTestWorld/room_by_id_total() helpers.
#include "../caster_snapshot.h"
#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include <gtest/gtest.h>

extern struct room_data world;
extern int top_of_world;

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

// Room numbers this file claims within the shared test-binary `world[]` --
// high, out-of-band values distinct from the other suites' rooms
// (affect_update_tests.cpp: 27/28; mage_tests.cpp: up to 32;
// fight_credit_tests.cpp: 900-903; interpre_account_menu_tests.cpp/
// spell_pa_tests.cpp/db_loader_tests.cpp: 1200/3001/3002).
constexpr int kFirstRoom = 950;
constexpr int kSecondRoom = 951;
constexpr int kThirdRoom = 952;
constexpr int kUnusedRoom = 953;

// Saves/restores one room's affect list and `.number` field so a test leaves
// the shared world[] state exactly as it found it for later tests in this
// binary. Mirrors affect_update_tests.cpp's RoomOccupantGuard.
//
// `.number` is stamped explicitly: room_data::room_data() default-initializes
// it to -1, and world[i]'s raw array slot (unlike the real create_room()
// path) never sets it to the slot's own index -- interpre_account_menu_tests.cpp/
// db_loader_tests.cpp/spell_pa_tests.cpp already stamp it the same way for
// their own world[] fixtures. The store below is keyed on `room->number`, so
// leaving it at the shared default -1 would key every test room under this
// depot's -1 sentinel rather than the room this test actually means.
struct RoomAffectGuard {
    int room_number; // which world[] slot this guard owns for the scope
    affected_type* original_affected; // whatever was linked there before the test
    int original_number; // whatever .number was set to before the test

    explicit RoomAffectGuard(int room)
        : room_number(room)
        , original_affected(nullptr)
        , original_number(-1)
    {
        ensure_test_world(room);
        original_affected = world[room].affected;
        original_number = world[room].number;
        world[room].number = room;
    }
    ~RoomAffectGuard()
    {
        world[room_number].affected = original_affected;
        world[room_number].number = original_number;
    }
    RoomAffectGuard(const RoomAffectGuard&) = delete;
    RoomAffectGuard& operator=(const RoomAffectGuard&) = delete;
};

// A minimally-populated mage caster, matching caster_snapshot_tests.cpp's
// CasterSnapshotTestContext field-for-field so a captured snapshot has
// non-default values worth asserting on.
struct CasterContext {
    char_data character {}; // the mage caster under test; snapshotted by each pin below
    char_prof_data profs {}; // backs character.profs; holds the prof level capture() reads
    char name_storage[16] = "test-mage"; // backs character.player.name (GET_NAME()); owned for the context's lifetime

    CasterContext()
    {
        character.profs = &profs;
        character.player.name = name_storage;
        profs.prof_level[PROF_MAGE] = 25;
        character.player.level = 30;
        character.player.race = RACE_HUMAN;
        character.tmpabilities.intel = 21;
    }
};

// A room-affect affected_type, matching how mage.cpp/mystic.cpp cast a
// ROOMAFF_SPELL (see affect_update_tests.cpp's inline blaze construction).
affected_type room_spell_affect(int spell, int duration, int modifier)
{
    affected_type af {};
    af.type = ROOMAFF_SPELL;
    af.duration = duration;
    af.modifier = modifier;
    af.location = spell;
    af.bitvector = 0;
    return af;
}

} // namespace

// The 3-argument affect_to_room() records the caster's snapshot; a fresh
// room_affect_caster() lookup returns it, and affect_remove_room() erases it
// again -- the two halves of the store's lifecycle in one round trip.
TEST(RoomAffectCaster, ThreeArgAffectToRoomRecordsTheSnapshotAndRemoveErasesIt)
{
    RoomAffectGuard guard(kFirstRoom);
    room_data* room = &world[kFirstRoom];
    ASSERT_EQ(room->affected, nullptr) << "the room must start with no affects";

    CasterContext caster;
    affected_type blaze = room_spell_affect(SPELL_BLAZE, 3, 20);
    affect_to_room(room, &blaze, caster_snapshot::capture(caster.character));

    const caster_snapshot* recorded = room_affect_caster(room, SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_FALSE(recorded->is_none());
    EXPECT_EQ(recorded->mage_prof_level, 25);
    EXPECT_STREQ(recorded->name, "test-mage");

    affect_remove_room(room, room_affected_by_spell(room, SPELL_BLAZE));
    EXPECT_EQ(room_affect_caster(room, SPELL_BLAZE), nullptr)
        << "affect_remove_room() must erase the store entry along with the affect";
    EXPECT_EQ(room->affected, nullptr);
}

// The 2-argument affect_to_room() (the builder/OLC path, and every other
// historical call site that has no caster) backfills caster_snapshot::none()
// when the (room, spell) pair has no record yet.
TEST(RoomAffectCaster, TwoArgAffectToRoomBackfillsNone)
{
    RoomAffectGuard guard(kSecondRoom);
    room_data* room = &world[kSecondRoom];
    ASSERT_EQ(room->affected, nullptr) << "the room must start with no affects";

    affected_type haze = room_spell_affect(SPELL_HAZE, 3, 5);
    affect_to_room(room, &haze);

    const caster_snapshot* recorded = room_affect_caster(room, SPELL_HAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->is_none())
        << "a ROOMAFF_SPELL cast through the two-argument form must record nobody, not leave "
           "the (room, spell) pair unrecorded";

    affect_remove_room(room, room_affected_by_spell(room, SPELL_HAZE));
    EXPECT_EQ(room_affect_caster(room, SPELL_HAZE), nullptr);
}

// An (room, spell) pair that was never recorded returns nullptr rather than
// a default-constructed or dangling entry. Uses its own RoomAffectGuard (and
// therefore its own stamped .number, distinct from every other room this
// file or affect_update_tests.cpp uses) so this is a genuinely unclaimed key
// rather than relying on the shared -1 default every un-stamped world[]
// slot in this depot starts with.
TEST(RoomAffectCaster, UnknownRoomAndSpellReturnsNullptr)
{
    RoomAffectGuard guard(kUnusedRoom);
    room_data* room = &world[kUnusedRoom];
    EXPECT_EQ(room_affect_caster(room, SPELL_BLAZE), nullptr);
    EXPECT_EQ(room_affect_caster(room, SPELL_HAZE), nullptr);
}

// Documents the Task 10 copy-before-remove hazard: room_affect_caster()
// hands back a pointer INTO the store, and affect_remove_room() erases the
// very entry that pointer aims at. This test pins the contract by taking the
// pointer, removing the affect, and asserting that a FRESH lookup returns
// nullptr -- it deliberately never dereferences the stale pointer itself
// (doing so would be a use-after-free the way room_affect_tick.cpp's mist
// caster must not risk it; see combat-credit-and-store.diff's
// affect_update_room() mist-move site, which copies the record into a local
// caster_snapshot BEFORE calling affect_remove_room() for exactly this
// reason).
TEST(RoomAffectCaster, PointerFromRoomAffectCasterIsInvalidatedByRemoval)
{
    RoomAffectGuard guard(kThirdRoom);
    room_data* room = &world[kThirdRoom];
    ASSERT_EQ(room->affected, nullptr) << "the room must start with no affects";

    CasterContext caster;
    affected_type mist = room_spell_affect(SPELL_MIST_OF_BAAZUNGA, 3, 0);
    affect_to_room(room, &mist, caster_snapshot::capture(caster.character));

    const caster_snapshot* const stale = room_affect_caster(room, SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(stale, nullptr);
    // `stale` is never read again below -- only compared as a value, never
    // dereferenced -- once affect_remove_room() below may have freed the
    // storage it pointed into.

    affect_remove_room(room, room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA));

    const caster_snapshot* const fresh = room_affect_caster(room, SPELL_MIST_OF_BAAZUNGA);
    EXPECT_EQ(fresh, nullptr)
        << "a fresh lookup after removal must report no record -- the pointer taken before "
           "removal must never be relied on again";
}
