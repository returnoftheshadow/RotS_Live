#include "advance_hours_request.h"
#include "affect_hours_schedule.h"
#include "fake_harness_request.h"
#include "harness_request_registry.h"
#include "request_dispatch.h"
#include "request_outcome.h"

#include "../structs.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
// The name the fake request is registered under.
constexpr std::string_view FAKE_REQUEST_NAME = "fake";

// A registry holding one fake request, and the state it records into.
struct FakeRequestRegistry {
    // What the fake request was handed, and what it answers.
    std::shared_ptr<harness_tests::FakeHarnessRequest::State> request_state
        = std::make_shared<harness_tests::FakeHarnessRequest::State>();
    // The registry the lines are dispatched to.
    harness::HarnessRequestRegistry requests;

    FakeRequestRegistry()
    {
        std::unique_ptr<harness::HarnessRequest> request
            = std::make_unique<harness_tests::FakeHarnessRequest>(
                std::string(FAKE_REQUEST_NAME), request_state);
        const bool registered = requests.register_request(std::move(request));
        EXPECT_TRUE(registered) << "Expected the fake request to register.";
    }
};

// A registry holding an AdvanceHoursRequest, and the schedule it grants hours on.
struct AdvanceHoursRegistry {
    // The schedule the request grants hours on.
    std::shared_ptr<harness::AffectHoursSchedule> schedule
        = std::make_shared<harness::AffectHoursSchedule>();
    // The registry the lines are dispatched to.
    harness::HarnessRequestRegistry requests;

    AdvanceHoursRegistry()
    {
        std::unique_ptr<harness::HarnessRequest> request
            = std::make_unique<harness::AdvanceHoursRequest>(schedule);
        const bool registered = requests.register_request(std::move(request));
        EXPECT_TRUE(registered) << "Expected advance-hours to register.";
    }
};

// Returns the body of outcome's immediate reply, or a note that it is deferred.
std::string immediate_body(const harness::RequestOutcome& outcome)
{
    const harness::ImmediateReply* const immediate
        = std::get_if<harness::ImmediateReply>(&outcome);
    if (immediate == nullptr) {
        return "<deferred reply>";
    }
    return immediate->body;
}

// The request line for the fake request with arguments after it.
std::string fake_line(std::string_view arguments)
{
    return std::string(FAKE_REQUEST_NAME) + " " + std::string(arguments);
}
} // namespace

TEST(RequestDispatch, HandsALineToTheRequestItNamesWithTheTextAfterTheFirstSpace)
{
    FakeRequestRegistry registry;
    const std::string line = fake_line("one two");

    const harness::RequestOutcome outcome = harness::dispatch_request_line(line, registry.requests);

    EXPECT_EQ(immediate_body(outcome), harness_tests::FakeHarnessRequest::UNSCRIPTED_REPLY)
        << "Expected the request's own reply.";
    const std::vector<std::string> expected_lines = { line };
    const std::vector<std::string> expected_arguments = { "one two" };
    EXPECT_EQ(registry.request_state->handled_lines, expected_lines) << "Unexpected line.";
    EXPECT_EQ(registry.request_state->handled_arguments, expected_arguments)
        << "Unexpected arguments.";
}

TEST(RequestDispatch, HandsALineWithNoSpaceToItsRequestWithNoArguments)
{
    FakeRequestRegistry registry;

    static_cast<void>(harness::dispatch_request_line(FAKE_REQUEST_NAME, registry.requests));

    const std::vector<std::string> expected_arguments = { "" };
    EXPECT_EQ(registry.request_state->handled_arguments, expected_arguments)
        << "Expected empty arguments for a bare request name.";
}

TEST(RequestDispatch, ReturnsTheRequestsDeferredReply)
{
    FakeRequestRegistry registry;
    constexpr std::uint64_t SCRIPTED_PASSES = 7;
    harness::DeferredReply scripted;
    scripted.pass_count = SCRIPTED_PASSES;
    scripted.build_body = []() -> std::string { return "done later"; };
    registry.request_state->outcomes.push_back(std::move(scripted));

    const harness::RequestOutcome outcome
        = harness::dispatch_request_line(fake_line("x"), registry.requests);

    const harness::DeferredReply* const deferred = std::get_if<harness::DeferredReply>(&outcome);
    ASSERT_NE(deferred, nullptr) << "Expected the request's deferred reply.";
    EXPECT_EQ(deferred->pass_count, SCRIPTED_PASSES) << "Unexpected pass count.";
}

