#include "affect_hours_registration.h"
#include "comm.h"
#include "game_run_settings.h"
#include "harness_control_channel.h"
#include "harness_controlled_schedule.h"
#include "harness_options.h"
#include "real_time_registration.h"
#include "run_token.h"
#include "schedule_definition.h"
#include "schedule_registry.h"
#include "schedule_setup.h"
#include "server_startup.h"
#include "standard_stream_channel.h"
#include "token_entry_policy.h"
#include "utils.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {
// The program name the usage text shows when the command line does not supply one.
constexpr const char* DEFAULT_PROGRAM_NAME = "ageland_harness";
} // namespace

int main(int argc, char** argv)
{
    harness::ScheduleRegistry schedules;
    const bool affect_hours_registered = harness::register_affect_hours_schedule(schedules);
    if (!affect_hours_registered) {
        return EXIT_FAILURE;
    }
    const bool real_time_registered = harness::register_real_time_schedule(schedules);
    if (!real_time_registered) {
        return EXIT_FAILURE;
    }

    harness::HarnessOptions options;
    std::string parse_error;
    if (!harness::parse_harness_options(argc, argv, schedules, options, parse_error)) {
        if (!parse_error.empty()) {
            log(parse_error.c_str());
        }
        const bool has_program_name = argc > 0 && argv != nullptr && argv[0] != nullptr;
        const char* const program_name = has_program_name ? argv[0] : DEFAULT_PROGRAM_NAME;
        std::cerr << harness::harness_usage(schedules, program_name);
        return EXIT_FAILURE;
    }

    prepare_server_process(options.startup);
    const std::string mode = "Harness: the world advances by the schedule " + options.schedule_name
        + "; harness requests are read from standard input.";
    log(mode.c_str());

    // The schedule is built after prepare_server_process, because a factory may rely on the data
    // directory and the random seed it sets. It is built before the channel starts its reading
    // thread, so no failure leaves that thread blocked on standard input as the process exits.
    const harness::ScheduleDefinition* const definition = schedules.find(options.schedule_name);
    if (definition == nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the schedule '"
            + options.schedule_name + "' is not registered.";
        log(message.c_str());
        return EXIT_FAILURE;
    }
    harness::ScheduleSetup setup = definition->create_setup();
    if (setup.schedule == nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the schedule '"
            + definition->name + "' could not be built.";
        log(message.c_str());
        return EXIT_FAILURE;
    }

    const std::string run_token = harness::create_run_token();
    setup.ready_state = harness::with_entry_token(setup.ready_state, run_token);
    harness::TokenEntryPolicy entry_policy(run_token);

    // Untied, so the reading thread's reads never flush standard output while the game thread
    // writes to it.
    std::cin.tie(nullptr);
    // The process's standard streams outlive the channel and its reading thread.
    std::unique_ptr<harness::HarnessControlChannel> channel
        = std::make_unique<harness::StandardStreamChannel>(std::cin, std::cout);
    std::shared_ptr<GameTickSchedule> tick_schedule
        = std::make_shared<harness::HarnessControlledSchedule>(
            definition->name, std::move(setup), std::move(channel));
    GameRunSettings settings { options.startup.port, std::move(tick_schedule), entry_policy };
    run_the_game(std::move(settings));
    return EXIT_SUCCESS;
}
