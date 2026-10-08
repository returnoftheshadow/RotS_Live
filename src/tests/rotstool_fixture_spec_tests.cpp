#include "fixture_spec_parser.h"

#include "account_management.h"
#include "character_json.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

const std::string valid_spec = R"({
  "account": { "email": "harness@example.com", "password": "Harness1x" },
  "characters": [
    { "name": "Testwiz", "race": "human", "sex": "female", "class": "wizard",
      "level": 30, "load_room": 1101, "skills": { "magic_missile": 5 } },
    { "name": "Testcust", "race": "wood elf", "sex": "male",
      "points": { "mage": 0, "mystic": 20, "ranger": 60, "warrior": 70 },
      "level": 25, "load_room": 1101, "set": { "hit": 300, "move": 150 } }
  ]
})";

const std::string valid_account = R"("account": { "email": "harness@example.com", "password": "Harness1x" })";

// The fields of a character the parser accepts, as (key, JSON value) pairs.
const std::vector<std::pair<std::string, std::string>> valid_character_fields = {
    { "name", R"("Testwiz")" },
    { "race", R"("human")" },
    { "sex", R"("female")" },
    { "class", R"("wizard")" },
    { "level", "30" },
    { "load_room", "1101" },
};

// Parses text and returns the error, or an empty string when the spec was accepted.
std::string parse_error_of(const std::string& text)
{
    FixtureSpec spec;
    std::string error_message;
    if (parse_fixture_spec(text, spec, error_message)) {
        return "";
    }
    return error_message.empty() ? "(refused with no message)" : error_message;
}

// Returns a spec with the valid account and the given character objects as its characters.
std::string spec_with_characters(const std::string& character_objects)
{
    return "{ " + valid_account + R"(, "characters": [ )" + character_objects + " ] }";
}

// Returns a character object with the valid fields except omitted_key, followed by extra_fields
// (each a complete "key": value text, joined with commas).
std::string character_object(const std::string& omitted_key, const std::string& extra_fields)
{
    std::string text = "{ ";
    bool first_field = true;
    for (const std::pair<std::string, std::string>& field : valid_character_fields) {
        if (field.first == omitted_key) {
            continue;
        }
        if (!first_field) {
            text += ", ";
        }
        text += "\"" + field.first + "\": " + field.second;
        first_field = false;
    }
    if (!extra_fields.empty()) {
        text += ", " + extra_fields;
    }
    return text + " }";
}

// Returns a one-character spec built by character_object().
std::string spec_with_character(const std::string& omitted_key, const std::string& extra_fields)
{
    return spec_with_characters(character_object(omitted_key, extra_fields));
}

// Returns true when text contains part.
bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// Returns a path for a scratch file in the system's temporary directory, or an empty path when
// the directory is unavailable.
std::filesystem::path scratch_file_path(const std::string& file_name)
{
    std::error_code error;
    const std::filesystem::path directory = std::filesystem::temp_directory_path(error);
    if (error) {
        return {};
    }
    return directory / file_name;
}

} // namespace

TEST(RotstoolFixtureSpec, ParsesAValidSpecIntoGameNumbers)
{
    FixtureSpec spec;
    std::string error_message;
    ASSERT_TRUE(parse_fixture_spec(valid_spec, spec, error_message)) << error_message;
    ASSERT_EQ(spec.characters.size(), 2u);
    EXPECT_EQ(spec.email, "harness@example.com");
    EXPECT_EQ(spec.password, "Harness1x");
    EXPECT_EQ(spec.characters[0].name, "Testwiz");
    EXPECT_EQ(spec.characters[0].race, RACE_HUMAN);
    EXPECT_EQ(spec.characters[0].sex, SEX_FEMALE);
    EXPECT_EQ(spec.characters[0].level, 30);
    EXPECT_EQ(spec.characters[0].load_room, 1101);
    ASSERT_TRUE(spec.characters[0].creation_points.has_value());
    EXPECT_EQ(spec.characters[0].creation_points->points(PROF_MAGE), 121);
    ASSERT_EQ(spec.characters[0].skill_practices.size(), 1u);
    EXPECT_EQ(spec.characters[0].skill_practices[0].first, SPELL_MAGIC_MISSILE);
    EXPECT_EQ(spec.characters[0].skill_practices[0].second, 5);
    EXPECT_EQ(spec.characters[1].race, RACE_WOOD);
    EXPECT_EQ(spec.characters[1].sex, SEX_MALE);
    ASSERT_TRUE(spec.characters[1].creation_points.has_value());
    EXPECT_EQ(spec.characters[1].creation_points->points(PROF_CLERIC), 20);
    EXPECT_EQ(spec.characters[1].creation_points->points(PROF_WARRIOR), 70);
    EXPECT_EQ(spec.characters[1].hit, 300);
    EXPECT_FALSE(spec.characters[1].mana.has_value());
    EXPECT_EQ(spec.characters[1].move, 150);
}

