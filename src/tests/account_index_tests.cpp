#include "account_index.h"
#include "account_management.h"
#include "account_management_types.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

// Byte-exact, because the assertion that matters for a record the server refused to read is that
// it is still there UNCHANGED -- not merely that some file exists at the path.
std::string read_whole_file(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return std::string();
    std::string contents;
    char buffer[512];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);
        if (bytes_read < sizeof(buffer))
            break;
    }
    std::fclose(file);
    return contents;
}

void write_whole_file(const std::string& path, const std::string& contents)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(std::fclose(file), 0);
}

account::AccountData make_account(const std::string& email, const std::string& name,
    const std::vector<std::string>& characters)
{
    account::AccountData account;
    account.normalized_email = email;
    account.account_name = name;
    account.characters = characters;
    return account;
}

// The resolvers under test read real files, so these need a real accounts/ tree. Kept local rather
// than shared with account_management_tests.cpp, which has its own copy in its own anonymous
// namespace.
class IndexTemporaryDirectory {
public:
    IndexTemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-account-index-XXXXXX";
        char* created_path = mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~IndexTemporaryDirectory()
    {
        if (!m_path.empty())
            remove_tree(m_path);
    }

    const std::string& path() const { return m_path; }

private:
    static void remove_tree(const std::string& path)
    {
        DIR* directory = opendir(path.c_str());
        if (directory == nullptr) {
            std::remove(path.c_str());
            return;
        }

        while (dirent* entry = readdir(directory)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;

            const std::string child_path = path + "/" + entry->d_name;
            struct stat file_info { };
            if (stat(child_path.c_str(), &file_info) != 0)
                continue;

            if (S_ISDIR(file_info.st_mode))
                remove_tree(child_path);
            else
                std::remove(child_path.c_str());
        }

        closedir(directory);
        rmdir(path.c_str());
    }

    std::string m_path;
};

// Restores the flag no matter how the test exits, so one failing EXPECT cannot leave the index on
// for every test that runs after it.
class ScopedIndexEnabled {
public:
    explicit ScopedIndexEnabled(bool enabled) { account_index::set_enabled(enabled); }
    ~ScopedIndexEnabled() { account_index::set_enabled(false); }
};

class AccountIndexTest : public ::testing::Test {
protected:
    void SetUp() override { account_index::clear(); }
    void TearDown() override { account_index::clear(); }
};

TEST_F(AccountIndexTest, UpsertMakesRecordFindableByAllThreeKeys)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo", "Sam" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");

    path.clear();
    ASSERT_TRUE(account_index::find_path_by_account_name("player", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");
    ASSERT_TRUE(account_index::find_owner_email_by_character("Sam", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");
}

TEST_F(AccountIndexTest, LookupsAreCaseInsensitiveViaNormalization)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("PLAYER@Example.COM", &path, nullptr));
    EXPECT_TRUE(account_index::find_path_by_account_name("PlAyEr", &path, nullptr));

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("FRODO", &owner_email, nullptr));
}

TEST_F(AccountIndexTest, UnknownKeysReturnFalse)
{
    std::string path;
    EXPECT_FALSE(account_index::find_path_by_email("nobody@example.com", &path, nullptr));
    EXPECT_FALSE(account_index::find_path_by_account_name("nobody", &path, nullptr));

    std::string owner_email;
    EXPECT_FALSE(account_index::find_owner_email_by_character("Nobody", &owner_email, nullptr));
}

TEST_F(AccountIndexTest, UpsertDropsKeysTheRecordNoLongerOwns)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo", "Sam" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_FALSE(account_index::find_owner_email_by_character("Sam", &owner_email, nullptr))
        << "an unlinked character must stop resolving, or a save writes to the wrong account";
}

TEST_F(AccountIndexTest, UpsertFollowsAnAccountNameChange)
{
    account_index::upsert(make_account("player@example.com", "oldname", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "newname", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_account_name("newname", &path, nullptr));
    EXPECT_FALSE(account_index::find_path_by_account_name("oldname", &path, nullptr));
}

TEST_F(AccountIndexTest, EnabledFlagDefaultsOffAndToggles)
{
    EXPECT_FALSE(account_index::is_enabled());
    account_index::set_enabled(true);
    EXPECT_TRUE(account_index::is_enabled());
    account_index::set_enabled(false);
    EXPECT_FALSE(account_index::is_enabled());
}

TEST_F(AccountIndexTest, SizeCountsRecordsNotKeys)
{
    account_index::upsert(make_account("a@example.com", "aaa", { "One", "Two", "Three" }),
        "accounts/A-E/a@example.com/account.json");
    account_index::upsert(make_account("b@example.com", "bbb", {}),
        "accounts/A-E/b@example.com/account.json");
    EXPECT_EQ(account_index::size(), 2u);
}

TEST_F(AccountIndexTest, FindEmailByAccountNameReturnsTheEmailForAnIndexedAccount)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    std::string email;
    ASSERT_TRUE(account_index::find_email_by_account_name("player", &email, nullptr));
    EXPECT_EQ(email, "player@example.com");
}

TEST_F(AccountIndexTest, FindEmailByAccountNameReturnsFalseForAnUnknownName)
{
    std::string email;
    EXPECT_FALSE(account_index::find_email_by_account_name("nobody", &email, nullptr));
}

TEST_F(AccountIndexTest, QuarantinedEmailStaysOccupiedSoItCannotBeOverwritten)
{
    account_index::quarantine("broken@example.com", "accounts/A-E/broken@example.com/account.json",
        "unparseable JSON");

    EXPECT_TRUE(account_index::is_quarantined("broken@example.com"));

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("broken@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "That account record could not be read.")
        << "must NOT read as 'no account exists', or creation would write over a real record";
}

TEST_F(AccountIndexTest, QuarantineDropsTheKeysTheRecordHeldBefore)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::quarantine("player@example.com", "accounts/P-T/player@example.com/account.json",
        "mismatched email");

    std::string owner_email;
    EXPECT_FALSE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_account_name("player", &path, nullptr));
}

TEST_F(AccountIndexTest, QuarantinedCountTracksOnlyBadRecords)
{
    account_index::upsert(make_account("good@example.com", "good", {}),
        "accounts/F-J/good@example.com/account.json");
    account_index::quarantine("bad1@example.com", "accounts/A-E/bad1@example.com/account.json", "x");
    account_index::quarantine("bad2@example.com", "accounts/A-E/bad2@example.com/account.json", "y");

    EXPECT_EQ(account_index::quarantined_count(), 2u);
    EXPECT_EQ(account_index::size(), 3u);
}

TEST_F(AccountIndexTest, QuarantineThresholdIsTen)
{
    // Ten of the roughly fifty accounts on live. Five was two orders of magnitude below the
    // record count it is meant to detect a format break in -- such a break takes out every
    // record at once -- while sitting right on top of the one-off it must never fire on.
    EXPECT_EQ(account_index::MAX_QUARANTINED_RECORDS_AT_BOOT, 10u);
}

TEST_F(AccountIndexTest, QuarantinedEntriesReportPathAndReason)
{
    account_index::quarantine("bad@example.com", "accounts/A-E/bad@example.com/account.json",
        "unparseable JSON");

    const std::vector<account_index::Entry> entries = account_index::quarantined_entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].normalized_email, "bad@example.com");
    EXPECT_EQ(entries[0].record_path, "accounts/A-E/bad@example.com/account.json");
    EXPECT_EQ(entries[0].quarantine_reason, "unparseable JSON");
}

TEST_F(AccountIndexTest, BootToleratesFailuresUpToTheThreshold)
{
    for (std::size_t i = 0; i < account_index::MAX_QUARANTINED_RECORDS_AT_BOOT; ++i) {
        const std::string email = "bad" + std::to_string(i) + "@example.com";
        account_index::quarantine(email, "accounts/A-E/" + email + "/account.json", "unparseable");
    }

    EXPECT_EQ(account_index::quarantined_count(), account_index::MAX_QUARANTINED_RECORDS_AT_BOOT);
    EXPECT_FALSE(account_index::quarantined_count() > account_index::MAX_QUARANTINED_RECORDS_AT_BOOT)
        << "exactly at the threshold must still boot";
}

TEST_F(AccountIndexTest, BootRefusesPastTheThreshold)
{
    for (std::size_t i = 0; i <= account_index::MAX_QUARANTINED_RECORDS_AT_BOOT; ++i) {
        const std::string email = "bad" + std::to_string(i) + "@example.com";
        account_index::quarantine(email, "accounts/A-E/" + email + "/account.json", "unparseable");
    }

    EXPECT_TRUE(account_index::quarantined_count() > account_index::MAX_QUARANTINED_RECORDS_AT_BOOT);
}

TEST_F(AccountIndexTest, FlatRecordDoesNotDisplaceADirectoryRecordForTheSameEmail)
{
    const account::AccountData directory_account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(directory_account, "accounts/P-T/player@example.com/account.json", /*legacy_flat_layout=*/false);

    const account::AccountData flat_account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(flat_account, "accounts/P-T/player.json", /*legacy_flat_layout=*/true);

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json")
        << "the directory record must remain authoritative; the flat record must not win";
}

TEST_F(AccountIndexTest, DirectoryRecordDisplacesAFlatRecordRegardlessOfArrivalOrder)
{
    const account::AccountData flat_account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(flat_account, "accounts/P-T/player.json", /*legacy_flat_layout=*/true);

    const account::AccountData directory_account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(directory_account, "accounts/P-T/player@example.com/account.json", /*legacy_flat_layout=*/false);

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json")
        << "a directory record arriving after a flat one must still overwrite it";
}

TEST_F(AccountIndexTest, DisabledIndexIsNotConsulted)
{
    account_index::set_enabled(false);
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    // The index still answers its own API when queried directly; is_enabled() only governs whether
    // the account resolvers consult it. This test pins that distinction so the flag is not
    // mistakenly wired into the lookups themselves.
    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_FALSE(account_index::is_enabled());
}

TEST_F(AccountIndexTest, UnknownEmailErrorMatchesTheScanText)
{
    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("nobody@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "No account exists for that email address.")
        << "must match find_account_by_email_internal's text verbatim; tests assert on it";
}

TEST_F(AccountIndexTest, UnknownAccountNameErrorMatchesTheScanText)
{
    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_account_name("nobody", &path, &error_message));
    EXPECT_EQ(error_message,
        std::string("Failed to open account file for account 'nobody': ") + std::strerror(ENOENT))
        << "must match find_account_file_path_by_account_name's not-found text verbatim";
}

