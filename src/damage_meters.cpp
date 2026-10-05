/* damage_meters.cpp */

#include "damage_meters.h"

#include "structs.h"

extern struct char_data* combat_list;

//============================================================================
void tick_damage_meters(float elapsed_seconds)
{
    for (char_data* fighter = combat_list; fighter != nullptr; fighter = fighter->next_fighting) {
        fighter->damage_details.tick(elapsed_seconds);
        if (fighter->group != nullptr) {
            fighter->group->track_combat_time(fighter, elapsed_seconds);
        }
    }
}
