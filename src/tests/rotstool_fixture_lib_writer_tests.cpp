#include "fixture_lib_writer.h"

#include "account_management.h"
#include "creation_points.h"
#include "spells.h"
#include "structs.h"
#include "test_random_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// The email of the account most tests write; its directory is in the U-Z bucket.
constexpr const char* writer_email = "writer@example.com";

// The email of the account a test's second run writes; its directory is in the P-T bucket.
constexpr const char* second_email = "second@example.com";

// The time every test passes to the writer.
constexpr long fixture_now = 1700000000;

// The room every test character loads into.
constexpr int fixture_load_room = 1101;

// Returns the contents of the file at path, or a marker when it cannot be opened.
std::string contents_of(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "(unreadable)";
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Returns every path under root, relative to it, mapped to the file's contents ("(directory)" for
// a directory), or the error text as the only entry when the directory cannot be listed.
std::map<std::string, std::string> files_under(const std::filesystem::path& root)
{
    std::map<std::string, std::string> contents_by_path;
    std::error_code error;
    std::filesystem::recursive_directory_iterator entry(root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && entry != end) {
        std::string relative_path = entry->path().lexically_relative(root).generic_string();
        std::error_code type_error;
        std::string contents = "(directory)";
        if (!entry->is_directory(type_error)) {
            contents = contents_of(entry->path());
        }
        contents_by_path.emplace(std::move(relative_path), std::move(contents));
        entry.increment(error);
    }
    if (error) {
        contents_by_path.emplace("(listing failed)", error.message());
    }
    return contents_by_path;
}

// Returns the names of the entries directly inside directory.
std::set<std::string> entry_names_in(const std::filesystem::path& directory)
{
    std::set<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator entry(directory, error);
    const std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        names.insert(entry->path().filename().string());
        entry.increment(error);
    }
    if (error) {
        names.insert("(listing failed: " + error.message() + ")");
    }
    return names;
}

// Returns whether path names an existing file or directory.
bool path_exists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

// Returns a human female with a 70/20/0/60 split at the given level.
FixtureSpec::Character make_character(std::string_view name, int level)
{
    FixtureSpec::Character character;
    character.name = std::string(name);
    character.race = RACE_HUMAN;
    character.sex = SEX_FEMALE;
    std::string split_error;
    character.creation_points = CreationPoints::custom({ 0, 70, 20, 0, 60 }, split_error);
    character.level = level;
    character.load_room = fixture_load_room;
    return character;
}

// Returns a spec for an account at email holding the given characters.
FixtureSpec make_spec(std::string_view email, std::vector<FixtureSpec::Character> characters)
{
    FixtureSpec spec;
    spec.email = std::string(email);
    spec.password = "Writer1pass";
    spec.characters = std::move(characters);
    return spec;
}

} // namespace

