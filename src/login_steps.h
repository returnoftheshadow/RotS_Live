/* login_steps.h */

#ifndef LOGIN_STEPS_H
#define LOGIN_STEPS_H

#include "account_character_selection.h"

#include <string>
#include <string_view>

struct descriptor_data;
namespace account {
struct AccountData;
}

// What admit_selected_character did with the connection's selected character.
enum class Admission {
    // Refused (site ban or wizlock); the connection has been told and is closing.
    Refused,
    // Joined to the character's body already in the game; the connection is playing.
    Reconnected,
    // Cleared to enter the game; the connection still has to enter it.
    Admitted,
};

// The directory account storage is read from and written to, relative to the game's data
// directory. A character array, as the account functions take std::string.
inline constexpr char ACCOUNT_STORAGE_ROOT[] = ".";

// Records account_data as the account the connection is logged in to, and clears the
// connection's failed password count.
void bind_connection_to_account(descriptor_data& connection, const account::AccountData& account_data);

// Loads character_name, a character linked to account_data, onto the connection, migrating it to
// account storage first when it has not been. The connection must already be bound to
// account_data (bind_connection_to_account): the character's files are read from the account the
// connection is bound to. Refuses, as the roster does, when another session on the account forbids
// the character. On success the player file is saved with its failed-login count cleared and the
// character's object data is staged for entering the game. When refused, the selected character
// is not on the connection; a refusal because the runtime support files could not be prepared
// comes after the player file was saved.
[[nodiscard]] AccountCharacterSelection select_account_character(descriptor_data& connection,
    const account::AccountData& account_data, std::string_view character_name);

// Applies the site ban and wizlock to the connection's selected character, and joins the
// connection to that character's body if it is already in the game: switched, link-dead, or
// played by another connection, which loses it.
[[nodiscard]] Admission admit_selected_character(descriptor_data& connection);

// Applies account_data's session rule to the connection's character, consuming the account's
// character-selection unlock when the character needs it. Returns false, with the game's
// refusal in out_refusal, when another session on the account forbids the character.
[[nodiscard]] bool claim_account_session(descriptor_data& connection,
    const account::AccountData& account_data, std::string& out_refusal);

// Puts the connection's admitted character into the game, in its load room, and sends the
// arrival text and a prompt. A body of the character already in the game is taken over
// instead, as admit_selected_character does. A connection logged in to an account claims its
// session with claim_account_session first. When the login's casts free the character, the
// connection is left with no character and closing; when they extract it with no room to go
// to, it is left at the character menu.
void enter_game(descriptor_data& connection);

#endif /* LOGIN_STEPS_H */
