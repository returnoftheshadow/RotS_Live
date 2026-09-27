#include "character_identity.h"

#include "handler.h"
#include "structs.h"

character_identity character_identity::capture(const char_data& character) {
    return character_identity{character.abs_number, &character, character.registration_serial};
}

// Only the registry's current owner of the slot is read. The captured address may name freed
// storage, or storage that a new character now occupies, so it is only compared; the serial then
// tells a new registration at the old address apart from the captured one.
char_data* character_identity::resolve() const {
    char_data* const slot_holder = char_by_abs_number(abs_number);
    if (slot_holder == nullptr || slot_holder != pointer) {
        return nullptr;
    }
    if (slot_holder->registration_serial != registration_serial) {
        return nullptr;
    }
    return slot_holder;
}
