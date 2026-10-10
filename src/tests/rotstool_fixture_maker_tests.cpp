#include "fixture_character_maker.h"

#include "account_management.h"
#include "creation_points.h"
#include "spells.h"
#include "structs.h"
#include "test_random_utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

extern int top_of_p_table;
extern long top_idnum;

namespace {

// The idnum every test hands the maker.
constexpr long fixture_idnum = 9000001;

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

// Sets top_of_p_table for one test and puts its previous value back when the test ends.
class ScopedPlayerTableTop {
public:
    explicit ScopedPlayerTableTop(int scoped_value)
        : saved_value(top_of_p_table)
    {
        top_of_p_table = scoped_value;
    }

    ~ScopedPlayerTableTop()
    {
        top_of_p_table = saved_value;
    }

    ScopedPlayerTableTop(const ScopedPlayerTableTop&) = delete;
    ScopedPlayerTableTop& operator=(const ScopedPlayerTableTop&) = delete;

private:
    // top_of_p_table before this object changed it.
    int saved_value;
};

// Expects two ability sets to hold the same values.
void expect_same_abilities(const char_ability_data& expected, const char_ability_data& actual)
{
    EXPECT_EQ(expected.str, actual.str);
    EXPECT_EQ(expected.lea, actual.lea);
    EXPECT_EQ(expected.intel, actual.intel);
    EXPECT_EQ(expected.wil, actual.wil);
    EXPECT_EQ(expected.dex, actual.dex);
    EXPECT_EQ(expected.con, actual.con);
    EXPECT_EQ(expected.hit, actual.hit);
    EXPECT_EQ(expected.mana, actual.mana);
    EXPECT_EQ(expected.move, actual.move);
}

} // namespace

// Gives each test a temporary lib holding one account, as the working directory.
class RotstoolFixtureMaker : public testing::Test {
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
        lib = temporary_root / ("rotstool-maker-" + std::string(testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(lib, error);
        std::filesystem::create_directories(lib, error);
        ASSERT_FALSE(error) << error.message();

        now = static_cast<long>(std::time(nullptr));
        account::AccountData account;
        std::string account_error;
        ASSERT_TRUE(account::create_account_for_email(lib.string(), "maker@example.com", "Maker1pass", now, &account,
            &account_error))
            << account_error;
        account_name = account.account_name;

        std::filesystem::current_path(lib, error);
        ASSERT_FALSE(error) << error.message();
    }

    void TearDown() override
    {
        std::error_code error;
        if (!saved_working_directory.empty()) {
            std::filesystem::current_path(saved_working_directory, error);
        }
        std::filesystem::remove_all(lib, error);
    }

    // Returns a human female with a 70/20/0/60 split at the given level.
    FixtureSpec::Character make_spec(int level) const
    {
        FixtureSpec::Character spec;
        spec.name = "testmage";
        spec.race = RACE_HUMAN;
        spec.sex = SEX_FEMALE;
        std::string split_error;
        spec.creation_points = CreationPoints::custom({ 0, 70, 20, 0, 60 }, split_error);
        spec.level = level;
        spec.load_room = fixture_load_room;
        return spec;
    }

    // The working directory before the test, restored after it.
    std::filesystem::path saved_working_directory;
    // The temporary lib the test makes characters in.
    std::filesystem::path lib;
    // The account the test's characters belong to, on disk without them.
    std::string account_name;
    // The time passed to the maker as the last logon.
    long now = 0;
};

TEST_F(RotstoolFixtureMaker, MakesACustomMageAtTheRequestedLevel)
{
    const FixtureSpec::Character spec = make_spec(30);
    ASSERT_TRUE(spec.creation_points.has_value());
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(stored.level, 30);
    EXPECT_EQ(stored.specials2.idnum, fixture_idnum);
    EXPECT_EQ(stored.specials2.load_room, fixture_load_room);
    EXPECT_EQ(stored.last_logon, now);
    EXPECT_STREQ(stored.name, "Testmage");
    EXPECT_EQ(stored.profs.prof_coof[PROF_MAGE], 70);
    EXPECT_EQ(stored.profs.prof_coof[PROF_CLERIC], 20);
    EXPECT_EQ(stored.profs.prof_coof[PROF_RANGER], 0);
    EXPECT_EQ(stored.profs.prof_coof[PROF_WARRIOR], 60);
}

TEST_F(RotstoolFixtureMaker, StoresPracticesAndReportsKnowledge)
{
    FixtureSpec::Character spec = make_spec(30);
    spec.skill_practices.emplace_back(SPELL_MAGIC_MISSILE, 5);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(stored.skills[SPELL_MAGIC_MISSILE], 5);
    ASSERT_EQ(knowledge.size(), 1u);
    EXPECT_GT(knowledge[0], 0);
}

TEST_F(RotstoolFixtureMaker, DoesNotPromoteTheFirstCharacterToImplementor)
{
    // 0 is the value at which do_start promotes a new character to implementor.
    const ScopedPlayerTableTop only_one_player(0);
    const FixtureSpec::Character spec = make_spec(1);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(stored.level, 1);
    EXPECT_EQ(top_of_p_table, 0);
}

TEST_F(RotstoolFixtureMaker, ReachesAnImmortalLevel)
{
    const FixtureSpec::Character spec = make_spec(95);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(stored.level, 95);
}

TEST_F(RotstoolFixtureMaker, StoresSetValuesBelowTheMaximumAsCurrentValues)
{
    FixtureSpec::Character spec = make_spec(30);
    spec.hit = 50;
    spec.mana = 20;
    spec.move = 30;
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(stored.tmpabilities.hit, 50);
    EXPECT_EQ(stored.tmpabilities.mana, 20);
    EXPECT_EQ(stored.tmpabilities.move, 30);
}

TEST_F(RotstoolFixtureMaker, RefusesASetValueAboveTheMaximum)
{
    FixtureSpec::Character spec = make_spec(30);
    spec.hit = 5000;
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    EXPECT_FALSE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message));

    const std::string expected_start = "Testmage's hit 5000 is above its maximum of ";
    ASSERT_EQ(error_message.compare(0, expected_start.size(), expected_start), 0) << error_message;
    const int maximum = std::atoi(error_message.c_str() + expected_start.size());
    EXPECT_GT(maximum, 0) << error_message;
    EXPECT_LT(maximum, 5000) << error_message;
}