TEST_F(AccountIndexTest, UnknownCharacterIsReportedWithoutAnErrorMessage)
{
    // find_linked_character_owner_account_uncached's index fast path tells "not linked" (a success
    // for that resolver, true with an empty owner) apart from a real failure by whether a message
    // was set. An unknown character MUST leave it empty or every save of an unlinked character
    // starts reporting an error.
    std::string owner_email;
    std::string error_message = "sentinel";
    EXPECT_FALSE(account_index::find_owner_email_by_character("Nobody", &owner_email, &error_message));
    EXPECT_EQ(error_message, "");
}

// Step 9 of the task brief: with a real accounts/ tree on disk, every resolver must give the same
// answer with the index on as it does with the index off (the directory scan). This is the test
// that would catch a fast path that resolves to the wrong record, not merely a slow one.
TEST_F(AccountIndexTest, ResolversAgreeWithTheScanWhenTheIndexIsOn)
{
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    // Declared before the writes below: write_account_file only indexes a record written against
    // the index's own root, so without this the "indexed" observation would run against an empty
    // index and the comparison would be vacuous in the other direction.
    account_index::set_root_directory(root);

    std::string error_message;
    account::AccountData first_account;
    ASSERT_TRUE(account::create_account_for_email(root, "agree@example.com", "ValidPass1", 1700000000,
        &first_account, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&first_account, "Frodo", &error_message))
        << error_message;
    ASSERT_TRUE(account::write_account_file(root, first_account, &error_message)) << error_message;

    account::AccountData second_account;
    ASSERT_TRUE(account::create_account_for_email(root, "other@example.com", "ValidPass1", 1700000000,
        &second_account, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&second_account, "Samwise", &error_message))
        << error_message;
    ASSERT_TRUE(account::write_account_file(root, second_account, &error_message)) << error_message;

    struct Observation {
        bool by_email_ok = false;
        std::string by_email_name;
        bool by_email_error = false;
        bool by_name_ok = false;
        std::string by_name_email;
        bool owner_ok = false;
        std::string owner_name;
        std::string owner_error;
        bool missing_owner_ok = false;
        std::string missing_owner_name;
        std::string missing_owner_error;
        bool unknown_email_ok = false;
        std::string unknown_email_error;
        bool unknown_name_ok = false;
        std::string unknown_name_error;
        std::string character_directory;
    };

    const std::string account_name = first_account.account_name;

    const auto observe = [&root, &account_name]() {
        Observation observation;
        std::string message;

        account::AccountData read_by_email;
        observation.by_email_ok = account::read_account_file_by_email(root, "agree@example.com",
            &read_by_email, &message);
        observation.by_email_name = read_by_email.account_name;
        observation.by_email_error = !message.empty();

        account::AccountData read_by_name;
        observation.by_name_ok = account::read_account_file(root, account_name, &read_by_name,
            &message);
        observation.by_name_email = read_by_name.normalized_email;

        observation.owner_ok = account::find_linked_character_owner_account_uncached(root, "Frodo",
            &observation.owner_name, &observation.owner_error);

        observation.missing_owner_ok = account::find_linked_character_owner_account_uncached(root,
            "Meriadoc", &observation.missing_owner_name, &observation.missing_owner_error);

        account::AccountData unknown;
        observation.unknown_email_ok = account::read_account_file_by_email(root, "nobody@example.com",
            &unknown, &observation.unknown_email_error);
        observation.unknown_name_ok = account::read_account_file(root, "nobodyatall", &unknown,
            &observation.unknown_name_error);

        observation.character_directory = account::account_character_directory(root,
            account_name, "Frodo");
        return observation;
    };

    Observation scanned;
    {
        ScopedIndexEnabled disabled(false);
        scanned = observe();
    }

    Observation indexed;
    {
        // The root the index was told about above is what keeps the fast paths from falling through
        // to the scan here, which would make the comparison below scan-against-scan, i.e. vacuous.
        ScopedIndexEnabled enabled(true);
        indexed = observe();
        account_index::set_root_directory(".");
    }

    EXPECT_EQ(scanned.by_email_ok, indexed.by_email_ok);
    EXPECT_EQ(scanned.by_email_name, indexed.by_email_name);
    EXPECT_EQ(scanned.by_name_ok, indexed.by_name_ok);
    EXPECT_EQ(scanned.by_name_email, indexed.by_name_email);

    EXPECT_EQ(scanned.owner_ok, indexed.owner_ok);
    EXPECT_EQ(scanned.owner_name, indexed.owner_name);
    EXPECT_EQ(scanned.owner_error, indexed.owner_error);

    // The load-bearing one: an unlinked character resolves successfully with an empty owner and an
    // empty error. If the index path returned false here, every save of an unlinked character would
    // start looking like a failure.
    EXPECT_TRUE(scanned.missing_owner_ok);
    EXPECT_EQ(scanned.missing_owner_ok, indexed.missing_owner_ok);
    EXPECT_EQ(scanned.missing_owner_name, indexed.missing_owner_name);
    EXPECT_EQ(scanned.missing_owner_error, indexed.missing_owner_error);
    EXPECT_EQ(indexed.missing_owner_name, "");
    EXPECT_EQ(indexed.missing_owner_error, "");

    EXPECT_EQ(scanned.unknown_email_ok, indexed.unknown_email_ok);
    EXPECT_EQ(scanned.unknown_email_error, indexed.unknown_email_error);
    EXPECT_EQ(scanned.unknown_name_ok, indexed.unknown_name_ok);
    EXPECT_EQ(scanned.unknown_name_error, indexed.unknown_name_error);

    EXPECT_EQ(scanned.character_directory, indexed.character_directory);
    EXPECT_NE(indexed.character_directory, "");
}

// --- Duplicate account names ------------------------------------------------------------------
// Two records claiming one account name are not merely ambiguous, they are dangerous:
// write_account_file std::remove()s the path find_account_file_path_by_account_name returns when it
// differs from the record being written, so resolving to one of the pair deletes the other's file.
// The directory scan refused to answer; so must the index.

