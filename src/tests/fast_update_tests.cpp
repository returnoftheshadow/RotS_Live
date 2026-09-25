// fast_update()'s regen pass when negative regen kills a character. raw_kill() frees an NPC or a
// link-dead player, so the pass must write the regen-death record before the kill and carry on to
// the characters after the dead one. The characters are heap characters registered the way the
// game registers them, so a death runs free_char() for real and AddressSanitizer sees any later
// use.
#include "../character_identity.h"
#include "../db.h"
#include "../structs.h"
#include "../utils.h"
#include "scoped_character_list.h"
#include "scoped_combat_list.h"
#include "scoped_descriptor_list.h"
#include "scoped_flee_world.h"
#include "scoped_mob_index.h"
#include "scoped_player_death_sandbox.h"
#include "scoped_room_occupants.h"
#include "scoped_waiting_list.h"
#include "test_flee_support.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

extern room_data world;

void fast_update(); // limits.cpp: not declared in limits.h

namespace {

// Rooms this file owns in the shared test world, beside raw_kill_tests.cpp's 1019 and 1020 and
// below the 1024 rooms gtest_main.cpp allocates. Everyone stands in the first; the second is only
// the other end of the scope's exit, and where a player would respawn.
constexpr int kRegenRoom = 1021;
constexpr int kSpareRoom = 1022;
constexpr int kUnusedDirection = EAST;

// Flat health regen per game hour. fast_update() applies a FAST_UPDATE_RATE-th of it each pass,
// so one pass moves hit points by about these per-pass figures whatever the rounding roll draws.
constexpr int kLethalRegenPerPass = -1000;
constexpr int kRestoringRegenPerPass = 1000;

// The survivor's hit points before the pass, below the builders' kFleeTestHitPoints maximum.
constexpr int kWoundedHitPoints = 50;

// One record add_exploit_record() handed to the writer.
struct captured_exploit_record {
    std::string recipient_name; // the recipient's name, copied when the record was written
    int type;                   // the record's EXPLOIT_* type
};

// Where capture_exploit_record() appends; owned by the live ScopedExploitCapture.
std::vector<captured_exploit_record>* g_captured_records = nullptr;

// The writer ScopedExploitCapture installs. It reads the recipient's name, as write_exploits()
// does, so a record written for a freed character is a use-after-free AddressSanitizer reports.
void capture_exploit_record(char_data* recipient, exploit_record* record) {
    if (g_captured_records == nullptr || recipient == nullptr || record == nullptr) {
        ADD_FAILURE() << "capture_exploit_record: no capture scope, recipient or record";
        return;
    }
    g_captured_records->push_back({GET_NAME(recipient), record->type});
}

// Routes every exploit record written during the scope into `records`, instead of the player's
// exploits file, and restores write_exploits() on exit.
class ScopedExploitCapture {
  public:
    ScopedExploitCapture() {
        g_captured_records = &records;
        set_exploit_record_writer_for_testing(capture_exploit_record);
    }
    ~ScopedExploitCapture() {
        set_exploit_record_writer_for_testing(nullptr);
        g_captured_records = nullptr;
    }
    ScopedExploitCapture(const ScopedExploitCapture&) = delete;
    ScopedExploitCapture& operator=(const ScopedExploitCapture&) = delete;

    std::vector<captured_exploit_record> records; // every record written, in write order
};

// Gives `character` a flat health regen of about `regen_per_pass` hit points per fast_update().
void set_regen_per_pass(char_data& character, int regen_per_pass) {
    character.points.health_regen = regen_per_pass * FAST_UPDATE_RATE;
}

} // namespace

class FastUpdateRegenDeath : public ::testing::Test {
  protected:
    FastUpdateRegenDeath() : m_rooms(kRegenRoom, kSpareRoom, kUnusedDirection) {}

    void TearDown() override {
        test_support::release_room_objects(kRegenRoom);
        test_support::release_room_objects(kSpareRoom);
    }

    // mob_index[0], the test NPCs' prototype.
    test_support::ScopedMobIndex m_prototype_table;
    // Keeps other suites' stale fighters away from extract_char()'s walk.
    test_support::ScopedCombatList m_combat_list;
    // Keeps other suites' stale entries away from extract_char()'s walk.
    test_support::ScopedWaitingList m_waiting_list;
    // The regen room, the spare room, and zone 0, which moving a player into a room updates.
    test_support::ScopedFleeWorld m_rooms;
    // Keeps the pass's records off disk and lets the tests read them.
    ScopedExploitCapture m_exploits;
};

