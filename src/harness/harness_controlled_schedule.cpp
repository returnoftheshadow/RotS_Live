/* harness_controlled_schedule.cpp */

#include "harness_controlled_schedule.h"

#include "request_dispatch.h"
#include "utils.h"

#include <limits>
#include <utility>
#include <variant>
#include <vector>

namespace harness {

namespace {
// Returns the ready reply for schedule_name with ready_state after it.
std::string build_ready_reply(std::string_view schedule_name, std::string_view ready_state)
{
    std::string ready = "ready schedule=" + std::string(schedule_name);
    if (!ready_state.empty()) {
        ready += ' ';
        ready.append(ready_state);
    }
    return ready;
}
} // namespace

//============================================================================
HarnessControlledSchedule::HarnessControlledSchedule(std::string_view schedule_name,
    ScheduleSetup setup, std::unique_ptr<HarnessControlChannel> channel)
    : ready_reply(build_ready_reply(schedule_name, setup.ready_state))
    , control_channel(std::move(channel))
    , schedule_setup(std::move(setup))
{
    if (control_channel == nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__
            + ": no control channel; no request will be read.";
        log(message.c_str());
    }
    if (schedule_setup.schedule == nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__
            + ": the setup for schedule '" + std::string(schedule_name)
            + "' has no schedule; no world update will run.";
        log(message.c_str());
    }
}

//============================================================================
GameTickSchedule::WorldPass HarnessControlledSchedule::next_pass()
{
    if (control_channel != nullptr) {
        if (!ready_sent) {
            control_channel->write_reply(ready_reply);
            ready_sent = true;
        }

        send_due_replies();
        const std::vector<std::string> lines = control_channel->take_lines();
        for (const std::string_view line : lines) {
            answer_line(line);
        }
        report_closed_input();
    }

    WorldPass world_pass;
    if (schedule_setup.schedule != nullptr) {
        world_pass = schedule_setup.schedule->next_pass();
    }
    passes_returned++;
    return world_pass;
}

//============================================================================
void HarnessControlledSchedule::send_due_replies()
{
    while (!queued_replies.empty() && queued_replies.front().due_pass <= passes_returned) {
        const QueuedReply due_reply = std::move(queued_replies.front());
        queued_replies.pop_front();
        const std::string body = due_reply.build_body();
        control_channel->write_reply(body);
    }
}

//============================================================================
void HarnessControlledSchedule::answer_line(std::string_view line)
{
    RequestOutcome outcome = dispatch_request_line(line, schedule_setup.requests);
    ImmediateReply* const immediate = std::get_if<ImmediateReply>(&outcome);
    if (immediate != nullptr) {
        control_channel->write_reply(immediate->body);
        return;
    }

    queue_reply(std::get<DeferredReply>(std::move(outcome)));
}

//============================================================================
void HarnessControlledSchedule::queue_reply(DeferredReply deferred)
{
    if (!deferred.build_body) {
        const std::string message = std::string("SYSERR: ") + __func__
            + ": a deferred reply cannot build its body; it will not be sent.";
        log(message.c_str());
        return;
    }

    std::uint64_t pass_count = deferred.pass_count;
    if (pass_count == 0) {
        const std::string message = std::string("SYSERR: ") + __func__
            + ": a deferred reply waits for no pass; sending it after one.";
        log(message.c_str());
        pass_count = 1;
    }

    if (pass_count > std::numeric_limits<std::uint64_t>::max() - passes_returned) {
        const std::string message = std::string("SYSERR: ") + __func__
            + ": a deferred reply waits for more passes than can be counted; it will not be sent.";
        log(message.c_str());
        return;
    }

    // This call's pass is not yet counted in passes_returned, so a reply waiting for one pass
    // falls due once this call's pass has been returned.
    QueuedReply queued;
    queued.due_pass = passes_returned + pass_count;
    queued.build_body = std::move(deferred.build_body);
    queued_replies.push_back(std::move(queued));
}

//============================================================================
void HarnessControlledSchedule::report_closed_input()
{
    if (closed_input_reported || !control_channel->input_closed()) {
        return;
    }

    closed_input_reported = true;
    log("Harness: the control channel's input has closed; no more time requests will arrive.");
}

} // namespace harness
