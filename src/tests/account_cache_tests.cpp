#include "../account_cache.h"
#include "../account_management.h"

#include <gtest/gtest.h>

#include <string>

// These tests exercise the cache's own logic (hit/miss/negative caching/(root,name) keying/clear)
// through the account_cache::set_backing_resolvers_for_testing seam, driving COUNTING FAKE resolvers
// instead of the real on-disk account scan. The real scan (find_account_file_path_by_account_name)
// does not resolve under QEMU i386 emulation in the offline gtest — the AccountManagement.* on-disk
// suites are pre-existing 32-bit baseline reds for exactly this reason — so a disk-backed fixture
// could not distinguish a cache hit from a miss here. Call counts make "served from cache, no rescan"
// directly observable.

namespace {

// Number of times the fake account reader was invoked since the last fixture reset; a cache hit must
// NOT increment it. The whole point of the suite is to assert this count.
int g_account_reader_calls = 0;
// AccountData the fake account reader returns on success; tests vary a field to prove identity replay.
account::AccountData g_account_reader_result;
// When false, the fake account reader reports a miss (returns false) so "misses are not cached" is testable.
bool g_account_reader_succeeds = true;

bool counting_account_reader(const std::string& root_directory, const std::string& account_name,
                             account::AccountData* account, std::string* error_message)
{
    (void)root_directory;
    (void)account_name;
    ++g_account_reader_calls;
    if (!g_account_reader_succeeds)
    {
        if (error_message != nullptr)
        {
            *error_message = "synthetic account miss";
        }
        return false;
    }
    if (account != nullptr)
    {
        *account = g_account_reader_result;
    }
    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}

// Number of times the fake owner resolver was invoked since the last fixture reset; a cache hit must NOT increment it.
int g_owner_resolver_calls = 0;
// Owner the fake owner resolver returns on success; empty string models the valid "not linked" outcome.
std::string g_owner_resolver_owner;
// When false, the fake owner resolver reports a genuine error (returns false), which must NOT be cached.
bool g_owner_resolver_succeeds = true;

bool counting_owner_resolver(const std::string& root_directory, const std::string& character_name,
                             std::string* owner_account_name, std::string* error_message)
{
    (void)root_directory;
    (void)character_name;
    ++g_owner_resolver_calls;
    if (!g_owner_resolver_succeeds)
    {
        if (error_message != nullptr)
        {
            *error_message = "synthetic owner error";
        }
        return false;
    }
    if (owner_account_name != nullptr)
    {
        *owner_account_name = g_owner_resolver_owner;
    }
    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}

// AccountCache fixture: clears the memo maps, resets the fake-resolver state, and installs the counting
// fakes before every case; restores the real resolvers afterward so other suites are unaffected.
class AccountCache : public ::testing::Test {
protected:
    void SetUp() override
    {
        account_cache::clear();
        account_cache::set_enabled(false); // start each case with the base functions NOT delegating
        g_account_reader_calls = 0;
        g_account_reader_succeeds = true;
        g_account_reader_result = account::AccountData{};
        g_owner_resolver_calls = 0;
        g_owner_resolver_succeeds = true;
        g_owner_resolver_owner.clear();
        account_cache::set_backing_resolvers_for_testing(counting_account_reader, counting_owner_resolver);
    }

    void TearDown() override
    {
        account_cache::set_enabled(false); // never leak enabled state to other suites' base reads
        account_cache::set_backing_resolvers_for_testing(nullptr, nullptr);
    }
};

} // namespace

TEST_F(AccountCache, CachedReadReplaysResolverResultAndMemoizesAfterFirstCall) {
    g_account_reader_result.normalized_email = "player@example.com";
    std::string error_message;

    account::AccountData first;
    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &first, &error_message)) << error_message;
    EXPECT_EQ(first.normalized_email, "player@example.com");
    EXPECT_EQ(g_account_reader_calls, 1);

    account::AccountData second;
    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &second, &error_message)) << error_message;
    EXPECT_EQ(second.normalized_email, "player@example.com");
    EXPECT_EQ(g_account_reader_calls, 1) << "Second read must be served from cache (no rescan).";
}

TEST_F(AccountCache, FailedAccountReadIsNotCachedSoItRetries) {
    g_account_reader_succeeds = false;
    std::string error_message;

    account::AccountData out;
    EXPECT_FALSE(account_cache::read_account_file_cached("root", "ghost", &out, &error_message));
    EXPECT_FALSE(account_cache::read_account_file_cached("root", "ghost", &out, &error_message));
    EXPECT_EQ(g_account_reader_calls, 2) << "A miss must not be cached; each call retries the resolver.";
}

TEST_F(AccountCache, AccountCacheIsKeyedByRootAndNameAndDoesNotBleedAcrossRoots) {
    g_account_reader_result.normalized_email = "from-a";
    std::string error_message;
    account::AccountData from_a;
    ASSERT_TRUE(account_cache::read_account_file_cached("root-a", "alpha-admin", &from_a, &error_message)) << error_message;

    // A different root with the same account name must resolve independently (a real scan, not the cache).
    g_account_reader_result.normalized_email = "from-b";
    account::AccountData from_b;
    ASSERT_TRUE(account_cache::read_account_file_cached("root-b", "alpha-admin", &from_b, &error_message)) << error_message;

    EXPECT_EQ(from_a.normalized_email, "from-a");
    EXPECT_EQ(from_b.normalized_email, "from-b");
    EXPECT_EQ(g_account_reader_calls, 2) << "Distinct (root,name) keys must each miss once.";

    // Re-reading root-a is now a hit and still returns root-a's data, proving keys don't collide.
    account::AccountData from_a_again;
    ASSERT_TRUE(account_cache::read_account_file_cached("root-a", "alpha-admin", &from_a_again, &error_message)) << error_message;
    EXPECT_EQ(from_a_again.normalized_email, "from-a");
    EXPECT_EQ(g_account_reader_calls, 2) << "root-a re-read must be a cache hit.";
}

