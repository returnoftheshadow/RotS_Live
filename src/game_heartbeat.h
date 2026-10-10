/* game_heartbeat.h */

#ifndef GAME_HEARTBEAT_H
#define GAME_HEARTBEAT_H

#include "crashsave_schedule.h"
#include "game_tick_schedule.h"
#include "lap_timer.h"

#include <memory>

// Runs the periodic work that ends each game-loop pass: the world updates its tick schedule
// says are due, then the server's own housekeeping. It publishes the process-wide time
// phase, so a process runs one heartbeat.
class GameHeartbeat {
public:
    // Observes schedule without keeping it alive. Once the schedule is gone, or if it is
    // null, no world update runs, the last published time phase stands, and the loss is
    // logged once.
    explicit GameHeartbeat(const std::shared_ptr<GameTickSchedule>& schedule);

    // A copy would ask the same schedule for a pass of its own.
    GameHeartbeat(const GameHeartbeat&) = delete;
    GameHeartbeat& operator=(const GameHeartbeat&) = delete;

    // Runs one pass's periodic work. Call it once per game-loop pass, after output is written.
    void run_pass();

private:
    // Logs that there is no schedule, the first time it is called.
    void report_schedule_loss();

    // Decides which world updates each pass runs. Not owned.
    std::weak_ptr<GameTickSchedule> tick_schedule;
    // True once the loss of the schedule has been logged, so it is logged only once.
    bool schedule_loss_logged = false;
    // Passes counted for the housekeeping cadences; restarts after 2400.
    int pulse = 0;
    // Decides when the periodic crash-save of every player is due.
    AutosaveTimer autosave_timer;
    // Times each pass in real seconds, for the damage meters.
    LapTimer combat_timer;
};

#endif /* GAME_HEARTBEAT_H */
