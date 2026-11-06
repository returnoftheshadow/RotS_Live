#include "advance_hours_request.h"
#include "affect_hours_schedule.h"
#include "request_outcome.h"

#include "../structs.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
// The refusal reply for a line whose hour count is not usable.
std::string bad_hours_reply(std::string_view line)
{
    return "error " + std::string(line) + ": hours must be a whole number from 1 to "
        + std::to_string(harness::AdvanceHoursRequest::MAX_REQUESTED_HOURS);
}

// The request line asking for hours_text game hours.
std::string advance_line(std::string_view hours_text)
{
    return std::string(harness::AdvanceHoursRequest::NAME) + " " + std::string(hours_text);
}

// Hands request the line asking for hours_text game hours, as dispatch_request_line would.
harness::RequestOutcome request_hours(
    harness::AdvanceHoursRequest& request, std::string_view hours_text)
{
    const std::string line = advance_line(hours_text);
    return request.handle(line, hours_text);
}

// The refusal reply for a request for hours_text game hours whose schedule has been destroyed.
std::string missing_schedule_reply(std::string_view hours_text)
{
    return "error " + advance_line(hours_text) + ": the schedule no longer exists";
}

// The passes in hours game hours.
std::uint64_t passes_in_hours(int hours)
{
    return static_cast<std::uint64_t>(hours) * static_cast<std::uint64_t>(FAST_UPDATE_RATE);
}
} // namespace

TEST(AdvanceHoursRequest, IsNamedAdvanceHours)
{
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    const harness::AdvanceHoursRequest request(schedule);

    EXPECT_EQ(request.name(), harness::AdvanceHoursRequest::NAME) << "Unexpected request name.";
}

TEST(AdvanceHoursRequest, GrantsAWholeNumberOfHoursAndDefersItsReply)
{
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);

    const harness::RequestOutcome outcome = request_hours(request, "3");

    const harness::DeferredReply* const deferred = std::get_if<harness::DeferredReply>(&outcome);
    ASSERT_NE(deferred, nullptr) << "Expected the reply to wait for the granted passes.";
    EXPECT_EQ(schedule->pending_passes(), 3 * FAST_UPDATE_RATE) << "Expected 3 hours granted.";
    EXPECT_EQ(deferred->pass_count, passes_in_hours(3)) << "Expected the reply to wait 3 hours.";
    ASSERT_TRUE(deferred->build_body) << "Expected a way to build the reply.";
    EXPECT_EQ(deferred->build_body(), "done " + advance_line("3") + " hours=0")
        << "Unexpected reply before any hour has run.";
}

TEST(AdvanceHoursRequest, AcceptsTheSmallestAndLargestHourCounts)
{
    const std::vector<int> hour_counts = { 1, harness::AdvanceHoursRequest::MAX_REQUESTED_HOURS };
    for (const int hours : hour_counts) {
        const std::shared_ptr<harness::AffectHoursSchedule> schedule
            = std::make_shared<harness::AffectHoursSchedule>();
        harness::AdvanceHoursRequest request(schedule);

        const harness::RequestOutcome outcome = request_hours(request, std::to_string(hours));

        const harness::DeferredReply* const deferred
            = std::get_if<harness::DeferredReply>(&outcome);
        ASSERT_NE(deferred, nullptr) << "Expected " << hours << " hours to be granted.";
        EXPECT_EQ(deferred->pass_count, passes_in_hours(hours))
            << "Unexpected wait for " << hours << " hours.";
    }
}

TEST(AdvanceHoursRequest, WaitsForTheWholeBacklogWhenQueuedBehindAnotherGrant)
{
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    static_cast<void>(request_hours(request, "1"));

    const harness::RequestOutcome outcome = request_hours(request, "2");

    const harness::DeferredReply* const deferred = std::get_if<harness::DeferredReply>(&outcome);
    ASSERT_NE(deferred, nullptr) << "Expected the second grant to be accepted.";
    EXPECT_EQ(deferred->pass_count, passes_in_hours(3))
        << "Expected the reply to wait for both grants' passes.";
}

TEST(AdvanceHoursRequest, BuildsItsReplyFromTheHoursCompletedWhenItIsSent)
{
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    const harness::RequestOutcome outcome = request_hours(request, "1");
    const harness::DeferredReply* const deferred = std::get_if<harness::DeferredReply>(&outcome);
    ASSERT_NE(deferred, nullptr) << "Expected the grant to be accepted.";
    ASSERT_TRUE(deferred->build_body) << "Expected a way to build the reply.";

    for (int pass_number = 1; pass_number <= FAST_UPDATE_RATE; ++pass_number) {
        static_cast<void>(schedule->next_pass());
    }

    EXPECT_EQ(deferred->build_body(), "done " + advance_line("1") + " hours=1")
        << "Expected the reply to count the hour run since the grant.";
}

