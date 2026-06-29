#ifndef CRASHSAVE_SCHEDULE_H
#define CRASHSAVE_SCHEDULE_H

// Pure crash-save scheduling logic, isolated from game state so it can be
// unit-tested without linking the game loop. See
// docs/superpowers/specs/2026-06-29-consistent-snapshot-autosave-design.md.

// Convert a configured crash-save interval (seconds) into game-loop pulses.
// Clamped so a mis-set 0/negative interval (or tic rate) still yields at least
// a one-second cadence rather than firing every pulse.
int autosave_interval_pulses(int interval_seconds, int tics_per_second);

// Per-pulse accumulator for the periodic crash-save. The game loop calls tick()
// once per pulse; it returns true (and resets) when interval_pulses have elapsed.
struct AutosaveTimer {
    // Pulses counted since the timer last fired; reset to 0 each time tick() fires.
    int pulses_since_fire = 0;

    // Advance by one pulse. Returns true exactly when the interval elapses,
    // then resets the counter. interval_pulses is clamped to >= 1.
    bool tick(int interval_pulses);
};

#endif // CRASHSAVE_SCHEDULE_H