TEST(RotstoolFixtureSpec, ResolvesEveryOfferedRaceByItsMenuName)
{
    const std::vector<std::pair<std::string, int>> races = {
        { "human", RACE_HUMAN },
        { "Dwarf", RACE_DWARF },
        { "wood elf", RACE_WOOD },
        { "hobbit", RACE_HOBBIT },
        { "beorning", RACE_BEORNING },
        { "uruk-hai", RACE_URUK },
        { "orc", RACE_ORC },
        { "uruk-lhuth", RACE_MAGUS },
        { "olog-hai", RACE_OLOGHAI },
        { "haradrim", RACE_HARADRIM },
    };
    for (const std::pair<std::string, int>& race : races) {
        FixtureSpec spec;
        std::string error_message;
        const std::string text = spec_with_character("race", "\"race\": \"" + race.first + "\"");
        ASSERT_TRUE(parse_fixture_spec(text, spec, error_message)) << race.first << ": " << error_message;
        EXPECT_EQ(spec.characters[0].race, race.second) << race.first;
    }
}

TEST(RotstoolFixtureSpec, RefusesTextThatIsNotJson)
{
    EXPECT_NE(parse_error_of("{"), "");
}

TEST(RotstoolFixtureSpec, RefusesTextAfterTheSpec)
{
    const std::string error_message = parse_error_of(valid_spec + " x");
    EXPECT_TRUE(contains(error_message, "trailing")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesAnUnknownKey)
{
    const std::string at_top_level = "{ " + valid_account + R"(, "characters": [ )"
        + character_object("", "") + R"( ], "colour": 1 })";
    EXPECT_TRUE(contains(parse_error_of(at_top_level), "Unknown key 'colour'")) << parse_error_of(at_top_level);

    const std::string in_account = R"({ "account": { "email": "harness@example.com", "password": "Harness1x",
        "colour": 1 }, "characters": [ )" + character_object("", "") + " ] }";
    EXPECT_TRUE(contains(parse_error_of(in_account), "'account': Unknown key 'colour'")) << parse_error_of(in_account);

    const std::string in_character = spec_with_character("", R"("colour": 1)");
    EXPECT_TRUE(contains(parse_error_of(in_character), "characters[0]: Unknown key 'colour'"))
        << parse_error_of(in_character);

    const std::string in_points = spec_with_character("class", R"("points": { "mage": 10, "colour": 1 })");
    EXPECT_TRUE(contains(parse_error_of(in_points), "'points': Unknown key 'colour'")) << parse_error_of(in_points);

    const std::string in_set = spec_with_character("", R"("set": { "hit": 10, "colour": 1 })");
    EXPECT_TRUE(contains(parse_error_of(in_set), "'set': Unknown key 'colour'")) << parse_error_of(in_set);
}

TEST(RotstoolFixtureSpec, RefusesARepeatedKey)
{
    const std::string in_character = parse_error_of(spec_with_character("", R"("level": 31)"));
    EXPECT_TRUE(contains(in_character, "characters[0]: Repeated key 'level'")) << in_character;

    const std::string at_top_level = parse_error_of("{ " + valid_account + ", " + valid_account
        + R"(, "characters": [ )" + character_object("", "") + " ] }");
    EXPECT_TRUE(contains(at_top_level, "Repeated key 'account'")) << at_top_level;

    const std::string in_account = parse_error_of(R"({ "account": { "email": "harness@example.com",
        "email": "harness@example.com", "password": "Harness1x" }, "characters": [ )"
        + character_object("", "") + " ] }");
    EXPECT_TRUE(contains(in_account, "'account': Repeated key 'email'")) << in_account;

    const std::string in_points = parse_error_of(
        spec_with_character("class", R"("points": { "mage": 10, "mage": 20 })"));
    EXPECT_TRUE(contains(in_points, "'points': Repeated key 'mage'")) << in_points;

    const std::string in_skills = parse_error_of(
        spec_with_character("", R"("skills": { "magic_missile": 5, "magic_missile": 6 })"));
    EXPECT_TRUE(contains(in_skills, "'skills': Repeated key 'magic_missile'")) << in_skills;

    const std::string in_set = parse_error_of(spec_with_character("", R"("set": { "hit": 10, "hit": 20 })"));
    EXPECT_TRUE(contains(in_set, "'set': Repeated key 'hit'")) << in_set;
}

