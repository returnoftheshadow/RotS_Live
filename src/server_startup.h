/* server_startup.h */

#ifndef SERVER_STARTUP_H
#define SERVER_STARTUP_H

#include "comm.h"

// Prepares the process to run the game with startup_options: the crash handler, the file
// mode, the start-up flags with a log line for each one in effect, the pid file, the data
// directory, the command log and the random seed. Exits with status 0 when the data directory
// cannot be entered.
void prepare_server_process(const StartupOptions& startup_options);

#endif /* SERVER_STARTUP_H */