TEST_F(AccountIndexTest, TwoEmailsClaimingOneAccountNameMakeTheNameLookupFail)
{
    account_index::upsert(make_account("first@example.com", "shared", { "Frodo" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", { "Sam" }),
        "accounts/P-T/second@example.com/account.json");

    EXPECT_TRUE(account_index::is_account_name_ambiguous("shared"));

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_account_name("shared", &path, &error_message));
    EXPECT_EQ(error_message, "Multiple account records exist for account 'shared'.")
        << "must match find_account_file_path_by_account_name's duplicate text verbatim";

    std::string email;
    error_message.clear();
    EXPECT_FALSE(account_index::find_email_by_account_name("shared", &email, &error_message));
    EXPECT_EQ(error_message, "Multiple account records exist for account 'shared'.");
}

TEST_F(AccountIndexTest, AnAmbiguousNameLeaksNoPathToEitherRecord)
{
    account_index::upsert(make_account("first@example.com", "shared", { "Frodo" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", { "Sam" }),
        "accounts/P-T/second@example.com/account.json");

    std::string path = "untouched";
    EXPECT_FALSE(account_index::find_path_by_account_name("shared", &path, nullptr));
    EXPECT_EQ(path, "untouched") << "a path here would let write_account_file delete the other record";

    std::string email = "untouched";
    EXPECT_FALSE(account_index::find_email_by_account_name("shared", &email, nullptr));
    EXPECT_EQ(email, "untouched");

    // The per-email keys are untouched: only the shared NAME is unresolvable.
    std::string by_email;
    EXPECT_TRUE(account_index::find_path_by_email("first@example.com", &by_email, nullptr));
    EXPECT_EQ(by_email, "accounts/A-E/first@example.com/account.json");
    EXPECT_TRUE(account_index::find_path_by_email("second@example.com", &by_email, nullptr));
    EXPECT_EQ(by_email, "accounts/P-T/second@example.com/account.json");
}

TEST_F(AccountIndexTest, OneEmailInBothLayoutsIsNotAmbiguous)
{
    // A flat record and a directory record for the SAME email are one account stored two ways --
    // exactly the case the Task 3 precedence rule exists for -- not two records claiming one name.
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player.json", /*legacy_flat_layout=*/true);
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json", /*legacy_flat_layout=*/false);

    EXPECT_FALSE(account_index::is_account_name_ambiguous("player"));

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_account_name("player", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");
}

TEST_F(AccountIndexTest, ReUpsertingTheSameRecordDoesNotMakeItsOwnNameAmbiguous)
{
    // write_account_file upserts on every account write, so the common case is the same email
    // re-binding its own name over and over. That must never look like a duplicate.
    for (int pass = 0; pass < 3; ++pass) {
        account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
            "accounts/P-T/player@example.com/account.json");
    }

    EXPECT_FALSE(account_index::is_account_name_ambiguous("player"));
    std::string path;
    EXPECT_TRUE(account_index::find_path_by_account_name("player", &path, nullptr));
}

TEST_F(AccountIndexTest, AmbiguityIsCaseInsensitiveLikeEveryOtherKey)
{
    account_index::upsert(make_account("first@example.com", "Shared", { "Frodo" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "SHARED", { "Sam" }),
        "accounts/P-T/second@example.com/account.json");

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_account_name("sHaReD", &path, &error_message));
    EXPECT_EQ(error_message, "Multiple account records exist for account 'shared'.");
}

// --- Root guard ---------------------------------------------------------------------------------

TEST_F(AccountIndexTest, RootDirectoryDefaultsToDotAndClearResetsIt)
{
    EXPECT_EQ(account_index::root_directory(), ".");
    EXPECT_TRUE(account_index::matches_root("."));
    EXPECT_FALSE(account_index::matches_root("/tmp/elsewhere"));

    account_index::set_root_directory("/tmp/elsewhere");
    EXPECT_TRUE(account_index::matches_root("/tmp/elsewhere"));
    EXPECT_FALSE(account_index::matches_root("."));

    account_index::clear();
    EXPECT_EQ(account_index::root_directory(), ".");
}

TEST_F(AccountIndexTest, ResolversFallBackToTheScanForAMismatchedRoot)
{
    // The fast paths ignore their own root_directory argument, so answering from an index built for
    // a different tree would hand back paths from that tree. They must fall through to the scan --
    // which still gets the right answer, just slowly.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData created;
    ASSERT_TRUE(account::create_account_for_email(root, "rooted@example.com", "ValidPass1",
        1700000000, &created, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&created, "Frodo", &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created, &error_message)) << error_message;

    // Index enabled, but still claiming the default "." root while the caller uses the temp tree.
    ScopedIndexEnabled enabled(true);
    ASSERT_EQ(account_index::root_directory(), ".");

    account::AccountData read_back;
    EXPECT_TRUE(account::read_account_file_by_email(root, "rooted@example.com", &read_back,
        &error_message))
        << error_message;
    EXPECT_EQ(read_back.account_name, created.account_name);

    std::string owner;
    EXPECT_TRUE(account::find_linked_character_owner_account_uncached(root, "Frodo", &owner,
        &error_message))
        << error_message;
    EXPECT_EQ(owner, created.account_name);
}

TEST_F(AccountIndexTest, QuarantinedAddressIsNotFreeForCreation)
{
    // create_account_for_email decides an address is free when its lookup fails, and a quarantined
    // record's lookup fails the same way an unused address's does. Without a guard, this would let a
    // brand-new account overwrite a real player's unparseable-but-real record.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    account::AccountData created;
    std::string error_message;
    {
        // Root must match the caller's root_directory, exactly like the resolver fast paths --
        // otherwise the guard would be silently skipped and this test would pass for the wrong
        // reason.
        account_index::set_root_directory(root);
        ScopedIndexEnabled enabled(true);
        account_index::quarantine("occupied@example.com",
            "accounts/K-O/occupied@example.com/account.json", "unparseable JSON");

        EXPECT_TRUE(account_index::is_quarantined("occupied@example.com"));

        ASSERT_FALSE(account::create_account_for_email(root, "occupied@example.com", "ValidPass1",
            1000, &created, &error_message))
            << "creating here would overwrite a real player's record";
        account_index::set_root_directory(".");
    }

    EXPECT_FALSE(error_message.empty());
    EXPECT_EQ(error_message.find("already exists"), std::string::npos)
        << "must not disclose that a record exists at this address: " << error_message;
}

// --- Character-name ambiguity -------------------------------------------------------------------
// Two records claiming one character is the silent wrong-account failure, and it lands on the save
// path: save_char picks the directory it writes a character file into from this answer, and creates
// one there when the resolved account has none.

TEST_F(AccountIndexTest, ACharacterClaimedByTwoRecordsRefusesWithTheScanText)
{
    account_index::upsert(make_account("first@example.com", "first", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "second", { "Bob" }),
        "accounts/P-T/second@example.com/account.json");

    EXPECT_TRUE(account_index::is_character_ambiguous("Bob"));

    std::string owner_email;
    std::string error_message;
    EXPECT_FALSE(account_index::find_owner_email_by_character("Bob", &owner_email, &error_message));
    EXPECT_EQ(error_message, "Multiple account records claim that linked character.")
        << "must match find_character_owner_account's text verbatim";
}

TEST_F(AccountIndexTest, AnAmbiguousCharacterProducesTheErrorFormNotTheNotLinkedForm)
{
    // The distinction the owner resolver draws: an EMPTY message means "resolved, linked to no
    // account", which save_char acts on by treating the character as unlinked -- the exact silent
    // mis-save this refusal exists to prevent. A non-empty message is the error form.
    account_index::upsert(make_account("first@example.com", "first", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "second", { "Bob" }),
        "accounts/P-T/second@example.com/account.json");

    std::string owner_email;
    std::string error_message;
    ASSERT_FALSE(account_index::find_owner_email_by_character("Bob", &owner_email, &error_message));
    EXPECT_FALSE(error_message.empty())
        << "an empty message here would make the save path treat the character as unlinked";
}

TEST_F(AccountIndexTest, AmbiguityIsRecognisedThroughNameNormalization)
{
    account_index::upsert(make_account("first@example.com", "first", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "second", { "BOB" }),
        "accounts/P-T/second@example.com/account.json");

    std::string owner_email;
    EXPECT_FALSE(account_index::find_owner_email_by_character("bOb", &owner_email, nullptr));
}

TEST_F(AccountIndexTest, RewritingOneRecordNeverMakesItsOwnCharactersAmbiguous)
{
    // The false positive that would matter most: every account write re-upserts the whole record,
    // so if a record's own characters counted as a second claim, one save would break every
    // subsequent one.
    account_index::upsert(make_account("only@example.com", "only", { "Bob", "Sam" }),
        "accounts/K-O/only@example.com/account.json");
    account_index::upsert(make_account("only@example.com", "only", { "Bob", "Sam" }),
        "accounts/K-O/only@example.com/account.json");
    account_index::upsert(make_account("only@example.com", "renamed", { "Bob", "Sam", "Frodo" }),
        "accounts/K-O/only@example.com/account.json");

    EXPECT_FALSE(account_index::is_character_ambiguous("Bob"));

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Bob", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "only@example.com");
}

TEST_F(AccountIndexTest, AmbiguousCharacterFailsTheSameWayWithTheIndexOnAsWithTheScan)
{
    // End to end through the resolver both the save path and the account menu call, against a real
    // accounts/ tree that really does have two records claiming one character.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    // Declared before the writes: write_account_file only indexes records written against the
    // index's own root.
    account_index::set_root_directory(root);

    std::string error_message;
    account::AccountData first_account;
    ASSERT_TRUE(account::create_account_for_email(root, "one@example.com", "ValidPass1", 1700000000,
        &first_account, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&first_account, "Bob", &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, first_account, &error_message)) << error_message;

    account::AccountData second_account;
    ASSERT_TRUE(account::create_account_for_email(root, "two@example.com", "ValidPass1", 1700000000,
        &second_account, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&second_account, "Bob", &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, second_account, &error_message)) << error_message;

    std::string scanned_owner;
    std::string scanned_error;
    bool scanned_ok = false;
    {
        ScopedIndexEnabled disabled(false);
        scanned_ok = account::find_linked_character_owner_account_uncached(root, "Bob",
            &scanned_owner, &scanned_error);
    }

    std::string indexed_owner;
    std::string indexed_error;
    bool indexed_ok = false;
    {
        ScopedIndexEnabled enabled(true);
        indexed_ok = account::find_linked_character_owner_account_uncached(root, "Bob",
            &indexed_owner, &indexed_error);
    }
    account_index::set_root_directory(".");

    EXPECT_FALSE(scanned_ok);
    EXPECT_EQ(scanned_error, "Multiple account records claim that linked character.");
    EXPECT_EQ(indexed_ok, scanned_ok);
    EXPECT_EQ(indexed_error, scanned_error);
    // The one deliberate difference: on a duplicate the scan leaves its first match sitting in the
    // out-parameter (account_management.cpp:975-981) while the index path clears it before
    // answering. Every caller tests the return value first -- save_char's own use is
    // `find_linked_character_owner_account(...) && !owner_account_name.empty()` (db.cpp:3269) -- so
    // nothing reads it either way, and empty is the safer of the two.
    EXPECT_EQ(indexed_owner, "");
}

// --- Email ambiguity ----------------------------------------------------------------------------

TEST_F(AccountIndexTest, TwoRecordsAtOneAddressUnderDifferentNamesRefuseWithTheScanText)
{
    account_index::upsert(make_account("shared@example.com", "aaa", {}),
        "accounts/P-T/aaa.json", /*legacy_flat_layout=*/true);
    account_index::upsert(make_account("shared@example.com", "bbb", {}),
        "accounts/P-T/bbb.json", /*legacy_flat_layout=*/true);

    EXPECT_TRUE(account_index::is_email_ambiguous("shared@example.com"));

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("SHARED@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "Multiple account records exist for that email address.")
        << "must match find_account_by_email_internal's text verbatim";
}

TEST_F(AccountIndexTest, TheOrdinaryFlatAndDirectoryPairIsNotAmbiguous)
{
    // Same account under both layouts is what the scan deduplicates by preferring the directory
    // record, not a duplicate. Getting this wrong would lock out every account mid-migration.
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player.json", /*legacy_flat_layout=*/true);
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json", /*legacy_flat_layout=*/false);

    EXPECT_FALSE(account_index::is_email_ambiguous("player@example.com"));

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");
}

TEST_F(AccountIndexTest, AnAccountNameChangeAtTheSamePathIsNotAmbiguous)
{
    // Every rewrite re-upserts, and a rename changes the account name while the path (composed from
    // the email) stays put. Treating that as a duplicate would lock an account out of its own record
    // the first time it renamed.
    account_index::upsert(make_account("player@example.com", "oldname", {}),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "newname", {}),
        "accounts/P-T/player@example.com/account.json");

    EXPECT_FALSE(account_index::is_email_ambiguous("player@example.com"));

    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
}

TEST_F(AccountIndexTest, AQuarantinedRecordIsNotRewordedAsAnAmbiguousEmail)
{
    // A quarantined entry holds no account name to compare against, and its email is reserved on
    // purpose. "Could not be read" must stay the answer for that address.
    account_index::quarantine("broken@example.com",
        "accounts/A-E/broken@example.com/account.json", "unparseable JSON");
    account_index::upsert(make_account("broken@example.com", "broken", {}),
        "accounts/A-E/broken.json", /*legacy_flat_layout=*/true);

    EXPECT_FALSE(account_index::is_email_ambiguous("broken@example.com"));

    std::string path;
    std::string error_message;
    EXPECT_FALSE(account_index::find_path_by_email("broken@example.com", &path, &error_message));
    EXPECT_EQ(error_message, "That account record could not be read.");
}

// --- Quarantine must not take the game down for everyone -----------------------------------------

TEST_F(AccountIndexTest, AQuarantinedRecordDoesNotBlockCreationForEveryOtherAddress)
{
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E/broken@example.com").c_str(), 0700), 0);
    const std::string broken_path = root + "/accounts/A-E/broken@example.com/account.json";
    {
        FILE* broken_file = std::fopen(broken_path.c_str(), "w");
        ASSERT_NE(broken_file, nullptr);
        std::fputs("{ this is not valid account json", broken_file);
        std::fclose(broken_file);
    }

    // Index off: the pre-branch behaviour, which this change must leave exactly as it was -- one
    // unreadable record refuses creation for everybody.
    account::AccountData blocked;
    std::string error_message;
    EXPECT_FALSE(account::create_account_for_email(root, "newbie@example.com", "ValidPass1", 1000,
        &blocked, &error_message));
    EXPECT_EQ(error_message, "Existing account records could not be read safely.");

    // Index on, with that record quarantined exactly as the boot walker would key it (a directory
    // record is keyed by its entry name). The game has already decided to run with this record set
    // aside; it must not keep every other player from creating an account.
    account_index::set_root_directory(root);
    account_index::quarantine("broken@example.com", broken_path, "unparseable JSON");
    account::AccountData created;
    {
        ScopedIndexEnabled enabled(true);
        EXPECT_TRUE(account::create_account_for_email(root, "newbie@example.com", "ValidPass1", 1000,
            &created, &error_message))
            << error_message;

        // ...while the quarantined address itself stays reserved.
        account::AccountData refused;
        std::string refusal;
        EXPECT_FALSE(account::create_account_for_email(root, "broken@example.com", "ValidPass1",
            1000, &refused, &refusal));
    }
    account_index::set_root_directory(".");
}

TEST_F(AccountIndexTest, CreateAccountItselfRefusesAQuarantinedAddress)
{
    // create_account is public header API and today has exactly one caller, but the guard belongs
    // here too: the mismatched-email quarantine shape parses fine, so
    // account_storage_contains_unreadable_records cannot see it and a direct caller would write a
    // fresh account straight over a real player's record.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    account_index::set_root_directory(root);
    account_index::quarantine("occupied@example.com",
        "accounts/K-O/occupied@example.com/account.json", "mismatched normalized email");

    account::AccountData created;
    std::string error_message;
    {
        ScopedIndexEnabled enabled(true);
        EXPECT_FALSE(account::create_account(root, "newname", "occupied@example.com", "ValidPass1",
            1000, &created, &error_message))
            << "creating here would overwrite a real player's record";
    }
    account_index::set_root_directory(".");

    EXPECT_FALSE(error_message.empty());
    EXPECT_EQ(error_message.find("already exists"), std::string::npos)
        << "must not disclose that a record exists at this address: " << error_message;
}

// --- Contention lowers again --------------------------------------------------------------------
// Raising a refusal that only a reboot can clear is its own outage: a contested character key makes
// save_char write NOTHING for that character, silently and to the log only. Repairing the duplicate
// on disk and writing the repaired record has to be enough.

TEST_F(AccountIndexTest, UnlinkingACharacterFromOneRecordClearsTheContention)
{
    account_index::upsert(make_account("first@example.com", "first", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "second", { "Bob" }),
        "accounts/P-T/second@example.com/account.json");
    ASSERT_TRUE(account_index::is_character_ambiguous("Bob"));

    // The repair: the second record no longer lists Bob, and is written.
    account_index::upsert(make_account("second@example.com", "second", {}),
        "accounts/P-T/second@example.com/account.json");

    EXPECT_FALSE(account_index::is_character_ambiguous("Bob"))
        << "the flag must lower, or saves stay broken until the next reboot";

    std::string owner_email;
    std::string error_message = "sentinel";
    ASSERT_TRUE(account_index::find_owner_email_by_character("Bob", &owner_email, &error_message))
        << error_message;
    EXPECT_EQ(owner_email, "first@example.com") << "the surviving claimant must resolve";
}

TEST_F(AccountIndexTest, AThirdClaimantKeepsACharacterContestedUntilOnlyOneRemains)
{
    account_index::upsert(make_account("a@example.com", "aaa", { "Bob" }), "accounts/A-E/a@example.com/account.json");
    account_index::upsert(make_account("b@example.com", "bbb", { "Bob" }), "accounts/A-E/b@example.com/account.json");
    account_index::upsert(make_account("c@example.com", "ccc", { "Bob" }), "accounts/A-E/c@example.com/account.json");
    ASSERT_TRUE(account_index::is_character_ambiguous("Bob"));

    account_index::upsert(make_account("c@example.com", "ccc", {}), "accounts/A-E/c@example.com/account.json");
    EXPECT_TRUE(account_index::is_character_ambiguous("Bob"))
        << "two claimants are still two claimants";

    account_index::upsert(make_account("b@example.com", "bbb", {}), "accounts/A-E/b@example.com/account.json");
    EXPECT_FALSE(account_index::is_character_ambiguous("Bob"));

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Bob", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "a@example.com");
}

TEST_F(AccountIndexTest, RenamingOneOfTwoAccountsClearsTheContestedName)
{
    account_index::upsert(make_account("first@example.com", "shared", {}),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", {}),
        "accounts/P-T/second@example.com/account.json");
    ASSERT_TRUE(account_index::is_account_name_ambiguous("shared"));

    account_index::upsert(make_account("second@example.com", "unshared", {}),
        "accounts/P-T/second@example.com/account.json");

    EXPECT_FALSE(account_index::is_account_name_ambiguous("shared"));

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_account_name("shared", &path, nullptr));
    EXPECT_EQ(path, "accounts/A-E/first@example.com/account.json")
        << "the record that kept the name must own it again";
    ASSERT_TRUE(account_index::find_path_by_account_name("unshared", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/second@example.com/account.json");
}

TEST_F(AccountIndexTest, TwoRecordsAtOneAddressStopBeingAmbiguousOnceTheirNamesAgree)
{
    account_index::upsert(make_account("shared@example.com", "aaa", {}),
        "accounts/P-T/aaa.json", /*legacy_flat_layout=*/true);
    account_index::upsert(make_account("shared@example.com", "bbb", {}),
        "accounts/P-T/bbb.json", /*legacy_flat_layout=*/true);
    ASSERT_TRUE(account_index::is_email_ambiguous("shared@example.com"));

    // The repair for an address: the two records stop disagreeing about the account name, which is
    // exactly find_account_by_email_internal's own duplicate test.
    account_index::upsert(make_account("shared@example.com", "aaa", {}),
        "accounts/P-T/bbb.json", /*legacy_flat_layout=*/true);

    EXPECT_FALSE(account_index::is_email_ambiguous("shared@example.com"));

    std::string path;
    std::string error_message = "sentinel";
    ASSERT_TRUE(account_index::find_path_by_email("shared@example.com", &path, &error_message))
        << error_message;
}

TEST_F(AccountIndexTest, QuarantineWithdrawsTheRecordsClaimsSoTheSurvivorResolves)
{
    // Setting a duplicate record aside IS a repair: the record is out of the running for every key
    // it claimed, so whoever else claimed them gets them back.
    account_index::upsert(make_account("first@example.com", "shared", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", { "Bob" }),
        "accounts/P-T/second@example.com/account.json");
    ASSERT_TRUE(account_index::is_character_ambiguous("Bob"));
    ASSERT_TRUE(account_index::is_account_name_ambiguous("shared"));

    account_index::quarantine("second@example.com", "accounts/P-T/second@example.com/account.json",
        "unparseable JSON");

    EXPECT_FALSE(account_index::is_character_ambiguous("Bob"));
    EXPECT_FALSE(account_index::is_account_name_ambiguous("shared"));

    std::string owner_email;
    ASSERT_TRUE(account_index::find_owner_email_by_character("Bob", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "first@example.com");

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_account_name("shared", &path, nullptr));
    EXPECT_EQ(path, "accounts/A-E/first@example.com/account.json");
}

TEST_F(AccountIndexTest, ContestedKeysAreListedForTheWizardCommand)
{
    // `account index` prints these. A contested character key silently stops that character's saves
    // while nothing else reports it either (index and disk agree perfectly about a real on-disk
    // duplicate), so this listing is the only place the state is visible.
    account_index::upsert(make_account("first@example.com", "shared", { "Bob" }),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", { "Bob" }),
        "accounts/P-T/second@example.com/account.json");

    const std::vector<account_index::ContestedKey> contested = account_index::contested_keys();
    ASSERT_EQ(contested.size(), 2u);

    // Sorted by kind then key: "account name" before "character".
    EXPECT_EQ(contested[0].kind, "account name");
    EXPECT_EQ(contested[0].key, "shared");
    ASSERT_EQ(contested[0].claimants.size(), 2u);
    EXPECT_EQ(contested[0].claimants[0], "first@example.com");
    EXPECT_EQ(contested[0].claimants[1], "second@example.com");

    EXPECT_EQ(contested[1].kind, "character");
    EXPECT_EQ(contested[1].key, "bob");
    ASSERT_EQ(contested[1].claimants.size(), 2u);
}

TEST_F(AccountIndexTest, ContestedKeysIsEmptyForAHealthyIndex)
{
    account_index::upsert(make_account("player@example.com", "player", { "Frodo", "Sam" }),
        "accounts/P-T/player@example.com/account.json");
    account_index::upsert(make_account("player@example.com", "player", { "Frodo" }),
        "accounts/P-T/player@example.com/account.json");

    EXPECT_TRUE(account_index::contested_keys().empty());
}

TEST_F(AccountIndexTest, ContestedEmailsAreListedWithTheirRecordPaths)
{
    account_index::upsert(make_account("shared@example.com", "aaa", {}),
        "accounts/P-T/aaa.json", /*legacy_flat_layout=*/true);
    account_index::upsert(make_account("shared@example.com", "bbb", {}),
        "accounts/P-T/bbb.json", /*legacy_flat_layout=*/true);

    const std::vector<account_index::ContestedKey> contested = account_index::contested_keys();
    ASSERT_EQ(contested.size(), 1u);
    EXPECT_EQ(contested[0].kind, "email");
    EXPECT_EQ(contested[0].key, "shared@example.com");
    ASSERT_EQ(contested[0].claimants.size(), 2u);
    EXPECT_EQ(contested[0].claimants[0], "accounts/P-T/aaa.json");
    EXPECT_EQ(contested[0].claimants[1], "accounts/P-T/bbb.json");
}

// --- Name-key erasure is per-record, not a scan -------------------------------------------------

TEST_F(AccountIndexTest, RetiringOneRecordsNameKeyLeavesEveryOtherRecordsNameAlone)
{
    // upsert erases the email's old name key by looking it up in the entry, not by walking every
    // indexed account name. This pins that the narrower erase still retires exactly the right key
    // and disturbs nothing else, at a size where a wrong key would show.
    for (int index = 0; index < 50; ++index) {
        const std::string email = "user" + std::to_string(index) + "@example.com";
        account_index::upsert(make_account(email, "name" + std::to_string(index), {}),
            "accounts/U-Z/" + email + "/account.json");
    }

    account_index::upsert(make_account("user7@example.com", "renamed", {}),
        "accounts/U-Z/user7@example.com/account.json");

    std::string path;
    EXPECT_FALSE(account_index::find_path_by_account_name("name7", &path, nullptr))
        << "the retired name must stop resolving";
    EXPECT_TRUE(account_index::find_path_by_account_name("renamed", &path, nullptr));

    for (int index = 0; index < 50; ++index) {
        if (index == 7)
            continue;
        EXPECT_TRUE(account_index::find_path_by_account_name("name" + std::to_string(index), &path,
            nullptr))
            << "record " << index << " lost its name key";
    }
}

TEST_F(AccountIndexTest, ARecordNeverRetiresANameKeyAnotherEmailOwns)
{
    // With a contested name the key can belong to somebody else by the time this record is written
    // again, so the erase confirms ownership before dropping anything.
    account_index::upsert(make_account("first@example.com", "shared", {}),
        "accounts/A-E/first@example.com/account.json");
    account_index::upsert(make_account("second@example.com", "shared", {}),
        "accounts/P-T/second@example.com/account.json");
    ASSERT_TRUE(account_index::is_account_name_ambiguous("shared"));

    // first bows out of the dispute, handing "shared" to second...
    account_index::upsert(make_account("first@example.com", "distinct", {}),
        "accounts/A-E/first@example.com/account.json");
    ASSERT_FALSE(account_index::is_account_name_ambiguous("shared"));

    std::string path;
    ASSERT_TRUE(account_index::find_path_by_account_name("shared", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/second@example.com/account.json");

    // ...and rewriting first again must not take second's key down with it.
    account_index::upsert(make_account("first@example.com", "distinct", {}),
        "accounts/A-E/first@example.com/account.json");
    ASSERT_TRUE(account_index::find_path_by_account_name("shared", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/second@example.com/account.json");
    EXPECT_TRUE(account_index::find_path_by_account_name("distinct", &path, nullptr));
}

// --- The candidate filter -----------------------------------------------------------------------

TEST_F(AccountIndexTest, TheRecordEnumeratorSkipsNonRegularJsonEntries)
{
    // read_account_file_from_bucket_entry requires S_ISREG. When the enumerator tested only the
    // ".json" suffix, a dangling symlink or a fifo was visited as a candidate, failed inside that
    // reader, and got quarantined -- counted against MAX_QUARANTINED_RECORDS_AT_BOOT, six of them
    // refusing a boot, over filesystem litter that account_storage_contains_unreadable_records does
    // not consider a record at all.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    std::string error_message;
    account::AccountData created;
    ASSERT_TRUE(account::create_account_for_email(root, "real@example.com", "ValidPass1", 1700000000,
        &created, &error_message))
        << error_message;

    const std::string dangling_path = root + "/accounts/A-E/dangling.json";
    const std::string fifo_path = root + "/accounts/A-E/pipe.json";
    ASSERT_EQ(symlink("no-such-target", dangling_path.c_str()), 0);
    ASSERT_EQ(mkfifo(fifo_path.c_str(), 0600), 0);

    std::vector<std::string> visited;
    ASSERT_TRUE(account::for_each_account_record_on_disk(
        root,
        [&visited](const account::AccountRecordOnDisk& record) {
            visited.push_back(record.record_path);
            // Whatever survives to be visited and fails must carry a reason: an empty one becomes a
            // log line and a wizard line that trail off into nothing.
            EXPECT_TRUE(record.parsed || !record.failure_reason.empty())
                << "empty quarantine reason for " << record.record_path;
        },
        &error_message))
        << error_message;

    ASSERT_EQ(visited.size(), 1u) << "litter must not be visited, or it counts as a bad record";
    EXPECT_NE(visited[0].find("real@example.com"), std::string::npos);

    // The assertion that actually matters, and the reason the enumerator and
    // read_account_file_from_bucket_entry have to agree on what a record is.
    // account_storage_contains_unreadable_records walks every bucket entry through that reader and
    // treats anything that fails WITHOUT the "Entry is not an account record." sentinel as a record
    // it could not read -- which refuses EVERY new account. A dangling symlink is exactly that case
    // (stat() follows the link and gets ENOENT), and once the enumerator stopped visiting it, it was
    // no longer quarantined either, so neither escape hatch fired: registration was refused
    // permanently, with nothing quarantined, nothing logged and `account index` showing 0
    // quarantined. Pinning the enumerator's visit count alone does not see any of that.
    account::AccountData newcomer;
    std::string creation_error;
    EXPECT_TRUE(account::create_account_for_email(root, "newcomer@example.com", "ValidPass1",
        1700000001, &newcomer, &creation_error))
        << "litter in a bucket must not refuse account creation: " << creation_error;

    // stat() cannot see through either of these, so the directory teardown skips them.
    unlink(dangling_path.c_str());
    unlink(fifo_path.c_str());
}

TEST_F(AccountIndexTest, ADanglingSymlinkInABucketDoesNotRefuseAccountCreation)
{
    // The same regression from the other side, with no index involved at all: this is the pure
    // scan path (index disabled), which still has to be correct on its own.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);
    const std::string dangling_path = root + "/accounts/A-E/broken-link.json";
    ASSERT_EQ(symlink("no-such-target", dangling_path.c_str()), 0);

    ASSERT_FALSE(account_index::is_enabled());
    account::AccountData created;
    std::string error_message;
    EXPECT_TRUE(account::create_account_for_email(root, "arrival@example.com", "ValidPass1",
        1700000002, &created, &error_message))
        << error_message;
    EXPECT_NE(error_message, "Existing account records could not be read safely.");

    unlink(dangling_path.c_str());
}

TEST_F(AccountIndexTest, AnUnstatableRecordIsStillReportedAsUnreadable)
{
    // The other half of the sentinel rule, and the easier one to lose: only ENOENT means "not a
    // record". A record we are not ALLOWED to stat is a real record we cannot read, and account
    // creation must keep refusing rather than writing over it.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    const std::string bucket = root + "/accounts/A-E";
    ASSERT_EQ(mkdir(bucket.c_str(), 0700), 0);
    const std::string hidden_record = bucket + "/hidden";
    ASSERT_EQ(mkdir(hidden_record.c_str(), 0700), 0);
    // Search permission removed from the bucket: readdir still lists the entry, stat on it fails
    // with EACCES.
    ASSERT_EQ(chmod(bucket.c_str(), 0600), 0);

    struct stat probe { };
    const bool stat_is_blocked = stat(hidden_record.c_str(), &probe) != 0 && errno == EACCES;
    if (!stat_is_blocked) {
        // Running as root, where permissions do not bite. Nothing to assert.
        chmod(bucket.c_str(), 0700);
        GTEST_SKIP() << "stat() is not blocked for this user";
    }

    account::AccountData created;
    std::string error_message;
    EXPECT_FALSE(account::create_account_for_email(root, "arrival@example.com", "ValidPass1",
        1700000003, &created, &error_message))
        << "an unstatable entry is a record we cannot read, not litter";
    EXPECT_EQ(error_message, "Existing account records could not be read safely.");

    chmod(bucket.c_str(), 0700);
}

// --- The owner fast path does not re-read the record ---------------------------------------------

TEST_F(AccountIndexTest, TheOwnerFastPathAnswersWithoutReadingTheRecordFile)
{
    // The fast path used to read and JSON-parse the whole account record just to pull account_name
    // back out of it, on every save of every linked character. Removing the file is what proves the
    // read is gone rather than merely fast.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();
    account_index::set_root_directory(root);

    std::string error_message;
    account::AccountData created;
    ASSERT_TRUE(account::create_account_for_email(root, "owner@example.com", "ValidPass1",
        1700000000, &created, &error_message))
        << error_message;
    ASSERT_TRUE(account::add_character_to_account(&created, "Frodo", &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created, &error_message)) << error_message;

    std::string scanned_owner;
    {
        ScopedIndexEnabled disabled(false);
        ASSERT_TRUE(account::find_linked_character_owner_account_uncached(root, "Frodo",
            &scanned_owner, &error_message))
            << error_message;
    }
    EXPECT_EQ(scanned_owner, created.account_name);

    std::string record_path;
    ASSERT_TRUE(account_index::find_path_by_email("owner@example.com", &record_path, nullptr));
    ASSERT_EQ(std::remove(record_path.c_str()), 0);

    std::string indexed_owner;
    {
        ScopedIndexEnabled enabled(true);
        ASSERT_TRUE(account::find_linked_character_owner_account_uncached(root, "Frodo",
            &indexed_owner, &error_message))
            << error_message;
    }
    EXPECT_EQ(indexed_owner, scanned_owner)
        << "the fast path must name the same account the scan named";

    // And the scan really cannot answer any more, which is what makes the assertion above mean
    // something: it did not accidentally fall through to the scan.
    std::string gone_owner;
    {
        ScopedIndexEnabled disabled(false);
        EXPECT_TRUE(account::find_linked_character_owner_account_uncached(root, "Frodo",
            &gone_owner, &error_message));
        EXPECT_EQ(gone_owner, "") << "the scan cannot see a record whose file is gone";
    }

    account_index::set_root_directory(".");
}

// --- Wave 3, item 1: the two walkers must agree about what is even a record ----------------------
//
// for_each_account_record_on_disk decides what the index sees;
// account_storage_contains_unreadable_records decides whether account creation is allowed at all.
// Every disagreement between them produced the same outcome: a record the guard called unreadable
// that nothing quarantined, so neither escape hatch fired and EVERY registration on the server was
// refused, permanently, with nothing logged and `account index` showing 0 quarantined.
//
// These tests deliberately do NOT assert visit counts alone. Pinning the enumerator's count is what
// let the last regression through: it never touched the sibling walker named in its own rationale.

// Replays the boot sweep (db.cpp) over a tree: parsed records are indexed, unparsed ones are
// quarantined under the same key both walkers derive. Without this the tests would be asserting
// against an index nobody built, which is not the state the server runs in.
void build_index_like_boot(const std::string& root)
{
    account_index::clear();
    account_index::set_root_directory(root);
    ASSERT_TRUE(account::for_each_account_record_on_disk(root, [](const account::AccountRecordOnDisk& record) {
        if (!record.parsed) {
            account_index::quarantine(account::account_index_quarantine_key(record), record.record_path,
                record.failure_reason);
            return;
        }
        account_index::upsert(record.account, record.record_path, !record.directory_layout);
    }));
}

TEST_F(AccountIndexTest, AnAccountDirectoryWeCannotSearchIsQuarantinedRatherThanVanishing)
{
    // The reachable one: chmod 0600 on an account directory (a bad umask on a manual SFTP deploy,
    // a partial restore) makes stat(<dir>/account.json) fail with EACCES. The enumerator used to
    // `continue` on that, so the account silently left the index -- its owner told "No account
    // exists for that email address.", every save of an in-game character refused -- while the
    // creation guard called the very same entry a record it could not read and refused EVERY
    // registration on the box. One chmod, no evidence anywhere.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData victim;
    ASSERT_TRUE(account::create_account_for_email(root, "victim@example.com", "ValidPass1", 1700000000,
        &victim, &error_message))
        << error_message;
    ASSERT_TRUE(account::admin_link_character(root, victim.account_name, "Frodo", 1700000001, nullptr,
        &error_message))
        << error_message;
    account::AccountData bystander;
    ASSERT_TRUE(account::create_account_for_email(root, "bystander@example.com", "ValidPass1", 1700000002,
        &bystander, &error_message))
        << error_message;

    const std::string victim_directory = root + "/accounts/U-Z/victim@example.com";
    ASSERT_EQ(chmod(victim_directory.c_str(), 0600), 0);

    struct stat probe { };
    const bool stat_is_blocked = stat((victim_directory + "/account.json").c_str(), &probe) != 0 && errno == EACCES;
    if (!stat_is_blocked) {
        chmod(victim_directory.c_str(), 0700);
        GTEST_SKIP() << "stat() is not blocked for this user (running as root)";
    }

    bool visited_the_victim = false;
    std::string victim_quarantine_key;
    ASSERT_TRUE(account::for_each_account_record_on_disk(
        root,
        [&](const account::AccountRecordOnDisk& record) {
            if (record.record_path.find("victim@example.com") == std::string::npos)
                return;
            visited_the_victim = true;
            EXPECT_FALSE(record.parsed);
            EXPECT_FALSE(record.failure_reason.empty())
                << "a quarantined record with no reason is evidence with the evidence missing";
            victim_quarantine_key = account::account_index_quarantine_key(record);
        },
        &error_message))
        << error_message;

    // Reverted, the enumerator skips this entry entirely and this is false -- which is exactly the
    // state in which nothing is quarantined, nothing is logged and verify reports agreement.
    ASSERT_TRUE(visited_the_victim) << "a record we cannot stat must be VISITED, not skipped";
    // Keyed by the account DIRECTORY's name, which is the email: that is what reserves the address.
    EXPECT_EQ(victim_quarantine_key, "victim@example.com");

    build_index_like_boot(root);
    ScopedIndexEnabled enabled(true);

    EXPECT_TRUE(account_index::is_quarantined("victim@example.com"));
    EXPECT_EQ(account_index::quarantined_count(), 1u);

    // The consequence that matters, and the one a visit-count assertion cannot see: with the record
    // quarantined its owner's address stays reserved, so nobody registers over a real player's
    // record. Reverted, the record is not in the index at all, `is_quarantined` is false, and this
    // creation SUCCEEDS -- writing a brand new account over the victim's directory.
    account::AccountData intruder;
    std::string creation_error;
    EXPECT_FALSE(account::create_account_for_email(root, "victim@example.com", "ValidPass1", 1700000003,
        &intruder, &creation_error));
    EXPECT_EQ(creation_error, "That email address cannot be used right now.");

    // And one bad record does not refuse everybody else.
    account::AccountData newcomer;
    EXPECT_TRUE(account::create_account_for_email(root, "newcomer@example.com", "ValidPass1", 1700000004,
        &newcomer, &creation_error))
        << creation_error;

    account_index::set_root_directory(".");
    chmod(victim_directory.c_str(), 0700);
}

TEST_F(AccountIndexTest, AnAppleDoubleDotfileIsLitterToBothWalkers)
{
    // The enumerator has always skipped every dotfile; the guard skipped only "." and "..". A macOS
    // AppleDouble "._account.json" left by an rsync was therefore litter to one walker and a fatal
    // unreadable record to the other, so it refused every registration while nothing quarantined it.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData resident;
    ASSERT_TRUE(account::create_account_for_email(root, "resident@example.com", "ValidPass1", 1700000000,
        &resident, &error_message))
        << error_message;

    const std::string apple_double = root + "/accounts/P-T/._account.json";
    FILE* litter = std::fopen(apple_double.c_str(), "w");
    ASSERT_NE(litter, nullptr);
    std::fputs("\x00\x05\x16\x07 not json at all", litter);
    std::fclose(litter);

    std::size_t visited = 0;
    ASSERT_TRUE(account::for_each_account_record_on_disk(
        root,
        [&visited](const account::AccountRecordOnDisk&) { ++visited; }, &error_message))
        << error_message;
    EXPECT_EQ(visited, 1u) << "a dotfile is litter, so it must not be quarantined either";

    // The half the old test never touched. Index DISABLED on purpose: this is the creation guard's
    // own walk, and it has to be right on its own. Reverted, this fails with
    // "Existing account records could not be read safely." -- for every registration, forever.
    ASSERT_FALSE(account_index::is_enabled());
    account::AccountData newcomer;
    std::string creation_error;
    EXPECT_TRUE(account::create_account_for_email(root, "newcomer@example.com", "ValidPass1", 1700000001,
        &newcomer, &creation_error))
        << "an AppleDouble must not refuse account creation: " << creation_error;
}

TEST_F(AccountIndexTest, AnUnopenableBucketIsVisitedAsOneUnreadableRecord)
{
    // opendir(bucket) == nullptr was a silent `continue` in the enumerator and a hard failure in the
    // guard: every account in that bucket left the index at once, AND every registration on the
    // server was refused, with nothing to show for either.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData resident;
    ASSERT_TRUE(account::create_account_for_email(root, "resident@example.com", "ValidPass1", 1700000000,
        &resident, &error_message))
        << error_message;

    const std::string bucket = root + "/accounts/P-T";
    ASSERT_EQ(chmod(bucket.c_str(), 0000), 0);
    DIR* probe = opendir(bucket.c_str());
    if (probe != nullptr) {
        closedir(probe);
        chmod(bucket.c_str(), 0700);
        GTEST_SKIP() << "opendir() is not blocked for this user (running as root)";
    }

    std::vector<account::AccountRecordOnDisk> visited;
    ASSERT_TRUE(account::for_each_account_record_on_disk(
        root,
        [&visited](const account::AccountRecordOnDisk& record) { visited.push_back(record); },
        &error_message))
        << error_message;

    // Reverted, visited is empty: the bucket and everything in it disappears without a trace.
    ASSERT_EQ(visited.size(), 1u);
    EXPECT_FALSE(visited[0].parsed);
    EXPECT_EQ(visited[0].record_path, bucket);
    EXPECT_NE(visited[0].failure_reason.find("Failed to open account bucket directory"), std::string::npos);
    // Path-shaped key, the same one the guard's escape hatch derives for a bucket.
    EXPECT_EQ(account::account_index_quarantine_key(visited[0]), bucket);

    build_index_like_boot(root);
    EXPECT_EQ(account_index::quarantined_count(), 1u);
    EXPECT_TRUE(account_index::is_quarantined_record_key(bucket));

    account_index::set_root_directory(".");
    chmod(bucket.c_str(), 0700);
}

TEST_F(AccountIndexTest, AFlatCandidateWeCannotStatIsVisitedRatherThanSkipped)
{
    // The flat branch's own errno axis. The previous wave fixed ENOENT (a dangling symlink is
    // litter); ELOOP, EIO and ENAMETOOLONG still fell through the same `continue`. A symlink loop
    // is the reproducible one.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData resident;
    ASSERT_TRUE(account::create_account_for_email(root, "resident@example.com", "ValidPass1", 1700000000,
        &resident, &error_message))
        << error_message;

    const std::string loop_a = root + "/accounts/P-T/loop.json";
    const std::string loop_b = root + "/accounts/P-T/loop-partner.json";
    ASSERT_EQ(symlink("loop-partner.json", loop_a.c_str()), 0);
    ASSERT_EQ(symlink("loop.json", loop_b.c_str()), 0);

    struct stat probe { };
    ASSERT_NE(stat(loop_a.c_str(), &probe), 0);
    ASSERT_EQ(errno, ELOOP);

    std::vector<std::string> unreadable;
    ASSERT_TRUE(account::for_each_account_record_on_disk(
        root,
        [&unreadable](const account::AccountRecordOnDisk& record) {
            if (!record.parsed)
                unreadable.push_back(record.record_path);
        },
        &error_message))
        << error_message;

    // Reverted, both loop entries are skipped and this is empty -- while the guard, which stats
    // them through the shared reader, calls each one a record it cannot read.
    EXPECT_EQ(unreadable.size(), 2u);

    // And with the index off the guard must still refuse over them, because an
    // entry we cannot stat is a record we cannot read, not litter.
    ASSERT_FALSE(account_index::is_enabled());
    account::AccountData newcomer;
    std::string creation_error;
    EXPECT_FALSE(account::create_account_for_email(root, "newcomer@example.com", "ValidPass1",
        1700000001, &newcomer, &creation_error));
    EXPECT_EQ(creation_error, "Existing account records could not be read safely.");

    unlink(loop_a.c_str());
    unlink(loop_b.c_str());
}

// --- Wave 3, item 3: verify has to look at the keys that actually drift --------------------------

// --- Wave 3, item 5: creation must stop walking the whole tree ------------------------------------

TEST_F(AccountIndexTest, CreationDoesNotWalkTheAccountTreeWhenTheIndexIsAuthoritative)
{
    // account_storage_contains_unreadable_records opened and JSON-parsed EVERY record on disk on
    // every registration attempt, from an unauthenticated path, on the single-threaded pulse loop --
    // O(N) per attempt, N^2 across N creations. The index made find_account_by_email_internal fast
    // and left this walk in place, so the quadratic the project was justified by was never removed.
    //
    // Proved by planting a record the walk would refuse over and the index has never heard of: if
    // the walk still runs, creation fails. Timing would not prove anything; this does.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData resident;
    ASSERT_TRUE(account::create_account_for_email(root, "resident@example.com", "ValidPass1", 1700000000,
        &resident, &error_message))
        << error_message;

    build_index_like_boot(root);
    ASSERT_EQ(account_index::quarantined_count(), 0u);

    // Planted AFTER the index was built, so the index does not know about it and the escape hatch
    // cannot be what saves this.
    const std::string poisoned = root + "/accounts/P-T/poisoned.json";
    FILE* file = std::fopen(poisoned.c_str(), "w");
    ASSERT_NE(file, nullptr);
    std::fputs("this is not JSON", file);
    std::fclose(file);

    {
        ScopedIndexEnabled enabled(true);
        account::AccountData created;
        std::string creation_error;
        EXPECT_TRUE(account::create_account_for_email(root, "fast@example.com", "ValidPass1",
            1700000001, &created, &creation_error))
            << "the scan still ran: " << creation_error;
    }

    // The scan path must stay correct on its own: with the index off the same tree still
    // refuses. This is what makes the assertion above mean "the walk did not happen" rather than
    // "the walk stopped noticing".
    ASSERT_FALSE(account_index::is_enabled());
    account::AccountData scanned_newcomer;
    std::string scan_error;
    EXPECT_FALSE(account::create_account_for_email(root, "slow@example.com", "ValidPass1", 1700000002,
        &scanned_newcomer, &scan_error));
    EXPECT_EQ(scan_error, "Existing account records could not be read safely.");

    account_index::set_root_directory(".");
}

TEST_F(AccountIndexTest, CreationStillRefusesAnAddressWhoseRecordBootCouldNotRead)
{
    // The protection the walk was written for, kept without the walk: a quarantined record's
    // address stays reserved. This is what makes skipping the scan safe rather than merely faster,
    // so it is asserted here next to the skip rather than left to a distant test.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData victim;
    ASSERT_TRUE(account::create_account_for_email(root, "victim@example.com", "ValidPass1", 1700000000,
        &victim, &error_message))
        << error_message;

    const std::string record_path = root + "/accounts/U-Z/victim@example.com/account.json";
    FILE* file = std::fopen(record_path.c_str(), "w");
    ASSERT_NE(file, nullptr);
    std::fputs("{ truncated", file);
    std::fclose(file);

    build_index_like_boot(root);
    ScopedIndexEnabled enabled(true);
    ASSERT_EQ(account_index::quarantined_count(), 1u);

    account::AccountData intruder;
    std::string creation_error;
    EXPECT_FALSE(account::create_account_for_email(root, "victim@example.com", "ValidPass1", 1700000001,
        &intruder, &creation_error));
    EXPECT_EQ(creation_error, "That email address cannot be used right now.");

    account_index::set_root_directory(".");
}

} // namespace

// --- Post-boot unreadable marking ---------------------------------------------------------------
//
// Boot reports every record it cannot read. Nothing walks the tree again afterwards, so a record
// that goes bad LATER used to be reported nowhere: write_account_file refused it, and its only
// live caller (clear_account_login_failures, on every successful login) passes a null error message
// and ignores the return. These pin the fix, and pin that it stays report-only.

TEST_F(AccountIndexTest, NotingARecordUnreadableAtRuntimeChangesNoLookup)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    ASSERT_TRUE(account_index::note_unreadable_at_runtime("player@example.com", "boom"));

    // The whole point: this marks and reports, it does not withdraw. A player whose record hiccups
    // must keep resolving, or save_char writes nothing for them until the next reboot.
    std::string path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &path, nullptr));
    EXPECT_EQ(path, "accounts/P-T/player@example.com/account.json");
    EXPECT_TRUE(account_index::find_path_by_account_name("player", &path, nullptr));
    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("Frodo", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");

    EXPECT_FALSE(account_index::is_quarantined("player@example.com"));
    EXPECT_EQ(account_index::quarantined_count(), 0u);
}

TEST_F(AccountIndexTest, ARecordUnreadableAtRuntimeIsListedWithItsReason)
{
    const account::AccountData account = make_account("player@example.com", "player", {});
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");
    ASSERT_TRUE(account_index::note_unreadable_at_runtime("player@example.com", "Expected string value."));

    const std::vector<account_index::Entry> listed = account_index::unreadable_at_runtime_entries();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].normalized_email, "player@example.com");
    EXPECT_EQ(listed[0].record_path, "accounts/P-T/player@example.com/account.json");
    EXPECT_EQ(listed[0].unreadable_at_runtime_reason, "Expected string value.");
    EXPECT_EQ(account_index::unreadable_at_runtime_count(), 1u);
}

TEST_F(AccountIndexTest, MarkingTheSameRecordTwiceReportsNewsOnlyOnce)
{
    const account::AccountData account = make_account("player@example.com", "player", {});
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");

    // write_account_file runs on EVERY successful login, so a record that stays broken reaches the
    // call site again and again. Only the first arrival may log, or one bad file fills the syslog.
    EXPECT_TRUE(account_index::note_unreadable_at_runtime("player@example.com", "first"));
    EXPECT_FALSE(account_index::note_unreadable_at_runtime("player@example.com", "second"));
    EXPECT_EQ(account_index::unreadable_at_runtime_count(), 1u);
    EXPECT_EQ(account_index::unreadable_at_runtime_entries()[0].unreadable_at_runtime_reason, "first");
}

TEST_F(AccountIndexTest, AnUnknownRecordKeyIsNotInventedAsAnEntry)
{
    // An entry carries a record path that find_path_by_email would hand out, so a key with no entry
    // must not gain one here.
    EXPECT_FALSE(account_index::note_unreadable_at_runtime("nobody@example.com", "boom"));
    EXPECT_EQ(account_index::unreadable_at_runtime_count(), 0u);
    EXPECT_FALSE(account_index::find_path_by_email("nobody@example.com", nullptr, nullptr));
}

TEST_F(AccountIndexTest, AQuarantinedRecordIsNotAlsoReportedAsUnreadableAtRuntime)
{
    account_index::quarantine("player@example.com", "accounts/P-T/player@example.com/account.json", "corrupt");

    // It was already refused at boot and is already listed under its own heading; counting it twice
    // would misreport one bad file as two.
    EXPECT_FALSE(account_index::note_unreadable_at_runtime("player@example.com", "boom"));
    EXPECT_EQ(account_index::unreadable_at_runtime_count(), 0u);
    EXPECT_EQ(account_index::quarantined_count(), 1u);
}

TEST_F(AccountIndexTest, ARepairedRecordClearsTheRuntimeMarkWithoutAReboot)
{
    const account::AccountData account = make_account("player@example.com", "player", { "Frodo" });
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");
    ASSERT_TRUE(account_index::note_unreadable_at_runtime("player@example.com", "boom"));
    ASSERT_EQ(account_index::unreadable_at_runtime_count(), 1u);

    // A successful write re-upserts the entry. There is no un-quarantine, but this mark must not
    // need one: once the file reads again the report has to stop, or it lies until the next boot.
    account_index::upsert(account, "accounts/P-T/player@example.com/account.json");
    EXPECT_EQ(account_index::unreadable_at_runtime_count(), 0u);
    EXPECT_TRUE(account_index::unreadable_at_runtime_entries().empty());
}

TEST_F(AccountIndexTest, ARecordThatGoesBadAfterBootIsMarkedWhenTheWriteChokepointHitsIt)
{
    // The integration half of the fix, and the actual gap: boot reports what it cannot read, then
    // nothing walks the tree again. write_account_file runs on every successful login and refuses
    // over a record it cannot parse, but its caller (clear_account_login_failures) passes a null
    // error message and ignores the return -- so the failure reached no log, no mudlog and no
    // wizard command. Reverted, this test fails at the unreadable_at_runtime_count assertion.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    std::string error_message;
    account::AccountData resident;
    ASSERT_TRUE(account::create_account_for_email(root, "resident@example.com", "ValidPass1",
        1700000000, &resident, &error_message))
        << error_message;

    build_index_like_boot(root);
    ASSERT_EQ(account_index::quarantined_count(), 0u);
    ASSERT_EQ(account_index::unreadable_at_runtime_count(), 0u);

    // Healthy at boot, corrupt afterwards -- the case quarantine cannot see.
    const std::string record_path = root + "/accounts/"
        + account::account_bucket_for_name("resident@example.com") + "/resident@example.com/account.json";
    {
        FILE* file = std::fopen(record_path.c_str(), "w");
        ASSERT_NE(file, nullptr) << record_path;
        std::fputs("{ not json any more", file);
        std::fclose(file);
    }

    {
        ScopedIndexEnabled enabled(true);
        std::string write_error;
        EXPECT_FALSE(account::write_account_file(root, resident, &write_error));
        EXPECT_EQ(write_error, "Existing account file could not be read safely.");

        ASSERT_EQ(account_index::unreadable_at_runtime_count(), 1u);
        const std::vector<account_index::Entry> listed = account_index::unreadable_at_runtime_entries();
        ASSERT_EQ(listed.size(), 1u);
        EXPECT_EQ(listed[0].normalized_email, "resident@example.com");
        EXPECT_FALSE(listed[0].unreadable_at_runtime_reason.empty())
            << "a report with no reason is the one piece of evidence, missing";

        // Report-only: the account still resolves, so the owner keeps saving.
        EXPECT_FALSE(account_index::is_quarantined("resident@example.com"));
        std::string path;
        EXPECT_TRUE(account_index::find_path_by_email("resident@example.com", &path, nullptr));
    }

    account_index::set_root_directory(".");
}

TEST(AccountIndexUpsert, KeepsTheIncumbentRecordsCharacterClaimsWhenASecondRecordSharesItsEmail)
{
    // Two files at one address with disagreeing account names -- reachable only by an operator (a
    // half-finished migration, a restored backup), never by registration. Before the index, the
    // email lookup refused but the separate character scan still resolved every character on BOTH
    // records. The second upsert now withdraws the first record's claims, so its characters resolve
    // as unlinked instead of refusing, and save_char declines the legacy fallback for them.
    account_index::clear();

    account_index::upsert(make_account("player@example.com", "alpha-admin", { "Aragorn" }),
        "accounts/A-E/alpha-admin.json", true);
    account_index::upsert(make_account("player@example.com", "beta-admin", { "Boromir" }),
        "accounts/P-T/player@example.com/account.json", false);

    ASSERT_TRUE(account_index::is_email_ambiguous("player@example.com"))
        << "the two records must be recorded as disputing the address";

    // Before the index, find_character_owner_account scanned files and resolved every character on
    // BOTH records; only the email lookup refused. Both must still resolve to their owner here --
    // the disputed address is refused downstream, where find_path_by_email answers.
    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("Aragorn", &owner_email, nullptr))
        << "the incumbent's character must survive a second record claiming its address";
    EXPECT_EQ(owner_email, "player@example.com");

    owner_email.clear();
    EXPECT_TRUE(account_index::find_owner_email_by_character("Boromir", &owner_email, nullptr));
    EXPECT_EQ(owner_email, "player@example.com");

    account_index::clear();
}

TEST(AccountIndexQuarantine, ReservesTheAddressOfADirectoryRecordWhoseDirectoryNameIsNotNormalized)
{
    // A directory-layout record is keyed by its directory name. The server always writes that name
    // normalized, but a hand-made or restored directory need not be, and on a case-sensitive
    // filesystem the raw name is then a key is_quarantined() -- which normalizes its argument --
    // can never produce. The address would read as free and registration would proceed over it.
    account_index::clear();

    account::AccountRecordOnDisk record;
    record.directory_entry_name = "Player@Example.com";
    record.record_path = "accounts/P-T/Player@Example.com/account.json";
    record.directory_layout = true;
    record.parsed = false;

    account_index::quarantine(account::account_index_quarantine_key(record), record.record_path,
        "unreadable record");

    EXPECT_TRUE(account_index::is_quarantined("player@example.com"))
        << "a quarantined record must keep its address reserved however its directory is cased";

    account_index::clear();
}

TEST(AccountIndexQuarantine, DoesNotEvictAHealthyRecordAlreadyIndexedAtThatAddress)
{
    // A misfiled second record -- an operator's `cp -r accounts/P-T/player@example.com{,.bak}` --
    // declares an address a healthy record already holds, and db.cpp reserves the DECLARED address
    // so registration cannot write a fresh account over an unreachable record. Reserving it must
    // not take the address away from the readable record at the canonical path: the walk visits the
    // two in readdir order, and the healthy one is what the running game resolves to either way.
    account_index::clear();

    account_index::upsert(make_account("player@example.com", "alpha-admin", { "Aragorn" }),
        "accounts/P-T/player@example.com/account.json", false);

    account_index::quarantine("player@example.com",
        "accounts/P-T/player@example.com.bak/account.json", "misfiled record");

    EXPECT_FALSE(account_index::is_quarantined("player@example.com"))
        << "the address is held by a record the server can read";

    std::string record_path;
    std::string error_message;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &record_path, &error_message))
        << error_message;
    EXPECT_EQ(record_path, "accounts/P-T/player@example.com/account.json");

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("Aragorn", &owner_email, nullptr))
        << "the healthy record's characters must not lose their owner";
    EXPECT_EQ(owner_email, "player@example.com");

    std::string account_email;
    EXPECT_TRUE(account_index::find_email_by_account_name("alpha-admin", &account_email, nullptr))
        << "the healthy record's account name must stay indexed";
    EXPECT_EQ(account_email, "player@example.com");

    account_index::clear();
}

