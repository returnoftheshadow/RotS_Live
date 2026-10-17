#include "fixture_spec_parser.h"

#include "account_management.h"
#include "character_json.h"
#include "json_utils.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// The races the creation menu offers, as RACE_* numbers; each one's name is pc_races[race].
constexpr std::array<int, 10> OFFERED_RACES = {
    RACE_HUMAN, RACE_DWARF, RACE_WOOD, RACE_HOBBIT, RACE_URUK,
    RACE_ORC, RACE_MAGUS, RACE_BEORNING, RACE_OLOGHAI, RACE_HARADRIM,
};

// A standard class by the name the creation menu shows and the letter that picks it.
struct NamedClass {
    // The class name as typed in a spec, lower case.
    std::string_view name;
    // The creation-menu letter.
    char letter;
};

constexpr std::array<NamedClass, 10> NAMED_CLASSES = { {
    { "mystic", 't' }, { "ranger", 'r' }, { "warrior", 'w' }, { "mage", 'm' }, { "conjurer", 'n' },
    { "wizard", 'i' }, { "healer", 'h' }, { "swashbuckler", 's' }, { "barbarian", 'b' }, { "adventurer", 'a' },
} };

// Most practice sessions one skill can store, because a character file keeps them in a byte.
constexpr int MAX_PRACTICES = std::numeric_limits<unsigned char>::max();

// Most a set value can be, because abilities are stored as sh_int.
constexpr int MAX_SET_VALUE = std::numeric_limits<sh_int>::max();

// Most bytes read_fixture_spec_file() reads: one past the limit, so a larger file is detected
// without reading it in full.
constexpr std::uintmax_t FIXTURE_SPEC_READ_LIMIT = FIXTURE_SPEC_BYTE_LIMIT + 1;

static_assert(FIXTURE_SPEC_READ_LIMIT <= static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()),
    "the read limit must fit the stream's read count");
static_assert(FIXTURE_SPEC_READ_LIMIT <= std::numeric_limits<std::size_t>::max(),
    "the read limit must fit a string's size");

using json_utils::JsonReader;

std::string lower_case(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) -> char {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

// Records key as seen in the current object; refuses a key the object already had.
bool note_new_key(const std::string& key, std::set<std::string>& seen_keys, std::string& out_error_message)
{
    if (!seen_keys.insert(key).second) {
        out_error_message = "Repeated key '" + key + "'.";
        return false;
    }
    return true;
}

// Prefixes an error found inside the value of key with the key's name.
void prefix_with_key(std::string_view key, std::string& error_message)
{
    error_message.insert(0, "'" + std::string(key) + "': ");
}

bool read_string_value(JsonReader& reader, std::string_view key, std::string& out_value, std::string& out_error_message)
{
    if (!reader.parse_string(&out_value, &out_error_message)) {
        prefix_with_key(key, out_error_message);
        return false;
    }
    return true;
}

// Reads an integer and refuses one outside minimum to maximum.
bool read_bounded_integer(JsonReader& reader, std::string_view key, int minimum, int maximum, int& out_value,
    std::string& out_error_message)
{
    int value = 0;
    if (!reader.parse_integer(&value, &out_error_message)) {
        prefix_with_key(key, out_error_message);
        return false;
    }
    if (value < minimum || value > maximum) {
        out_error_message = std::to_string(value) + " is outside " + std::to_string(minimum) + " to "
            + std::to_string(maximum) + ".";
        prefix_with_key(key, out_error_message);
        return false;
    }
    out_value = value;
    return true;
}

bool resolve_race(std::string_view race_name, int& out_race, std::string& out_error_message)
{
    const std::string wanted = lower_case(race_name);
    for (const int race : OFFERED_RACES) {
        if (lower_case(pc_races[race]) == wanted) {
            out_race = race;
            return true;
        }
    }
    out_error_message = "'race': '" + std::string(race_name) + "' is not a race the creation menu offers.";
    return false;
}

bool resolve_sex(std::string_view sex_name, int& out_sex, std::string& out_error_message)
{
    const std::string wanted = lower_case(sex_name);
    if (wanted == "male") {
        out_sex = SEX_MALE;
        return true;
    }
    if (wanted == "female") {
        out_sex = SEX_FEMALE;
        return true;
    }
    out_error_message = "'sex': '" + std::string(sex_name) + "' is not 'male' or 'female'.";
    return false;
}

bool resolve_class(
    std::string_view class_name, std::optional<CreationPoints>& out_creation_points, std::string& out_error_message)
{
    const std::string wanted = lower_case(class_name);
    const auto named_class = std::find_if(NAMED_CLASSES.begin(), NAMED_CLASSES.end(),
        [&wanted](const NamedClass& candidate) -> bool { return candidate.name == wanted; });
    if (named_class == NAMED_CLASSES.end()) {
        out_error_message = "'class': '" + std::string(class_name) + "' is not a class the creation menu offers.";
        return false;
    }

    std::optional<CreationPoints> creation_points = CreationPoints::standard_class(named_class->letter);
    if (!creation_points) {
        out_error_message = "'class': no standard class has the menu letter '" + std::string(1, named_class->letter)
            + "' that '" + std::string(class_name) + "' maps to.";
        return false;
    }
    out_creation_points = creation_points;
    return true;
}

// Returns true when name is MIN_NAME_LENGTH to MAX_NAME_LENGTH ASCII letters, the names the
// creation dialogue accepts, checked as written so spaces are not trimmed away.
bool is_letters_only_name(std::string_view name)
{
    if (name.size() < static_cast<std::size_t>(MIN_NAME_LENGTH)
        || name.size() > static_cast<std::size_t>(MAX_NAME_LENGTH)) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) -> bool {
        return std::isalpha(character) != 0;
    });
}

