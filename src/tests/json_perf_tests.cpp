#include "../json_utils.h"

#include "../character_json.h"
#include "../color.h"
#include "../db.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Aggregates one value of every type the readers expose, so a JsonReader vs JsonReaderV2 parse of
// the same document can be compared field by field.
struct ParsedDocument {
    std::string name;
    int level = 0;
    long big = 0;
    bool flag_true = false;
    bool flag_false = false;
    std::vector<std::string> tags;
    int nested_x = 0;
    int nested_y = 0;
};

// Drives a full root-object parse through whichever reader Reader names; both readers share the
// public surface, so the same lambda body deduces against each reader's own nested typedefs.
template <class Reader>
bool parse_document(const std::string& json, ParsedDocument* out, std::string* error_message)
{
    Reader reader(json);
    return reader.parse_root_object([out](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&out->name, nested_error_message);
        if (key == "level")
            return nested_reader->parse_integer(&out->level, nested_error_message);
        if (key == "big")
            return nested_reader->parse_long(&out->big, nested_error_message);
        if (key == "flag_true")
            return nested_reader->parse_bool(&out->flag_true, nested_error_message);
        if (key == "flag_false")
            return nested_reader->parse_bool(&out->flag_false, nested_error_message);
        if (key == "tags")
            return nested_reader->parse_string_array(&out->tags, nested_error_message);
        if (key == "nested") {
            return nested_reader->parse_object([out](const std::string& nested_key, Reader* inner_reader, std::string* inner_error_message) {
                if (nested_key == "x")
                    return inner_reader->parse_integer(&out->nested_x, inner_error_message);
                if (nested_key == "y")
                    return inner_reader->parse_integer(&out->nested_y, inner_error_message);
                return inner_reader->skip_value(inner_error_message);
            },
                nested_error_message);
        }
        return nested_reader->skip_value(nested_error_message);
    },
        error_message);
}

const char* const kSampleJson = R"JSON({
  "name": "Frodo \"the\" Brave\n\tBaggins",
  "level": 42,
  "big": 2147483000,
  "flag_true": true,
  "flag_false": false,
  "tags": ["alpha", "beta", "gamma"],
  "nested": { "x": -5, "y": 10 },
  "ignored": [1, 2, {"z": 3}]
})JSON";

} // namespace

TEST(JsonPerf, JsonReaderV2MatchesBaselineParse)
{
    const std::string json = kSampleJson;

    ParsedDocument v1;
    std::string v1_error;
    ASSERT_TRUE(parse_document<json_utils::JsonReader>(json, &v1, &v1_error)) << v1_error;

    ParsedDocument v2;
    std::string v2_error;
    ASSERT_TRUE(parse_document<json_utils::JsonReaderV2>(json, &v2, &v2_error)) << v2_error;

    EXPECT_EQ(v1.name, v2.name);
    EXPECT_EQ(v1.level, v2.level);
    EXPECT_EQ(v1.big, v2.big);
    EXPECT_EQ(v1.flag_true, v2.flag_true);
    EXPECT_EQ(v1.flag_false, v2.flag_false);
    EXPECT_EQ(v1.tags, v2.tags);
    EXPECT_EQ(v1.nested_x, v2.nested_x);
    EXPECT_EQ(v1.nested_y, v2.nested_y);

    // Pin the decoded values too, so a matching-but-wrong parse can't slip through.
    EXPECT_EQ("Frodo \"the\" Brave\n\tBaggins", v2.name);
    EXPECT_EQ(42, v2.level);
    EXPECT_EQ(2147483000L, v2.big);
    EXPECT_TRUE(v2.flag_true);
    EXPECT_FALSE(v2.flag_false);
    ASSERT_EQ(3u, v2.tags.size());
    EXPECT_EQ("beta", v2.tags[1]);
    EXPECT_EQ(-5, v2.nested_x);
    EXPECT_EQ(10, v2.nested_y);
}

TEST(JsonPerf, AppendEscapedMatchesEscapeJsonString)
{
    const std::string inputs[] = {
        "",
        "plain_slug_key",
        "needs \" quote",
        "needs \\ backslash",
        std::string("control\x01\x1f end", 13),
        "tab\tnewline\nreturn\rmix",
    };
    for (const std::string& input : inputs) {
        std::string appended = "PREFIX:";
        json_utils::append_escaped_json_string(appended, input);
        EXPECT_EQ(std::string("PREFIX:") + json_utils::escape_json_string(input), appended);
    }
}

