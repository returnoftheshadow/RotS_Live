#include "poison_origin.h"

#include "handler.h"
#include "structs.h"

// The recorded address is only compared, never dereferenced: the character it named may have been
// extracted and freed. char_by_abs_number() names whoever holds the slot now, and only the same
// address under the same registration serial counts. An extracted poisoner, a slot recycled by a
// different character and a re-registration at the old address therefore all resolve to null, as
// does a poisoner parked at the character menu, which is out of the game.
char_data* resolve_poisoner(const char_data& victim) {
    const poison_origin& origin = victim.specials.poisoned_by;
    if (origin.abs_number < 0 || origin.identity == nullptr) {
        return nullptr;
    }
    char_data* const slot_holder = char_by_abs_number(origin.abs_number);
    if (slot_holder != nullptr && slot_holder == origin.identity &&
        slot_holder->registration_serial == origin.registration_serial &&
        character_in_game(slot_holder)) {
        return slot_holder;
    }
    return nullptr;
}

// Every field is written at once: a slot left standing without its address, or the reverse, could
// answer for whoever holds that slot today.
void record_poison_origin(char_data* victim, char_data* poisoner) {
    if (poisoner == nullptr) {
        clear_poison_origin(victim);
        return;
    }
    victim->specials.poisoned_by.abs_number = poisoner->abs_number;
    victim->specials.poisoned_by.identity = poisoner;
    victim->specials.poisoned_by.registration_serial = poisoner->registration_serial;
}

void clear_poison_origin(char_data* victim) {
    victim->specials.poisoned_by.abs_number = -1;
    victim->specials.poisoned_by.identity = nullptr;
    victim->specials.poisoned_by.registration_serial = 0;
}
