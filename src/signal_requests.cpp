/* signal_requests.cpp */

#include "signal_requests.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace {

// A signal handler may only touch lock-free atomics (or volatile std::sig_atomic_t). Atomics are
// used because the handler can run on another thread than the game loop's: the integration
// harness reads its standard input on a thread that does not block signals.
static_assert(std::atomic<int>::is_always_lock_free, "a signal handler needs a lock-free int");
static_assert(std::atomic<bool>::is_always_lock_free, "a signal handler needs a lock-free bool");

// The first stop signal recorded and not yet performed; 0 when there is none.
std::atomic<int> pending_stop_signal(0);
// Whether a wizard-list reread is waiting for the game loop.
std::atomic<bool> wizlist_reread_pending(false);
// Whether lifting the new-player restriction is waiting for the game loop.
std::atomic<bool> unrestrict_pending(false);
// Whether an ignored signal is waiting for the game loop to log it.
std::atomic<bool> ignored_signal_pending(false);

} // namespace

void record_stop_request(int signal_number)
{
    int no_stop_recorded = 0;
    pending_stop_signal.compare_exchange_strong(no_stop_recorded, signal_number);
}

void record_wizlist_reread_request()
{
    wizlist_reread_pending.store(true);
}

void record_unrestrict_request()
{
    unrestrict_pending.store(true);
}

void record_ignored_signal()
{
    ignored_signal_pending.store(true);
}

bool stop_requested()
{
    return pending_stop_signal.load() != 0;
}

void perform_signal_requests(SignalRequestSteps& steps)
{
    // Each flag is cleared before its step runs, so a signal arriving during the step is kept for
    // the next call instead of being lost.
    if (wizlist_reread_pending.exchange(false)) {
        steps.reread_wizlists();
    }
    if (unrestrict_pending.exchange(false)) {
        steps.unrestrict_game();
    }
    if (ignored_signal_pending.exchange(false)) {
        steps.log_ignored_signal();
    }

    const int stop_signal = pending_stop_signal.exchange(0);
    if (stop_signal != 0) {
        steps.stop(stop_signal);
    }
}

void sleep_unless_stop_requested(std::chrono::steady_clock::duration duration)
{
    // No standard facility lets a signal handler wake a sleeping thread, so the wait is cut into
    // short sleeps that each check the flag. Waking every STOP_POLL_INTERVAL costs little next to
    // a stop waiting out a whole pulse.
    const std::chrono::steady_clock::time_point wake_time = std::chrono::steady_clock::now() + duration;
    while (!stop_requested()) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now >= wake_time) {
            return;
        }

        const std::chrono::steady_clock::duration time_left = wake_time - now;
        const std::chrono::steady_clock::duration next_sleep
            = std::min<std::chrono::steady_clock::duration>(time_left, STOP_POLL_INTERVAL);
        std::this_thread::sleep_for(next_sleep);
    }
}