TEST(RequestDispatch, IgnoresOneTrailingCarriageReturn)
{
    FakeRequestRegistry registry;

    static_cast<void>(harness::dispatch_request_line(fake_line("2") + "\r", registry.requests));

    const std::vector<std::string> expected_lines = { fake_line("2") };
    const std::vector<std::string> expected_arguments = { "2" };
    EXPECT_EQ(registry.request_state->handled_lines, expected_lines)
        << "Expected a CRLF line to read like an LF line.";
    EXPECT_EQ(registry.request_state->handled_arguments, expected_arguments)
        << "Expected the carriage return trimmed from the arguments.";
}

TEST(RequestDispatch, GrantsAdvanceHoursFromACarriageReturnLine)
{
    AdvanceHoursRegistry registry;
    const std::string line = std::string(harness::AdvanceHoursRequest::NAME) + " 2\r";

    const harness::RequestOutcome outcome = harness::dispatch_request_line(line, registry.requests);

    EXPECT_NE(std::get_if<harness::DeferredReply>(&outcome), nullptr)
        << "Expected a CRLF line to be granted like an LF line.";
    EXPECT_EQ(registry.schedule->pending_passes(), 2 * FAST_UPDATE_RATE)
        << "Expected 2 hours granted.";
}

TEST(RequestDispatch, RefusesAnUnknownRequestByItsWholeName)
{
    AdvanceHoursRegistry registry;
    const std::vector<std::string> lines
        = { "status", std::string(harness::AdvanceHoursRequest::NAME) + "3" };
    for (const std::string_view line : lines) {
        const harness::RequestOutcome outcome
            = harness::dispatch_request_line(line, registry.requests);
        EXPECT_EQ(immediate_body(outcome), "error " + std::string(line) + ": unknown request")
            << "Unexpected refusal for: " << line;
    }
    EXPECT_EQ(registry.schedule->pending_passes(), 0) << "Expected nothing granted.";
}

TEST(RequestDispatch, QuotesAnUnknownRequestWithoutItsCarriageReturn)
{
    AdvanceHoursRegistry registry;

    const harness::RequestOutcome outcome
        = harness::dispatch_request_line("status\r", registry.requests);

    EXPECT_EQ(immediate_body(outcome), "error status: unknown request")
        << "Expected the carriage return left out of the quoted line.";
}

TEST(RequestDispatch, RefusesAnEmptyLine)
{
    AdvanceHoursRegistry registry;

    const harness::RequestOutcome empty = harness::dispatch_request_line("", registry.requests);
    const harness::RequestOutcome bare_return
        = harness::dispatch_request_line("\r", registry.requests);

    EXPECT_EQ(immediate_body(empty), "error empty request") << "Unexpected reply to an empty line.";
    EXPECT_EQ(immediate_body(bare_return), "error empty request")
        << "Unexpected reply to a bare CR.";
}

TEST(RequestDispatch, RefusesALineLongerThanTheLimitWithoutQuotingIt)
{
    AdvanceHoursRegistry registry;
    const std::string overlong(harness::MAX_REQUEST_LINE_LENGTH + 1, 'a');

    const harness::RequestOutcome outcome
        = harness::dispatch_request_line(overlong, registry.requests);

    EXPECT_EQ(immediate_body(outcome),
        "error request longer than " + std::to_string(harness::MAX_REQUEST_LINE_LENGTH)
            + " characters")
        << "Unexpected reply to an overlong line.";
}

TEST(RequestDispatch, AcceptsALineOfTheLimitsLengthFollowedByACarriageReturn)
{
    FakeRequestRegistry registry;
    const std::size_t arguments_length
        = harness::MAX_REQUEST_LINE_LENGTH - FAKE_REQUEST_NAME.size() - 1;
    const std::string arguments(arguments_length, 'a');
    const std::string longest = fake_line(arguments);

    static_cast<void>(harness::dispatch_request_line(longest + "\r", registry.requests));

    const std::vector<std::string> expected_lines = { longest };
    EXPECT_EQ(registry.request_state->handled_lines, expected_lines)
        << "Expected a line of the longest length to reach its request.";
}
