# Account-Level Player Preferred Config (PPC) — Design

Date: 2026-09-05
Status: Approved design, not yet implemented
Branch: `feat/account-level-ppc`

## Problem

A player's display and protocol settings are stored per character. Every alt starts from
defaults, and a player who has tuned a colour scheme and a prompt must redo that work on each
new character. These settings describe *how the player wants to see the game*, not anything
about the character, so per-character storage is the wrong home for them.

Accounts already exist and are mandatory: `CON_NME` is the account email prompt, and every path
into `CON_PLYNG` passes through an authenticated account. The account is the natural owner of
this data.

## Definition: PPC

**PPC ("player preferred config")** is the set of `set` options that define how the MUD is
viewed and how it communicates, as opposed to character state or in-the-moment tactical choices.
Twelve options:

`prompt`, `advancedprompt`, `advancedview`, `brief`, `compact`, `spam`, `wrap`, `echo`, `msdp`,
`latin1`, `spinner`, `sorting`

plus the full colour configuration: the 16 configurable colour slots and the `colour on/off`
toggle.

Everything else under `set` is character- or mood-specific and stays per character: `tactics`,
`wimpy`, `title`, `description`, `language`, `shooting`, `casting`, `nosummon`, `notell`,
`narrate`, `chat`, `sing`, `mental`, `swim`, `incognito`, and the immortal-only options.

The guiding principle: **one player, one fixed way to play.** A player does not want a different
colour scheme per character.

## Goals

- Store PPC on the account; every character on the account uses it.
- Keep the `set` and `color` commands exactly as they are today — same names, same syntax, same
  output. A player still configures from whichever character they happen to be playing.
- Change no game code that *reads* these settings.
- Migrate existing players with no data loss and no manual step.

## Non-goals

- Changing which options exist, or their syntax.
- Moving any non-PPC setting to the account.
- Fixing the `color`/`CMD_SEND` command-index collision (tracked separately).
- Fixing the dead `set time` option (dispatches `SCMD_TIME`, which `do_gen_tog` has no case for;
  tracked separately).

## Current state

| Data | Where it lives now |
|---|---|
| The 12 PPC toggles | `PRF_*` bits in `char_data`, serialized by `kPreferenceFlags[]`, `character_json.cpp:55` |
| Colour slots | `ch->profs->colors[16]` and `ch->profs->color_settings[16]`, `structs.h:1291` |
| Colour on/off | `PRF_COLOR` |
| Account record | `AccountData`, `account_management_types.h:28` — no preference storage of any kind, schema version 1 |
| Account name at runtime | `d->account_name`, `structs.h:2027`, set at login, valid for the whole session |

Relevant existing behaviour:

- `save_char` (`db.cpp:3170`) already resolves the owning account via
  `find_linked_character_owner_account` in order to write the account-native character file.
- `save_char` early-returns when `!ch->desc` (`db.cpp:3175`), so a linkdead character is never
  saved. There is no descriptor-less save path to design around.
- `write_account_file` triggers `account_cache::invalidate_all()`, a full flush of both memo
  maps. The cache header states that account mutations "are rare and never happen on the hot
  save/load path" (`account_cache.h:30`). The design must preserve that.
- Character creation asks two PPC questions of every new character: `CON_COLOR`
  (`interpre.cpp:4124`) and `CON_LATIN` (`interpre.cpp:4148`).
- An account can have two characters online simultaneously.
  `first_restricting_active_account_session_for_account` (`interpre.cpp:2499`) returns no
  restriction when the account has a high-level linked character, and a separate unlock path
  exists besides.

## Architecture

The account is the store. The character struct remains the runtime representation.

- **Load** copies account → character.
- **Save** copies character → account.
- **Change** propagates immediately to every other online character on the account.

Because the character fields keep their existing names and meanings, no code that reads a PPC
value changes. `PRF_FLAGGED`, `CC_USE`, the prompt builder, MSDP, and the colour renderer all
keep working untouched.

### Data model

New struct in `account_management_types.h`:

