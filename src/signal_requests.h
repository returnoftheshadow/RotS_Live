/* signal_requests.h */

#ifndef SIGNAL_REQUESTS_H
#define SIGNAL_REQUESTS_H

#include <chrono>

// The work each kind of recorded signal asks the server to do. The game loop performs it through
// game_signal_steps() (signals.h); a test supplies its own steps to see what was asked for.
class SignalRequestSteps {
public:
    virtual ~SignalRequestSteps() = default;

    // Stops the server after SIGHUP, SIGINT or SIGTERM; signal_number is the first of them that
    // arrived. The server's version saves, closes the connections and exits without returning.
    virtual void stop(int signal_number) = 0;

    // Rereads the wizard lists, as SIGUSR1 asks.
    virtual void reread_wizlists() = 0;

    // Lets new players in again, as SIGUSR2 asks.
    virtual void unrestrict_game() = 0;

    // Logs that a signal the server ignores (SIGALRM) arrived.
    virtual void log_ignored_signal() = 0;
};

// Records a stop request, keeping the first signal number when several arrive before the game
// loop acts. This and the record functions below only store to lock-free atomics, so they are
// safe in a signal handler running on any thread.
void record_stop_request(int signal_number);

// Records a request to reread the wizard lists.
void record_wizlist_reread_request();

// Records a request to let new players in again.
void record_unrestrict_request();

// Records that a signal the server ignores arrived.
void record_ignored_signal();

// Whether a stop has been recorded and not yet performed. Safe on any thread.
[[nodiscard]] bool stop_requested();

// Performs each recorded request once through steps, then forgets it; a request recorded while
// a step runs waits for the next call. The stop runs after the other requests, since the
// server's stop does not return. Call only from the game loop's thread.
void perform_signal_requests(SignalRequestSteps& steps);

// Waits at least duration, as std::this_thread::sleep_for does, but returns early, within
// STOP_POLL_INTERVAL, once a stop is recorded. Returns at once when a stop is already recorded.
void sleep_unless_stop_requested(std::chrono::steady_clock::duration duration);

// The longest sleep_unless_stop_requested() sleeps before checking for a stop request again.
inline constexpr std::chrono::milliseconds STOP_POLL_INTERVAL(10);

#endif /* SIGNAL_REQUESTS_H */