// Gives each test an empty temporary lib and restores the working directory after it.
class RotstoolFixtureLibWriter : public testing::Test {
protected:
    void SetUp() override
    {
        // Values another test queued for the wrapped number() would otherwise be used here.
        clear_test_random_values();

        std::error_code error;
        saved_working_directory = std::filesystem::current_path(error);
        ASSERT_FALSE(error) << error.message();

        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error) << error.message();
        lib = temporary_root
            / ("rotstool-lib-writer-" + std::string(testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(lib, error);
        std::filesystem::create_directories(lib, error);
        ASSERT_FALSE(error) << error.message();
    }

    void TearDown() override
    {
        std::error_code error;
        if (!saved_working_directory.empty()) {
            std::filesystem::current_path(saved_working_directory, error);
            if (error) {
                ADD_FAILURE() << "Could not restore the working directory: " << error.message();
            }
        }
        std::filesystem::remove_all(lib, error);
    }

    // Returns the directory the account for email has in the test lib.
    std::filesystem::path account_directory(std::string_view bucket, std::string_view email) const
    {
        return lib / "accounts" / bucket / email;
    }

    // Creates an empty legacy player entry named entry_name in players/<bucket> of the test lib.
    void add_legacy_player_entry(std::string_view bucket, std::string_view entry_name) const
    {
        const std::filesystem::path bucket_directory = lib / "players" / bucket;
        std::error_code error;
        std::filesystem::create_directories(bucket_directory, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream entry(bucket_directory / entry_name);
        ASSERT_TRUE(entry.good()) << entry_name;
    }

    // The working directory before the test, restored after it.
    std::filesystem::path saved_working_directory;
    // The temporary lib the test writes into.
    std::filesystem::path lib;
};

TEST_F(RotstoolFixtureLibWriter, WritesAVerifiedAccountAndThreeFilesPerCharacter)
{
    FixtureSpec::Character mage = make_character("testmage", 10);
    mage.skill_practices.emplace_back(SPELL_MAGIC_MISSILE, 5);
    const FixtureSpec spec = make_spec(writer_email, { mage, make_character("Testwarrior", 12) });
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(spec, lib, fixture_now, written, error_message)) << error_message;

    const std::filesystem::path account_path = account_directory("U-Z", writer_email);
    EXPECT_TRUE(path_exists(account_path / "account.json"));
    for (const char* file_stem : { "testmage", "testwarrior" }) {
        const std::string stem(file_stem);
        EXPECT_TRUE(path_exists(account_path / (stem + ".character.json"))) << stem;
        EXPECT_TRUE(path_exists(account_path / (stem + ".objects.json"))) << stem;
        EXPECT_TRUE(path_exists(account_path / (stem + ".exploits.json"))) << stem;
    }

    account::AccountData account;
    std::string read_error;
    ASSERT_TRUE(account::read_account_file_by_email(lib.string(), writer_email, &account, &read_error)) << read_error;
    EXPECT_TRUE(account.email_verified);
    EXPECT_TRUE(account::account_has_character(account, "testmage"));
    EXPECT_TRUE(account::account_has_character(account, "testwarrior"));

    char_file_u stored_mage {};
    ASSERT_TRUE(account::read_account_character_file(lib.string(), account.account_name, "testmage", &stored_mage,
        &read_error))
        << read_error;
    EXPECT_EQ(stored_mage.level, 10);
    char_file_u stored_warrior {};
    ASSERT_TRUE(account::read_account_character_file(lib.string(), account.account_name, "testwarrior",
        &stored_warrior, &read_error))
        << read_error;
    EXPECT_EQ(stored_warrior.level, 12);

    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[0].name, "Testmage");
    EXPECT_EQ(written[0].idnum, FIRST_FIXTURE_IDNUM);
    EXPECT_EQ(written[0].character_file, account_path / "testmage.character.json");
    ASSERT_EQ(written[0].skill_knowledge.size(), 1u);
    EXPECT_EQ(written[0].skill_knowledge[0].first, SPELL_MAGIC_MISSILE);
    EXPECT_GT(written[0].skill_knowledge[0].second, 0);
    EXPECT_EQ(written[1].name, "Testwarrior");
    EXPECT_EQ(written[1].idnum, FIRST_FIXTURE_IDNUM + 1);
    EXPECT_EQ(written[1].character_file, account_path / "testwarrior.character.json");
    EXPECT_TRUE(written[1].skill_knowledge.empty());
    EXPECT_EQ(stored_mage.specials2.idnum, written[0].idnum);
    EXPECT_EQ(stored_warrior.specials2.idnum, written[1].idnum);
}

