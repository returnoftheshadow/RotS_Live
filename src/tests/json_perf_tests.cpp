#include "../json_utils.h"

#include <gtest/gtest.h>

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
