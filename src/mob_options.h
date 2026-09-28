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
 * over MOB_OPTIONS_MAX, any '#' or '~', or a leading '$'. */
bool mob_options_storable(const char* text, const char** why);

/* Removes leading blank lines and whitespace in place, as reading the text
 * back from the mob file would (read_mob_options skips them). */
void mob_options_trim_leading(char* text);

/* Reads the optional options string that may follow a mob record. Returns
 * nullptr, leaving the stream at the next token, if that token starts the
 * next record ('#' or '$') or the file ends, or if the text is empty. */
char* read_mob_options(FILE* f, char* context);

/* Writes the options string; writes nothing for null/empty text. */
void write_mob_options(FILE* f, const char* options);

/* Line helpers shared with the mob programs that parse options. */
namespace mob_options_detail {
std::string trim(const std::string& s); /* strips spaces, tabs, \r and \n at both ends */
std::vector<std::string> split_lines(const char* text); /* on \n, dropping \r */
} // namespace mob_options_detail

#endif
