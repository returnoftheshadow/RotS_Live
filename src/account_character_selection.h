/* account_character_selection.h */

#ifndef ACCOUNT_CHARACTER_SELECTION_H
#define ACCOUNT_CHARACTER_SELECTION_H

#include <string>

// The outcome of select_account_character.
struct AccountCharacterSelection {
    // True when the character is now on the connection.
    bool selected = false;
    // When not selected, the game's words, as the roster sends them.
    std::string refusal;
    // Failed password attempts recorded since the character's last login, shown with the MOTD.
    int failed_logins = 0;
};

#endif /* ACCOUNT_CHARACTER_SELECTION_H */