TEST(AccountIndexQuarantine, KeepsAStrayRecordVisibleWhenItsAddressBelongsToAHealthyOne)
{
    // A record in the WRONG BUCKET under the right directory name declares -- and is keyed by -- the
    // same address as the healthy record. Refusing to evict the healthy entry is right, but silently
    // dropping the stray one leaves it unindexed, uncounted, and absent from `account index`: the
    // operator sees one boot log line and nothing else, for a file the game cannot reach.
    account_index::clear();

    account_index::upsert(make_account("player@example.com", "alpha-admin", { "Aragorn" }),
        "accounts/P-T/player@example.com/account.json", false);

    account_index::quarantine("player@example.com",
        "accounts/A-E/player@example.com/account.json", "filed in the wrong bucket");

    std::string record_path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &record_path, nullptr));
    EXPECT_EQ(record_path, "accounts/P-T/player@example.com/account.json")
        << "the readable record still holds its address";

    EXPECT_TRUE(account_index::is_quarantined_record_key("accounts/A-E/player@example.com/account.json"))
        << "and the stray one is still reported, filed under its own path";
    EXPECT_EQ(account_index::quarantined_count(), 1u);

    account_index::clear();
}

TEST(AccountIndexUpsert, WithdrawsAStrandedAccountNameOnceTheDisputeSettles)
{
    // Keeping both records' claims while an address is disputed must not strand a key forever.
    // erase_owned_keys can only withdraw the name held in the CURRENT entry, so the incumbent's name
    // survives every later rewrite -- and once the dispute settles it resolves again, handing out a
    // record that no longer carries that name.
    account_index::clear();

    account_index::upsert(make_account("player@example.com", "alpha-admin", {}),
        "accounts/A-E/alpha-admin.json", true);
    account_index::upsert(make_account("player@example.com", "beta-admin", {}),
        "accounts/P-T/player@example.com/account.json", false);
    ASSERT_TRUE(account_index::is_email_ambiguous("player@example.com"));

    // The operator repairs the flat record so both now declare the same account name.
    account_index::upsert(make_account("player@example.com", "beta-admin", {}),
        "accounts/A-E/alpha-admin.json", true);
    ASSERT_FALSE(account_index::is_email_ambiguous("player@example.com"))
        << "agreeing records must settle the address";

    // The next ordinary rewrite of the surviving record must clear out what the dispute left behind.
    account_index::upsert(make_account("player@example.com", "beta-admin", {}),
        "accounts/P-T/player@example.com/account.json", false);

    std::string resolved_path;
    EXPECT_FALSE(account_index::find_path_by_account_name("alpha-admin", &resolved_path, nullptr))
        << "no record declares 'alpha-admin' any more, so it must not resolve";

    account_index::clear();
}

