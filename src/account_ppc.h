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

#endif /* ACCOUNT_PPC_H */
