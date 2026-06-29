#include "crashsave_schedule.h"

// The shortest interval the periodic snapshot may run at: a floor so a mis-set or
// deliberately tiny configured value cannot hammer the disk.
static constexpr int MIN_AUTOSAVE_INTERVAL_SECONDS = 15;

int autosave_interval_pulses(int interval_seconds, int tics_per_second) {
    if (tics_per_second < 1) {
        tics_per_second = 1;
    }
    if (interval_seconds < MIN_AUTOSAVE_INTERVAL_SECONDS) {
        interval_seconds = MIN_AUTOSAVE_INTERVAL_SECONDS;
    }
    return interval_seconds * tics_per_second;
}

bool AutosaveTimer::tick(int interval_pulses) {
    if (interval_pulses < 1) {
        interval_pulses = 1;
    }
    if (++pulses_since_fire >= interval_pulses) {
        pulses_since_fire = 0;
        return true;
    }
    return false;
}
