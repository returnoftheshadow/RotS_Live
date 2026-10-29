#ifndef MOB_OPTIONS_H
#define MOB_OPTIONS_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

/* Mob options: a persisted multi-line text field for mob programs' settings
 * (one setting per line: "key", "key=value", or a keyword line like "price ...").
 * A line starting with // is a comment and is ignored. */

constexpr int MOB_OPTIONS_MAX = 4000;

/* True if a line is exactly `key` or `key=value` (spaces around '=' allowed);
 * *value receives the trimmed text after '=' (empty for a bare key). */
bool mob_option_find(const char* options, const char* key, std::string* value);

/* False, with a reason in *why, if saving `text` would corrupt a mob file:
 * over MOB_OPTIONS_MAX, any '#' or '~', or a leading '$'. The editor has
 * already turned '#' and '~' into '+' and '-' (clean_record_text), so that
 * refusal is only a backstop for other callers. */
bool mob_options_storable(const char* text, const char** why);

/* Removes leading whitespace and every blank line in place, as reading the
 * text back from the mob file would, so the stored text (and the line numbers
 * in its warnings) is the same after /save, /imp and boot. */
void mob_options_tidy(char* text);

/* Reads the optional options string that may follow a mob record. Returns
 * nullptr, leaving the stream at the next token, if that token starts the
 * next record ('#' or '$') or the file ends, or if the text is empty. */
char* read_mob_options(FILE* f, const char* context);

/* Writes the options string; writes nothing for null/empty text. */
void write_mob_options(FILE* f, const char* options);

/* General settings: valid on any mob, whatever its program. A mob program's
 * parser skips these lines instead of reporting them unknown. A new feature
 * adds its keys here once, and to the options help (lib/text/shap_tbl). */
struct mob_general_option {
    const char* key;
    const char* help; /* one line, at most 60 columns */
};
const std::vector<mob_general_option>& mob_general_options();
bool mob_option_is_general(const std::string& key);

/* A rejected options line, as every mob program's settings check reports
 * it; line 0 = the whole config. */
struct vendor_problem {
    int line;
    std::string text;
};
std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem); /* formatted warning */
struct char_data;
struct waiting_type;
void vendor_send(const std::string& line, const struct char_data* builder); /* warning to the builder log */

/* Which program a mob runs, for the programs that keep settings by mob rnum
 * (prog = its number, func = its function).
 * mob_runs_program: a loaded mob, as one_mobile_activity picks it: none
 * without the spec flag (or on a -s boot), else its type's function if set,
 * else its own number. A loaded copy keeps its old number
 * when /implement switches its type.
 * mob_program_candidate: a prototype set to the program (MOB_SPEC and /29)
 * that no function assigned by vnum (guild, receptionist...) overrides
 * (rnum < 0: none yet). A function the old /29 put there (the loaded type has
 * the spec flag and that number's function) does not count, so /save before
 * /implement already checks the new program's settings. */
typedef int (*special_func)(struct char_data* host, struct char_data* character, int cmd,
    char* argument, int call_flag, struct waiting_type* wait_list);
bool mob_runs_program(const struct char_data* mob, special_func func, int prog);
bool mob_program_candidate(const struct char_data* proto, int rnum, special_func func, int prog);

/* A mob program's key=yes|no setting and the flag it sets. */
struct mob_yes_no_setting {
    const char* key;
    bool* flag;
};

/* Reads key=yes|no lines into the flags; a flag whose key is absent keeps its
 * value. Blank lines, // comments and general settings are passed over. Each
 * rejected line appends a vendor_problem to *problems when non-null:
 * "unknown setting", "duplicate <key>" (the first good line stands) or
 * "bad <key>", each followed by " - line ignored". */
void parse_yes_no_options(const char* text, const mob_yes_no_setting* settings, int count,
    std::vector<vendor_problem>* problems);

/* Line helpers shared with the mob programs that parse options. */
namespace mob_options_detail {
std::string trim(const std::string& s); /* strips spaces, tabs, \r and \n at both ends */
std::vector<std::string> split_lines(const char* text); /* on \n, dropping \r */
} // namespace mob_options_detail

#endif
