#ifndef ROSTER_CACHE_H
#define ROSTER_CACHE_H

#include "structs.h"

#include <string>

namespace roster_cache {

// The only fields the account roster renders or sorts by. The display NAME is deliberately absent:
// it comes from account.characters[index], which account_cache already memoizes. SIDE is absent
// too: it derives from race via other_side_num(). Storing 14 bytes here replaces a ~4 KB JSON
// read-and-parse per rendered row.
struct RosterSummary {
    unsigned char level = 0;
    unsigned char race = 0;
    // False when the character file could not be read or parsed. The roster renders those as
    // "[ ?? ???]"; caching the failure stops the most useless rows being the most expensive.
    bool readable = false;
    // Raw square_root[] index, domain 0..170. Held as short rather than a byte because nothing
    // validates the range on load (character_json.cpp reads it into an int unclamped), and a byte
    // would silently truncate a corrupt value into a plausible-looking wrong sort position.
    short prof_coof[MAX_PROFS + 1] = { 0 };
};

// Returns the summary for one linked character, reading and parsing the character file only on a
// miss. Returns false only when the character cannot be resolved at all; an unreadable character
// is a successful call with readable == false.
bool get(const std::string& root_directory, const std::string& account_name,
    const std::string& character_name, RosterSummary* summary);

// Drops the entry for exactly one character. Called from the write_account_character_file
// chokepoint. Deliberately NOT a global flush: character saves are frequent (autosave), so a
// coarse flush like account_cache's would keep this cache permanently cold and useless.
void invalidate_character(const std::string& root_directory, const std::string& character_name);

// Empties the map. Call in test-fixture SetUp() for isolation.
void clear();

// Whether get() memoizes. Default OFF so the test binary and non-server callers keep exact
// uncached behavior; the live server calls set_enabled(true) once at boot.
void set_enabled(bool enabled);
bool is_enabled();

// Signature of the on-disk reader get() delegates to on a miss.
using ReaderFn = bool (*)(const std::string&, const std::string&, const std::string&,
    char_file_u*, std::string*);

// Test-only seam: override the backing reader. Pass nullptr to restore the real
// account::read_account_character_file. Not thread-safe (the MUD and the tests are single-threaded).
void set_backing_reader_for_testing(ReaderFn reader);

} // namespace roster_cache

#endif
