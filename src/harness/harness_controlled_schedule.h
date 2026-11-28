/* harness_controlled_schedule.h */

#ifndef HARNESS_CONTROLLED_SCHEDULE_H
#define HARNESS_CONTROLLED_SCHEDULE_H

#include "game_tick_schedule.h"
#include "harness_control_channel.h"
#include "request_outcome.h"
#include "schedule_setup.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace harness {

// Hands each line received over a control channel to its request, sends the replies when they
// fall due, and delegates each pass to the schedule the requests drive.
class HarnessControlledSchedule : public GameTickSchedule {
public:
    // Takes ownership of setup and channel. schedule_name and setup.ready_state make up the ready
    // reply. A null channel, or a setup with no schedule, is logged once; with no channel no
    // request is read, and with no schedule every pass runs no world update.
    HarnessControlledSchedule(std::string_view schedule_name, ScheduleSetup setup,
        std::unique_ptr<HarnessControlChannel> channel);

    // Answers the channel, then returns the schedule's pass.
    // - The first call replies "ready schedule=<schedule_name> <ready_state>", without the space
    //   when the ready state is empty.
    // - Each call sends every deferred reply whose passes the previous calls have returned,
    //   oldest first, building each body as it is sent. No deferred reply is sent before one
    //   queued ahead of it.
    // - Each line received is handed to dispatch_request_line before the schedule decides this
    //   call's pass. An immediate reply is sent at once; a deferred one is queued.
    // - Logs once when the channel's input closes; deferred replies are still sent.
    [[nodiscard]] WorldPass next_pass() override;

private:
    // A deferred reply waiting for its passes to be returned.
    struct QueuedReply {
        // The count of passes returned at which the reply falls due.
        std::uint64_t due_pass = 0;
        // Builds the reply body when it is sent.
        std::function<std::string()> build_body;
    };

    // Sends, oldest first, every queued reply that has fallen due.
    void send_due_replies();
    // Answers one received line now, or queues its deferred reply.
    void answer_line(std::string_view line);
    // Queues deferred to fall due once its pass count has been returned, counting this call's.
    void queue_reply(DeferredReply deferred);
    // Logs, once, that the channel's input has closed.
    void report_closed_input();

    // The first reply, sent on the first pass. Built from the setup before it is moved into
    // schedule_setup, so it is declared first.
    std::string ready_reply;
    // Where request lines come from and replies go; owned, and null only when constructed so.
    std::unique_ptr<HarnessControlChannel> control_channel;
    // The schedule and the requests that drive it.
    ScheduleSetup schedule_setup;
    // Deferred replies not yet sent, oldest first.
    std::deque<QueuedReply> queued_replies;
    // Passes returned so far.
    std::uint64_t passes_returned = 0;
    // True once the ready reply has been sent.
    bool ready_sent = false;
    // True once the closed input has been logged.
    bool closed_input_reported = false;
};

} // namespace harness

#endif /* HARNESS_CONTROLLED_SCHEDULE_H */
