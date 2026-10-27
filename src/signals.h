/* signals.h */

#ifndef SIGNALS_H
#define SIGNALS_H

class SignalRequestSteps;

// Installs the server's signal handlers.
void signal_setup(void);

// The handlers for SIGHUP, SIGINT and SIGTERM (a stop), SIGUSR1, SIGUSR2 and SIGALRM. Each only
// records its request (signal_requests.h); the game loop performs it through game_signal_steps().
void hupsig(int signal_number);
void reread_wizlists(int signal_number);
void unrestrict_game(int signal_number);
void logsig(int signal_number);

// The server's response to each recorded signal request, for the game loop to pass to
// perform_signal_requests(). Its stop logs the shutdown notice, saves every playing character
// and any vault file left behind, closes every connection and exits with status 0.
SignalRequestSteps& game_signal_steps();

// The saving a stop does before it closes every connection and exits: every playing character and
// their gear, then every vault file a failed write left behind.
void save_everything_for_stop();

#endif /* SIGNALS_H */