namespace {

// Builds a valid char_file_u to exercise the deserialize paths. heavy=true populates every skill
// slot (and all talks) to stress the per-key index lookups that dominate load cost. Mirrors the
// proven-round-tripping fixture in save_benchmark_tests.cpp; no affects are set, so affected_flags
// stay trivially consistent.
char_file_u make_perf_character(bool heavy)
{
    char_file_u stored {};
    std::snprintf(stored.name, sizeof(stored.name), "%s", "aragorn");
    std::snprintf(stored.title, sizeof(stored.title), "%s", "the Ranger");
    std::snprintf(stored.description, sizeof(stored.description), "%s", "A ranger from the north.");
    stored.sex = SEX_MALE;
    stored.race = RACE_HUMAN;
    stored.bodytype = 1;
    stored.level = 12;
    stored.language = LANG_HUMAN;
    stored.birth = 1700000000;
    stored.played = 456;
    stored.weight = 190;
    stored.height = 72;
    stored.hometown = 7;
    stored.last_logon = 1700000100;
    stored.points.gold = 1234;
    stored.points.exp = 5678;
    stored.specials2.idnum = 4242;
    stored.specials2.load_room = 3001;
    stored.specials2.tactics = TACTICS_BERSERK;
    stored.specials2.shooting = SHOOTING_FAST;
    stored.specials2.casting = CASTING_SLOW;
    stored.specials2.two_handed = 1;
    stored.profs.colors[COLOR_MAGIC] = CBMAG;
    stored.profs.prof_level[PROF_WARRIOR] = 12;
    stored.profs.prof_coof[PROF_WARRIOR] = 34;
    stored.skills[0] = 1;
    stored.skills[1] = 10;
    stored.skills[2] = 95;
    stored.talks[0] = 100;
    stored.talks[1] = 75;
    if (heavy) {
        for (int index = 0; index < MAX_SKILLS; ++index)
            stored.skills[index] = (index % 50) + 1;
        for (int index = 0; index < MAX_TOUNGE; ++index)
            stored.talks[index] = (index * 30) + 10;
    }
    return stored;
}

using DeserializeFn = bool (*)(const std::string&, character_json::CharacterData*, std::string*);

// Deserialize json with fn, then apply into a zeroed char_file_u so two paths can be byte-compared.
bool deserialize_and_apply(DeserializeFn fn, const std::string& json, char_file_u* out, std::string* error)
{
    character_json::CharacterData character;
    if (!fn(json, &character, error))
        return false;
    return character_json::apply_character_data_to_store(character, out, error);
}

// Serialize a v1 char of the given tier, then assert the v2 deserializer reaches the SAME OUTCOME as
// v1 on identical input: success with a byte-identical stored struct, or the identical rejection.
// (The all-skills "heavy" tier intentionally produces duplicate skill keys -- some game-table skill
// names slugify to the same key -- which v1 itself rejects; v2 must reject identically. Comparing
// outcomes rather than asserting success makes this a stronger equivalence gate that also covers the
// rejection path.)
void expect_deserialize_matches_v1(DeserializeFn v2_fn, bool heavy)
{
    const char_file_u stored = make_perf_character(heavy);
    const character_json::CharacterData character = character_json::character_data_from_store(stored);
    const std::string json = character_json::serialize_character_to_json(character);

    char_file_u s1 {};
    char_file_u s2 {};
    std::string e1;
    std::string e2;
    const bool ok1 = deserialize_and_apply(&character_json::deserialize_character_from_json, json, &s1, &e1);
    const bool ok2 = deserialize_and_apply(v2_fn, json, &s2, &e2);
    EXPECT_EQ(ok1, ok2) << "v1 err='" << e1 << "' v2 err='" << e2 << "'";
    EXPECT_EQ(e1, e2);
    if (ok1 && ok2)
        EXPECT_EQ(0, std::memcmp(&s1, &s2, sizeof(char_file_u)));
}

} // namespace

TEST(JsonPerf, DeserializeV2aMatchesV1)
{
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2a, /*heavy=*/false);
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2a, /*heavy=*/true);
}

TEST(JsonPerf, DeserializeV2bMatchesV1)
{
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2b, /*heavy=*/false);
    expect_deserialize_matches_v1(&character_json::deserialize_character_from_json_v2b, /*heavy=*/true);
}

