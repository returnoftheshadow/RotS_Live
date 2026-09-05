#ifndef ACCOUNT_PPC_H
#define ACCOUNT_PPC_H

#include "account_management_types.h"

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

#endif /* ACCOUNT_PPC_H */