```c++
struct AccountPreferences {
    bool present = false;          // false = this account has no PPC yet (seeding trigger)
    long preference_flags = 0;     // the PPC-masked PRF bits
    unsigned char colors[MAX_COLOR_FIELDS];
    color_slot_data color_settings[MAX_COLOR_FIELDS];
};
```

added to `AccountData` as `AccountPreferences preferences;`.

### The PPC mask

One named constant is the single definition of PPC membership:

```
PRF_PROMPT | PRF_ADVANCED_PROMPT | PRF_ADVANCED_VIEW | PRF_BRIEF | PRF_COMPACT |
PRF_SPAM | PRF_WRAP | PRF_ECHO | PRF_MSDP | PRF_LATIN1 | PRF_SPINNER |
PRF_INV_SORT1 | PRF_INV_SORT2 | PRF_COLOR
```

Fourteen bits covering the twelve named options: `sorting` is a two-bit value
(`PRF_INV_SORT2:PRF_INV_SORT1` encoding default/grouped/alpha/length), and `PRF_COLOR` travels
with the colour scheme.

Every apply and compare operation is masked. Bits outside the mask are never read from or
written to the account, so a character's `notell`, `sing`, `incognito`, syslog level and
immortal flags are untouched by anything in this design.

### On-disk format

A `preferences` object inside `account.json`. Flags are written **by name**, reusing the
naming already established by `kPreferenceFlags[]` in `character_json.cpp:55`, not as a raw
integer — the file stays human-readable and survives any future bit renumbering. Colour slots
reuse the existing `ColorSettingData` JSON shape verbatim so the codebase has one colour
serialization format rather than two.

`ACCOUNT_SCHEMA_VERSION` goes from 1 to 2. A version-1 file has no `preferences` key and loads
successfully with `present == false`, which is exactly the signal that the account still needs
seeding. Old files are never rejected.

### Runtime flow

**`apply_account_ppc_to_character(ch, account_name)`** — reads the account through the cache.

- If `preferences.present`, overwrite the masked bits and both colour arrays on the character,
  leaving all other `PRF` bits alone.
- If not present, seed the other direction: populate the account's preferences from this
  character's current values and write the account once.

Called from the character-enter paths in `interpre.cpp`: the fresh-load path and both reconnect
paths (`interpre.cpp:2763`, `interpre.cpp:2800`, and the `CON_SLCT` enter-game path). It is
idempotent, so re-applying when a reconnect attaches to an already-loaded in-memory body is
harmless. It runs before the first output of the session, so `latin1` is correct for the login
text.

**Save — inside `save_char`.** After `char_to_store`, using the `owner_account_name` that
`save_char` already resolves: compare the character's masked bits and colour arrays against the
account's stored preferences and **write only if they differ**. The comparison is a cached read
plus a memcmp.

This compare-before-write is what protects the account cache. Account writes then happen only as
often as a player actually changes a setting — a rare, human-initiated event — rather than on
every save, so `invalidate_all()` is not called on the hot save path and the cache's stated
invariant holds. No dirty flag is required.

**`propagate_ppc_from(ch)`** — walks `descriptor_list` and, for every other playing character on
the same account, copies the masked bits and colour arrays across. Called at the tail of
`do_gen_tog`, `do_inventory_sort`, and `do_color` (`color.cpp:495`).

It is called unconditionally rather than only for PPC subcommands, so there is no per-subcommand
membership test to keep in sync with the mask. Cost is a walk of connected sockets — not linked
characters — with a few pointer checks and one normalized string compare each, on a
human-triggered command. The same walk already happens in `active_account_character_sessions`
(`interpre.cpp:2430`) during the account menu.

Without this, a player with two characters online who changes a setting on one would have it
silently reverted when the other character saves its stale in-memory copy.

## Migration

Implicit. No offline tool, no boot sweep.

An account with `present == false` is seeded the first time any of its characters enters the
game, from that character's own current settings. Because the seeding character is by definition
the one being played at that moment, "first login after deploy" and "most recently played
character" are the same thing.

Accounts that never log in again keep their version-1 file, which continues to load.

