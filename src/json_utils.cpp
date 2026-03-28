#include "json_utils.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <cctype>
#include <limits>

namespace json_utils {
namespace {

    char hex_digit(unsigned int value)
    {
        return static_cast<char>(value < 10 ? ('0' + value) : ('a' + (value - 10)));
    }

    bool parse_hex_digit(char character, unsigned int* value)
    {
        if (value == nullptr)
            return false;

        if (character >= '0' && character <= '9') {
            *value = static_cast<unsigned int>(character - '0');
            return true;
        }
        if (character >= 'a' && character <= 'f') {
            *value = 10u + static_cast<unsigned int>(character - 'a');
            return true;
        }
        if (character >= 'A' && character <= 'F') {
            *value = 10u + static_cast<unsigned int>(character - 'A');
            return true;
        }

        return false;
    }

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
    }

} // namespace

std::string escape_json_string(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);

    for (char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                escaped += "\\u00";
                escaped += hex_digit((static_cast<unsigned char>(character) >> 4) & 0x0f);
                escaped += hex_digit(static_cast<unsigned char>(character) & 0x0f);
            } else {
                escaped += character;
            }
            break;
        }
    }

    return escaped;
}

JsonReader::JsonReader(const std::string& input)
    : m_input(input)
{
}

bool JsonReader::parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    if (!parse_object(property_parser, error_message))
        return false;

    skip_whitespace();
    if (!is_at_end()) {
        set_error(error_message, "Unexpected trailing characters after JSON object.");
        return false;
    }

    return true;
}

bool JsonReader::parse_object(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    skip_whitespace();
    if (!consume('{')) {
        set_error(error_message, "Expected JSON object.");
        return false;
    }

    return parse_object_body(property_parser, error_message);
}

bool JsonReader::parse_array(const ArrayValueParser& value_parser, std::string* error_message)
{
    if (!consume('[')) {
        set_error(error_message, "Expected array value.");
        return false;
    }

    bool first_value = true;
    while (true) {
        skip_whitespace();
        if (consume(']'))
            return true;

        if (!first_value) {
            if (!consume(',')) {
                set_error(error_message, "Expected ',' between array values.");
                return false;
            }
            skip_whitespace();
        }

        if (!value_parser(this, error_message))
            return false;

        first_value = false;
    }
}

bool JsonReader::parse_string(std::string* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "String output parameter must not be null.");
        return false;
    }

    if (!consume('"')) {
        set_error(error_message, "Expected string value.");
        return false;
    }

    std::string parsed;
    while (!is_at_end()) {
        char character = m_input[m_position++];
        if (character == '"') {
            *value = parsed;
            return true;
        }

        if (character == '\\') {
            if (is_at_end()) {
                set_error(error_message, "Unexpected end of input in string escape sequence.");
                return false;
            }

            char escaped = m_input[m_position++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                parsed += escaped;
                break;
            case 'b':
                parsed += '\b';
                break;
            case 'f':
                parsed += '\f';
                break;
            case 'n':
                parsed += '\n';
                break;
            case 'r':
                parsed += '\r';
                break;
            case 't':
                parsed += '\t';
                break;
            case 'u': {
                if (m_position + 4 > m_input.size()) {
                    set_error(error_message, "Incomplete unicode escape sequence in JSON string.");
                    return false;
                }

                unsigned int code_point = 0;
                for (int index = 0; index < 4; ++index) {
                    unsigned int nibble = 0;
                    if (!parse_hex_digit(m_input[m_position + index], &nibble)) {
                        set_error(error_message, "Invalid unicode escape sequence in JSON string.");
                        return false;
                    }
                    code_point = (code_point << 4) | nibble;
                }
                m_position += 4;

                if (code_point > 0x7f) {
                    set_error(error_message, "Unsupported unicode escape sequence in JSON string.");
                    return false;
                }

                parsed += static_cast<char>(code_point);
                break;
            }
            default:
                set_error(error_message, "Unsupported escape sequence in JSON string.");
                return false;
            }
        } else {
            if (static_cast<unsigned char>(character) < 0x20) {
                set_error(error_message, "JSON strings must escape control characters.");
                return false;
            }
            parsed += character;
        }
    }

    set_error(error_message, "Unterminated JSON string.");
    return false;
}

