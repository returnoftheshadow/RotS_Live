#include "../crashsave_schedule.h"
#include <gtest/gtest.h>

TEST(CrashsaveSchedule, IntervalPulsesBasic) {
    EXPECT_EQ(autosave_interval_pulses(240, 4), 960); // the new 4-minute default
    EXPECT_EQ(autosave_interval_pulses(30, 4), 120);
    EXPECT_EQ(autosave_interval_pulses(15, 4), 60);   // exactly the 15s floor
    EXPECT_EQ(autosave_interval_pulses(60, 4), 240);
}

TEST(CrashsaveSchedule, IntervalPulsesClampsBelowMinimum) {
    EXPECT_EQ(autosave_interval_pulses(14, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(1, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(0, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(-5, 4), 60);
    EXPECT_EQ(autosave_interval_pulses(30, 0), 30); // tics clamped to >= 1
    EXPECT_EQ(autosave_interval_pulses(0, 0), 15);
}

TEST(CrashsaveSchedule, TimerFiresOnIntervalAndResets) {
    AutosaveTimer t;
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120));
    for (int i = 0; i < 119; i++) {
        EXPECT_FALSE(t.tick(120)) << "unexpected fire after reset at pulse " << i;
    }
    EXPECT_TRUE(t.tick(120));
}

TEST(CrashsaveSchedule, TimerIntervalOfOneFiresEveryPulse) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(1));
    EXPECT_TRUE(t.tick(1));
}

TEST(CrashsaveSchedule, TimerClampsNonPositiveInterval) {
    AutosaveTimer t;
    EXPECT_TRUE(t.tick(0));
    EXPECT_TRUE(t.tick(0));
}
