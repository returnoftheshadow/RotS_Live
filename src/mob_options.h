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

/* False, with a reason in *why, if saving `text` would corrupt a mob file. */
bool mob_options_storable(const char* text, const char** why);

/* Reads the optional options string that may follow a mob record. Returns
 * nullptr, leaving the stream at the next token, if that token starts the
 * next record ('#' or '$') or the file ends, or if the text is empty. */
char* read_mob_options(FILE* f, char* context);

/* Writes the options string; writes nothing for null/empty text. */
void write_mob_options(FILE* f, const char* options);

#endif
