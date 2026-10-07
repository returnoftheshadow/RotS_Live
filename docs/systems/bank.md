# Player bank (account vaults)

**Source files:** `src/mob_progs/banker.cpp`, `src/mob_progs/banker.h`,
`src/game_boot_options.cpp`, `src/game_boot_options.h`
**Status:** ✅ done

## Purpose

Every account has a small vault for items and coins, one per side, reached through any banker
mob (mob program 34) on that side. All of an account's characters on a side share the vault.
Immortals inspect and adjust vaults with `vault`. The vault limits live in a game settings file
that staff change with `gameoptions`.

## Data structures

- `banker_config` (`banker.h`): one banker's parsed options: `hours`, `fee` (copper per item per
  day), `maxdays`, `markup` (percent for another race), `greeting` / `greeting_other` (the line
  above the vault on `balance`), and `ok` (false = does no business).
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
Only a missing file is: one that exists but cannot be opened (wrong owner or mode after a
restore, no file handles left) is treated as unreadable, below.

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

`bank_vault_open` (`banker.cpp:380`) holds the **only** in-memory copy of each vault, keyed by
normalized account name + side, loaded on first use and kept for the boot. Only the account's
name is a key: an email is refused (`vault_location`, `banker.cpp:313`), since it would find the same folder under
a second key, and so is the shared folder that every unresolvable name maps to. Bankers and the
`vault` command both go through it, so an immortal's change is what the player sees next.
`bank_vault_write` (`banker.cpp:443`) writes a temp file and renames it over the real one.

An unreadable file is refused (`readable == false`) and never overwritten; it is logged once
per reason, not once per command. Other vaults keep working. A refused vault holds nothing in
memory, so `bank_vault_open` reads the file again on every use: once staff repair or replace
it, it opens without a reboot.

A vault that was read successfully is never read again, and "missing, so empty" stays empty in
memory, so the player's next deposit would write over a file restored underneath it. **A
readable vault file is restored or edited by hand only with the server down.**

Restoring one vault from a backup of `lib/accounts`: stop the server, copy that account's
`vault_<side>.json` back into its folder (owner and mode like the files beside it), start the
server. The vault is then as it was at the backup: anything deposited since is gone, and
anything withdrawn since is back in the vault as well as on the character.

The write is temp file + rename with no `fsync`, the same as the account files; that is on the
account durability backlog, not handled here. After a power loss a vault file can come back
empty; it is then unreadable (never overwritten), and its contents come from a backup.

### Save order

Every change writes two things, ordered so a crash in between leaves a duplicate, never a loss:

| Action | First | Second |
|--------|-------|--------|
| deposit, `vault put` | vault file | character |
| withdraw, `vault take` | character | vault file |

