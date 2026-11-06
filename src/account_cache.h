#ifndef ACCOUNT_CACHE_H
#define ACCOUNT_CACHE_H

#include "account_management_types.h"

#include <string>

namespace account_cache {

// Memoized parallel of account::read_account_file. On a hit, copies the cached AccountData with
// zero filesystem work; on a miss, delegates to account::read_account_file, stores a copy, returns.
bool read_account_file_cached(const std::string& root_directory, const std::string& account_name,
                              account::AccountData* account, std::string* error_message);

// Memoized parallel of account::find_linked_character_owner_account, with negative ("not linked")
// caching so a repeat unlinked lookup short-circuits the O(N) account-directory scan.
bool find_linked_character_owner_account_cached(const std::string& root_directory, const std::string& character_name,
                                                std::string* owner_account_name, std::string* error_message);

// Empties both memo maps. Call in test-fixture SetUp() for isolation.
void clear();

// Whether the BASE account resolvers (account::read_account_file / find_linked_character_owner_account)
// delegate to this cache. Default OFF, so the test binary and any non-server caller keep the exact
// uncached behavior; the live server calls set_enabled(true) once at boot. The explicit *_cached entry
// points above always cache regardless of this flag.
void set_enabled(bool enabled);
bool is_enabled();

// Coarse live invalidation: empties BOTH memo maps. Called after a successful account.json write (the
// single mutation chokepoint). Account mutations (create/link/block/verify/reset/delete/email-verify)
// are rare and never happen on the hot save/load path, so a full flush is provably free of the
// relink/rename/normalization staleness traps a targeted invalidation must handle, at negligible cost
// (the cache re-warms on the next reads; worst case degrades to no-cache, never a regression).
void invalidate_all();

// Signatures of the two on-disk resolvers the cache delegates to on a miss; default to the real
// account:: functions. They are seams (below) only because the on-disk account-directory readdir
// scan does not resolve under QEMU i386 emulation, so offline unit tests must inject fakes.
using AccountReaderFn = bool (*)(const std::string&, const std::string&, account::AccountData*, std::string*);
using OwnerResolverFn = bool (*)(const std::string&, const std::string&, std::string*, std::string*);

// Test-only seam: override the backing resolvers the cache calls on a miss. Pass nullptr for either
// argument to restore that resolver's real account:: default. Production never calls this; it lets
// unit tests exercise the cache's hit/miss/negative/keying logic without the (QEMU-unresolvable)
// on-disk scan. Not thread-safe (the MUD and the tests are single-threaded).
void set_backing_resolvers_for_testing(AccountReaderFn account_reader, OwnerResolverFn owner_resolver);

} // namespace account_cache

#endif
