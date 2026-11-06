#include "player_file_finalize.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

bool finalize_player_file_legacy(const char* scratch_path, const char* base_path,
                                 const char* versioned_path) {
    char command[300];

    snprintf(command, sizeof(command), "rm %s.*", base_path);
    int rc_rm = system(command);
    snprintf(command, sizeof(command), "cp %s %s", scratch_path, versioned_path);
    int rc_cp = system(command);

    return (rc_rm != -1) && (rc_cp != -1);
}

bool finalize_player_file_rename(const char* scratch_path, const char* dir_path,
                                 const char* base_name, const char* versioned_path) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Publish the new file first so a crash here cannot lose the save (atomic move).
    fs::rename(scratch_path, versioned_path, ec);
    if (ec) {
        return false;
    }

    // 2. Remove any OTHER stale "<base>." entries, leaving the file we just wrote.
    const size_t base_len = std::char_traits<char>::length(base_name);
    const std::string_view versioned_view(versioned_path);
    const size_t v_slash = versioned_view.find_last_of('/');
    const std::string_view keep_name =
        (v_slash == std::string_view::npos) ? versioned_view : versioned_view.substr(v_slash + 1);

    std::vector<fs::path> victims;
    fs::directory_iterator it(dir_path, ec);
    if (ec) {
        return false;
    }
    const fs::directory_iterator end;
    // Check ec right AFTER each increment: on error libstdc++ resets the iterator to end,
    // so a top-of-loop check would be skipped and the error silently swallowed.
    while (it != end) {
        const std::string& full = it->path().native();
        const size_t slash = full.find_last_of('/');
        const std::string_view name = (slash == std::string::npos)
                                          ? std::string_view(full)
                                          : std::string_view(full).substr(slash + 1);
        if (name.size() > base_len && name.compare(0, base_len, base_name) == 0 &&
            name[base_len] == '.' && name != keep_name) {
            victims.push_back(it->path());
        }
        it.increment(ec);
        if (ec) {
            return false;
        }
    }
    for (const fs::path& victim : victims) {
        fs::remove(victim, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}