A failed vault write on deposit refuses the transaction. A withdrawal or `vault take` first
writes the unchanged vault, and refuses if that fails: a vault that cannot be saved would hand
the same thing out again after every reboot. The character save (`bank_save_character`:
`save_char` + `Crash_crashsave`) reports whether it worked (see "Knowing the character was
saved" below), and every action undoes itself when it did not. After a saved withdrawal the
second vault write follows; if that one fails it is a `SYSERR`, and `vault take` also tells
the immortal. `save_char` does nothing for a character with no connection, so
`vault take` and `vault put` are refused for one (a forced, linkless immortal), with a `(GC)`
line for whoever forced it.

### Fees and bank days

```
fee = min(days stored, maxdays) x fee per day x objects in the slot that still exist
```

A stored object whose prototype a builder has deleted since is dropped at withdrawal, so it is
not counted (`slot_fee`): the fee is for what comes back.

`bank_fee` (`banker.cpp:183`). A customer whose race differs from the banker's pays
`racial_markup` percent more, rounded up to a whole copper. Coins are never charged. The rate is
that of the banker being withdrawn from. The fee is taken from carried coins first, then from
vault coins. A purse in debt (negative) counts as empty: it pays nothing and is not topped up.

`bank_days_stored` counts how many day-start points lie between the deposit and now. A bank day
starts at the routine daily reboot: `daily_reboot_hour_utc`, default 10, counted in **UTC**, so it
never moves with daylight saving (5am Central in summer, 4am in winter). The same game option
drives the reboot itself (`point_update`, `limits.cpp`, through `daily_reboot_minutes_left`;
the hour used to be written there as 10), so an implementor can change it with `gameoptions`
and the two always stay together. It uses the real clock only: reboots never add a day, and it
is not 24 hours from the deposit. Same bank day = free.

### Banker options (mob editor field 42)

| Line | Meaning | Bad value |
|------|---------|-----------|
| `hours=6-20` | opening hours, vendor format | banker disabled |
| `fee=50` | copper per item per day, 1-10000 | banker disabled |
| `maxdays=30` | fee cap in days, 1-365; required with `fee=` | banker disabled |
| `racial_markup=yes` | 30%; or a number 1-3000 | banker disabled |
| `greeting=<message>` | the line above the vault on `balance` | - |
| `greeting_other=<message>` | the same, for a customer of another race than the banker | - |
| `attacks=yes` or `no` | `no`: the banker never starts a fight (see barter-vendors.md, "Who a vendor serves, and whether it fights"). Left out = `yes` | line ignored |

`racial_markup=` with no `fee=` line at all only warns on `/implement`, as do a missing NOBASH
flag, a greeting longer than 78 columns (it still shows, wrapped), and with `attacks=no` each of
the SCAVENGER, AGGR, MEMORY, HELPER, BODYGUARD, HUNTER and ASSISTANT flags (`<flag> flag with
attacks=no`). Race aggression is
never reported: it is how a keeper chooses who it serves. Put only one keeper in a room: the
first one answers every command and the second can't be reached.

Greetings: the greeting is the plain header line above the `balance` listing, as the old
shops open `list` with "You can buy:". Nobody speaks it. A builder's message is sent exactly as
written, to the customer alone. "Another
race" is the test `racial_markup` uses (customer race != banker race) and does not need a
markup. With only `greeting=`, every customer gets it; with only `greeting_other=`, the banker's
own race gets the default header, `Your vault:`.
Configs are parsed at boot (`banker_config_boot`) and on mob save / implement
(`shapemob.cpp`), like barter vendors.

### Player commands (`SPECIAL(banker)`, `banker.cpp:1153`)

`balance`, `deposit <item>`, `deposit <N> gold|silver|copper`, `withdraw <item or #>`,
`withdraw <N> gold|silver|copper`. Only loose inventory can be deposited. The bank refuses what
rent refuses (`Crash_is_unrentable`) and anything cursed (`ITEM_NODROP`, as `give` and `drop`
do), for the item and everything inside it. A container is one slot and is sealed while stored;
`balance` marks every container `(sealed, N inside)`, an empty one with 0. Each deposit and
withdrawal logs one `BANK:` line.

The banker refuses customers the way barter vendors do (aggressive, shadow, race check, can't
see, closed), plus anyone with no side, any immortal by level (whatever their race) and anyone
with no account on the connection. Damage, blinding dust and gifts are refused like a vendor's.

The banker speaks the way the old shopkeepers do, through the vendor's helpers in
`shopkeeper.cpp`. A refusal to serve at all is a real `say` the room hears (`vendor_say`): the
vendor's refusals, "I'm closed. Come back later.", "I hold nothing for your kind." and "The bank
is closed for now." (bad options). Every other reply is a tell to the customer
(`vendor_tell`, which also reaches a customer with `notell` on or whom the banker can't see): what to deposit or withdraw, "You don't have that.", vault full, fees, gifts,
attacks, and the vault faults ("I can't find your account.", "I can't open your vault right
now.", "I can't reach the vault right now.", "I can't get that out right now."). `say` needs
INT 6 or more, so a banker below that is warned (`intelligence below 6 - banker can't speak`).
The room still sees `$n deposits $p.` / `$n withdraws $p.`.

Stored flags are kept as carried, `ITEM_WILLPOWER` (mystic attune) included, exactly as rent
does; attune is currently disabled. A change to how that flag is kept must be made for rent
(`Crash_obj2store` / `Crash_obj2char`) and the vault (`append_record`, `banker.cpp:523`) together.

### Settings file and `gameoptions`

`lib/misc/game_boot_options.json`, read once at boot (`boot_options_load`,
`game_boot_options.cpp:104`). Settings (`game_boot_options.cpp:17`):

| Name | Default | Range |
|------|---------|-------|
| `bank_slots` | 10 | 1-100 |
| `bank_coin_limit_gold` | 1000 | 0-100000 |
| `daily_reboot_hour_utc` | 10 | 0-23 |

A missing file or key uses the default. An out-of-range number uses that setting's default and
logs a warning. A file that is not valid JSON, or holds a value that is not a number, uses every
default and logs one warning; `gameoptions` then says so and refuses to change any setting until
the file is fixed or removed, because a change would write those defaults over it. Nothing
in the file can stop the boot. `gameoptions` (level 100) lists the settings;
`gameoptions <name> <number>` validates, writes the file at once, and takes effect at the next
reboot. Lowering a limit below what a vault holds removes nothing; the vault takes no more
until it is back under.

### `vault` (level 97, `do_vault`, `banker.cpp:1591`)

`vault <character | email | account> [1|2|3]`, `vault take|put <account> <1|2|3>
<slot | item | coins <amount> [gold|silver|copper]>`. Viewing tries the name as an account
first, then as a character; a character shows its own side unless a side is given. An account
with no side shows one summary line per side (coins and slots used) and a hint at the full
command; the slots, and what is inside stored containers, are listed for a single side only.
Every view goes through the pager. `take` and `put` are matched before any name, so an account
or character named Take or Put is viewed by email. `take` and `put` accept only the exact
account name, and are refused from a switched mob body (a mob is never saved). Every `vault`
command logs one `(GC)` line holding what was typed, with runs of spaces collapsed; its output
is not logged. A take or put that really happens logs a second line naming what moved, in the
form of the players' lines: `BANK: <immortal> takes <item> (<vnum>) from the vault of account
<name>, side <n>`. `vault put` is an immortal's judgment and is not bound by the banker's
cursed-item refusal.

### What comes out of the vault

A slot is rebuilt the way the rent load rebuilds gear (`bank_rebuild_records`, calling
`Crash_obj2char`): a fresh copy of the prototype, with the stored flags, timer, bitvector and
affects put back, so a builder's change to an item's values or weight reaches banked copies as
it reaches rented ones. Two differences from rent:

- **Wands and staves keep their charges.** The rent load refills them (`Crash_obj2char` keeps
  stored values only for lights and drink containers); the vault puts the stored `value[2]`
  back, never more than a new one has. Rent itself is unchanged.
- **A deleted prototype is skipped, not fatal.** What still exists is handed out and each
  missing vnum is logged (`BANK: stored object #<vnum> no longer exists and was dropped ...`).
  When a container is gone its contents move up into the nearest surviving container, or come
  out loose; loose contents are all handed over even past the carry limits, since they have
  nowhere else to go, and nothing is put back in the vault. A slot holding only deleted
  objects is cleared, with no fee. Impossible nesting in the file is still refused.

### Knowing the character was saved

`save_char` and `Crash_crashsave` return nothing and fail quietly. `bank_save_character`
clears two runtime-only flags on the character (`specials.saved_character_file`,
`specials.saved_object_file`, `structs.h`), runs both saves, and reads the flags: `save_char`
sets its flag only where the account's character file was written, `Crash_crashsave` only
after the object file, its alias section, the close and the account's copy all succeeded. No
other caller looks at them. On a withdraw (item or coins) and on `vault take`, a false result
takes the item or coins back off the character, leaves the vault untouched and says so
("I can't reach the vault right now." / "Your character could not be saved. Nothing was
taken."). A save that failed part-way can leave the item in the character's file as well: an
extra copy after a reboot, never a loss. A deposit (item or coins) and `vault put` check the
same result: the vault is written first, the item or coins come off the character, and if the
character is then not saved they go back on the character and off the vault again, with the
same messages ("Nothing was put into the vault." for `vault put`). Without that, the unsaved
character file would still hold what the vault now holds too. The object file is written only when the
character file was (`bank_save_character`), so an undo never finds an object file already
rewritten without the item. If the undo's own vault rewrite fails as well, the item is on the
character and still in the vault file: a second copy after a reboot. That needs two failures in
a row. The vault in play is right, only its file is behind: `bank_vault_write` marks the vault
`file_behind` until a write works, and the next `bank_vault_open` (any balance, deposit,
withdrawal or staff look) writes it again, so the extra copy goes away by itself. A shutdown,
reboot or service stop writes every such vault once more first (`bank_vaults_write_behind`, called
from `game_loop` in `comm.cpp` and from `hupsig` in `signals.cpp`), because the files are what the
next boot reads; one that still can't be written is reported (`SYSERR: bank: account <name> side
<n>: vault file still not written at shutdown (<error>)`). Staff are told
on the syslog and online from area god up with their log at normal (`SYSERR: bank: account
<name> side <n>: vault file write failed (<error>); the file may still hold <what>`, `mudlog`), as an incident report only: staff check with their own tools,
remembering that `vault` shows the vault in play, not the file. The write that settles it is
sent the same way (`BANK: account <name> side <n>: vault file written again`).
An immortal whose `vault take` raised the alert and who sees mudlog is not told a second time.
The same retry and alert cover a vault write that fails after a
withdrawal or a `vault take`. An item withdrawal
whose object file fails after the character file was written gives the fee back to the purse
and writes the character file again, so a crash cannot keep the fee while the item stays in
the vault. If that second save fails too, staff are told how much the character file is
short, as an incident report only (`SYSERR: bank: <name>: withdrawal undone; the character file
may be <N> copper short (fee)`); staff check with their own tools. When the first save never wrote
the character file, its purse on disk is still whole: there is no second save and no alert
(`bank_save_character` reports which file was written). The object file's account copy is
refreshed only from an object file written whole (`Crash_crashsave`, `objsave.cpp`), so a cut-off
save can't replace the account's good copy.

## RotS-specific notes

- `command_interpreter` parses a command's targets before the specials run and, after the command
  special returns, still runs the `SPECIAL_TARGET` specials on those targets (`interpre.cpp`, the
  `CMD_SELL` "TEMPORARY" skip is the same hazard). A deposited object is destroyed, so `bank_deposit`
  removes it, and anything inside it, from the parsed targets first (`vendor_forget_target`,
  `shopkeeper.cpp`). The barter vendor does the same for each object it takes as payment:
  `buy ruby` for a ring priced in rubies names the ruby as the command's target.
- The in-game `linkaccount` command is removed. It put the name of the account being linked to
  on the connection before checking a password, and the banker keys the vault off that name; a
  forced bank command at that prompt reached another account's vault. Nobody can be in the game
  without an account any more, so the command had no use. Legacy characters are claimed from the
  account menu.

- No stock CircleMUD bank code is involved. `balance` / `deposit` / `withdraw` were already
  commands 138-140 bound to `do_not_here`; program 34 catches them.
- Stored items are frozen like rented ones and do not count toward zone load limits.
- Object version refresh on withdraw (PR #343) is not wired yet; the call site is marked in
  `bank_obj_from_records` (`banker.cpp:607`).

## Open questions

- None in code. Whether backups and deploys cover `lib/misc/game_boot_options.json` and the
  `vault_*.json` files is a server-side question.
