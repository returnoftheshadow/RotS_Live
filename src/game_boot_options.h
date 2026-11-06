#ifndef GAME_BOOT_OPTIONS_H
#define GAME_BOOT_OPTIONS_H

/* Game-wide settings staff can change without a code change. Read once at
 * boot from lib/misc/game_boot_options.json; a change takes effect at the
 * next reboot. A missing file, missing key or bad value uses the built-in
 * default and can never stop the boot. */

#include "interpre.h"

#include <ctime>
#include <string>
#include <vector>

constexpr const char* BOOT_OPTIONS_PATH = "misc/game_boot_options.json"; /* cwd is lib/ */

enum { BOOT_BANK_SLOTS,
    BOOT_BANK_COIN_LIMIT_GOLD,
    BOOT_DAILY_REBOOT_HOUR_UTC, /* the routine reboot, and the moment the bank's fee day starts */
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
/* The file was there at boot but could not be read or parsed: defaults are in
 * use and boot_option_set refuses, so the hand-edited file is not replaced. */
bool boot_options_file_unreadable();

/* Minutes until the routine daily reboot for which a notice is due at this
 * moment: 30, 5, 4 or 1; 0 when it is time to reboot; -1 otherwise. The hour
 * is counted in UTC, so the reboot never moves with daylight saving. */
int daily_reboot_minutes_left(time_t now, int reboot_hour_utc);

ACMD(do_gameoptions);

#endif