TEST_F(AccountIndexTest, ADisputedEmailAddressIsTakenAndRefusesAccountCreation)
{
    // find_path_by_email refuses a CONTESTED address with its own message, and the creation guard
    // read that refusal as "the address is free" -- the opposite of what its own comment claims
    // ("an address the index already holds is TAKEN, whether or not its record still reads"). A
    // disputed address is held by two records, not none. find_account_by_email_internal below
    // refuses for the same reason and its caller tests only whether one was FOUND, so creation went
    // on to write a third record at an address two records were already fighting over.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    // Two records, two paths, two account names, one address: the shape upsert files as contested.
    account_index::upsert(make_account("bob@example.com", "bob", {}),
        root + "/accounts/A-E/bob@example.com/account.json");
    account_index::upsert(make_account("bob@example.com", "bob-restored", {}),
        root + "/accounts/A-E/bob-restored.json");
    ASSERT_FALSE(account_index::find_path_by_email("bob@example.com", nullptr, nullptr))
        << "the fixture is only meaningful if the address really is disputed";

    account::AccountData created;
    std::string error_message;
    const bool creation_succeeded = account::create_account_for_email(root, "bob@example.com",
        "ValidPass1", 1700000001, &created, &error_message);

    account_index::set_enabled(false);

    EXPECT_FALSE(creation_succeeded) << "a disputed address must not accept a third record";
    EXPECT_EQ(error_message, "An account already exists for that email address.");
}

