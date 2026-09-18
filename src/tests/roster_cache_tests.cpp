#include "../roster_cache.h"
#include "../account_management.h"

#include <gtest/gtest.h>

namespace {

int g_reader_calls = 0;
char_file_u g_reader_result {};
bool g_reader_succeeds = true;

bool fake_reader(const std::string&, const std::string&, const std::string&,
    char_file_u* stored_character, std::string* error_message)
{
    ++g_reader_calls;
    if (!g_reader_succeeds) {
        if (error_message)
            *error_message = "fake read failure";
        return false;
    }
    *stored_character = g_reader_result;
    if (error_message)
        *error_message = "";
    return true;
}

class RosterCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        roster_cache::clear();
        roster_cache::set_backing_reader_for_testing(&fake_reader);
        roster_cache::set_enabled(true);
        g_reader_calls = 0;
        g_reader_succeeds = true;
        g_reader_result = char_file_u {};
        g_reader_result.level = 42;
        g_reader_result.race = RACE_WOOD;
        g_reader_result.profs.prof_coof[PROF_WARRIOR] = 150;
    }

    void TearDown() override
    {
        roster_cache::set_backing_reader_for_testing(nullptr);
        roster_cache::set_enabled(false);
        roster_cache::clear();
    }
};

} // namespace

TEST_F(RosterCacheTest, SecondReadOfTheSameCharacterDoesNotHitTheBackingReader)
{
    roster_cache::RosterSummary first {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &first));
    EXPECT_EQ(g_reader_calls, 1);
    EXPECT_EQ(first.level, 42);
    EXPECT_EQ(first.race, RACE_WOOD);
    EXPECT_TRUE(first.readable);
    EXPECT_EQ(first.prof_coof[PROF_WARRIOR], 150);

    roster_cache::RosterSummary second {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &second));
    EXPECT_EQ(g_reader_calls, 1) << "second get() re-read the character file";
    EXPECT_EQ(second.level, 42);
}

TEST_F(RosterCacheTest, InvalidatingOneCharacterLeavesOtherEntriesIntact)
{
    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    ASSERT_TRUE(roster_cache::get(".", "acct", "legolas", &summary));
    ASSERT_EQ(g_reader_calls, 2);

    roster_cache::invalidate_character(".", "aragorn");

    ASSERT_TRUE(roster_cache::get(".", "acct", "legolas", &summary));
    EXPECT_EQ(g_reader_calls, 2) << "invalidating aragorn also dropped legolas";

    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    EXPECT_EQ(g_reader_calls, 3) << "aragorn was not re-read after invalidation";
}

TEST_F(RosterCacheTest, UnreadableCharacterIsCachedAsUnreadableAndNotReparsed)
{
    g_reader_succeeds = false;

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "broken", &summary));
    EXPECT_FALSE(summary.readable);
    EXPECT_EQ(g_reader_calls, 1);

    ASSERT_TRUE(roster_cache::get(".", "acct", "broken", &summary));
    EXPECT_FALSE(summary.readable);
    EXPECT_EQ(g_reader_calls, 1) << "an unreadable character was re-parsed on every render";
}

TEST_F(RosterCacheTest, CoefficientsAreClampedToTheSquareRootTableDomain)
{
    g_reader_result.profs.prof_coof[PROF_MAGE] = 99999;
    g_reader_result.profs.prof_coof[PROF_CLERIC] = -5;

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "corrupt", &summary));
    EXPECT_EQ(summary.prof_coof[PROF_MAGE], 170);
    EXPECT_EQ(summary.prof_coof[PROF_CLERIC], 0);
}

TEST_F(RosterCacheTest, DisabledCacheAlwaysReadsThrough)
{
    roster_cache::set_enabled(false);

    roster_cache::RosterSummary summary {};
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    ASSERT_TRUE(roster_cache::get(".", "acct", "aragorn", &summary));
    EXPECT_EQ(g_reader_calls, 2);
}
