#include "../json_utils.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

TEST(JsonUtils, EscapesQuotesBackslashesAndControlCharacters)
{
    const std::string raw = "\"slash\\\\\n\t\r\b\f";
    EXPECT_EQ(json_utils::escape_json_string(raw), "\\\"slash\\\\\\\\\\n\\t\\r\\b\\f");
}

TEST(JsonUtils, EscapesOtherControlCharactersAsUnicodeEscapes)
{
    const std::string raw(1, '\v');
    EXPECT_EQ(json_utils::escape_json_string(raw), "\\u000b");
}

TEST(JsonUtils, ParsesTypedObjectProperties)
{
    const std::string json = R"({
        "name": "Aragorn",
        "level": 42,
        "trusted": true,
        "aliases": ["strider", "king"]
    })";

    json_utils::JsonReader reader(json);
    std::string name;
    int level = 0;
    bool trusted = false;
    std::vector<std::string> aliases;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object([&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&name, nested_error_message);
        if (key == "level")
            return nested_reader->parse_integer(&level, nested_error_message);
        if (key == "trusted")
            return nested_reader->parse_bool(&trusted, nested_error_message);
        if (key == "aliases")
            return nested_reader->parse_string_array(&aliases, nested_error_message);
        return nested_reader->skip_value(nested_error_message);
    },
        &error_message))
        << error_message;

    EXPECT_EQ(name, "Aragorn");
    EXPECT_EQ(level, 42);
    EXPECT_TRUE(trusted);
    EXPECT_EQ(aliases, (std::vector<std::string> { "strider", "king" }));
}

TEST(JsonUtils, SkipsUnknownNestedValuesWithoutBreakingKnownFields)
{
    const std::string json = R"({
        "ignore_object": { "nested": ["value", {"deeper": false}] },
        "ignore_array": [1, true, {"name": "ignored"}],
        "name": "Legolas"
    })";

    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object([&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&name, nested_error_message);
        return nested_reader->skip_value(nested_error_message);
    },
        &error_message))
        << error_message;

    EXPECT_EQ(name, "Legolas");
}

TEST(JsonUtils, RejectsTrailingCharactersAfterRootObject)
{
    json_utils::JsonReader reader("{\"name\":\"Aragorn\"} trailing");
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [](const std::string&, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, RejectsUnsupportedStringEscapes)
{
    json_utils::JsonReader reader("{\"name\":\"bad\\u263A\"}");
    std::string name;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, RejectsRawControlCharactersInsideStrings)
{
    const std::string json = std::string("{\"name\":\"bad\nnewline\"}");
    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, ParsesAsciiUnicodeEscapesProducedBySerializer)
{
    const std::string json = "{\"name\":\"line\\u000bbreak\"}";
    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;

    ASSERT_EQ(name.size(), 10u);
    EXPECT_EQ(name[4], '\v');
}

TEST(JsonUtils, RejectsIntegersOutsideIntRange)
{
    const std::string json = "{\"level\":2147483648}";
    json_utils::JsonReader reader(json);
    int level = 0;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "level")
                return nested_reader->parse_integer(&level, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_NE(error_message.find("out of range"), std::string::npos);
}

} // namespace