TEST_F(AccountIndexTest, ARepairedRecordStopsBeingReportedUnreadableOnceItReadsAgain)
{
    // The unreadable-since-boot mark is set at the WRITE chokepoint, and cleared only because a
    // successful write re-upserts the whole Entry. Four comments claim that chokepoint "runs on
    // every successful login" -- it does not: interpre.cpp only calls clear_account_login_failures
    // when there is a failure notice to show, and that function early-returns without writing when
    // the counters are already zero. So after an operator repairs a record by hand, `account index`
    // went on reporting UNREADABLE SINCE BOOT for a record that reads perfectly, until something
    // happened to write it or the server rebooted.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "bob", "bob@example.com", "ValidPass1", 1700000000, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account_index::note_unreadable_at_runtime("bob@example.com", "Account record could not be read."));
    ASSERT_EQ(account_index::unreadable_at_runtime_count(), 1u);

    // Reading the record is the proof that it is readable again, whatever the rest of the
    // authentication then decides.
    account::authenticate_account(root, "bob", "ValidPass1", nullptr, nullptr);

    const std::size_t still_reported = account_index::unreadable_at_runtime_count();
    account_index::set_enabled(false);

    EXPECT_EQ(still_reported, 0u)
        << "a record that has just been read successfully must stop being listed as unreadable";
}

