# Player bank (account vaults)

**Source files:** `src/mob_progs/banker.cpp`, `src/mob_progs/banker.h`,
`src/game_boot_options.cpp`, `src/game_boot_options.h`
**Status:** ✅ done

## Purpose

Every account has a small vault for items and coins, one per side, reached through any banker
mob (mob program 34) on that side. All of an account's characters on a side share the vault.
Immortals inspect and adjust vaults with `vault`. The vault limits live in a game settings file
that staff change with `bootoptions`.

## Data structures

- `banker_config` (`banker.h`): one banker's parsed options: `hours`, `fee` (copper per item per
  day), `maxdays`, `markup` (percent for another race), and `ok` (false = does no business).
- `bank_slot`: one stored item: `deposited` (real time) and `objects`, the item followed by its
  contents. Each entry is an `objects_json::ObjectRecord`, the same record the character object
  save uses, with `wear_pos` reused as nesting depth (0 = the item itself).
- `bank_vault`: `coins` (copper), `slots`, and `readable` (false when the file on disk could not
  be read).
- Sides (`bank_side_for_race`, `banker.cpp:152`): 1 light = races 1-6; 2 dark = Uruk 11, Orc 13,
  Olog-hai 17; 3 third = Magus 15, Haradrim 18. Any other race has no vault.

## Format / Algorithm

### Files

One JSON file per side in the account's own folder (`lib/accounts/<bucket>/<email>/`):
`vault_light.json`, `vault_dark.json`, `vault_third.json`. A missing file is an empty vault.

```json
{
  "version": 1,
  "coins": 142500,
  "slots": [
    { "deposited": 1790000000, "objects": [ { "item_number": 5410, "wear_pos": 0, "...": 0 } ] },
    { "deposited": 1790086400, "objects": [
        { "item_number": 2101, "wear_pos": 0, "...": 0 },
        { "item_number": 7005, "wear_pos": 1, "...": 0 } ] }
  ]
}
```

A file is rejected (`deserialize_bank_vault`) when the version is not 1, coins are negative, a
slot has no objects, its first object is not depth 0, or a depth jumps by more than one.

### The vault table

`bank_vault_open` (`banker.cpp:347`) holds the **only** in-memory copy of each vault, keyed by
normalized account name + side, loaded on first use and kept for the boot. Bankers and the
`vault` command both go through it, so an immortal's change is what the player sees next.
`bank_vault_write` (`banker.cpp:377`) writes a temp file and renames it over the real one.

An unreadable file is logged once, refused from then on (`readable == false`), and never
overwritten. Other vaults keep working.

### Save order

Every change writes two things, ordered so a crash in between leaves a duplicate, never a loss:

| Action | First | Second |
|--------|-------|--------|
| deposit, `vault put` | vault file | character |
| withdraw, `vault take` | character | vault file |

A failed vault write on deposit refuses the transaction. The character save
(`bank_save_character`, `banker.cpp:335`: `save_char` + `Crash_crashsave`) reports no result,
so a withdrawal always goes on to the vault write.

### Fees and bank days

```
fee = min(days stored, maxdays) x fee per day x objects in the slot
```

`bank_fee` (`banker.cpp:183`). A customer whose race differs from the banker's pays
`racial_markup` percent more, rounded up to a whole copper. Coins are never charged. The rate is
that of the banker being withdrawn from. The fee is taken from carried coins first, then from
vault coins.

`bank_days_stored` (`banker.cpp:177`) counts how many day-start points (default 5am, server
**local** time, so it follows daylight saving) lie between the deposit and now. It uses the real
clock only: reboots never add a day, and it is not 24 hours from the deposit. Same bank day =
free.

### Banker options (mob editor field 42)

| Line | Meaning | Bad value |
|------|---------|-----------|
| `hours=6-20` | opening hours, vendor format | banker disabled |
| `fee=50` | copper per item per day, 1-10000 | banker disabled |
| `maxdays=30` | fee cap in days, 1-365; required with `fee=` | banker disabled |
| `racial_markup=yes` | 30%; or a number 1-300 | banker disabled |

`racial_markup=` without `fee=` only warns on `/implement`, as does a missing NOBASH flag.
Configs are parsed at boot (`banker_config_boot`) and on mob save / implement
(`shapemob.cpp`), like barter vendors.

### Player commands (`SPECIAL(banker)`, `banker.cpp:930`)

`balance`, `deposit <item>`, `deposit <N> gold|silver|copper`, `withdraw <item or #>`,
`withdraw <N> gold|silver|copper`. Only loose inventory can be deposited. The bank refuses what
rent refuses (`Crash_is_unrentable`), for the item and everything inside it. A container is one
slot and is sealed while stored. Each deposit and withdrawal logs one `BANK:` line.

The banker refuses customers the way barter vendors do (aggressive, shadow, race check, can't
see, closed), plus anyone with no side (immortals) or no account on the connection. Damage,
blinding dust and gifts are refused like a vendor's.

### Settings file and `bootoptions`

`lib/misc/game_boot_options.json`, read once at boot (`boot_options_load`,
`game_boot_options.cpp:97`). Settings (`game_boot_options.cpp:15`):

| Name | Default | Range |
|------|---------|-------|
| `bank_slots` | 10 | 1-100 |
| `bank_coin_limit_gold` | 1000 | 0-100000 |
| `bank_day_start_hour` | 5 | 0-23 |

A missing file or key uses the default. An out-of-range number uses that setting's default and
logs a warning. A file that is not valid JSON uses every default and logs one warning. Nothing
in the file can stop the boot. `bootoptions` (level 100) lists the settings;
`bootoptions <name> <number>` validates, writes the file at once, and takes effect at the next
reboot. Lowering a limit below what a vault holds removes nothing; the vault takes no more
until it is back under.

### `vault` (level 97, `do_vault`, `banker.cpp:1288`)

`vault <character>`, `vault <email or account> [1|2|3]`, `vault take|put <account> <1|2|3>
<slot | item | coins <amount> [gold|silver|copper]>`. Viewing tries the name as an account
first, then as a character. `take` and `put` accept only the exact account name. Every `vault`
command logs one `(GC)` line; its output is not logged.

## RotS-specific notes

- `command_interpreter` parses a command's targets before the specials run and, after the command
  special returns, still runs the `SPECIAL_TARGET` specials on those targets (`interpre.cpp`, the
  `CMD_SELL` "TEMPORARY" skip is the same hazard). A deposited object is destroyed, so `bank_deposit`
  removes it from the parsed targets first (`forget_target`).

- No stock CircleMUD bank code is involved. `balance` / `deposit` / `withdraw` were already
  commands 138-140 bound to `do_not_here`; program 34 catches them.
- Stored items are frozen like rented ones and do not count toward zone load limits.
- Object version refresh on withdraw (PR #343) is not wired yet; the call site is marked in
  `bank_obj_from_records` (`banker.cpp:483`).

## Open questions

- None in code. Whether backups and deploys cover `lib/misc/game_boot_options.json` and the
  `vault_*.json` files is a server-side question.
