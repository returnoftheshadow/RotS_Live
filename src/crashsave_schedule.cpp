#include "crashsave_schedule.h"

int autosave_interval_pulses(int interval_seconds, int tics_per_second) {
    if (tics_per_second < 1) {
        tics_per_second = 1;
    }
    if (interval_seconds < 1) {
        interval_seconds = 1; // never faster than once per second
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