TEST_F(AccountCache, NegativeOwnerResultIsCachedAndShortCircuitsRescan) {
    g_owner_resolver_owner = ""; // "aragorn" is not linked yet -> valid negative outcome.
    std::string error_message;

    std::string owner_first;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_first, &error_message)) << error_message;
    EXPECT_TRUE(owner_first.empty());
    EXPECT_EQ(g_owner_resolver_calls, 1);

    // Even though the underlying owner would now resolve, the cached negative must be replayed (no rescan).
    g_owner_resolver_owner = "alpha-admin";
    std::string owner_second;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_second, &error_message)) << error_message;
    EXPECT_TRUE(owner_second.empty()) << "The negative result must have been cached (no rescan).";
    EXPECT_EQ(g_owner_resolver_calls, 1);
}

TEST_F(AccountCache, PositiveOwnerResultIsCached) {
    g_owner_resolver_owner = "alpha-admin";
    std::string error_message;

    std::string owner_first;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_first, &error_message)) << error_message;
    EXPECT_EQ(owner_first, "alpha-admin");

    std::string owner_second;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_second, &error_message)) << error_message;
    EXPECT_EQ(owner_second, "alpha-admin");
    EXPECT_EQ(g_owner_resolver_calls, 1) << "Second owner resolve must be served from cache.";
}

TEST_F(AccountCache, OwnerResolverErrorIsNotCachedSoItRetries) {
    g_owner_resolver_succeeds = false;
    std::string error_message;

    std::string owner;
    EXPECT_FALSE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner, &error_message));
    EXPECT_FALSE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner, &error_message));
    EXPECT_EQ(g_owner_resolver_calls, 2) << "A genuine resolver error must not be cached; each call retries.";
}

TEST_F(AccountCache, ClearResetsBothMapsSoNextResolveRescans) {
    g_account_reader_result.normalized_email = "before-clear";
    g_owner_resolver_owner = "alpha-admin";
    std::string error_message;

    account::AccountData account_out;
    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &account_out, &error_message)) << error_message;
    std::string owner_out;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_out, &error_message)) << error_message;
    EXPECT_EQ(g_account_reader_calls, 1);
    EXPECT_EQ(g_owner_resolver_calls, 1);

    account_cache::clear();

    // After clear() both maps are empty, so the next resolves rescan (call counts increment again).
    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &account_out, &error_message)) << error_message;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_out, &error_message)) << error_message;
    EXPECT_EQ(g_account_reader_calls, 2) << "clear() must drop the account entry so it rescans.";
    EXPECT_EQ(g_owner_resolver_calls, 2) << "clear() must drop the owner entry so it rescans.";
}

TEST_F(AccountCache, InvalidateAllDropsBothMapsAfterAccountWrite) {
    // invalidate_all() is the live hook fired after a successful write_account_file: any cached
    // account/owner data must be dropped so subsequent reads see the new state.
    g_account_reader_result.normalized_email = "before-write";
    g_owner_resolver_owner = "alpha-admin";
    std::string error_message;

    account::AccountData account_out;
    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &account_out, &error_message)) << error_message;
    std::string owner_out;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_out, &error_message)) << error_message;
    EXPECT_EQ(g_account_reader_calls, 1);
    EXPECT_EQ(g_owner_resolver_calls, 1);

    account_cache::invalidate_all();

    ASSERT_TRUE(account_cache::read_account_file_cached("root", "alpha-admin", &account_out, &error_message)) << error_message;
    ASSERT_TRUE(account_cache::find_linked_character_owner_account_cached("root", "aragorn", &owner_out, &error_message)) << error_message;
    EXPECT_EQ(g_account_reader_calls, 2) << "invalidate_all() must drop the account entry so it re-reads.";
    EXPECT_EQ(g_owner_resolver_calls, 2) << "invalidate_all() must drop the owner entry so it re-reads.";
}

TEST_F(AccountCache, BaseReadDelegatesToCacheOnlyWhenEnabled) {
    // The adoption seam: account::read_account_file (the BASE function all live callers use) delegates
    // to the cache when enabled, and behaves as the uncached read when not. With the counting fake as
    // the backing resolver, a delegated read is memoized (second call does not hit the resolver).
    g_account_reader_result.normalized_email = "delegated@example.com";
    std::string error_message;

    account_cache::set_enabled(true);
    account::AccountData first;
    ASSERT_TRUE(account::read_account_file("root", "alpha-admin", &first, &error_message)) << error_message;
    EXPECT_EQ(first.normalized_email, "delegated@example.com");
    EXPECT_EQ(g_account_reader_calls, 1);

    account::AccountData second;
    ASSERT_TRUE(account::read_account_file("root", "alpha-admin", &second, &error_message)) << error_message;
    EXPECT_EQ(g_account_reader_calls, 1) << "when enabled, the base read must delegate to the cache and be memoized.";

    // Owner resolver delegates the same way.
    g_owner_resolver_owner = "alpha-admin";
    std::string owner;
    ASSERT_TRUE(account::find_linked_character_owner_account("root", "aragorn", &owner, &error_message)) << error_message;
    ASSERT_TRUE(account::find_linked_character_owner_account("root", "aragorn", &owner, &error_message)) << error_message;
    EXPECT_EQ(owner, "alpha-admin");
    EXPECT_EQ(g_owner_resolver_calls, 1) << "when enabled, the base owner resolve must delegate to the cache.";

    // TearDown disables again so this never leaks to other suites.
}
