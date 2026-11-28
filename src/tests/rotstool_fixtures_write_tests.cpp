#include "fixture_lib_writer.h"
#include "rotstool_command.h"

#include "account_management.h"
#include "spells.h"
#include "structs.h"
#include "test_random_utils.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// The email of the account the test spec describes.
constexpr const char* spec_email = "fwrite@example.com";

// A spec for one account holding a wizard with one skill and a warrior with a set hit value.
const std::string valid_spec = R"({
  "account": { "email": "fwrite@example.com", "password": "Writer1pass" },
  "characters": [
    { "name": "Testmage", "race": "human", "sex": "female", "class": "wizard",
      "level": 10, "load_room": 1101, "skills": { "magic_missile": 5 } },
    { "name": "Testwarrior", "race": "human", "sex": "male", "class": "warrior",
      "level": 12, "load_room": 1101, "set": { "hit": 20 } }
  ]
})";

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

// Splits text into its lines, without the line breaks.
std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

// Splits a line into its tab-separated fields.
std::vector<std::string> fields_of(std::string_view line)
{
    std::vector<std::string> fields;
    while (true) {
        const std::size_t tab_position = line.find('\t');
        if (tab_position == std::string_view::npos) {
            fields.emplace_back(line);
            return fields;
        }
        fields.emplace_back(line.substr(0, tab_position));
        line.remove_prefix(tab_position + 1);
    }
}

// Returns whether path names an existing file or directory.
bool path_exists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

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

// The result of one rotstool run.
struct RunResult {
    // The exit code.
    int exit_code = -1;
    // What the run wrote to standard output.
    std::string out;
    // What the run wrote to standard error.
    std::string err;
};

