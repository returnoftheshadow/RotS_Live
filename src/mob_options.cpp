#include "mob_options.h"

#include "db.h"
#include "utils.h"

#include <cctype>
#include <cstring>

namespace mob_options_detail {

std::string trim(const std::string& s)
{
    const char* space = " \t\r\n";
    size_t b = s.find_first_not_of(space);
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(space) - b + 1);
}

std::vector<std::string> split_lines(const char* text)
{
    std::vector<std::string> lines;
    if (!text)
        return lines;
    std::string current;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (*p != '\r') {
            current += *p;
        }
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

} // namespace mob_options_detail

using mob_options_detail::split_lines;
using mob_options_detail::trim;

bool mob_option_find(const char* options, const char* key, std::string* value)
{
    for (const std::string& raw : split_lines(options)) {
        std::string line = trim(raw);
        if (line.compare(0, 2, "//") == 0) /* comment */
            continue;
        size_t eq = line.find('=');
        std::string name = trim(eq == std::string::npos ? line : line.substr(0, eq));
        if (name != key)
            continue;
        if (value)
            *value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        return true;
    }
    return false;
}

bool mob_options_storable(const char* text, const char** why)
{
    if (!text)
        return true;
    if (strlen(text) > (size_t)MOB_OPTIONS_MAX) {
        *why = "options are too long (max 4000 characters)";
        return false;
    }
    /* The shape editor's record scanner (find_mob/replace_proto) treats any
     * '#' in a mob file as a record header, so '#' is refused anywhere. */
    if (strchr(text, '~') || strchr(text, '#')) {
        *why = "options can't contain # or ~";
        return false;
    }
    const char* p = text;
    while (*p && isspace((unsigned char)*p))
        ++p;
    if (*p == '$') {
        *why = "options can't start with $";
        return false;
    }
    return true;
}

void mob_options_trim_leading(char* text)
{
    if (!text)
        return;
    const char* p = text;
    while (*p && isspace((unsigned char)*p))
        ++p;
    if (p != text)
        memmove(text, p, strlen(p) + 1);
}

char* read_mob_options(FILE* f, char* context)
{
    int c;
    do {
        c = fgetc(f);
    } while (c != EOF && isspace(c));
    if (c == EOF)
        return nullptr;
    ungetc(c, f);
    if (c == '#' || c == '$')
        return nullptr;
    char* text = fread_string(f, context);
    if (text && !*text) {
        RELEASE(text);
        return nullptr;
    }
    return text;
}

void write_mob_options(FILE* f, const char* options)
{
    if (!options || !*options)
        return;
    fprintf(f, "%s~\n", options);
}
