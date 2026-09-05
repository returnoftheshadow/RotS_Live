#ifndef ACCOUNT_PPC_H
#define ACCOUNT_PPC_H

#include "account_management_types.h"

#include <string>

struct char_data;

/* Player Preferred Config: the account-owned settings that describe how a player wants to
   see and hear the game. The account is the store; the character struct stays the runtime
   representation, so nothing that reads a PPC value has to change. Every operation here is
   masked with PPC_PRF_MASK, so no other preference bit is ever read or written.
   See docs/superpowers/specs/2026-09-05-account-level-player-preferred-config-design.md. */

/* Snapshot a character's PPC. Returns present = true on success, or a default-constructed
   (present = false) value when ch or ch->profs is null. */
account::AccountPreferences ppc_read_from_character(const struct char_data* ch);

/* Apply stored preferences onto a character, leaving every non-PPC bit untouched.
   Does nothing when preferences.present is false, or ch/ch->profs is null. */
void ppc_write_to_character(const account::AccountPreferences& preferences, struct char_data* ch);

/* Compare two PPCs by value: masked flags plus both colour arrays. Ignores `present`.
   save_char uses this to write the account only when something actually changed. */
bool ppc_equal(const account::AccountPreferences& left, const account::AccountPreferences& right);

/* THE INVARIANT: a character may never write its PPC to the account before it has read the
   account's PPC. ppc_apply_account_to_character_in is the only thing that sets
   char_data::ppc_account_loaded (a runtime-only, never-serialized flag), and
   ppc_store_character_to_account_in refuses to write the account while it is false. Callers
   should still apply before they save -- it reads better -- but correctness does not depend on
   them getting the order right. */

/* Apply the account's stored PPC to a character at login. When the account has none yet,
   seed it from this character and write it once -- that is the migration path, and the
   seeding character is by definition the account's most recently played one.
   Never blocks login: a null/empty account name, a null character, or a failed read is a
   silent no-op, and a failed write is logged and otherwise ignored. */
void ppc_apply_account_to_character(const char* account_name, struct char_data* ch);

/* Same, against an explicit storage root. Production calls the "." form above; this exists
   so tests can drive a temporary directory. */
void ppc_apply_account_to_character_in(const std::string& root_directory,
    const char* account_name, struct char_data* ch);

/* Write a character's PPC back to its account, but only when it actually differs from what
   is stored, and only once this character has read the account's PPC (see THE INVARIANT
   above). The compare matters: write_account_file triggers a full account_cache flush,
   and the cache assumes account mutations never happen on the save path. Returns true only
   when a write occurred. */
bool ppc_store_character_to_account_in(const std::string& root_directory,
    const std::string& account_name, const struct char_data* ch);
void ppc_store_character_to_account(const std::string& account_name, const struct char_data* ch);

/* Copy one character's PPC onto another, masked. Used to keep an account's characters in
   step while more than one is online. */
void ppc_copy_between_characters(const struct char_data* source, struct char_data* destination);

/* Push this character's PPC to every other playing character on the same account. Called
   after any command that can change a PPC value. Without it, a second character's save
   would write its stale copy over the change the player just made. Walks descriptor_list --
   connected sockets, not linked characters -- so the cost is bounded by players online. */
void ppc_propagate_from(const struct char_data* ch);

/* The account's other character that is currently in the game (CON_PLYNG), or null. Shares its
   descriptor walk and account matching with ppc_propagate_from. Skips ch itself, NPCs and
   descriptors whose character has been detached. Exposed for tests. */
struct char_data* ppc_find_playing_sibling(const struct char_data* ch);

/* If such a sibling exists, adopt its in-memory PPC. Called by ppc_apply_account_to_character
   right after the account file has been applied, because the file can be a full autosave
   interval behind a change a sibling has already made. Deliberately CON_PLYNG only -- see the
   reasoning at ppc_find_playing_sibling. A no-op when there is no sibling. */
void ppc_prefer_playing_sibling(struct char_data* ch);

/* True when this account already has a stored PPC. Character creation uses it to skip the
   colour and latin-1 prompts, which would otherwise overwrite a scheme the player has
   already tuned. False for an unknown or unreadable account. */
bool ppc_account_has_preferences(const char* account_name);
bool ppc_account_has_preferences_in(const std::string& root_directory, const char* account_name);

#endif /* ACCOUNT_PPC_H */
