#pragma once

struct char_data;
struct character_identity;
struct descriptor_data;

namespace test_support {

// The hit points and move points the characters below are given.
constexpr int kFleeTestHitPoints = 100;
constexpr int kFleeTestMovePoints = 100;

// A heap NPC built and registered the way the game builds one (clear_char(), register_npc_char()),
// standing, human, with kFleeTestHitPoints, kFleeTestMovePoints and the stats moving divides by,
// so a death runs extract_char() and free_char() for real. Its nr of 0 names the prototype slot a
// ScopedMobIndex publishes, which must outlive it; it also belongs on a ScopedCharacterList and in
// a room before anything can kill it. `short_description` is its name and is not freed with it.
// A survivor is released with release_test_character(). A null name is reported as a test
// failure and replaced with a placeholder, as it is for the player below.
[[nodiscard]] char_data* make_registered_test_npc(char* short_description);

// A heap player built and registered the way login builds one, with the NPC's points and stats,
// mortal, and linked to `descriptor`, which is reset to capture its output and given a socket
// number so the game treats the player as connected: it respawns when it dies, and do_look()
// shows it the room. `descriptor` must outlive the player; release the player with
// release_test_character().
[[nodiscard]] char_data* make_linked_test_player(descriptor_data& descriptor, const char* name);

// Extracts every object lying in world[room_number], such as the corpse a death left there.
void release_room_objects(int room_number);

// Takes the character `identity` names out of its room and releases it with
// release_test_character(), if it still resolves; one that no longer resolves is left alone. A
// test ends with this for a survivor, and for a character it expected to be gone, so that even a
// failing test leaves nothing registered or standing in a room.
void release_survivor(const character_identity& identity);

} // namespace test_support
