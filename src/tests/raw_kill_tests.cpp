// raw_kill() on a player. Death clears the "already kitted" flag, and extract_char() saves the
// player and, when it has no link, frees it, so the clear must happen before that call. The
// players are heap characters registered the way login registers them, so a link-dead player's
// death runs close_socket() and free_char() for real and AddressSanitizer sees any later use.
#include "../character_identity.h"
#include "../db.h"
#include "../structs.h"
#include "../utils.h"
#include "raw_kill_declaration.h"
#include "scoped_character_list.h"
#include "scoped_combat_list.h"
#include "scoped_descriptor_list.h"
#include "scoped_flee_world.h"
#include "scoped_player_death_sandbox.h"
#include "scoped_room_occupants.h"
#include "scoped_waiting_list.h"
#include "test_descriptor_support.h"
#include "test_flee_support.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <system_error>

extern room_data world;
extern player_index_element* player_table;
extern int top_of_p_table;

namespace {

// Rooms this file owns in the shared test world, beside affect_update_tests.cpp's 1017 and 1018
// and below the 1024 rooms gtest_main.cpp allocates. The player dies in the first; a connected
// player respawns in the second.
constexpr int kDeathRoom = 1019;
constexpr int kRespawnRoom = 1020;
constexpr int kUnusedDirection = EAST;

// The link-dead player's name, and the lower-case form its player-table entry holds. Its initial
// puts its saved file in players/K-O.
constexpr const char* kLinkDeadPlayerName = "Kitted";
constexpr char kLinkDeadPlayerKey[] = "kitted";

// Publishes a one-entry player table naming kLinkDeadPlayerKey, and the players/K-O directory its
// file is saved into under the working directory, so save_char() writes the player's record and
// load_player() reads it back; the previous table is restored on exit. Construct it inside a
// ScopedPlayerDeathSandbox, whose scratch working directory holds the file.
class ScopedPlayerRecord {
  public:
    ScopedPlayerRecord() : m_previous_table(player_table), m_previous_top(top_of_p_table) {
        std::error_code error;
        std::filesystem::create_directories("players/K-O", error);
        if (error) {
            ADD_FAILURE() << "ScopedPlayerRecord: cannot make players/K-O: " << error.message();
        }
        std::strcpy(m_name, kLinkDeadPlayerKey);
        m_entry.name = m_name;
        player_table = &m_entry;
        top_of_p_table = 0;
    }
    ~ScopedPlayerRecord() {
        player_table = m_previous_table;
        top_of_p_table = m_previous_top;
    }
    ScopedPlayerRecord(const ScopedPlayerRecord&) = delete;
    ScopedPlayerRecord& operator=(const ScopedPlayerRecord&) = delete;

    // Loads the saved record into `out_record`; false, with a test failure, when none loads.
    bool load(char_file_u& out_record) {
        char lookup_name[sizeof(m_name)];
        std::strcpy(lookup_name, m_name); // load_player() lower-cases the name in place
        if (load_player(lookup_name, &out_record) < 0) {
            ADD_FAILURE() << "ScopedPlayerRecord: no saved record for " << m_name;
            return false;
        }
        return true;
    }

  private:
    player_index_element* m_previous_table;  // player_table before the scope
    int m_previous_top;                      // top_of_p_table before the scope
    char m_name[sizeof(kLinkDeadPlayerKey)]; // the entry's writable copy of kLinkDeadPlayerKey
    player_index_element m_entry{};          // the table's only entry; save_player() sets ch_file
};

} // namespace

class RawKill : public ::testing::Test {
  protected:
    RawKill() : m_rooms(kDeathRoom, kRespawnRoom, kUnusedDirection) {}

    void TearDown() override {
        test_support::release_room_objects(kDeathRoom);
        test_support::release_room_objects(kRespawnRoom);
    }

    // Keeps other suites' stale fighters away from extract_char()'s walk.
    test_support::ScopedCombatList m_combat_list;
    // Keeps other suites' stale entries away from extract_char()'s walk.
    test_support::ScopedWaitingList m_waiting_list;
    // The death room, the respawn room, and zone 0, which moving a player into a room updates.
    test_support::ScopedFleeWorld m_rooms;
};

// A link-dead player keeps its descriptor, with no socket: extract_char() saves it and then frees
// it through close_socket(). raw_kill() must not touch it after that call, and the save must
// already carry the cleared flag.
TEST_F(RawKill, ALinkDeadPlayerIsFreedAndItsSavedRecordIsNoLongerKitted) {
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kRespawnRoom);
    ScopedPlayerRecord record;
    descriptor_data* link_dead_descriptor = nullptr;
    CREATE(link_dead_descriptor, descriptor_data, 1); // close_socket() releases it
    char_data* const player =
        test_support::make_linked_test_player(*link_dead_descriptor, kLinkDeadPlayerName);
    link_dead_descriptor->descriptor = 0;
    link_dead_descriptor->connected = CON_LINKLS;
    SET_BIT(PLR_FLAGS(player), PLR_WAS_KITTED);
    const character_identity player_identity = character_identity::capture(*player);
    test_support::ScopedDescriptorList descriptors(link_dead_descriptor);
    test_support::ScopedCharacterList characters({player});
    test_support::ScopedRoomOccupants death_room_occupants(kDeathRoom, {player});

    raw_kill(player, nullptr, 0);
    // The player and its descriptor have been freed: below they are only resolved, never read.

    EXPECT_EQ(player_identity.resolve(), nullptr);
    EXPECT_EQ(world[kDeathRoom].people, nullptr);
    EXPECT_EQ(world[kRespawnRoom].people, nullptr);
    char_file_u saved{};
    if (record.load(saved)) {
        EXPECT_FALSE(IS_SET(saved.specials2.act, PLR_WAS_KITTED))
            << "death must clear the flag in the record extract_char() saved";
    }
    test_support::release_survivor(player_identity);
}

// A connected player respawns: it still resolves, and its in-memory flag is cleared for the next
// save to carry.
TEST_F(RawKill, AConnectedPlayerRespawnsNoLongerKitted) {
    test_support::ScopedPlayerDeathSandbox sandbox(RACE_HUMAN, kRespawnRoom);
    descriptor_data descriptor{};
    char_data* const player = test_support::make_linked_test_player(descriptor, "Survivor");
    SET_BIT(PLR_FLAGS(player), PLR_WAS_KITTED);
    const character_identity player_identity = character_identity::capture(*player);
    test_support::ScopedCharacterList characters({player});
    test_support::ScopedRoomOccupants death_room_occupants(kDeathRoom, {player});

    raw_kill(player, nullptr, 0);

    char_data* const survivor = player_identity.resolve();
    ASSERT_EQ(survivor, player) << "a respawned player keeps its registration";
    EXPECT_EQ(survivor->in_room, kRespawnRoom);
    EXPECT_FALSE(IS_SET(PLR_FLAGS(survivor), PLR_WAS_KITTED));
    test_support::release_survivor(player_identity);
    test_support::release_large_output(descriptor);
}
