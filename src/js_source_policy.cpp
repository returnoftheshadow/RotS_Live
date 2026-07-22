#include "js_source_policy.h"

#include <cctype>
#include <set>
#include <string>

namespace {

bool is_identifier_start(unsigned char ch) { return std::isalpha(ch) || ch == '_' || ch == '$'; }

bool is_identifier_continue(unsigned char ch)
{
    return std::isalnum(ch) || ch == '_' || ch == '$';
}

void add_violation(std::vector<JsSourcePolicyViolation>& violations, const std::string& message,
    const std::string& token = {})
{
    violations.push_back({ message, token });
}

bool is_forbidden_property_name(const std::string& property)
{
    return property == "eval" || property == "constructor" || property == "Function"
        || property == "AsyncFunction" || property == "GeneratorFunction"
        || property == "AsyncGeneratorFunction";
}

bool is_quoted_property_access(const std::string& source, std::size_t index, std::string* property,
    std::size_t* end_index, bool* had_escape)
{
    *had_escape = false;
    if (source[index] != '[')
        return false;
    ++index;
    while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index])))
        ++index;
    if (index >= source.size() || (source[index] != '\'' && source[index] != '"' && source[index] != '`'))
        return false;

    const char quote = source[index++];
    std::string parsed;
    while (index < source.size()) {
        const char ch = source[index];
        if (ch == '\\') {
            *had_escape = true;
            if (index + 1 >= source.size())
                return false;
            parsed.push_back(source[index + 1]);
            index += 2;
            continue;
        }
        if (ch == quote) {
            ++index;
            while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index])))
                ++index;
            if (index < source.size() && source[index] == ']') {
                *property = parsed;
                *end_index = index + 1;
                return true;
            }
            return false;
        }
        parsed.push_back(ch);
        ++index;
    }
    return false;
}

std::size_t skip_whitespace_and_comments(const std::string& source, std::size_t index)
{
    while (index < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[index]))) {
            ++index;
            continue;
        }
        if (index + 1 < source.size() && source[index] == '/' && source[index + 1] == '/') {
            index += 2;
            while (index < source.size() && source[index] != '\n' && source[index] != '\r')
                ++index;
            continue;
        }
        if (index + 1 < source.size() && source[index] == '/' && source[index + 1] == '*') {
            index += 2;
            while (index + 1 < source.size() && !(source[index] == '*' && source[index + 1] == '/'))
                ++index;
            if (index + 1 < source.size())
                index += 2;
            continue;
        }
        return index;
    }
    return index;
}

std::size_t skip_regex_literal(const std::string& source, std::size_t index)
{
    ++index;
    bool in_character_class = false;
    while (index < source.size()) {
        const char ch = source[index];
        if (ch == '\\') {
            index += index + 1 < source.size() ? 2 : 1;
            continue;
        }
        if (ch == '[') {
            in_character_class = true;
            ++index;
            continue;
        }
        if (ch == ']' && in_character_class) {
            in_character_class = false;
            ++index;
            continue;
        }
        if (ch == '/' && !in_character_class) {
            ++index;
            while (index < source.size()
                && is_identifier_continue(static_cast<unsigned char>(source[index])))
                ++index;
            return index;
        }
        if (ch == '\n' || ch == '\r')
            return index;
        ++index;
    }
    return index;
}

} // namespace

std::vector<JsSourcePolicyViolation> js_source_policy_validate(const std::string& source)
{
    std::vector<JsSourcePolicyViolation> violations;
    if (source.find("sourceMappingURL") != std::string::npos
        || source.find("sourceURL") != std::string::npos)
        add_violation(violations, "compiled JavaScript must not include source map references");

    const std::set<std::string> forbidden_tokens = {
        "import",
        "eval",
        "Function",
        "AsyncFunction",
        "GeneratorFunction",
        "AsyncGeneratorFunction",
        "async",
        "Promise",
        "setTimeout",
        "setInterval",
    };

    bool in_single_line_comment = false;
    bool in_multi_line_comment = false;
    bool in_string = false;
    bool in_template = false;
    char string_quote = 0;
    std::string previous_identifier_token;

    for (std::size_t index = 0; index < source.size();) {
        const char ch = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';

        if (in_single_line_comment) {
            if (ch == '\n' || ch == '\r')
                in_single_line_comment = false;
            ++index;
            continue;
        }
        if (in_multi_line_comment) {
            if (ch == '*' && next == '/') {
                in_multi_line_comment = false;
                index += 2;
            } else {
                ++index;
            }
            continue;
        }
        if (in_string || in_template) {
            if (ch == '\\') {
                index += index + 1 < source.size() ? 2 : 1;
                continue;
            }
            if (in_template && ch == '$' && next == '{') {
                add_violation(violations,
                    "compiled JavaScript uses unsupported template interpolation");
                index += 2;
                continue;
            }
            if (ch == string_quote) {
                in_string = false;
                in_template = false;
                string_quote = 0;
            }
            ++index;
            continue;
        }

        if (ch == '/' && next == '/') {
            in_single_line_comment = true;
            index += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_multi_line_comment = true;
            index += 2;
            continue;
        }
        if (ch == '/' && (next == '\\' || next == '[')) {
            add_violation(violations, "compiled JavaScript uses unsupported regular expression literal");
            index = skip_regex_literal(source, index);
            continue;
        }
        if (ch == '\'' || ch == '"' || ch == '`') {
            in_string = ch != '`';
            in_template = ch == '`';
            string_quote = ch;
            ++index;
            continue;
        }
        if (ch == '[') {
            std::string property;
            std::size_t end_index = index + 1;
            bool had_escape = false;
            if (is_quoted_property_access(source, index, &property, &end_index, &had_escape)) {
                if (had_escape)
                    add_violation(violations,
                        "compiled JavaScript uses escaped bracket property access", property);
                if (is_forbidden_property_name(property))
                    add_violation(violations,
                        "compiled JavaScript uses forbidden property access '" + property + "'",
                        property);
                index = end_index;
                continue;
            }
        }
        if (is_identifier_start(static_cast<unsigned char>(ch))) {
            std::size_t end = index + 1;
            while (end < source.size()
                && is_identifier_continue(static_cast<unsigned char>(source[end])))
                ++end;
            const std::string token = source.substr(index, end - index);
            if (forbidden_tokens.find(token) != forbidden_tokens.end())
                add_violation(violations, "compiled JavaScript uses forbidden token '" + token + "'",
                    token);
            if (token == "function") {
                const std::size_t next_index = skip_whitespace_and_comments(source, end);
                if (next_index < source.size() && source[next_index] == '*')
                    add_violation(violations,
                        "compiled JavaScript uses unsupported generator function syntax",
                        "function*");
            }
            if (token == "constructor" && previous_identifier_token == "constructor")
                add_violation(violations,
                    "compiled JavaScript uses forbidden constructor code generation", token);
            previous_identifier_token = token;
            index = end;
            continue;
        }
        ++index;
    }

    return violations;
}
