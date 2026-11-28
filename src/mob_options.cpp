#include "mob_options.h"

#include "comm.h"
#include "db.h"
#include "structs.h"
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

const std::vector<mob_general_option>& mob_general_options()
{
    static const std::vector<mob_general_option> options = {
        { "walker_type", "path, loop or bounded: how walker_rooms is used" },
        { "walker_rooms", "room vnums in order: 1101-1150,1162 (lines join)" },
        { "walker_move_chance", "1-100: chance a route step succeeds (path, loop)" },
        { "path_complete_extract", "yes: remove the mob at the end of a path" },
        { "path_complete_message", "line shown to the room at the end (path, loop)" },
        { "continue_wandering", "yes: wander freely after a path ends" },
    };
    return options;
}

bool mob_option_is_general(const std::string& key)
{
    for (const mob_general_option& option : mob_general_options())
        if (key == option.key)
            return true;
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

void mob_options_tidy(char* text)
{
    if (!text)
        return;
    const char* p = text;
    while (*p && isspace((unsigned char)*p))
        ++p;
    if (p != text)
        memmove(text, p, strlen(p) + 1);
    /* fread_string keeps nothing of a blank line, so drop every whole line
     * that is only whitespace; the unterminated last line stays as it is. */
    char* out = text;
    const char* line = text;
    while (*line) {
        const char* nl = strchr(line, '\n');
        const char* end = nl ? nl + 1 : line + strlen(line);
        bool blank = true;
        for (const char* c = line; c < end && blank; ++c)
            blank = isspace((unsigned char)*c);
        if (!blank || !nl) {
            memmove(out, line, end - line);
            out += end - line;
        }
        line = end;
    }
    *out = '\0';
}

char* read_mob_options(FILE* f, const char* context)
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

void parse_yes_no_options(const char* text, const mob_yes_no_setting* settings, int count,
    std::vector<vendor_problem>* problems)
{
    std::vector<bool> seen(count, false);
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text ? text : "")) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        int index = -1;
        for (int i = 0; i < count; ++i)
            if (key == settings[i].key) {
                index = i;
                break;
            }
        if (index < 0 && mob_option_is_general(key))
            continue; /* another feature's line (mob_general_options) */
        if (index < 0 || eq == std::string::npos) {
            problem(line_no, "unknown setting - line ignored");
            continue;
        }
        if (seen[index]) {
            problem(line_no, "duplicate " + key + " - line ignored");
            continue;
        }
        if (value != "yes" && value != "no") {
            /* Not seen: a later line that corrects it still counts. */
            problem(line_no, "bad " + key + " - line ignored");
            continue;
        }
        seen[index] = true;
        *settings[index].flag = value == "yes";
    }
}

std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem)
{
    char buf[512];
    if (problem.line > 0)
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d, options line %d: %s", mob_vnum, problem.line, problem.text.c_str());
    else
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d: %s", mob_vnum, problem.text.c_str());
    return buf;
}

void vendor_send(const std::string& line, const struct char_data* builder)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    mudlog(buf, NRM, LEVEL_AREAGOD, TRUE);
    if (builder && !mudlog_reaches(builder, LEVEL_AREAGOD, NRM)) {
        send_to_char(buf, builder);
        send_to_char("\n\r", builder);
    }
}

extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;
extern int no_specials;
void* virt_program_number(int number);

bool mob_runs_program(const struct char_data* mob, special_func func, int prog)
{
    if (!mob || !IS_NPC(mob) || mob->nr < 0 || mob->nr > top_of_mobt)
        return false;
    if (!IS_SET(mob->specials2.act, MOB_SPEC) || no_specials)
        return false; /* runs no program at all (one_mobile_activity) */
    return mob_index[mob->nr].func ? mob_index[mob->nr].func == func : mob->specials.store_prog_number == prog;
}

bool mob_program_candidate(const struct char_data* proto, int rnum, special_func func, int prog)
{
    if (no_specials)
        return false;
    if (!IS_SET(proto->specials2.act, MOB_SPEC) || proto->specials.store_prog_number != prog)
        return false;
    if (rnum < 0 || rnum > top_of_mobt)
        return true;
    /* The old /29 gave the slot only if the loaded type has the spec flag, as
     * implement_proto checks: without it the number is a mudlle index. */
    special_func loaded = mob_index[rnum].func;
    return !loaded || loaded == func
        || (IS_SET(mob_proto[rnum].specials2.act, MOB_SPEC)
            && (void*)loaded == virt_program_number(mob_proto[rnum].specials.store_prog_number));
}