TEST_F(RotstoolFixtureLibWriter, StartsIdnumsAboveTheHighestInTheLib)
{
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testfirst", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].idnum, FIRST_FIXTURE_IDNUM);

    ASSERT_TRUE(write_fixtures_to_lib(make_spec(second_email, { make_character("Testsecond", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].idnum, FIRST_FIXTURE_IDNUM + 1);
}

TEST_F(RotstoolFixtureLibWriter, RefusesANameAlreadyInTheLibRegardlessOfCase)
{
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    const std::map<std::string, std::string> files_before = files_under(lib);

    EXPECT_FALSE(write_fixtures_to_lib(make_spec(second_email, { make_character("testmage", 5) }), lib, fixture_now,
        written, error_message));
    EXPECT_NE(error_message.find("testmage is already in the lib"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_FALSE(path_exists(account_directory("P-T", second_email)));
    EXPECT_EQ(files_under(lib), files_before);
}

TEST_F(RotstoolFixtureLibWriter, RefusesANameUsedByALegacyPlayerFile)
{
    add_legacy_player_entry("P-T", "testlegacy.10.1.9000050.1700000000.0");
    const std::map<std::string, std::string> files_before = files_under(lib);

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testlegacy", 5) }), lib,
        fixture_now, written, error_message));
    EXPECT_NE(error_message.find("Testlegacy is already in the lib"), std::string::npos) << error_message;
    EXPECT_FALSE(path_exists(account_directory("U-Z", writer_email)));
    EXPECT_EQ(files_under(lib), files_before);

    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testnewer", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].idnum, 9000051);
}

TEST_F(RotstoolFixtureLibWriter, RefusesANameBootIndexesFromABareLegacyEntry)
{
    // Boot indexes every legacy entry by the text before its first '.', whatever follows.
    add_legacy_player_entry("P-T", "testbare");
    const std::map<std::string, std::string> files_before = files_under(lib);

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testbare", 5) }), lib, fixture_now,
        written, error_message));
    EXPECT_NE(error_message.find("Testbare is already in the lib"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_EQ(files_under(lib), files_before);
}

TEST_F(RotstoolFixtureLibWriter, RefusesIdnumsPastTheLargestBootCanHold)
{
    // The first character would take the largest int; the second would pass it.
    add_legacy_player_entry("P-T", "testhigh.10.1.2147483646.1700000000.0");
    const std::map<std::string, std::string> files_before = files_under(lib);

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testfirst", 5),
                                                     make_character("Testsecond", 5) }),
        lib, fixture_now, written, error_message));
    EXPECT_NE(error_message.find("leaves no room for the spec's characters (2) at or below 2147483647"), std::string::npos)
        << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_EQ(files_under(lib), files_before);
}