TEST(AdvanceHoursRequest, RefusesAnHourCountOutOfRange)
{
    const std::vector<std::string> hour_texts
        = { "0", std::to_string(harness::AdvanceHoursRequest::MAX_REQUESTED_HOURS + 1) };
    for (const std::string_view hour_text : hour_texts) {
        const std::shared_ptr<harness::AffectHoursSchedule> schedule
            = std::make_shared<harness::AffectHoursSchedule>();
        harness::AdvanceHoursRequest request(schedule);

        const harness::RequestOutcome outcome = request_hours(request, hour_text);

        const std::string line = advance_line(hour_text);
        const harness::ImmediateReply* const immediate
            = std::get_if<harness::ImmediateReply>(&outcome);
        ASSERT_NE(immediate, nullptr) << "Accepted: " << line;
        EXPECT_EQ(immediate->body, bad_hours_reply(line)) << "Unexpected refusal for: " << line;
        EXPECT_EQ(schedule->pending_passes(), 0) << "Expected nothing granted for: " << line;
    }
}

TEST(AdvanceHoursRequest, RefusesASignPaddingLeadingZerosOrTrailingText)
{
    const std::vector<std::string> hour_texts = { "-1", "+1", " 1", "1 ", "01", "1x", "x", "" };
    for (const std::string_view hour_text : hour_texts) {
        const std::shared_ptr<harness::AffectHoursSchedule> schedule
            = std::make_shared<harness::AffectHoursSchedule>();
        harness::AdvanceHoursRequest request(schedule);

        const harness::RequestOutcome outcome = request_hours(request, hour_text);

        const std::string line = advance_line(hour_text);
        const harness::ImmediateReply* const immediate
            = std::get_if<harness::ImmediateReply>(&outcome);
        ASSERT_NE(immediate, nullptr) << "Accepted: " << line;
        EXPECT_EQ(immediate->body, bad_hours_reply(line)) << "Unexpected refusal for: " << line;
        EXPECT_EQ(schedule->pending_passes(), 0) << "Expected nothing granted for: " << line;
    }
}

TEST(AdvanceHoursRequest, RefusesALineWithNoHourCount)
{
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    const std::string bare_name(harness::AdvanceHoursRequest::NAME);

    const harness::RequestOutcome outcome = request.handle(bare_name, "");

    const harness::ImmediateReply* const immediate
        = std::get_if<harness::ImmediateReply>(&outcome);
    ASSERT_NE(immediate, nullptr) << "Expected a request with no hour count to be refused.";
    EXPECT_EQ(immediate->body, bad_hours_reply(bare_name))
        << "Unexpected refusal for the bare name.";
}

TEST(AdvanceHoursRequest, RepliesWithTheSchedulesReasonWhenItRefusesTheHours)
{
    const int maximum = harness::AffectHoursSchedule::MAX_PENDING_HOURS;
    const std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    static_cast<void>(request_hours(request, std::to_string(maximum)));

    const harness::RequestOutcome outcome = request_hours(request, "1");

    harness::AffectHoursSchedule reference;
    static_cast<void>(reference.grant_hours(maximum));
    const std::optional<std::string> refusal = reference.grant_hours(1);
    ASSERT_TRUE(refusal.has_value()) << "Expected the reference schedule to refuse.";
    const harness::ImmediateReply* const immediate
        = std::get_if<harness::ImmediateReply>(&outcome);
    ASSERT_NE(immediate, nullptr) << "Expected the refused grant to be answered at once.";
    EXPECT_EQ(immediate->body, "error " + advance_line("1") + ": " + *refusal)
        << "Expected the schedule's refusal as the reply.";
}

TEST(AdvanceHoursRequest, RefusesAGrantOnceItsScheduleHasBeenDestroyed)
{
    std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    schedule.reset();

    testing::internal::CaptureStderr();
    const harness::RequestOutcome outcome = request_hours(request, "2");
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_NE(logged.find("SYSERR: advance-hours 2: the schedule no longer exists."),
        std::string::npos)
        << "Expected the missing schedule logged; logged: " << logged;
    const harness::ImmediateReply* const immediate
        = std::get_if<harness::ImmediateReply>(&outcome);
    ASSERT_NE(immediate, nullptr) << "Expected the grant to be refused at once.";
    EXPECT_EQ(immediate->body, missing_schedule_reply("2"))
        << "Unexpected refusal with no schedule.";
}

TEST(AdvanceHoursRequest, SendsARefusalAsItsDeferredReplyOnceItsScheduleHasBeenDestroyed)
{
    std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    harness::AdvanceHoursRequest request(schedule);
    const harness::RequestOutcome outcome = request_hours(request, "1");
    const harness::DeferredReply* const deferred = std::get_if<harness::DeferredReply>(&outcome);
    ASSERT_NE(deferred, nullptr) << "Expected the grant to be accepted.";
    ASSERT_TRUE(deferred->build_body) << "Expected a way to build the reply.";

    schedule.reset();

    testing::internal::CaptureStderr();
    const std::string body = deferred->build_body();
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(body, missing_schedule_reply("1"))
        << "Expected the reply to refuse once the schedule is gone.";
    EXPECT_NE(logged.find("SYSERR: advance-hours 1: the schedule no longer exists."),
        std::string::npos)
        << "Expected the missing schedule logged; logged: " << logged;
}
