#ifndef ACCOUNT_MANAGEMENT_STORAGE_H
#define ACCOUNT_MANAGEMENT_STORAGE_H

#include "account_management_types.h"

#include <functional>
#include <string>

namespace account {

// The leaf directory name account_character_directory() (and account_record_directory(), its
// record-in-hand twin) falls back to when an account's storage key cannot be resolved.
//
// It is NOT a real account directory. for_each_account_record_on_disk only visits
// accounts/<bucket>/<entry>, and "__invalid_account__" is not a bucket that holds account records,
// so everything written under it is invisible to the game: the index never sees it, `account index
// verify` never mentions it, and the next operator cleanup sweeps it away as litter. A character
// save written there is silently lost -- the log reports success, the real file goes stale, and
// player_table is repointed at a path that will not survive the next reboot.
//
// So the sentinel is a "could not resolve" signal, never a destination. Every write path tests for
// it with is_invalid_account_storage_directory() below and refuses.
constexpr const char* kInvalidAccountDirectoryName = "__invalid_account__";

// True when `directory_path` names the sentinel above. Callers about to WRITE must refuse; callers
// merely reading get a path that does not exist, which is harmless.
bool is_invalid_account_storage_directory(const std::string& directory_path);

std::string account_bucket_for_name(const std::string& name);
std::string legacy_player_file_path(const std::string& root_directory, const std::string& character_name);
std::string legacy_object_file_path(const std::string& root_directory, const std::string& character_name);
std::string legacy_exploits_file_path(const std::string& root_directory, const std::string& character_name);
std::string account_file_path(const std::string& root_directory, const std::string& account_name);
std::string account_character_directory(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_snapshot_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_player_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_object_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);
std::string account_character_exploits_path(const std::string& root_directory, const std::string& account_name, const std::string& character_name);

std::string serialize_account_to_json(const AccountData& account);
bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message = nullptr);

// record_committed, when given, says whether account.json reached its final path before the call
// returned. Everything after that rename -- the cache flush, the index upsert, retiring a stale or
// legacy copy -- can still fail, and a caller that treats such a failure as "nothing happened" and
// undoes its own file moves leaves the committed record describing a state no longer on disk.
bool write_account_file(const std::string& root_directory, const AccountData& account, std::string* error_message = nullptr,
    bool* record_committed = nullptr);
bool read_account_file(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message = nullptr);
// Uncached on-disk read (the real scan). read_account_file delegates here when the cache is disabled,
// and it is the cache's backing resolver on a miss. Call directly to bypass the cache.
bool read_account_file_uncached(const std::string& root_directory, const std::string& account_name, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_email(const std::string& root_directory, const std::string& email, AccountData* account, std::string* error_message = nullptr);
bool read_account_file_by_identifier(const std::string& root_directory, const std::string& identifier, AccountData* account, std::string* error_message = nullptr);

// One account record found on disk, in either supported layout.
struct AccountRecordOnDisk {
    // The bucket entry name: "<email>" for the directory layout, "<name>.json" for the legacy flat one.
    std::string directory_entry_name;
    // Full path of the JSON actually read.
    std::string record_path;
    // Whether the record parsed. When false, `account` is meaningless and failure_reason says why.
    bool parsed = false;
    // True when this record came from the directory layout (<email>/account.json), false for the
    // legacy flat layout (<name>.json). Taken from stat(), not guessed from the entry name.
    bool directory_layout = false;
    // True when this "record" is really a bucket DIRECTORY that would not open. It stands in for an
    // unknown number of real accounts, so callers must not treat it as one missing record.
    bool unreadable_bucket = false;
    AccountData account;
    std::string failure_reason;
};

// Walks accounts/ and visits every record in either layout, parsed or not. Visits one record at a
// time rather than returning them all: at boot this runs over every account on the box, and holding
// every parsed AccountData at once would be a real memory spike on a machine that already swaps.
// Returns false only when the accounts directory itself cannot be read; an individual bad record is
// reported to the visitor with parsed == false, never as a failure of the walk.
bool for_each_account_record_on_disk(const std::string& root_directory,
    const std::function<void(const AccountRecordOnDisk&)>& visitor,
    std::string* error_message = nullptr);

// The key a record is (or should be) indexed/quarantined under, derived purely from the record
// itself, covering every shape the boot walker (db.cpp) and the account-creation guard
// (account_management.cpp) use it for:
//   - directory layout, parsed or not -> its entry name, which IS the email even when the file
//     fails to parse (and even when the email INSIDE a parsed file disagrees with it -- the
//     directory name is the one the record is actually filed under).
//   - legacy flat, parsed, with a usable (non-empty) email -> that email.
//   - legacy flat, parsed, with no usable email, or not parsed at all -> its own record path: it
//     has revealed no email to key it by (either it never parsed, or it parsed to an empty one),
//     and an unparseable/emailless flat record must not reserve an email it never disclosed.
// Both callers must go through this one function with no local branching of their own -- two
// independent derivations of this rule is what twice put the index and the disk into disagreement
// over a record that was really right there. `directory_layout` comes straight from stat() in the enumerator, not guessed from the
// entry name.
std::string account_index_quarantine_key(const AccountRecordOnDisk& record);

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration);
bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message = nullptr);

// Read an entire text file into *contents (POSIX-backed). Exposed for stage-timing the
// LOAD pipeline's file-read step.
bool read_text_file(const std::string& path, std::string* contents, std::string* error_message);

// Atomic write: temp(path+".tmp") -> fwrite -> rename. Exposed for stage-timing the SAVE
// pipeline's disk-write step against a throwaway path.
bool write_text_file_atomically(const std::string& path, const std::string& text,
                                std::string* error_message);

} // namespace account

#endif
