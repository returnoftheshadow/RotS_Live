#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <functional>
#include <string>
#include <vector>

namespace json_utils {

std::string escape_json_string(const std::string& value);

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

} // namespace json_utils

#endif
