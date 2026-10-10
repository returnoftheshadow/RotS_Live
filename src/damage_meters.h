/* damage_meters.h */

#ifndef DAMAGE_METERS_H
#define DAMAGE_METERS_H

// Adds elapsed_seconds of combat time to the personal and group damage meters of every
// character on combat_list. It changes nothing else about the fight.
void tick_damage_meters(float elapsed_seconds);

#endif /* DAMAGE_METERS_H */