TEST_F(RotstoolFixtureMaker, WritesNothingWhileMakingACharacter)
{
    const std::map<std::string, std::string> files_before = files_under(lib);
    const FixtureSpec::Character spec = make_spec(30);
    // A fixed seed makes the level-ups' stat gains, whose records the orphan guard drops, repeatable.
    std::srand(4242);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    const std::map<std::string, std::string> files_after = files_under(lib);
    EXPECT_EQ(files_before, files_after);
    bool lists_the_account_file = false;
    for (const std::pair<const std::string, std::string>& file : files_after) {
        if (std::filesystem::path(file.first).filename() == "account.json") {
            lists_the_account_file = true;
        }
    }
    EXPECT_TRUE(lists_the_account_file);
}

TEST_F(RotstoolFixtureMaker, SameSeedMakesTheSameCharacter)
{
    const FixtureSpec::Character spec = make_spec(30);
    char_file_u first {};
    char_file_u second {};
    std::vector<int> knowledge;
    std::string error_message;
    std::srand(1234);
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, first, knowledge, error_message))
        << error_message;
    std::srand(1234);
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, second, knowledge, error_message))
        << error_message;

    expect_same_abilities(first.constabilities, second.constabilities);
    expect_same_abilities(first.tmpabilities, second.tmpabilities);
    for (int profession = PROF_MAGE; profession <= PROF_WARRIOR; ++profession) {
        EXPECT_EQ(first.profs.prof_level[profession], second.profs.prof_level[profession]) << profession;
    }
}

TEST_F(RotstoolFixtureMaker, RestoresThePlayerTableAndIdnumGlobals)
{
    const int saved_top_of_p_table = top_of_p_table;
    const long saved_top_idnum = top_idnum;
    const FixtureSpec::Character spec = make_spec(30);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    ASSERT_TRUE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message))
        << error_message;

    EXPECT_EQ(top_of_p_table, saved_top_of_p_table);
    EXPECT_EQ(top_idnum, saved_top_idnum);
}

TEST_F(RotstoolFixtureMaker, RefusesALevelItCannotReach)
{
    const FixtureSpec::Character spec = make_spec(0);
    char_file_u stored {};
    std::vector<int> knowledge;
    std::string error_message;
    EXPECT_FALSE(make_fixture_character(spec, account_name, fixture_idnum, now, stored, knowledge, error_message));
    EXPECT_FALSE(error_message.empty());
}