TEST(RotstoolFixtureSpec, RefusesAMissingRequiredField)
{
    for (const std::string& key : { "name", "race", "sex", "level", "load_room" }) {
        const std::string error_message = parse_error_of(spec_with_character(key, ""));
        EXPECT_TRUE(contains(error_message, "characters[0]: Missing key '" + key + "'"))
            << key << ": " << error_message;
    }

    const std::string without_account = R"({ "characters": [ )" + character_object("", "") + " ] }";
    EXPECT_TRUE(contains(parse_error_of(without_account), "Missing key 'account'")) << parse_error_of(without_account);
}

TEST(RotstoolFixtureSpec, RefusesBothClassAndPoints)
{
    const std::string error_message = parse_error_of(spec_with_character("", R"("points": { "mage": 10 })"));
    EXPECT_TRUE(contains(error_message, "Give 'class' or 'points', not both")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesNeitherClassNorPoints)
{
    const std::string error_message = parse_error_of(spec_with_character("class", ""));
    EXPECT_TRUE(contains(error_message, "Missing key 'class' or 'points'")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesARaceTheCreationMenuDoesNotOffer)
{
    for (const std::string& race : { "high elf", "god", "troll" }) {
        const std::string error_message = parse_error_of(spec_with_character("race", "\"race\": \"" + race + "\""));
        EXPECT_TRUE(contains(error_message, "'" + race + "' is not a race the creation menu offers"))
            << race << ": " << error_message;
    }
}

TEST(RotstoolFixtureSpec, RefusesAnUnknownSex)
{
    const std::string error_message = parse_error_of(spec_with_character("sex", R"("sex": "neuter")"));
    EXPECT_TRUE(contains(error_message, "'neuter' is not 'male' or 'female'")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesAnUnknownClass)
{
    const std::string error_message = parse_error_of(spec_with_character("class", R"("class": "necromancer")"));
    EXPECT_TRUE(contains(error_message, "'necromancer' is not a class the creation menu offers")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesAnUnknownSkillKey)
{
    const std::string error_message = parse_error_of(spec_with_character("", R"("skills": { "magic missile": 5 })"));
    EXPECT_TRUE(contains(error_message, "Unknown skill key 'magic missile'")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesTheKeyOfASkillSlotWithNoSkill)
{
    const skill_data* skills = get_skill_array();
    int empty_index = -1;
    for (int index = 0; index < MAX_SKILLS; ++index) {
        if (skills[index].name[0] == '\0') {
            empty_index = index;
            break;
        }
    }
    ASSERT_GE(empty_index, 0) << "no skill slot without a name";
    const std::string key = "skill_" + std::to_string(empty_index);
    ASSERT_EQ(character_json::skill_index_for_file_key(key), empty_index);

    const std::string error_message = parse_error_of(spec_with_character("", "\"skills\": { \"" + key + "\": 5 }"));
    EXPECT_TRUE(contains(error_message, "Unknown skill key '" + key + "'")) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesACustomSplitOverTheBudget)
{
    const std::string error_message = parse_error_of(
        spec_with_character("class", R"("points": { "mage": 100, "mystic": 51 })"));
    EXPECT_TRUE(contains(error_message, "'points': ")) << error_message;
    EXPECT_TRUE(contains(error_message, "most is " + std::to_string(CREATION_POINT_BUDGET))) << error_message;
}

TEST(RotstoolFixtureSpec, RefusesALevelOutOfRange)
{
    for (const int level : { 0, LEVEL_IMPL + 1 }) {
        const std::string error_message = parse_error_of(
            spec_with_character("level", "\"level\": " + std::to_string(level)));
        const std::string expected
            = "'level': " + std::to_string(level) + " is outside 1 to " + std::to_string(LEVEL_IMPL);
        EXPECT_TRUE(contains(error_message, expected)) << level << ": " << error_message;
    }
}

TEST(RotstoolFixtureSpec, RefusesANameThatIsNotThreeToTwelveLetters)
{
    const std::string expected_rule
        = " is not " + std::to_string(MIN_NAME_LENGTH) + " to " + std::to_string(MAX_NAME_LENGTH) + " letters";
    for (const std::string& name : { "Abcdefghijklm", " Testwiz", "Test_1", "12-x", "Ab", "" }) {
        const std::string error_message = parse_error_of(
            spec_with_character("name", "\"name\": \"" + name + "\""));
        EXPECT_TRUE(contains(error_message, "'name': '" + name + "'" + expected_rule)) << name << ": " << error_message;
    }
}

TEST(RotstoolFixtureSpec, AcceptsNamesOfThreeAndTwelveLetters)
{
    for (const std::string& name : { "Abc", "Abcdefghijkl" }) {
        const std::string error_message = parse_error_of(
            spec_with_character("name", "\"name\": \"" + name + "\""));
        EXPECT_EQ(error_message, "") << name;
    }
}

TEST(RotstoolFixtureSpec, RefusesNamesThatDifferOnlyInCase)
{
    const std::string text = spec_with_characters(
        character_object("", "") + ", " + character_object("name", R"("name": "testwiz")"));
    const std::string error_message = parse_error_of(text);
    EXPECT_TRUE(contains(error_message, "characters[1]: 'name': 'testwiz' is already the name of another character"))
        << error_message;
}

TEST(RotstoolFixtureSpec, RefusesAPasswordThePolicyRefuses)
{
    const std::string text = R"({ "account": { "email": "harness@example.com", "password": "short" },
        "characters": [ )" + character_object("", "") + " ] }";
    const std::string error_message = parse_error_of(text);
    std::string policy_message;
    ASSERT_FALSE(account::is_valid_password("short", &policy_message));
    EXPECT_EQ(error_message, "'account': 'password': " + policy_message);
}

TEST(RotstoolFixtureSpec, RefusesPracticesOutOfRange)
{
    // A character file stores practices in a byte.
    const int most_practices = std::numeric_limits<unsigned char>::max();
    for (const int practices : { 0, most_practices + 1 }) {
        const std::string error_message = parse_error_of(
            spec_with_character("", "\"skills\": { \"magic_missile\": " + std::to_string(practices) + " }"));
        const std::string expected = "'skills': 'magic_missile': " + std::to_string(practices) + " is outside 1 to "
            + std::to_string(most_practices);
        EXPECT_TRUE(contains(error_message, expected)) << practices << ": " << error_message;
    }
}

TEST(RotstoolFixtureSpec, RefusesAnEmptyCharacterList)
{
    const std::string error_message = parse_error_of(spec_with_characters(""));
    EXPECT_TRUE(contains(error_message, "'characters': the spec names no characters")) << error_message;
}

TEST(RotstoolFixtureSpec, ReadRefusesAFileOverTheLimit)
{
    const std::filesystem::path path = scratch_file_path("rotstool_fixture_spec_over_limit.json");
    ASSERT_FALSE(path.empty());
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string contents(static_cast<size_t>(FIXTURE_SPEC_BYTE_LIMIT + 1), ' ');
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::string contents;
    std::string error_message;
    EXPECT_FALSE(read_fixture_spec_file(path, contents, error_message));
    EXPECT_TRUE(contains(error_message, "is larger than " + std::to_string(FIXTURE_SPEC_BYTE_LIMIT) + " bytes"))
        << error_message;

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(RotstoolFixtureSpec, ReadReturnsTheFileContents)
{
    const std::filesystem::path path = scratch_file_path("rotstool_fixture_spec_contents.json");
    ASSERT_FALSE(path.empty());
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file << valid_spec;
    }

    std::string contents;
    std::string error_message;
    EXPECT_TRUE(read_fixture_spec_file(path, contents, error_message)) << error_message;
    EXPECT_EQ(contents, valid_spec);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(RotstoolFixtureSpec, ReadRefusesAMissingFile)
{
    const std::filesystem::path path = scratch_file_path("rotstool_fixture_spec_missing.json");
    ASSERT_FALSE(path.empty());
    std::error_code error;
    std::filesystem::remove(path, error);

    std::string contents;
    std::string error_message;
    EXPECT_FALSE(read_fixture_spec_file(path, contents, error_message));
    EXPECT_FALSE(error_message.empty());
}
