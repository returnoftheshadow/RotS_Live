#include "comm.h"
#include "utils.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern int mini_mud;
extern int new_mud;
extern int no_rent_check;
extern int restrict;

int main(int argc, char** argv)
{
    signal(SIGSEGV, sigsegv_handler);

    char buf[512];
    StartupOptions startup_options {};
    std::string parse_error;

    /* lets put the rots process in rwxrwx--- file mode */
    umask(S_IRWXO);

    if (!parse_startup_options(argc, argv, &startup_options, &parse_error)) {
        if (!parse_error.empty())
            log(parse_error.c_str());
        fprintf(stderr,
            "Usage: %s [-m] [-q] [-r] [-s] [-x] [-d pathname] [-p port #] [--random-seed n] "
            "[ port # ]\n",
            argv[0]);
        exit(0);
    }

    has_proxy = startup_options.has_proxy ? 1 : 0;
    mini_mud = startup_options.mini_mud ? 1 : 0;
    new_mud = startup_options.new_mud ? 1 : 0;
    no_rent_check = startup_options.no_rent_check ? 1 : 0;
    restrict = startup_options.restrict_game ? 1 : 0;
    no_specials = startup_options.no_specials ? 1 : 0;

    if (mini_mud)
        log("Running in minimized mode & with no rent check.");
    if (new_mud)
        log("Running in pnew mode & with no rent check.");
    if (!startup_options.mini_mud && !startup_options.new_mud && no_rent_check)
        log("Quick boot mode -- rent check supressed.");
    if (restrict)
        log("Restricting game -- no pnew players allowed.");
    if (no_specials)
        log("Suppressing assignment of special routines.");
    if (has_proxy)
        log("Expecting proxy server.");

    /* Create the pidfile and log some info */
    sprintf(buf, "echo %d > .ageland.pid", getpid());
    system(buf);
    sprintf(buf, "Running game as pid %d.", getpid());
    log(buf);

    sprintf(buf, "Running game on port %d.", startup_options.port);
    log(buf);

    if (chdir(startup_options.dir.c_str()) < 0) {
        perror("Fatal error changing to data directory");
        exit(0);
    }

    sprintf(buf, "Using %s as data directory.", startup_options.dir.c_str());
    log(buf);

    // Open command log
    system("mv -f last_cmds crash_cmds");
    fpCommand = fopen("last_cmds", "w");
    seed_random_numbers(startup_options.random_seed, draw_clock_seed);
    run_the_game(startup_options.port);
    return (0);
}