bool JsonReader::parse_bool(bool* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Boolean output parameter must not be null.");
        return false;
    }

    if (match_literal("true")) {
        *value = true;
        return true;
    }

    if (match_literal("false")) {
        *value = false;
        return true;
    }

    set_error(error_message, "Expected boolean value.");
    return false;
}

bool JsonReader::parse_integer(int* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Integer output parameter must not be null.");
        return false;
    }

    long parsed = 0;
    if (!parse_long(&parsed, error_message))
        return false;

    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        set_error(error_message, "Integer value is out of range.");
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

bool JsonReader::parse_long(long* value, std::string* error_message)
{
    if (value == nullptr) {
        set_error(error_message, "Long output parameter must not be null.");
        return false;
    }

    if (is_at_end()) {
        set_error(error_message, "Expected integer value.");
        return false;
    }

    size_t start = m_position;
    if (m_input[m_position] == '-')
        ++m_position;

    if (m_position >= m_input.size() || !std::isdigit(static_cast<unsigned char>(m_input[m_position]))) {
        set_error(error_message, "Expected integer value.");
        return false;
    }

    while (m_position < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_position])))
        ++m_position;

    std::string number_text = m_input.substr(start, m_position - start);
    char* end_ptr = nullptr;
    errno = 0;
    long parsed = std::strtol(number_text.c_str(), &end_ptr, 10);
    if (errno != 0 || end_ptr == nullptr || *end_ptr != '\0') {
        set_error(error_message, "Invalid integer value.");
        return false;
    }

    *value = parsed;
    return true;
}

bool JsonReader::parse_string_array(std::vector<std::string>* values, std::string* error_message)
{
    if (values == nullptr) {
        set_error(error_message, "Array output parameter must not be null.");
        return false;
    }

    values->clear();
    return parse_array([values](JsonReader* reader, std::string* nested_error_message) {
        std::string value;
        if (!reader->parse_string(&value, nested_error_message))
            return false;
        values->push_back(value);
        return true;
    },
        error_message);
}

bool JsonReader::skip_value(std::string* error_message)
{
    skip_whitespace();
    if (is_at_end()) {
        set_error(error_message, "Expected JSON value.");
        return false;
    }

    char current = m_input[m_position];
    if (current == '"') {
        std::string ignored;
        return parse_string(&ignored, error_message);
    }

    if (current == '{') {
        ++m_position;
        return parse_object_body([](const std::string&, JsonReader* reader, std::string* nested_error_message) {
            return reader->skip_value(nested_error_message);
        },
            error_message);
    }

    if (current == '[') {
        ++m_position;
        bool first_value = true;
        while (true) {
            skip_whitespace();
            if (consume(']'))
                return true;

            if (!first_value) {
                if (!consume(',')) {
                    set_error(error_message, "Expected ',' between nested array values.");
                    return false;
                }
                skip_whitespace();
            }

            if (!skip_value(error_message))
                return false;

            first_value = false;
        }
    }

    if (current == 't' || current == 'f') {
        bool ignored = false;
        return parse_bool(&ignored, error_message);
    }

    if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
        long ignored = 0;
        return parse_long(&ignored, error_message);
    }

    set_error(error_message, "Unsupported JSON value.");
    return false;
}

bool JsonReader::parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message)
{
    bool first_property = true;
    while (true) {
        skip_whitespace();

        if (consume('}'))
            return true;

        if (!first_property) {
            if (!consume(',')) {
                set_error(error_message, "Expected ',' between object properties.");
                return false;
            }

            skip_whitespace();
        }

        std::string key;
        if (!parse_string(&key, error_message))
            return false;

        skip_whitespace();
        if (!consume(':')) {
            set_error(error_message, "Expected ':' after object key.");
            return false;
        }

        skip_whitespace();
        if (!property_parser(key, this, error_message))
            return false;

        first_property = false;
    }
}

bool JsonReader::consume(char expected)
{
    if (m_position >= m_input.size() || m_input[m_position] != expected)
        return false;

    ++m_position;
    return true;
}

bool JsonReader::match_literal(const char* literal)
{
    size_t literal_length = std::strlen(literal);
    if (m_input.compare(m_position, literal_length, literal) != 0)
        return false;

    m_position += literal_length;
    return true;
}

void JsonReader::skip_whitespace()
{
    while (m_position < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_position])))
        ++m_position;
}

bool JsonReader::is_at_end() const
{
    return m_position >= m_input.size();
}

} // namespace json_utils
