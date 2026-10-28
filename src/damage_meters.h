/* damage_meters.h */

#ifndef DAMAGE_METERS_H
#define DAMAGE_METERS_H

#include <string>

struct char_data;
class player_damage_details;

// Adds elapsed_seconds of combat time to the personal and group damage meters of every
// character on combat_list. A fighter that keeps a personal meter gets one here if it has none
// yet, so its combat time counts from the first round; other fighters' personal meters are left
// alone. It changes nothing else about the fight.
void tick_damage_meters(float elapsed_seconds);

// True for a character whose damage the `damage` report meters: a player, or a charmed mob
// that has a master.
bool keeps_damage_meter(const char_data& character);

// Returns the character's personal damage meter, creating an empty one on first use. The
// character owns it until free_char.
player_damage_details& ensure_damage_meter(char_data& character);

// Empties the character's personal damage meter. A character with no meter is left without one.
void reset_damage_meter(char_data& character);

// The character's personal damage report. A character with no meter gets the report of an empty
// meter.
std::string get_damage_meter_report(const char_data& character);

// Deletes the character's personal damage meter, if it has one, and leaves it with none.
void free_damage_meter(char_data& character);

#endif /* DAMAGE_METERS_H */
