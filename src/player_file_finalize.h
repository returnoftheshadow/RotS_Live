#ifndef PLAYER_FILE_FINALIZE_H
#define PLAYER_FILE_FINALIZE_H

// Crash-safe finalize primitives for the legacy (non-account) player-file save path.
// Portable standard C++ (std::filesystem with non-throwing error_code).

// Historical A/B oracle: system("rm <base>.*") then system("cp scratch versioned").
// Kept ONLY to prove byte/stale-file equivalence against the rename path in tests.
// NOT for production use.
bool finalize_player_file_legacy(const char* scratch_path, const char* base_path,
                                 const char* versioned_path);

// Crash-safe finalize: atomically rename scratch -> versioned FIRST, then remove every
// OTHER "<base_name>." entry in dir_path (dot-anchored, so "bob." never matches "bobby.").
// Return contract: false BEFORE the rename means nothing changed (old save intact); false
// AFTER the rename means the new file IS written and only stale cleanup failed.
bool finalize_player_file_rename(const char* scratch_path, const char* dir_path,
                                 const char* base_name, const char* versioned_path);

#endif // PLAYER_FILE_FINALIZE_H
