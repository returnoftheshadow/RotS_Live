#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <functional>
#include <string>
#include <vector>

namespace json_utils {

std::string escape_json_string(const std::string& value);

// Fast-path JSON string escaper for serialize v2. Scans value once; if it contains no character
// that needs escaping ('"', '\\', or a control char < 0x20) the bytes are appended to out verbatim,
// otherwise it falls back to the same escaping escape_json_string produces. Appends (no return copy).
void append_escaped_json_string(std::string& out, const std::string& value);

class JsonReader {
public:
    using ObjectPropertyParser = std::function<bool(const std::string&, JsonReader*, std::string*)>;
    using ArrayValueParser = std::function<bool(JsonReader*, std::string*)>;

    explicit JsonReader(const std::string& input);

    bool parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_array(const ArrayValueParser& value_parser, std::string* error_message);
    bool parse_string(std::string* value, std::string* error_message);
    bool parse_bool(bool* value, std::string* error_message);
    bool parse_integer(int* value, std::string* error_message);
    bool parse_long(long* value, std::string* error_message);
    bool parse_string_array(std::vector<std::string>* values, std::string* error_message);
    bool skip_value(std::string* error_message);

private:
    bool parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool consume(char expected);
    bool match_literal(const char* literal);
    void skip_whitespace();
    bool is_at_end() const;

    const std::string& m_input;
    size_t m_position = 0;
};

// Optimized drop-in equivalent of JsonReader used by the v2 character deserializers: identical
// public surface and observable behavior, but lower-allocation internals (from_chars integer parse,
// move-out / no-escape-fast-path strings, branchless whitespace/digit tests, strlen-free literal
// match). Kept as a separate non-virtual class so JsonReader stays an untouched measurement baseline.
class JsonReaderV2 {
public:
    using ObjectPropertyParser = std::function<bool(const std::string&, JsonReaderV2*, std::string*)>;
    using ArrayValueParser = std::function<bool(JsonReaderV2*, std::string*)>;

    explicit JsonReaderV2(const std::string& input);

    bool parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_array(const ArrayValueParser& value_parser, std::string* error_message);
    bool parse_string(std::string* value, std::string* error_message);
    bool parse_bool(bool* value, std::string* error_message);
    bool parse_integer(int* value, std::string* error_message);
    bool parse_long(long* value, std::string* error_message);
    bool parse_string_array(std::vector<std::string>* values, std::string* error_message);
    bool skip_value(std::string* error_message);

private:
    bool parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool consume(char expected);
    bool match_literal(const char* literal, size_t literal_length);
    void skip_whitespace();
    bool is_at_end() const;

    // Immutable view of the JSON text being parsed; owned by the caller and must outlive this reader.
    const std::string& m_input;
    // Cursor into m_input of the next unconsumed character; advanced by every consume/parse step.
    size_t m_position = 0;
};

} // namespace json_utils

#endif
