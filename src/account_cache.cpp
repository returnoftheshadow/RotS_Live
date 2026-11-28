#include "account_cache.h"

#include "account_management.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace account_cache {

namespace {

// Full memoized outcome of account::find_linked_character_owner_account for one (root, character):
// its bool return, the owner name (empty string is the valid "character is not linked" result we
// cache), and the error string — all replayed verbatim on a hit so the cached path is byte-identical.
struct OwnerResolution
{
    // Return value of the underlying resolver; replayed on a hit.
    bool resolved = false;
    // Resolved owner account name; empty == the negative "not linked" outcome.
    std::string owner_account_name;
    // Error string the underlying resolver produced; replayed on a hit.
    std::string error_message;
};

// Memoized successful account reads keyed by compose_key(root, account_name); value is the full
// parsed AccountData. A hit returns a copy with zero filesystem work; misses are not cached.
// Emptied by clear() for test isolation.
std::unordered_map<std::string, account::AccountData> g_account_cache;

// Memoized owner resolutions keyed by compose_key(root, character_name); value is the full
// OwnerResolution, so a repeat unlinked lookup short-circuits the O(N) account scan. Emptied by clear().
std::unordered_map<std::string, OwnerResolution> g_owner_cache;

// Unit-separator byte joining root and name into a single map key; chosen because it cannot appear
// in a filesystem path component, so (root, name) pairs never collide or bleed across roots.
const char kKeySeparator = '\x1f';

std::string compose_key(const std::string& root_directory, const std::string& name)
{
    std::string key;
    key.reserve(root_directory.size() + 1 + name.size());
    key.append(root_directory);
    key.push_back(kKeySeparator);
    key.append(name);
    return key;
}

// Whether the base account resolvers delegate to this cache (see account_cache.h). Default OFF so the
// test binary and non-server callers keep the exact uncached behavior; the live server flips it on.
bool g_enabled = false;

// Backing resolver the cache calls on an account-map miss; defaults to the UNCACHED on-disk reader
// (never the public read_account_file, which would recurse via the enabled delegation) and is
// overridden only by set_backing_resolvers_for_testing.
AccountReaderFn g_account_reader = &account::read_account_file_uncached;

// Backing resolver the cache calls on an owner-map miss; defaults to the UNCACHED on-disk resolver.
OwnerResolverFn g_owner_resolver = &account::find_linked_character_owner_account_uncached;

} // namespace

bool read_account_file_cached(const std::string& root_directory, const std::string& account_name,
                              account::AccountData* account, std::string* error_message)
{
    if (account == nullptr)
    {
        return g_account_reader(root_directory, account_name, account, error_message);
    }

    const std::string key = compose_key(root_directory, account_name);
    const auto cached_entry = g_account_cache.find(key);
    if (cached_entry != g_account_cache.end())
    {
        *account = cached_entry->second;
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        return true;
    }

    account::AccountData loaded_account;
    if (!g_account_reader(root_directory, account_name, &loaded_account, error_message))
    {
        return false;
    }

    g_account_cache.emplace(key, loaded_account);
    *account = loaded_account;
    return true;
}

bool find_linked_character_owner_account_cached(const std::string& root_directory, const std::string& character_name,
                                                std::string* owner_account_name, std::string* error_message)
{
    if (owner_account_name == nullptr)
    {
        // Preserve the underlying null-guard behavior exactly; nothing to cache without an out-param.
        return g_owner_resolver(root_directory, character_name, owner_account_name, error_message);
    }

    const std::string key = compose_key(root_directory, character_name);
    const auto cached_entry = g_owner_cache.find(key);
    if (cached_entry != g_owner_cache.end())
    {
        *owner_account_name = cached_entry->second.owner_account_name;
        if (error_message != nullptr)
        {
            *error_message = cached_entry->second.error_message;
        }
        return cached_entry->second.resolved;
    }

    OwnerResolution outcome;
    outcome.resolved = g_owner_resolver(root_directory, character_name, &outcome.owner_account_name, &outcome.error_message);

    // Replay the outcome to the caller before any move, then cache ONLY a successful resolution
    // (including the valid negative "not linked" == resolved-with-empty-owner). A failure (genuine
    // error) is left uncached so a later call retries, mirroring read_account_file_cached.
    *owner_account_name = outcome.owner_account_name;
    if (error_message != nullptr)
    {
        *error_message = outcome.error_message;
    }

    const bool resolved = outcome.resolved;
    if (resolved)
    {
        g_owner_cache.emplace(key, std::move(outcome));
    }
    return resolved;
}

void clear()
{
    g_account_cache.clear();
    g_owner_cache.clear();
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

void invalidate_all()
{
    // Coarse but correct: any account mutation flushes the whole cache (see account_cache.h). Mutations
    // are rare and off the hot path, so this never regresses the read-heavy save/load traffic.
    g_account_cache.clear();
    g_owner_cache.clear();
}

void set_backing_resolvers_for_testing(AccountReaderFn account_reader, OwnerResolverFn owner_resolver)
{
    g_account_reader = (account_reader != nullptr) ? account_reader : &account::read_account_file_uncached;
    g_owner_resolver = (owner_resolver != nullptr) ? owner_resolver : &account::find_linked_character_owner_account_uncached;
}

} // namespace account_cache
