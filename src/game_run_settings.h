/* game_run_settings.h */

#ifndef GAME_RUN_SETTINGS_H
#define GAME_RUN_SETTINGS_H

#include "platdef.h"

#include <memory>

class ConnectionEntryPolicy;
class GameTickSchedule;

// What run_the_game needs from the main() that starts the server, so a new start-up choice is
// one more member here rather than one more parameter on each function it passes through.
struct GameRunSettings {
    // The port the listening socket opens on.
    sh_int port;
    // Decides each pass's world updates and is kept alive until the game loop ends; with a null
    // schedule no world update runs, and the loss is logged once.
    std::shared_ptr<GameTickSchedule> schedule;
    // Offered each line typed at the account email prompt. Not owned: it must outlive
    // run_the_game.
    ConnectionEntryPolicy& entry_policy;
};

#endif /* GAME_RUN_SETTINGS_H */