// Runs rotstool with arguments and returns what it did.
RunResult run_tool(const std::vector<std::string>& arguments)
{
    std::ostringstream out;
    std::ostringstream err;
    RunResult result;
    result.exit_code = rotstool::run_rotstool(arguments, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

} // namespace

// Gives each test a temporary directory holding an empty lib and a spec file, and restores the
// working directory after it.
class RotstoolFixturesWrite : public testing::Test {
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
        work_directory = temporary_root
            / ("rotstool-fixtures-write-"
                + std::string(testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(work_directory, error);
        lib = work_directory / "lib";
        std::filesystem::create_directories(lib, error);
        ASSERT_FALSE(error) << error.message();
        spec_path = work_directory / "spec.json";
        write_spec(valid_spec);
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
        std::filesystem::remove_all(work_directory, error);
    }

    // Replaces the spec file's contents with text.
    void write_spec(std::string_view text) const
    {
        std::ofstream spec_file(spec_path, std::ios::binary | std::ios::trunc);
        spec_file << text;
        ASSERT_TRUE(spec_file.good()) << spec_path;
    }

    // Reads the stored character name from the account the test spec describes in lib_directory.
    void read_stored_character(const std::filesystem::path& lib_directory, const std::string& name,
        char_file_u& out_stored) const
    {
        account::AccountData account;
        std::string read_error;
        ASSERT_TRUE(account::read_account_file_by_email(lib_directory.string(), spec_email, &account, &read_error))
            << read_error;
        ASSERT_TRUE(account::read_account_character_file(lib_directory.string(), account.account_name, name,
            &out_stored, &read_error))
            << read_error;
    }

    // Expects a bad-usage run: exit 2, a message naming the command, and the usage on stderr.
    static void expect_bad_usage(const RunResult& result)
    {
        EXPECT_EQ(result.exit_code, rotstool::EXIT_CODE_USAGE);
        EXPECT_NE(result.err.find("rotstool fixtures write: "), std::string::npos) << result.err;
        EXPECT_NE(result.err.find("usage: rotstool fixtures write"), std::string::npos) << result.err;
        EXPECT_TRUE(result.out.empty()) << result.out;
    }

    // The working directory before the test, restored after it.
    std::filesystem::path saved_working_directory;
    // The temporary directory holding the lib and the spec file; removed after the test.
    std::filesystem::path work_directory;
    // The empty lib the command writes into.
    std::filesystem::path lib;
    // The spec file passed to the command.
    std::filesystem::path spec_path;
};

TEST_F(RotstoolFixturesWrite, WritesTheLibAndPrintsOneLinePerCharacter)
{
    const RunResult result = run_tool({ "fixtures", "write", "--lib", lib.string(), spec_path.string() });

    ASSERT_EQ(result.exit_code, rotstool::EXIT_CODE_SUCCESS) << result.err;
    const std::vector<std::string> lines = lines_of(result.out);
    ASSERT_EQ(lines.size(), 2u) << result.out;

    const std::vector<std::string> mage_fields = fields_of(lines[0]);
    ASSERT_EQ(mage_fields.size(), 3u) << lines[0];
    EXPECT_EQ(mage_fields[0], "Testmage");
    EXPECT_EQ(mage_fields[1], std::to_string(FIRST_FIXTURE_IDNUM));
    EXPECT_EQ(std::filesystem::path(mage_fields[2]).filename(), "testmage.character.json");
    EXPECT_TRUE(path_exists(mage_fields[2])) << mage_fields[2];

    const std::vector<std::string> warrior_fields = fields_of(lines[1]);
    ASSERT_EQ(warrior_fields.size(), 3u) << lines[1];
    EXPECT_EQ(warrior_fields[0], "Testwarrior");
    EXPECT_EQ(warrior_fields[1], std::to_string(FIRST_FIXTURE_IDNUM + 1));
    EXPECT_EQ(std::filesystem::path(warrior_fields[2]).filename(), "testwarrior.character.json");
    EXPECT_TRUE(path_exists(warrior_fields[2])) << warrior_fields[2];

    char_file_u stored_warrior {};
    read_stored_character(lib, "testwarrior", stored_warrior);
    EXPECT_EQ(stored_warrior.level, 12);
    EXPECT_EQ(stored_warrior.tmpabilities.hit, 20);
}

TEST_F(RotstoolFixturesWrite, VerboseAddsAKnowledgeLinePerSkill)
{
    const RunResult result
        = run_tool({ "fixtures", "write", "--lib", lib.string(), "--verbose", spec_path.string() });

    ASSERT_EQ(result.exit_code, rotstool::EXIT_CODE_SUCCESS) << result.err;
    const std::vector<std::string> lines = lines_of(result.out);
    ASSERT_EQ(lines.size(), 3u) << result.out;

    EXPECT_EQ(fields_of(lines[0]).size(), 3u) << lines[0];
    const std::vector<std::string> skill_fields = fields_of(lines[1]);
    ASSERT_EQ(skill_fields.size(), 4u) << lines[1];
    EXPECT_EQ(skill_fields[0], "Testmage");
    EXPECT_EQ(skill_fields[1], get_skill_array()[SPELL_MAGIC_MISSILE].name);
    EXPECT_EQ(skill_fields[2], "5");
    EXPECT_GT(std::stoi(skill_fields[3]), 0) << lines[1];
    EXPECT_EQ(fields_of(lines[2])[0], "Testwarrior");
}

TEST_F(RotstoolFixturesWrite, AcceptsBothOptionForms)
{
    const RunResult separate = run_tool(
        { "fixtures", "write", "--lib", lib.string(), "--random-seed", "7", spec_path.string() });
    ASSERT_EQ(separate.exit_code, rotstool::EXIT_CODE_SUCCESS) << separate.err;
    EXPECT_EQ(lines_of(separate.out).size(), 2u) << separate.out;

    const std::filesystem::path second_lib = work_directory / "second-lib";
    std::error_code error;
    std::filesystem::create_directories(second_lib, error);
    ASSERT_FALSE(error) << error.message();
    const RunResult joined = run_tool(
        { "fixtures", "write", "--lib=" + second_lib.string(), "--random-seed=7", spec_path.string() });
    ASSERT_EQ(joined.exit_code, rotstool::EXIT_CODE_SUCCESS) << joined.err;
    const std::vector<std::string> joined_lines = lines_of(joined.out);
    ASSERT_EQ(joined_lines.size(), 2u) << joined.out;
    const std::filesystem::path joined_path(fields_of(joined_lines[0]).back());
    EXPECT_EQ(joined_path.lexically_relative(second_lib).string().rfind("accounts", 0), 0u) << joined_path;
}

TEST_F(RotstoolFixturesWrite, SameSeedWritesTheSameCharacters)
{
    const std::filesystem::path second_lib = work_directory / "second-lib";
    std::error_code error;
    std::filesystem::create_directories(second_lib, error);
    ASSERT_FALSE(error) << error.message();

    const RunResult first
        = run_tool({ "fixtures", "write", "--lib", lib.string(), "--random-seed", "42", spec_path.string() });
    ASSERT_EQ(first.exit_code, rotstool::EXIT_CODE_SUCCESS) << first.err;
    const RunResult second = run_tool(
        { "fixtures", "write", "--lib", second_lib.string(), "--random-seed", "42", spec_path.string() });
    ASSERT_EQ(second.exit_code, rotstool::EXIT_CODE_SUCCESS) << second.err;

    for (const char* name : { "testmage", "testwarrior" }) {
        char_file_u first_stored {};
        char_file_u second_stored {};
        read_stored_character(lib, name, first_stored);
        read_stored_character(second_lib, name, second_stored);
        SCOPED_TRACE(name);
        expect_same_abilities(first_stored.constabilities, second_stored.constabilities);
        expect_same_abilities(first_stored.tmpabilities, second_stored.tmpabilities);
        EXPECT_EQ(first_stored.level, second_stored.level);
        EXPECT_EQ(first_stored.specials2.idnum, second_stored.specials2.idnum);
    }
}

TEST_F(RotstoolFixturesWrite, MissingLibIsBadUsage)
{
    const RunResult result = run_tool({ "fixtures", "write", spec_path.string() });

    expect_bad_usage(result);
    EXPECT_NE(result.err.find("--lib"), std::string::npos) << result.err;
}

TEST_F(RotstoolFixturesWrite, MissingSpecIsBadUsage)
{
    const RunResult result = run_tool({ "fixtures", "write", "--lib", lib.string() });

    expect_bad_usage(result);
    EXPECT_EQ(files_under(lib).size(), 0u);
}

TEST_F(RotstoolFixturesWrite, TwoSpecFilesIsBadUsage)
{
    const RunResult result
        = run_tool({ "fixtures", "write", "--lib", lib.string(), spec_path.string(), spec_path.string() });

    expect_bad_usage(result);
    EXPECT_EQ(files_under(lib).size(), 0u);
}

TEST_F(RotstoolFixturesWrite, UnknownOptionIsBadUsage)
{
    const RunResult result
        = run_tool({ "fixtures", "write", "--lib", lib.string(), "--colour", spec_path.string() });

    expect_bad_usage(result);
    EXPECT_NE(result.err.find("--colour"), std::string::npos) << result.err;
    EXPECT_EQ(files_under(lib).size(), 0u);
}

TEST_F(RotstoolFixturesWrite, IllegalSeedIsBadUsage)
{
    const RunResult result
        = run_tool({ "fixtures", "write", "--lib", lib.string(), "--random-seed", "-1", spec_path.string() });

    expect_bad_usage(result);
    EXPECT_NE(result.err.find("-1"), std::string::npos) << result.err;
    EXPECT_EQ(files_under(lib).size(), 0u);
}

TEST_F(RotstoolFixturesWrite, InvalidSpecFailsAndWritesNothing)
{
    write_spec(R"({ "account": { "email": "fwrite@example.com", "password": "Writer1pass" }, "colour": 1 })");

    const RunResult result = run_tool({ "fixtures", "write", "--lib", lib.string(), spec_path.string() });

    EXPECT_EQ(result.exit_code, rotstool::EXIT_CODE_FAILURE);
    EXPECT_NE(result.err.find("rotstool fixtures write: "), std::string::npos) << result.err;
    EXPECT_TRUE(result.out.empty()) << result.out;
    EXPECT_EQ(files_under(lib).size(), 0u);
}

TEST_F(RotstoolFixturesWrite, UnreadableSpecFileFails)
{
    const std::filesystem::path missing_spec = work_directory / "missing.json";

    const RunResult result = run_tool({ "fixtures", "write", "--lib", lib.string(), missing_spec.string() });

    EXPECT_EQ(result.exit_code, rotstool::EXIT_CODE_FAILURE);
    EXPECT_NE(result.err.find("rotstool fixtures write: "), std::string::npos) << result.err;
    EXPECT_EQ(files_under(lib).size(), 0u);
}
