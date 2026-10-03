#include "test_harness.h"

#include "comm.h"
#include "limits.h"
#include "structs.h"
#include "utils.h"

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>

int harness_mode = 0;
int harness_force_affect_phase = 0;

// The periodic work game_loop() runs; no header declares these.
void weather_and_time(int mode);
void stat_update();
void fast_update();
void affect_update();
void clean_expose_elements();

namespace {

// True when `argument` starts with the whole word `subcommand`.
bool is_subcommand(std::string_view argument, std::string_view subcommand)
{
    if (argument.substr(0, subcommand.size()) != subcommand) {
        return false;
    }

    return argument.size() == subcommand.size() || argument[subcommand.size()] == ' ';
}

} // namespace

bool seed_random_from_environment()
{
    if (!harness_mode) {
        return false;
    }

    const char* seed_text = std::getenv("ROTS_RANDOM_SEED");
    if (seed_text == nullptr || *seed_text == '\0') {
        return false;
    }

    // from_chars, unlike strtoul, rejects a sign, leading spaces and a value too large for
    // unsigned, so only a plain decimal that fits is applied.
    const std::string_view seed_view(seed_text);
    const char* const seed_end = seed_view.data() + seed_view.size();
    unsigned seed = 0;
    const std::from_chars_result parse_result = std::from_chars(seed_view.data(), seed_end, seed);
    if (parse_result.ec != std::errc {} || parse_result.ptr != seed_end) {
        log("Harness mode: ignoring ROTS_RANDOM_SEED, it is not an unsigned integer.");
        return false;
    }

    std::srand(seed);

    const std::string message
        = "Harness mode: random number generator seeded with " + std::to_string(seed) + ".";
    log(message.c_str());
    return true;
}

ACMD(do_harness)
{
    if (!harness_mode) {
        send_to_char("The harness command only exists when the server was started with -t.\r\n",
            ch);
        return;
    }
    if (GET_LEVEL(ch) < LEVEL_IMPL || IS_NPC(ch) || !ch->desc) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }

    const char* subcommand = argument;
    if (subcommand == nullptr) {
        subcommand = "";
    }
    while (*subcommand == ' ') {
        ++subcommand;
    }

    if (is_subcommand(subcommand, "tick")) {
        // The hourly block, then the fast block, in game_loop() order. Room-affect ticks live in
        // affect_update(), so the fast block is needed for them to advance.
        weather_and_time(1);
        point_update();
        stat_update();
        fast_update();
        affect_update();
        clean_expose_elements();
        send_to_char("Harness: hourly tick complete.\r\n", ch);
        return;
    }

    if (is_subcommand(subcommand, "affects")) {
        harness_force_affect_phase = 1;
        affect_update();
        clean_expose_elements();
        harness_force_affect_phase = 0;
        send_to_char("Harness: affect tick complete.\r\n", ch);
        return;
    }

    send_to_char("Usage: harness tick | harness affects\r\n", ch);
}
