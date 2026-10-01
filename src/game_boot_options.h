#ifndef GAME_BOOT_OPTIONS_H
#define GAME_BOOT_OPTIONS_H

/* Game-wide settings staff can change without a code change. Read once at
 * boot from lib/misc/game_boot_options.json; a change takes effect at the
 * next reboot. A missing file, missing key or bad value uses the built-in
 * default and can never stop the boot. */

#include "interpre.h"

#include <string>
#include <vector>

constexpr const char* BOOT_OPTIONS_PATH = "misc/game_boot_options.json"; /* cwd is lib/ */

enum { BOOT_BANK_SLOTS,
    BOOT_BANK_COIN_LIMIT_GOLD,
    BOOT_BANK_DAY_START_HOUR,
    BOOT_OPTION_COUNT };

struct boot_option_def {
    const char* name;
    int def;
    int min;
    int max;
    const char* what;
};
extern const boot_option_def BOOT_OPTION_DEFS[BOOT_OPTION_COUNT];

struct boot_options_values {
    int v[BOOT_OPTION_COUNT];
};

boot_options_values boot_options_defaults();
boot_options_values boot_options_parse(const std::string& json, std::vector<std::string>* warnings);
std::string boot_options_serialize(const boot_options_values& values);
int boot_option_index(const std::string& name); /* -1 if unknown */

void boot_options_load(const char* path = BOOT_OPTIONS_PATH); /* logs each warning */
int boot_option(int index); /* value in use this boot */
int boot_option_pending(int index); /* value in the file: in use after the next reboot */
bool boot_option_set(int index, int value, std::string* error, const char* path = BOOT_OPTIONS_PATH);
void boot_options_set_running_for_tests(int index, int value);

ACMD(do_bootoptions);

#endif