bool parse_account_object(JsonReader& reader, FixtureSpec& out_spec, std::string& out_error_message)
{
    std::set<std::string> seen_keys;
    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &out_spec, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        if (key == "email") {
            return read_string_value(*property_reader, key, out_spec.email, out_error_message);
        }
        if (key == "password") {
            return read_string_value(*property_reader, key, out_spec.password, out_error_message);
        }
        out_error_message = "Unknown key '" + key + "'.";
        return false;
    };
    if (!reader.parse_object(parse_property, &out_error_message)) {
        return false;
    }

    for (const char* required_key : { "email", "password" }) {
        if (seen_keys.count(required_key) == 0) {
            out_error_message = "Missing key '" + std::string(required_key) + "'.";
            return false;
        }
    }

    std::string policy_message;
    if (!account::is_valid_email(out_spec.email, &policy_message)) {
        out_error_message = "'email': " + policy_message;
        return false;
    }
    if (!account::is_valid_password(out_spec.password, &policy_message)) {
        out_error_message = "'password': " + policy_message;
        return false;
    }
    return true;
}

bool parse_points_object(
    JsonReader& reader, std::optional<CreationPoints>& out_creation_points, std::string& out_error_message)
{
    constexpr std::array<std::pair<std::string_view, int>, 4> profession_keys = { {
        { "mage", PROF_MAGE },
        { "mystic", PROF_CLERIC },
        { "ranger", PROF_RANGER },
        { "warrior", PROF_WARRIOR },
    } };

    std::set<std::string> seen_keys;
    CreationPoints::Split split {};
    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &split, &profession_keys, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        for (const std::pair<std::string_view, int>& profession_key : profession_keys) {
            if (key == profession_key.first) {
                if (!property_reader->parse_integer(&split[profession_key.second], &out_error_message)) {
                    prefix_with_key(key, out_error_message);
                    return false;
                }
                return true;
            }
        }
        out_error_message = "Unknown key '" + key + "'.";
        return false;
    };
    if (!reader.parse_object(parse_property, &out_error_message)) {
        return false;
    }

    std::optional<CreationPoints> creation_points = CreationPoints::custom(split, out_error_message);
    if (!creation_points) {
        return false;
    }
    out_creation_points = creation_points;
    return true;
}

bool parse_skills_object(
    JsonReader& reader, std::vector<std::pair<int, int>>& out_skill_practices, std::string& out_error_message)
{
    std::set<std::string> seen_keys;
    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &out_skill_practices, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        const int skill_index = character_json::skill_index_for_file_key(key);
        // A slot with no skill still has a fallback key in character files; it names nothing to practise.
        if (skill_index < 0 || get_skill_array()[skill_index].name[0] == '\0') {
            out_error_message = "Unknown skill key '" + key + "'.";
            return false;
        }
        int practices = 0;
        if (!read_bounded_integer(*property_reader, key, 1, MAX_PRACTICES, practices, out_error_message)) {
            return false;
        }
        out_skill_practices.emplace_back(skill_index, practices);
        return true;
    };
    return reader.parse_object(parse_property, &out_error_message);
}

