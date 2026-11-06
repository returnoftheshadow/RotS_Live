/* damage_meters.cpp */

#include "damage_meters.h"

#include "char_utils.h"
#include "structs.h"

extern struct char_data* combat_list;

//============================================================================
void tick_damage_meters(float elapsed_seconds)
{
    for (char_data* fighter = combat_list; fighter != nullptr; fighter = fighter->next_fighting) {
        if (keeps_damage_meter(*fighter)) {
            ensure_damage_meter(*fighter).tick(elapsed_seconds);
        }
        if (fighter->group != nullptr) {
            fighter->group->track_combat_time(fighter, elapsed_seconds);
        }
    }
}

//============================================================================
bool keeps_damage_meter(const char_data& character)
{
    if (utils::is_pc(character)) {
        return true;
    }
    return character.master != nullptr && utils::is_affected_by(character, AFF_CHARM);
}

//============================================================================
player_damage_details& ensure_damage_meter(char_data& character)
{
    if (character.damage_details == nullptr) {
        character.damage_details = new player_damage_details();
    }
    return *character.damage_details;
}

//============================================================================
void reset_damage_meter(char_data& character)
{
    if (character.damage_details != nullptr) {
        character.damage_details->reset();
    }
}

//============================================================================
std::string get_damage_meter_report(const char_data& character)
{
    if (character.damage_details != nullptr) {
        return character.damage_details->get_damage_report(&character);
    }
    // An empty meter writes the "no damage" report, so the text matches a reset meter's exactly.
    const player_damage_details empty_meter;
    return empty_meter.get_damage_report(&character);
}

//============================================================================
void free_damage_meter(char_data& character)
{
    delete character.damage_details;
    character.damage_details = nullptr;
}
