#pragma once

#include <filesystem>

struct player_index_element;

namespace test_support {

// Lets a player with a descriptor die for real in a test and respawn: for the scope, raw_kill()'s
// save_char() finds no player-table entry and writes nothing, Crash_crashsave() writes into an
// empty plrobjs/ tree in a scratch working directory, and extract_char() respawns a player of
// `race` in world[respawn_room]. The player table, the working directory and the start room are
// restored on exit, and the scratch directory is removed. A scratch directory that cannot be made,
// entered or removed is reported as a test failure.
class ScopedPlayerDeathSandbox {
  public:
    ScopedPlayerDeathSandbox(int race, int respawn_room);
    ~ScopedPlayerDeathSandbox();
    ScopedPlayerDeathSandbox(const ScopedPlayerDeathSandbox&) = delete;
    ScopedPlayerDeathSandbox& operator=(const ScopedPlayerDeathSandbox&) = delete;

  private:
    player_index_element* m_previous_player_table; // player_table before the scope
    int m_previous_top_of_player_table;            // top_of_p_table before the scope
    int m_race;                                    // the race whose start room is overridden
    int m_previous_start_room;                     // r_mortal_start_room[m_race] before the scope
    std::filesystem::path m_previous_working_directory; // restored on exit; empty if unknown
    std::filesystem::path m_scratch_directory; // the scope's working directory; empty if none
};

} // namespace test_support