bool parse_set_object(JsonReader& reader, FixtureSpec::Character& out_character, std::string& out_error_message)
{
    std::set<std::string> seen_keys;
    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &out_character, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        std::optional<int>* target = nullptr;
        if (key == "hit") {
            target = &out_character.hit;
        } else if (key == "mana") {
            target = &out_character.mana;
        } else if (key == "move") {
            target = &out_character.move;
        } else {
            out_error_message = "Unknown key '" + key + "'.";
            return false;
        }
        int value = 0;
        if (!read_bounded_integer(*property_reader, key, 1, MAX_SET_VALUE, value, out_error_message)) {
            return false;
        }
        *target = value;
        return true;
    };
    return reader.parse_object(parse_property, &out_error_message);
}

bool parse_character_object(JsonReader& reader, FixtureSpec::Character& out_character, std::string& out_error_message)
{
    std::set<std::string> seen_keys;
    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &out_character, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        if (key == "name") {
            return read_string_value(*property_reader, key, out_character.name, out_error_message);
        }
        if (key == "race") {
            std::string race_name;
            return read_string_value(*property_reader, key, race_name, out_error_message)
                && resolve_race(race_name, out_character.race, out_error_message);
        }
        if (key == "sex") {
            std::string sex_name;
            return read_string_value(*property_reader, key, sex_name, out_error_message)
                && resolve_sex(sex_name, out_character.sex, out_error_message);
        }
        if (key == "class") {
            std::string class_name;
            return read_string_value(*property_reader, key, class_name, out_error_message)
                && resolve_class(class_name, out_character.creation_points, out_error_message);
        }
        if (key == "points") {
            if (!parse_points_object(*property_reader, out_character.creation_points, out_error_message)) {
                prefix_with_key(key, out_error_message);
                return false;
            }
            return true;
        }
        if (key == "level") {
            return read_bounded_integer(*property_reader, key, 1, LEVEL_IMPL, out_character.level, out_error_message);
        }
        if (key == "load_room") {
            return read_bounded_integer(*property_reader, key, std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max(), out_character.load_room, out_error_message);
        }
        if (key == "skills") {
            if (!parse_skills_object(*property_reader, out_character.skill_practices, out_error_message)) {
                prefix_with_key(key, out_error_message);
                return false;
            }
            return true;
        }
        if (key == "set") {
            if (!parse_set_object(*property_reader, out_character, out_error_message)) {
                prefix_with_key(key, out_error_message);
                return false;
            }
            return true;
        }
        out_error_message = "Unknown key '" + key + "'.";
        return false;
    };
    if (!reader.parse_object(parse_property, &out_error_message)) {
        return false;
    }

    for (const char* required_key : { "name", "race", "sex", "level", "load_room" }) {
        if (seen_keys.count(required_key) == 0) {
            out_error_message = "Missing key '" + std::string(required_key) + "'.";
            return false;
        }
    }
    const bool has_class = seen_keys.count("class") != 0;
    const bool has_points = seen_keys.count("points") != 0;
    if (has_class && has_points) {
        out_error_message = "Give 'class' or 'points', not both.";
        return false;
    }
    if (!has_class && !has_points) {
        out_error_message = "Missing key 'class' or 'points'.";
        return false;
    }

    if (!is_letters_only_name(out_character.name)) {
        out_error_message = "'name': '" + out_character.name + "' is not " + std::to_string(MIN_NAME_LENGTH) + " to "
            + std::to_string(MAX_NAME_LENGTH) + " letters.";
        return false;
    }
    return true;
}

// Reads up to buffer.size() - offset bytes from file into buffer at offset; returns the count read.
std::size_t read_into(std::ifstream& file, std::string& buffer, std::size_t offset)
{
    // buffer never exceeds FIXTURE_SPEC_READ_LIMIT, which the static_asserts above prove fits a
    // std::streamsize.
    file.read(buffer.data() + offset, static_cast<std::streamsize>(buffer.size() - offset));
    return static_cast<std::size_t>(file.gcount());
}

} // namespace

