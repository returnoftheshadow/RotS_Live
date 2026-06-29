#include "../crashsave_schedule.h"
#include <gtest/gtest.h>

TEST(CrashsaveSchedule, IntervalPulsesBasic) {
    EXPECT_EQ(autosave_interval_pulses(30, 4), 120);
    EXPECT_EQ(autosave_interval_pulses(1, 4), 4);
    EXPECT_EQ(autosave_interval_pulses(60, 4), 240);
}

TEST(CrashsaveSchedule, IntervalPulsesClampsNonPositive) {
    EXPECT_EQ(autosave_interval_pulses(0, 4), 4);   // seconds clamped to >= 1
    EXPECT_EQ(autosave_interval_pulses(-5, 4), 4);
    EXPECT_EQ(autosave_interval_pulses(30, 0), 30); // tics clamped to >= 1
    EXPECT_EQ(autosave_interval_pulses(0, 0), 1);   // both args clamped -> 1*1 = 1
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
