/* game_heartbeat.h */

#ifndef GAME_HEARTBEAT_H
#define GAME_HEARTBEAT_H

#include "crashsave_schedule.h"
#include "lap_timer.h"

// Runs the periodic work that ends each game-loop pass: the world updates that are due, then
// the server's own housekeeping.
class GameHeartbeat {
public:
    // Runs one pass's periodic work. Call it once per game-loop pass, after output is written.
    void run_pass();

private:
    // Decides when the periodic crash-save of every player is due.
    AutosaveTimer autosave_timer;
    // Times each pass in real seconds, for the damage meters.
    LapTimer combat_timer;
};

#endif /* GAME_HEARTBEAT_H */