bool read_fixture_spec_file(
    const std::filesystem::path& path, std::string& out_contents, std::string& out_error_message)
{
    // A directory opens and reads as empty on some platforms, so it is refused by name here.
    std::error_code status_error;
    const std::filesystem::file_status status = std::filesystem::status(path, status_error);
    if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status)) {
        out_error_message = "Fixture spec '" + path.string() + "' is not a file.";
        return false;
    }

    std::error_code size_error;
    const std::uintmax_t size_hint = std::filesystem::file_size(path, size_error);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        out_error_message = "Cannot open fixture spec '" + path.string() + "'.";
        return false;
    }

    // The size is only a hint: the file can change before it is read, so a read that fills the
    // hinted size reads on up to the limit.
    std::uintmax_t initial_size = FIXTURE_SPEC_READ_LIMIT;
    if (!size_error && size_hint < FIXTURE_SPEC_READ_LIMIT) {
        initial_size = size_hint;
    }
    std::string contents(static_cast<std::size_t>(initial_size), '\0');
    std::size_t bytes_read = read_into(file, contents, 0);
    if (bytes_read == contents.size() && contents.size() < FIXTURE_SPEC_READ_LIMIT) {
        contents.resize(static_cast<std::size_t>(FIXTURE_SPEC_READ_LIMIT));
        bytes_read += read_into(file, contents, bytes_read);
    }
    if (file.bad()) {
        out_error_message = "Cannot read fixture spec '" + path.string() + "'.";
        return false;
    }
    if (bytes_read > FIXTURE_SPEC_BYTE_LIMIT) {
        out_error_message = "Fixture spec '" + path.string() + "' is larger than "
            + std::to_string(FIXTURE_SPEC_BYTE_LIMIT) + " bytes.";
        return false;
    }

    contents.resize(bytes_read);
    out_contents = std::move(contents);
    return true;
}

bool parse_fixture_spec(const std::string& json, FixtureSpec& out_spec, std::string& out_error_message)
{
    FixtureSpec spec;
    std::set<std::string> seen_keys;
    std::set<std::string> lower_case_names;
    // Set when the error came from inside one character, which already names it.
    bool character_failed = false;

    const JsonReader::ArrayValueParser parse_character
        = [&spec, &lower_case_names, &character_failed, &out_error_message](
              JsonReader* character_reader, std::string*) -> bool {
        const std::string index_prefix = "characters[" + std::to_string(spec.characters.size()) + "]: ";
        FixtureSpec::Character character;
        if (!parse_character_object(*character_reader, character, out_error_message)) {
            out_error_message.insert(0, index_prefix);
            character_failed = true;
            return false;
        }
        if (!lower_case_names.insert(lower_case(character.name)).second) {
            out_error_message = index_prefix + "'name': '" + character.name
                + "' is already the name of another character in the spec, regardless of case.";
            character_failed = true;
            return false;
        }
        spec.characters.push_back(std::move(character));
        return true;
    };

    const JsonReader::ObjectPropertyParser parse_property
        = [&seen_keys, &spec, &parse_character, &character_failed, &out_error_message](
              const std::string& key, JsonReader* property_reader, std::string*) -> bool {
        if (!note_new_key(key, seen_keys, out_error_message)) {
            return false;
        }
        if (key == "account") {
            if (!parse_account_object(*property_reader, spec, out_error_message)) {
                prefix_with_key(key, out_error_message);
                return false;
            }
            return true;
        }
        if (key == "characters") {
            if (!property_reader->parse_array(parse_character, &out_error_message)) {
                if (!character_failed) {
                    prefix_with_key(key, out_error_message);
                }
                return false;
            }
            return true;
        }
        out_error_message = "Unknown key '" + key + "'.";
        return false;
    };

    JsonReader reader(json);
    if (!reader.parse_root_object(parse_property, &out_error_message)) {
        return false;
    }

    for (const char* required_key : { "account", "characters" }) {
        if (seen_keys.count(required_key) == 0) {
            out_error_message = "Missing key '" + std::string(required_key) + "'.";
            return false;
        }
    }
    if (spec.characters.empty()) {
        out_error_message = "'characters': the spec names no characters.";
        return false;
    }

    out_spec = std::move(spec);
    return true;
}
