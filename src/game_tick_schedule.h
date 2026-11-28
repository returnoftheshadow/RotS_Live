/* game_tick_schedule.h */

#ifndef GAME_TICK_SCHEDULE_H
#define GAME_TICK_SCHEDULE_H

// Decides which periodic world updates each pass of the game loop runs, and the game hour's
// fast-update phase.
class GameTickSchedule {
public:
    // The periodic world updates due on one game-loop pass.
    struct WorldPass {
        // The game hour's fast-update phase from this pass until the next, 0 to
        // FAST_UPDATE_RATE - 1. It advances by one, wrapping to 0, only on a pass that runs
        // the fast update.
        int time_phase = 0;
        // True when zones age and reset on this pass.
        bool run_zone_update = false;
        // True when mobiles act on this pass.
        bool run_mobile_activity = false;
        // True when combat advances on this pass.
        bool run_violence = false;
        // True when the game hour advances on this pass. Such a pass also runs the fast
        // update, at phase 0.
        bool run_hourly_update = false;
        // True when regeneration and affects advance on this pass.
        bool run_fast_update = false;
        // True when skill cooldowns count down on this pass.
        bool run_skill_timer = false;
    };

    virtual ~GameTickSchedule() = default;

    // Advances the schedule by one game-loop pass and returns the updates due on it.
    // Call it exactly once per pass.
    [[nodiscard]] virtual WorldPass next_pass() = 0;
};

#endif /* GAME_TICK_SCHEDULE_H */