TEST_F(AccountIndexTest, RegisteringDoesNotDeleteALegacyFlatRecordItCannotRead)
{
    // An unparseable record is quarantined under its own PATH (it has disclosed no email to key it
    // by), so is_quarantined("bob@example.com") misses, the address reads as free, and registration
    // proceeds. write_account_file then composes legacy_flat_path from the ACCOUNT NAME --
    // "bob@example.com" derives the name "bob", which is that very file -- and std::remove()s it.
    // A real player's account record, deleted by someone else signing up.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    const std::string flat_path = root + "/accounts/A-E/bob.json";
    const std::string original_bytes = "{ \"account_name\": \"bob\", this record no longer parses";
    write_whole_file(flat_path, original_bytes);

    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    account::AccountData created;
    std::string error_message;
    const bool creation_succeeded = account::create_account_for_email(root, "bob@example.com",
        "ValidPass1", 1700000001, &created, &error_message);

    account_index::set_enabled(false);

    EXPECT_FALSE(creation_succeeded) << "registration must not proceed over a record the server cannot read";
    EXPECT_EQ(read_whole_file(flat_path), original_bytes)
        << "a record the server cannot read must never be deleted -- it is the only copy";
}

TEST_F(AccountIndexTest, RegisteringDoesNotOverwriteARecordThatAppearedAfterBoot)
{
    // The index is built once at boot and nothing walks accounts/ again, so a record restored from
    // backup while the game is up is invisible to it. find_account_by_email_internal's index fast
    // path hard-returns on a miss with no fallback, every creation guard is index-keyed and misses
    // too, and the derived account name matches the restored record's own -- so write_account_file's
    // occupancy check (name equality) does not fire and the rename() lands on top of it.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);

    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "erik", "erik@example.com", "ValidPass1", 1700000000, nullptr, &error_message)) << error_message;

    const std::string record_path = root + "/accounts/A-E/erik@example.com/account.json";
    const std::string original_bytes = read_whole_file(record_path);
    ASSERT_FALSE(original_bytes.empty());

    // Exactly the post-restore state: the record is on disk, the index has never seen it.
    account_index::clear();
    account_index::set_root_directory(root);
    account_index::set_enabled(true);

    account::AccountData created;
    const bool creation_succeeded = account::create_account_for_email(root, "erik@example.com",
        "DifferentPw9", 1700000002, &created, &error_message);

    account_index::set_enabled(false);

    EXPECT_FALSE(creation_succeeded) << "the address is taken by a record that is right there on disk";
    EXPECT_EQ(read_whole_file(record_path), original_bytes)
        << "the restored record must survive -- its password hash and character list are in it";
}