TEST(JsonPerf, DeserializeV2bMatchesV2a)
{
    // v2a (JsonReader + memoized) vs v2b (JsonReaderV2 + memoized): the only difference is the
    // reader, so the applied structs must match byte-for-byte across both tiers.
    for (bool heavy : { false, true }) {
        const char_file_u stored = make_perf_character(heavy);
        const character_json::CharacterData character = character_json::character_data_from_store(stored);
        const std::string json = character_json::serialize_character_to_json(character);

        char_file_u sa {};
        char_file_u sb {};
        std::string ea;
        std::string eb;
        const bool oka = deserialize_and_apply(&character_json::deserialize_character_from_json_v2a, json, &sa, &ea);
        const bool okb = deserialize_and_apply(&character_json::deserialize_character_from_json_v2b, json, &sb, &eb);
        EXPECT_EQ(oka, okb) << "heavy=" << heavy << " ea='" << ea << "' eb='" << eb << "'";
        EXPECT_EQ(ea, eb) << "heavy=" << heavy;
        if (oka && okb)
            EXPECT_EQ(0, std::memcmp(&sa, &sb, sizeof(char_file_u))) << "heavy=" << heavy;
    }
}

TEST(JsonPerf, MemoizedSkillTalkLookupMatchesSlowScan)
{
    // The memoized lookups are file-local to character_json.cpp, so they are exercised through the
    // public dispatch: v1 routes skill/talk keys through the slow linear scan, v2a routes them
    // through the memoized maps. For every index we serialize a single-entry skills/talks object
    // (its canonical key) and confirm v1 and v2a map it back to the SAME index.
    const character_json::CharacterData base = character_json::character_data_from_store(make_perf_character(false));

    for (int index = 0; index < MAX_SKILLS; ++index) {
        character_json::CharacterData character = base;
        character.skills.assign(MAX_SKILLS, 0);
        character.talks.assign(MAX_TOUNGE, 0);
        character.skills[index] = 7;
        const std::string json = character_json::serialize_character_to_json(character);

        character_json::CharacterData v1;
        character_json::CharacterData v2a;
        std::string e1;
        std::string e2;
        ASSERT_TRUE(character_json::deserialize_character_from_json(json, &v1, &e1)) << "skill " << index << ": " << e1;
        ASSERT_TRUE(character_json::deserialize_character_from_json_v2a(json, &v2a, &e2)) << "skill " << index << ": " << e2;
        // The memoized map and the slow scan must map this key to the SAME index (colliding skill
        // slugs both resolve to the lowest index -- so we compare the decoded vectors, not index N).
        EXPECT_EQ(v1.skills, v2a.skills) << "skill index " << index;
    }

    for (int index = 0; index < MAX_TOUNGE; ++index) {
        character_json::CharacterData character = base;
        character.skills.assign(MAX_SKILLS, 0);
        character.talks.assign(MAX_TOUNGE, 0);
        character.talks[index] = 9;
        const std::string json = character_json::serialize_character_to_json(character);

        character_json::CharacterData v1;
        character_json::CharacterData v2a;
        std::string e1;
        std::string e2;
        ASSERT_TRUE(character_json::deserialize_character_from_json(json, &v1, &e1)) << "talk " << index << ": " << e1;
        ASSERT_TRUE(character_json::deserialize_character_from_json_v2a(json, &v2a, &e2)) << "talk " << index << ": " << e2;
        EXPECT_EQ(v1.talks, v2a.talks) << "talk index " << index;
    }
}

TEST(JsonPerf, UnknownSkillKeyFailsIdenticallyForV1AndV2a)
{
    // Unknown key -> index -1 -> "Unknown skill key" error, the same for the slow scan (v1) and the
    // memoized map (v2a). Build a skills-empty character (serializes `"skills": {}`), inject a bogus
    // key, and assert both paths reject it with the identical message.
    character_json::CharacterData character = character_json::character_data_from_store(make_perf_character(false));
    character.skills.assign(MAX_SKILLS, 0);
    character.talks.assign(MAX_TOUNGE, 0);
    std::string json = character_json::serialize_character_to_json(character);

    const std::string empty_skills = "\"skills\": {}";
    const std::string bogus_skills = "\"skills\": {\"totally_not_a_real_skill\": 5}";
    const size_t at = json.find(empty_skills);
    ASSERT_NE(std::string::npos, at);
    json.replace(at, empty_skills.size(), bogus_skills);

    character_json::CharacterData parsed;
    std::string e1;
    std::string e2;
    EXPECT_FALSE(character_json::deserialize_character_from_json(json, &parsed, &e1));
    EXPECT_FALSE(character_json::deserialize_character_from_json_v2a(json, &parsed, &e2));
    EXPECT_EQ(e1, e2);
}
