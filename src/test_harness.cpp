#include "test_harness.h"

#include "comm.h"
#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <cstring>
#include <string>

int harness_mode = 0;
int harness_force_affect_phase = 0;

// The three calls comm.cpp's game loop makes every SECS_PER_MUD_HOUR * 4 pulses.
void weather_and_time(int mode);
void point_update(void);
void stat_update();
void fast_update(void);
void affect_update(void);
void clean_expose_elements();

bool seed_random_from_environment()
{
    if (!harness_mode) {
        return false;
    }

    const char* seed_text = std::getenv("ROTS_RANDOM_SEED");
    if (seed_text == nullptr || *seed_text == '\0') {
        return false;
    }

    char* end_of_number = nullptr;
    const unsigned long seed = std::strtoul(seed_text, &end_of_number, 10);
    if (end_of_number == seed_text || *end_of_number != '\0') {
        log("Harness mode: ignoring ROTS_RANDOM_SEED, it is not an unsigned integer.");
        return false;
    }

    std::srand(static_cast<unsigned>(seed));
    srandom(static_cast<unsigned>(seed));

    const std::string message = "Harness mode: random number generators seeded with " + std::to_string(seed) + ".";
    log(message.c_str());
    return true;
}

ACMD(do_harness)
{
    if (!harness_mode) {
        send_to_char("The harness command only exists when the server was started with -t.\r\n", ch);
        return;
    }
    if (GET_LEVEL(ch) < LEVEL_IMPL || IS_NPC(ch) || !ch->desc) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }

    while (argument && *argument == ' ') {
        ++argument;
    }

    if (argument && std::strncmp(argument, "tick", 4) == 0 && (argument[4] == '\0' || argument[4] == ' ')) {
        // One forced tick performs a full pulse's periodic work: the hourly
        // block (weather/point/stat) and the fast block (fast_update /
        // affect_update / clean_expose_elements). Room-affect blaze/poison
        // ticks live in affect_update(), so a tick that omitted it would never
        // advance them. Same calls, same order as game_loop() in comm.cpp.
        weather_and_time(1);
        point_update();
        stat_update();
        fast_update();
        affect_update();
        clean_expose_elements();
        send_to_char("Harness: hourly tick complete.\r\n", ch);
        return;
    }

    if (argument && std::strncmp(argument, "affects", 7) == 0 && (argument[7] == '\0' || argument[7] == ' ')) {
        harness_force_affect_phase = 1;
        affect_update();
        clean_expose_elements();
        harness_force_affect_phase = 0;
        send_to_char("Harness: affect tick complete.\r\n", ch);
        return;
    }

    send_to_char("Usage: harness tick | harness affects\r\n", ch);
}