// A regen death does not end the pass: the character after the dead one still regenerates.
TEST_F(FastUpdateRegenDeath, TheCharacterAfterARegenDeathStillRegenerates) {
    char dying_name[] = "a withering test orc";
    char survivor_name[] = "a mending test orc";
    char_data* const dying = test_support::make_registered_test_npc(dying_name);
    char_data* const survivor = test_support::make_registered_test_npc(survivor_name);
    set_regen_per_pass(*dying, kLethalRegenPerPass);
    set_regen_per_pass(*survivor, kRestoringRegenPerPass);
    GET_HIT(survivor) = kWoundedHitPoints;
    const character_identity dying_identity = character_identity::capture(*dying);
    const character_identity survivor_identity = character_identity::capture(*survivor);
    // Built by prepending: the dying NPC heads the list and the survivor follows it.
    test_support::ScopedCharacterList characters({survivor, dying});
    test_support::ScopedRoomOccupants occupants(kRegenRoom, {survivor, dying});

    fast_update();

    EXPECT_EQ(dying_identity.resolve(), nullptr);
    ASSERT_EQ(survivor_identity.resolve(), survivor);
    EXPECT_EQ(GET_HIT(survivor), GET_MAX_HIT(survivor)) << "the pass must reach the survivor";
    test_support::release_survivor(dying_identity);
    test_support::release_survivor(survivor_identity);
}

// An NPC's regen death frees it; the pass writes no record for it and reads nothing of it after
// the kill.
TEST_F(FastUpdateRegenDeath, AnNpcRegenDeathFreesTheNpcAndWritesNoRecord) {
    char name[] = "a withering test orc";
    char_data* const npc = test_support::make_registered_test_npc(name);
    set_regen_per_pass(*npc, kLethalRegenPerPass);
    const character_identity npc_identity = character_identity::capture(*npc);
    test_support::ScopedCharacterList characters({npc});
    test_support::ScopedRoomOccupants occupants(kRegenRoom, {npc});

    fast_update();
    // The NPC has been freed: below it is only resolved, never read.

    EXPECT_EQ(npc_identity.resolve(), nullptr);
    EXPECT_EQ(world[kRegenRoom].people, nullptr);
    EXPECT_TRUE(m_exploits.records.empty()) << "NPCs get no exploit records";
    test_support::release_survivor(npc_identity);
}

// A link-dead player keeps its descriptor, with no socket, so its regen death frees it through
// close_socket(). The regen-death record is written for it before that.
TEST_F(FastUpdateRegenDeath, ALinkDeadPlayersRegenDeathIsRecordedBeforeItIsFreed) {
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kSpareRoom);
    descriptor_data* link_dead_descriptor = nullptr;
    CREATE(link_dead_descriptor, descriptor_data, 1); // close_socket() releases it
    char_data* const player =
        test_support::make_linked_test_player(*link_dead_descriptor, "Withering");
    link_dead_descriptor->descriptor = 0;
    link_dead_descriptor->connected = CON_LINKLS;
    set_regen_per_pass(*player, kLethalRegenPerPass);
    const character_identity player_identity = character_identity::capture(*player);
    test_support::ScopedDescriptorList descriptors(link_dead_descriptor);
    test_support::ScopedCharacterList characters({player});
    test_support::ScopedRoomOccupants occupants(kRegenRoom, {player});

    fast_update();
    // The player and its descriptor have been freed: below they are only resolved, never read.

    EXPECT_EQ(player_identity.resolve(), nullptr);
    EXPECT_EQ(world[kRegenRoom].people, nullptr);
    EXPECT_EQ(world[kSpareRoom].people, nullptr);
    ASSERT_EQ(m_exploits.records.size(), 1u);
    EXPECT_EQ(m_exploits.records.front().recipient_name, "Withering");
    EXPECT_EQ(m_exploits.records.front().type, EXPLOIT_REGEN_DEATH);
    test_support::release_survivor(player_identity);
}
