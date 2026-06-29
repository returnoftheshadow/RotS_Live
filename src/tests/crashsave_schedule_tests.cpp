#include "../crashsave_schedule.h"
#include <gtest/gtest.h>

TEST(CrashsaveSchedule, IntervalPulsesBasic) {
    EXPECT_EQ(autosave_interval_pulses(30, 4), 120); // default 30s
    EXPECT_EQ(autosave_interval_pulses(15, 4), 60);  // exactly the 15s floor
    EXPECT_EQ(autosave_interval_pulses(60, 4), 240);
}

TEST(CrashsaveSchedule, IntervalPulsesClampsBelowMinimum) {
    EXPECT_EQ(autosave_interval_pulses(14, 4), 60); // just below the 15s floor
    EXPECT_EQ(autosave_interval_pulses(1, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(0, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(-5, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(30, 0), 30); // tics clamped to >= 1; 30s kept
    EXPECT_EQ(autosave_interval_pulses(0, 0), 15);  // both clamped -> 15s * 1 tic
}

TEST(CrashsaveSchedule, TimerFiresOnIntervalAndResets) {
    AutosaveTimer t;
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120)); // 120th pulse fires
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire after reset at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120)); // fires again after reset
}

TEST(CrashsaveSchedule, TimerIntervalOfOneFiresEveryPulse) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(1));
    EXPECT_TRUE(t.tick(1));
}

TEST(CrashsaveSchedule, TimerClampsNonPositiveInterval) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(0)); // clamped to 1 -> fires every pulse
    EXPECT_TRUE(t.tick(0)); // still fires every pulse after clamped reset
}