TEST_F(RotstoolFixtureLibWriter, RefusesALegacyIdnumTooLargeToRead)
{
    add_legacy_player_entry("P-T", "testhuge.10.1.99999999999999999999.1700000000.0");
    const std::map<std::string, std::string> files_before = files_under(lib);

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 5) }), lib, fixture_now,
        written, error_message));
    EXPECT_NE(error_message.find("leaves no room for the spec's characters (1)"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_EQ(files_under(lib), files_before);
}

TEST_F(RotstoolFixtureLibWriter, RefusesAnAccountThatAlreadyExists)
{
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testfirst", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    const std::map<std::string, std::string> files_before = files_under(lib);

    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testother", 5) }), lib,
        fixture_now + 60, written, error_message));
    EXPECT_NE(error_message.find("already exists"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_EQ(files_under(lib), files_before);
}

TEST_F(RotstoolFixtureLibWriter, RefusesAnAccountDirectoryThatAlreadyExists)
{
    const std::filesystem::path existing_directory = account_directory("U-Z", writer_email);
    std::error_code error;
    std::filesystem::create_directories(existing_directory, error);
    ASSERT_FALSE(error) << error.message();
    const std::filesystem::path sentinel_path = existing_directory / "sentinel.txt";
    {
        std::ofstream sentinel(sentinel_path, std::ios::binary);
        ASSERT_TRUE(sentinel.good());
        sentinel << "not written by rotstool";
    }
    const std::map<std::string, std::string> files_before = files_under(lib);

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 5) }), lib, fixture_now,
        written, error_message));
    EXPECT_NE(error_message.find("already exists"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_EQ(files_under(lib), files_before);
    EXPECT_EQ(contents_of(sentinel_path), "not written by rotstool");
}

TEST_F(RotstoolFixtureLibWriter, RefusesALibThatIsNotADirectory)
{
    const FixtureSpec spec = make_spec(writer_email, { make_character("Testmage", 5) });
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    const std::filesystem::path missing = lib / "missing";
    EXPECT_FALSE(write_fixtures_to_lib(spec, missing, fixture_now, written, error_message));
    EXPECT_NE(error_message.find("is not a directory"), std::string::npos) << error_message;
    EXPECT_FALSE(path_exists(missing));

    const std::filesystem::path plain_file = lib / "plain-file";
    {
        std::ofstream file(plain_file);
        file << "not a lib";
        ASSERT_TRUE(file.good());
    }
    error_message.clear();
    EXPECT_FALSE(write_fixtures_to_lib(spec, plain_file, fixture_now, written, error_message));
    EXPECT_NE(error_message.find("is not a directory"), std::string::npos) << error_message;
    EXPECT_EQ(contents_of(plain_file), "not a lib");
    EXPECT_EQ(entry_names_in(lib), std::set<std::string>({ "plain-file" }));
}

TEST_F(RotstoolFixtureLibWriter, RemovesTheAccountWhenAPartWayStepFails)
{
    // The maker refuses level 0, after the account and the first character are on disk.
    const FixtureSpec spec = make_spec(writer_email, { make_character("Testfirst", 5), make_character("Testsecond", 0) });
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    EXPECT_FALSE(write_fixtures_to_lib(spec, lib, fixture_now, written, error_message));
    EXPECT_NE(error_message.find("Testsecond"), std::string::npos) << error_message;
    EXPECT_TRUE(written.empty());
    EXPECT_FALSE(path_exists(account_directory("U-Z", writer_email)));
}

TEST_F(RotstoolFixtureLibWriter, WritesNoExploitRecordOrLegacyFile)
{
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 30) }), lib, fixture_now,
        written, error_message))
        << error_message;

    account::AccountData account;
    std::string read_error;
    ASSERT_TRUE(account::read_account_file_by_email(lib.string(), writer_email, &account, &read_error)) << read_error;
    std::vector<exploit_record> records;
    ASSERT_TRUE(account::read_account_exploit_file(lib.string(), account.account_name, "testmage", &records,
        &read_error))
        << read_error;
    EXPECT_TRUE(records.empty());

    // Only the account tree: no players/, plrobjs/ or exploits/ entries, nor anything else.
    EXPECT_EQ(entry_names_in(lib), std::set<std::string>({ "accounts" }));
}

TEST_F(RotstoolFixtureLibWriter, LeavesTheWorkingDirectoryUnchanged)
{
    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 5) }), lib, fixture_now,
        written, error_message))
        << error_message;
    std::error_code error;
    EXPECT_EQ(std::filesystem::current_path(error), saved_working_directory);

    // A failure while the maker runs in the lib restores it as well.
    EXPECT_FALSE(write_fixtures_to_lib(make_spec(second_email, { make_character("Testfailed", 0) }), lib,
        fixture_now, written, error_message));
    EXPECT_EQ(std::filesystem::current_path(error), saved_working_directory);
}

TEST_F(RotstoolFixtureLibWriter, WritesIntoALibGivenAsARelativePath)
{
    std::error_code error;
    std::filesystem::current_path(lib.parent_path(), error);
    ASSERT_FALSE(error) << error.message();

    std::vector<WrittenFixtureCharacter> written;
    std::string error_message;
    ASSERT_TRUE(write_fixtures_to_lib(make_spec(writer_email, { make_character("Testmage", 5) }), lib.filename(),
        fixture_now, written, error_message))
        << error_message;

    EXPECT_TRUE(path_exists(account_directory("U-Z", writer_email) / "testmage.character.json"));
    EXPECT_EQ(std::filesystem::current_path(error), lib.parent_path());
}
