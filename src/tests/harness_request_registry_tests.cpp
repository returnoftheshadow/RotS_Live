#include "fake_harness_request.h"
#include "harness_request.h"
#include "harness_request_registry.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {
// The words the registry logs when it refuses a duplicate name.
constexpr std::string_view DUPLICATE_PHRASE = "already registered";
// The words the registry logs when it refuses a null request.
constexpr std::string_view NULL_REQUEST_PHRASE = "null request";

// A fake request named request_name, recording into state.
std::unique_ptr<harness_tests::FakeHarnessRequest> make_fake_request(
    std::string_view request_name, std::shared_ptr<harness_tests::FakeHarnessRequest::State> state)
{
    return std::make_unique<harness_tests::FakeHarnessRequest>(
        std::string(request_name), std::move(state));
}

// A fresh state for a fake request.
std::shared_ptr<harness_tests::FakeHarnessRequest::State> make_request_state()
{
    return std::make_shared<harness_tests::FakeHarnessRequest::State>();
}
} // namespace

TEST(HarnessRequestRegistry, FindsARegisteredRequestByName)
{
    harness::HarnessRequestRegistry registry;
    std::unique_ptr<harness_tests::FakeHarnessRequest> first
        = make_fake_request("first", make_request_state());
    std::unique_ptr<harness_tests::FakeHarnessRequest> second
        = make_fake_request("second", make_request_state());
    const harness::HarnessRequest* const first_address = first.get();
    const harness::HarnessRequest* const second_address = second.get();
    ASSERT_TRUE(registry.register_request(std::move(first))) << "Expected first to register.";
    ASSERT_TRUE(registry.register_request(std::move(second))) << "Expected second to register.";

    EXPECT_EQ(registry.find("first"), first_address) << "Expected the first request.";
    EXPECT_EQ(registry.find("second"), second_address) << "Expected the second request.";
}

TEST(HarnessRequestRegistry, FindsNothingForAnUnregisteredName)
{
    harness::HarnessRequestRegistry registry;
    ASSERT_TRUE(registry.register_request(make_fake_request("known", make_request_state())))
        << "Expected the request to register.";

    EXPECT_EQ(registry.find("unknown"), nullptr) << "Expected no request for an unknown name.";
    EXPECT_EQ(registry.find("know"), nullptr) << "Expected a prefix not to match.";
    EXPECT_EQ(registry.find(""), nullptr) << "Expected no request for an empty name.";
}

TEST(HarnessRequestRegistry, RefusesAndLogsADuplicateNameKeepingTheFirst)
{
    harness::HarnessRequestRegistry registry;
    std::unique_ptr<harness_tests::FakeHarnessRequest> first
        = make_fake_request("twice", make_request_state());
    const harness::HarnessRequest* const first_address = first.get();
    ASSERT_TRUE(registry.register_request(std::move(first))) << "Expected first to register.";

    testing::internal::CaptureStderr();
    const bool duplicate_registered
        = registry.register_request(make_fake_request("twice", make_request_state()));
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(duplicate_registered) << "Expected the duplicate refused.";
    EXPECT_NE(logged.find(DUPLICATE_PHRASE), std::string::npos)
        << "Expected the refusal logged; log was: " << logged;
    EXPECT_EQ(registry.find("twice"), first_address) << "Expected the first request kept.";
}

TEST(HarnessRequestRegistry, RefusesAndLogsANullRequest)
{
    harness::HarnessRequestRegistry registry;

    testing::internal::CaptureStderr();
    const bool registered = registry.register_request(nullptr);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(registered) << "Expected a null request refused.";
    EXPECT_NE(logged.find(NULL_REQUEST_PHRASE), std::string::npos)
        << "Expected the refusal logged; log was: " << logged;
}