TEST_F(AccountIndexTest, AnUnreadableBucketRefusesOnlyTheAddressesThatWouldLiveInIt)
{
    // The escape hatch in account_storage_contains_unreadable_records is &&-gated on the index being
    // authoritative -- but the one condition that creates its quarantine key, an unreadable bucket,
    // is also what makes build_account_native_player_index clear() the index and disable it. So the
    // hatch is dead exactly when it is needed, email_bucket_is_quarantined is skipped for the same
    // reason, and the walk falls through to refusing EVERY account creation on the server with a raw
    // errno string until an operator notices the directory mode.
    //
    // The design is per-address and correct: quarantine reserves the unreadable bucket, the
    // per-address guard refuses addresses in it, and everything else carries on. Only the wiring
    // was broken.
    IndexTemporaryDirectory root_directory;
    ASSERT_FALSE(root_directory.path().empty());
    const std::string root = root_directory.path();

    ASSERT_EQ(mkdir((root + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((root + "/accounts/P-T").c_str(), 0700), 0);
    ASSERT_EQ(chmod((root + "/accounts/P-T").c_str(), 0000), 0);

    account_index::clear();
    account_index::set_root_directory(root);
    account_index::set_enabled(true);
    account_index::quarantine(root + "/accounts/P-T", root + "/accounts/P-T",
        "Failed to open account bucket directory");
    // What the boot walk does once a bucket could not be read: stop trusting the index for lookups.
    account_index::set_enabled(false);

    account::AccountData created;
    std::string elsewhere_error;
    const bool elsewhere = account::create_account_for_email(root, "bob@example.com",
        "ValidPass1", 1700000001, &created, &elsewhere_error);

    std::string inside_error;
    const bool inside_the_bucket = account::create_account_for_email(root, "peter@example.com",
        "ValidPass1", 1700000002, &created, &inside_error);

    chmod((root + "/accounts/P-T").c_str(), 0700);
    account_index::clear();

    EXPECT_TRUE(elsewhere)
        << "one unreadable bucket must not refuse registration for the whole server: " << elsewhere_error;
    EXPECT_FALSE(inside_the_bucket)
        << "but an address that would live in that bucket cannot be proven free, so it must be refused";
}
