#include "scoped_player_death_sandbox.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <system_error>

extern player_index_element* player_table;
extern int top_of_p_table;
extern int r_mortal_start_room[];

namespace test_support {

namespace {

// The plrobjs/ subdirectories Crash_get_filename() sorts players into by initial. Crash_crashsave()
// writes the dying player's object file into one; with them present it logs nothing.
constexpr const char* kPlayerObjectDirectories[] = {"plrobjs/A-E", "plrobjs/F-J", "plrobjs/K-O",
                                                    "plrobjs/P-T", "plrobjs/U-Z", "plrobjs/ZZZ"};

// A directory name no earlier scope in this process, or a concurrent test binary, has used: the
// steady-clock reading separates processes and the counter separates scopes within one. Returns
// an empty path, with `error` set, when the system has no temporary directory.
std::filesystem::path unique_scratch_directory(std::error_code& error) {
    static long scopes_made = 0;
    ++scopes_made;
    const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
    if (error) {
        return {};
    }
    const auto now = std::chrono::steady_clock::now();
    const std::string clock_ticks = std::to_string(now.time_since_epoch().count());
    const std::string scope_number = std::to_string(scopes_made);
    const std::string name = "rots-player-death-" + clock_ticks + "-" + scope_number;
    return temporary_root / name;
}

} // namespace

ScopedPlayerDeathSandbox::ScopedPlayerDeathSandbox(int race, int respawn_room)
    : m_previous_player_table(player_table), m_previous_top_of_player_table(top_of_p_table),
      m_race(race), m_previous_start_room(r_mortal_start_room[race]) {
    // save_char() scans entries 0 to top_of_p_table, so an empty table needs -1, not 0.
    player_table = nullptr;
    top_of_p_table = -1;
    r_mortal_start_room[race] = respawn_room;

    std::error_code error;
    m_previous_working_directory = std::filesystem::current_path(error);
    if (error) {
        ADD_FAILURE() << "ScopedPlayerDeathSandbox: no working directory: " << error.message();
        m_previous_working_directory.clear();
        return;
    }
    const std::filesystem::path scratch = unique_scratch_directory(error);
    if (scratch.empty() || !std::filesystem::create_directory(scratch, error) || error) {
        ADD_FAILURE() << "ScopedPlayerDeathSandbox: cannot make " << scratch << ": "
                      << error.message();
        return;
    }
    m_scratch_directory = scratch;
    for (const char* const object_directory : kPlayerObjectDirectories) {
        std::filesystem::create_directories(scratch / object_directory, error);
        if (error) {
            ADD_FAILURE() << "ScopedPlayerDeathSandbox: cannot make " << scratch / object_directory
                          << ": " << error.message();
            return;
        }
    }
    std::filesystem::current_path(m_scratch_directory, error);
    if (error) {
        ADD_FAILURE() << "ScopedPlayerDeathSandbox: cannot enter " << m_scratch_directory << ": "
                      << error.message();
    }
}

ScopedPlayerDeathSandbox::~ScopedPlayerDeathSandbox() {
    std::error_code error;
    if (!m_previous_working_directory.empty()) {
        std::filesystem::current_path(m_previous_working_directory, error);
        if (error) {
            ADD_FAILURE() << "ScopedPlayerDeathSandbox: cannot return to "
                          << m_previous_working_directory << ": " << error.message();
        }
    }
    if (!m_scratch_directory.empty()) {
        error.clear();
        std::filesystem::remove_all(m_scratch_directory, error);
        if (error) {
            ADD_FAILURE() << "ScopedPlayerDeathSandbox: cannot remove " << m_scratch_directory
                          << ": " << error.message();
        }
    }
    r_mortal_start_room[m_race] = m_previous_start_room;
    top_of_p_table = m_previous_top_of_player_table;
    player_table = m_previous_player_table;
}

} // namespace test_support