## Character creation

Both `CON_COLOR` and `CON_LATIN` are skipped when the account already has a PPC. Asking them on
a player's fifth alt is redundant, and answering them would overwrite a scheme the player has
already tuned.

Two helpers are extracted from the existing code rather than duplicating the branch at each of
the two entry points:

- **`begin_creation_appearance_prompts(d)`** — if the account has no PPC, send the colour blurb
  and return `CON_COLOR` (today's behaviour); otherwise apply the account PPC to the new
  character and go straight to the completion tail. Both current entry points call this: the
  `CON_QPROF` path (`interpre.cpp:4122`) and the custom point-allocation path that returns
  `CON_COLOR` on `=` (`interpre.cpp:4600`). This also collapses the colour blurb — currently
  duplicated at both sites and marked with a `/* Must sync with message in CON_COLOR above */`
  comment — into a single string, retiring that hazard.
- **`finish_new_character_creation(d)`** — the tail currently at the end of `CON_LATIN`: wimpy
  10, `introduce_char`, `show_character_menu`, `STATE = CON_SLCT`, and the "new player" log.
  Both the prompted and skipped paths end here, so creation cannot drift between them.

A brand-new account's first character still sees both prompts, and its answers seed the account.

## Failure handling

- **Account read fails at login:** `apply_account_ppc_to_character` logs and returns without
  touching the character, which keeps whatever its own file contained. A broken or unreadable
  account never blocks login and never blanks a player's colours.
- **Account write fails at save:** log and continue. The character save itself is unaffected.
  This matches how `save_char` already handles a failed account-character-file write
  (`db.cpp:3223`).
- **Version-1 account file:** loads with `present == false` and is seeded on next play.

## Testing

### Unit tests (`src/tests/`, run via `scripts/rots-docker.sh test`)

- `preferences` round-trips through serialize/deserialize, including a mix of default, ANSI-16
  and truecolor slots.
- A version-1 `account.json` with no `preferences` key loads successfully with
  `present == false`.
- Masked apply: a character with `notell`, `sing` and `incognito` set retains all three after an
  account PPC apply in which those bits are clear.
- Compare-before-write: an unchanged PPC produces zero account writes; a single changed bit
  produces exactly one.
- Seeding: `present == false` plus a character with a configured scheme produces an account
  whose PPC matches that character exactly.
- Propagation: given a set of characters on one account, a change to one lands on all of them.
  Written against the pure function so it needs no descriptor fixtures.

The existing suite has a known pre-existing segfault partway through on `release-frodo`, so it
must be baselined on a clean tree before any failure is attributed to this work.

### Live smoke tests

Booted server, via the local `testing/` harness and by hand:

1. Set `brief`, `compact` and a colour slot on character A, quit, log in as character B on the
   same account — B has all three.
2. A and B online simultaneously; change a setting on A — B reflects it without relogging.
3. Roll a new alt on a configured account — neither the colour nor the latin-1 prompt appears,
   and the character starts with the account's scheme.
4. Roll the first character on a fresh account — both prompts appear and the answers persist to
   the account.
5. Confirm `latin1` is applied early enough that login text renders correctly.

Per existing convention, in-game characters must be quit at the end of each smoke scenario; a
character left in the world causes later selections to reconnect to it and fakes the result.

## Files touched

| File | Change |
|---|---|
| `account_management_types.h` | `AccountPreferences` struct; `AccountData::preferences`; schema version 1 → 2 |
| `account_management_storage.cpp` | Serialize and deserialize the `preferences` object |
| `structs.h` | The PPC mask constant, beside the `PRF_*` defines it is built from |
| `db.cpp` | Compare-and-write the account PPC in `save_char` |
| `interpre.cpp` | `apply_account_ppc_to_character` calls on the enter-game paths; the two creation helpers; skip logic |
| `act_othe.cpp` | `propagate_ppc_from` at the tail of `do_gen_tog` and `do_inventory_sort` |
| `color.cpp` | `propagate_ppc_from` at the tail of `do_color` |
| `src/tests/` | New test file for the cases above |
