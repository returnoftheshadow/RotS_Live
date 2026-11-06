#include "comm.h"
#include "real_time_tick_schedule.h"
#include "server_startup.h"
#include "utils.h"

#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <utility>

int main(int argc, char** argv)
{
    StartupOptions startup_options {};
    std::string parse_error;

    if (!parse_startup_options(argc, argv, &startup_options, &parse_error)) {
        if (!parse_error.empty())
            log(parse_error.c_str());
        fprintf(stderr,
            "Usage: %s [-m] [-q] [-r] [-s] [-x] [-d pathname] [-p port #] [--random-seed n] "
            "[ port # ]\n",
            argv[0]);
        exit(0);
    }

    prepare_server_process(startup_options);
    std::shared_ptr<GameTickSchedule> tick_schedule = std::make_shared<RealTimeTickSchedule>();
    run_the_game(startup_options.port, std::move(tick_schedule));
    return (0);
}
