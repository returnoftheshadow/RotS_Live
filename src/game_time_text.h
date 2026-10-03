#ifndef GAME_TIME_TEXT_H
#define GAME_TIME_TEXT_H

#include <string>

// The game-time text every reader shares: the MSDP WORLD_TIME value and the `time` command's
// report. Both change only when the game hour changes, so they are built once per hour instead
// of once per reader. Both are std::string rather than a view so callers can pass c_str()
// straight to the C-string APIs that send text, without copying it.
namespace game_time_text {

// Rebuilds both texts from the current game time, moon and sun times. reset_time() and
// another_hour() call this after changing them; before the first call both texts are empty.
void refresh();

// The hour alone, such as "It is about 11:00 PM". The reference stays valid, and its text
// unchanged, until the next refresh().
const std::string& hour_text();

// The full report the `time` command prints, line breaks included. The reference stays valid,
// and its text unchanged, until the next refresh().
const std::string& time_report();

} // namespace game_time_text

#endif // GAME_TIME_TEXT_H
