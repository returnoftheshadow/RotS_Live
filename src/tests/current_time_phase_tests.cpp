#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>
#include <string>

namespace {
// A phase in range that differs from the phase 0 the fixture restores.
constexpr int PUBLISHED_PHASE = 3;

// Restores phase 0 after each test, the phase tests see when nothing publishes one.
class CurrentTimePhase : public testing::Test {
protected:
    void TearDown() override
    {
        set_current_time_phase(0);
    }
};
} // namespace

TEST_F(CurrentTimePhase, ReturnsThePublishedPhase)
{
    const int last_phase = FAST_UPDATE_RATE - 1;

    set_current_time_phase(last_phase);

    const int current_phase = get_current_time_phase();
    EXPECT_EQ(current_phase, last_phase) << "Expected the last valid phase to be returned.";
}

TEST_F(CurrentTimePhase, IgnoresAndLogsAPhasePastTheLastOne)
{
    set_current_time_phase(PUBLISHED_PHASE);

    testing::internal::CaptureStderr();
    set_current_time_phase(FAST_UPDATE_RATE);
    const std::string logged = testing::internal::GetCapturedStderr();

    const int current_phase = get_current_time_phase();
    EXPECT_EQ(current_phase, PUBLISHED_PHASE)
        << "Expected a phase of FAST_UPDATE_RATE to leave the published phase unchanged.";
    EXPECT_NE(logged.find("set_current_time_phase"), std::string::npos)
        << "Expected the refused phase to be logged. Logged: " << logged;
}

TEST_F(CurrentTimePhase, IgnoresAndLogsANegativePhase)
{
    set_current_time_phase(PUBLISHED_PHASE);

    testing::internal::CaptureStderr();
    set_current_time_phase(-1);
    const std::string logged = testing::internal::GetCapturedStderr();

    const int current_phase = get_current_time_phase();
    EXPECT_EQ(current_phase, PUBLISHED_PHASE)
        << "Expected a negative phase to leave the published phase unchanged.";
    EXPECT_NE(logged.find("set_current_time_phase"), std::string::npos)
        << "Expected the refused phase to be logged. Logged: " << logged;
}
