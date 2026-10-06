# Player bank (account vault) — design

**Date:** 2026-09-30
**Status:** design agreed in discussion; spec awaiting review. Nothing built.
**Proposal page:** https://claude.ai/artifact/MCGj5saWAuv3rM7H4dDYpX

## Goal

Give every account a small vault for items and coins, reached through a **banker** mob. All
characters of an account on the same side share one vault, and every banker on that side opens
the same vault. Builders set a banker up through data only: mob program **34** plus the mob
options field that barter vendors (program 33) introduced.

Nothing like this exists today. `balance`, `deposit` and `withdraw` are already commands
138-140 (`src/interpre.cpp:2031-2035`) bound to `do_not_here`, waiting for a program to catch
them.

**Two hard requirements:**

- **No lost items.** Every transaction saves at once, in an order where a crash between the
  two writes leaves a duplicate, never a loss.
- **One copy of each vault in memory.** Players at bankers and immortal vault commands all go
  through the same in-memory vault, so no stale copy can overwrite a change.

## Vaults

- One vault per **account per side**. Up to three per account.
- Sides (matching `other_side_num()`, `src/handler.cpp:158`):

  | # | Side | Races |
  |---|------|-------|
  | 1 | light | Human, Dwarf, Wood Elf, Hobbit, High Elf, Beorning (races 1-6) |
  | 2 | dark | Uruk (11), Orc (13), Olog-hai (17) |
  | 3 | third | Magus (15), Haradrim (18) |

- A character with no side is refused by every banker. That covers immortals of race God;
  any other immortal is refused by level (91 and up), whatever their race.
- The vault belongs to the account. Deleting a character does not touch it.
- A vault holds **10 item slots** and up to **1,000 gold** in coins. Both limits come from the
  game settings file (below). Coins never use a slot.
- A vault is never loaded into or saved with a character.

## Storage

- One JSON file per side in the account's folder (`lib/accounts/<bucket>/<email>/`):
  `vault_light.json`, `vault_dark.json`, `vault_third.json`. A missing file is an empty vault.
- Contents: a schema version, the coins (in copper), and a list of slots. Each slot holds its
  deposit time (a real-time timestamp) and its objects: the stored item and, for a container,
  everything inside it.
- Objects are written with the same object record the character save uses
  (`objects_json::ObjectRecord`, `src/objects_json.h`), including how nesting is encoded, so
  the bank adds no second object format.
- Writes are atomic the same way account files are: write `<file>.tmp`, then rename.
- **In memory:** one table keyed by account + side. A vault is read from its file the first
  time it is needed in a boot and kept; every change is written straight to its file. There
  are 41 accounts on live, so the table stays tiny.
- A vault file that cannot be read is never overwritten: the banker refuses business for that
  vault, and one warning is logged. That includes a file that exists but cannot be opened
  (wrong owner or mode after a restore): only a file that is not there is an empty vault.
- Vault files are restored or edited by hand only with the server down.

## Player commands

All three only work at a banker; elsewhere they keep answering as they do today.

A banker speaks the way the old shopkeepers do (decided 2026-10-01, replacing the private
speech-shaped lines of earlier that day): a refusal to serve at all is a real `say` the room
hears, every other reply is a real `tell` to the customer. (The barter vendor does the same;
both use `vendor_say` / `vendor_tell`.)

### `balance`

Lists coins, slots used, and each stored item with what it costs to withdraw **at this
banker for this customer** (markup included). No days column. The fee column is left out at a
banker with no fee. A container shows as `(sealed, N inside)`, N being the items inside it
(0 for an empty one). The line above the list is the banker's greeting (see Banker setup), or
the plain line `Your vault:` when none is set. Nobody speaks it: it is the listing's header,
as the old shops open `list` with "You can buy:".
Money is printed with the game's own
wording (`money_message`: "1 silver and 50 copper"), since game text is meant to read as a
story. Output stays within 78 columns.

```
Coins: 142 gold and 5 silver (limit 1000 gold)
Slots: 3 of 10 used

 #  Item                                   Fee to withdraw
 1  a bastard sword                        1 silver and 50 copper
 2  a leather backpack (sealed, 3 inside)  6 silver
 3  a crisp ticket                         free
```

### `deposit <item>`

Stores one item from loose inventory (not worn). Refused, with nothing changed, when:

- the vault's slots are full;
- the item, or anything inside it, is something rent refuses (`Crash_is_unrentable`,
  `src/objsave.cpp:1271`: no-rent flag, keys, negative cost, no prototype);
- the item, or anything inside it, is cursed (`ITEM_NODROP`), as `give` and `drop` refuse it.

The customer reads `You hand over <item>.`

A container takes one slot however full it is, and is sealed while stored.

### `deposit <N> gold` / `silver` / `copper`

The player names the coin, so nobody has to work an amount out in copper. `coins` on its own
means copper. Free. If it would pass the coin limit, the banker takes what fits and says how much was
refused.

### `withdraw <item or #>`

By name or by the number `balance` shows. In order:

1. Work out the fee (below).
2. Refuse if the character can't carry it (item count or weight, a container counting its
   contents' weight). Nothing is charged.
3. Refuse if carried coins plus vault coins can't cover the fee. Nothing is taken.
4. Take the fee from carried coins first, then from vault coins, and say how much came from
   each.
5. Check that the vault file can be written (write it unchanged). If not, refuse: a vault
   that can't be saved would hand the same thing out again after every reboot.
6. Hand the item over (`You are handed <item>.`). Each object gets the version refresh that
   login applies (PR #343).

### `withdraw <N> gold` / `silver` / `copper`

Free. Refused if the vault holds fewer, or if the vault file can't be written.

### Stored items

- Frozen like rented items: no timers, no decay.
- Do not count toward zone load limits (same as rent).

## Fees

```
fee = min(days stored, maxdays) × fee per day × items
```

- **Items:** 1 for a plain item; for a container, itself plus everything inside it. The whole
  slot uses its one deposit date.
- **Rate:** the banker being withdrawn from.
- **Day:** a real day that starts at the routine daily reboot: the `daily_reboot_hour_utc`
  game option (default 10), counted in **UTC**, so it never moves with daylight saving (5am
  Central in summer, 4am in winter). The same option drives the reboot itself. Days stored =
  the number of those day-start points between the deposit time and now. It goes by the clock
  only: reboots and crashes never add a day, and it is not 24 hours from the deposit.
  Withdrawing before the next day start is free.
- **Racial markup:** applied when the customer's race differs from the banker mob's race.
  The marked-up total is rounded **up** to a whole copper.
- Coins are never charged.
- Fees are worked out only when asked (`balance`, `withdraw`). There is no daily pass.

## Banker setup (builders)

1. On the mob: `MOB_SPEC` and program number **34** (shaping field 29).
2. On the mob: options (shaping field 42), one per line, all optional:

   ```
   hours=6-20
   fee=50
   maxdays=30
   racial_markup=yes
   greeting=The clerk slides a heavy ledger across the counter.
   greeting_other=The clerk eyes you coldly and opens a ledger.
   ```

| Line | Meaning |
|------|---------|
| `hours=` | Opening hours, same format and parser as barter vendors. |
| `fee=` | Copper per item per day. Left out = free banker. |
| `maxdays=` | Most days a fee is charged for. Required when `fee=` is set. |
| `racial_markup=` | `yes` = 30%; a number 1-3000 = that percentage. |
| `greeting=` | The line above the vault on `balance`, in place of the default. |
| `greeting_other=` | The same, for a customer of another race than the banker. |

A greeting is sent exactly as written, to the customer alone. "Another race" is the test
`racial_markup` uses (customer race differs from the banker's) and needs no markup. With only
`greeting=`, every customer gets it. With only `greeting_other=`, the banker's own race gets the
default line. An empty value counts as not set.

- **Strict:** a bad `hours=`, `fee=`, `maxdays=` or `racial_markup=`, or `fee=` without
  `maxdays=`, means the banker does no business and a warning is logged, in the house
  warning style (type + vnum + line).
- **Soft:** `racial_markup=` with no `fee=` line at all has nothing to mark up, and a greeting
  longer than 78 columns wraps. The banker works; `/imp` tells the implementer only.
- Parsed ahead of time into an in-memory table at boot and when the mob is saved, like
  vendors.

### Who the banker refuses

The same checks and wording pattern as barter vendors (`vendor_serves`,
`src/mob_progs/shopkeeper.cpp:446`): aggressive to the customer, shadow form, race check,
can't see the customer, closed. Plus: no side, and any immortal by level.

### Protection

Like barter vendors: damage to the banker is cancelled through the `SPECIAL_DAMAGE` hook,
including poison ticks and room effects, and blinding dust on the banker is refused.

## Game settings file

A new file `lib/misc/game_boot_options.json` for game-wide settings staff can change without a
code change. Read once at boot.

| Setting | Default |
|---------|---------|
| bank item slots | 10 |
| bank coin limit (gold) | 1000 |
| bank day start hour | 5 |

- Missing file or missing setting: the built-in default.
- Bad value: the default, plus a boot warning. The file can never stop the game booting.
- Lowering a limit below what a vault already holds removes nothing. The vault simply takes
  no more until it is back under the limit.

### `gameoptions` (Implementor, level 100)

Named `gameoptions`, not `bootoptions`: commands match by prefix before socials do, so a
command starting with `bo` would take `bo` away from `bow` for anyone who can use it.

- `gameoptions`: lists every setting with the value in use, the value after the next reboot
  (only when different), the default and the allowed range.
- `gameoptions <name> <value>`: checks the value, refuses a bad one, writes the file at once
  (atomic), and says it takes effect at the next reboot.
- If the file was there at boot but could not be read (not JSON, or a value that is not a
  number), every setting runs on its default, the list says so, and a change is refused until
  the file is fixed or removed, so the hand-edited file is never replaced by defaults.

## Immortal vault commands (Greater God, level 97)

Level 97 matches `account show`, which also shows emails.

### Looking

- `vault <character>`: the vault for that character's side; with `<1|2|3>`, that side instead.
- `vault <email or account>`: all three vaults.
- `vault <email or account> <1|2|3>`: one side.

Every view shows the account name and email, plus the character name when the lookup was by
character. Container contents are listed, indented. A name that matches both an account and a
character shows the account. Lookup works like `account`'s `<email-or-account>`. `take` and
`put` are matched before any name, so an account or character named Take or Put is viewed by
its email.

### Stepping in

These take the **account name only**, so a change can't quietly land on the wrong account.
They work whether or not anyone on the account is logged in.

- `vault take <account> <1|2|3> <slot>`: item to the immortal's inventory. No fee, no player
  checks. A container comes out whole.
- `vault take <account> <1|2|3> coins <N>`
- `vault put <account> <1|2|3> <item>`: any item the immortal is carrying. Deposit date is
  today. Refused when the vault is full, or for anything rent refuses.
- `vault put <account> <1|2|3> coins <N>`: from the immortal's own coins, up to the coin
  limit.

`take` and `put` are refused for an immortal with no connection (forced while linkless): that
character is not saved, so the item would leave the vault and be lost. One `(GC)` line says
why, for whoever forced it. `vault put` is not bound by the banker's cursed-item refusal.

### Logging

Every `vault` command, looking included, writes one `(GC)` line with who ran which command,
with runs of spaces collapsed. What the command prints is not logged.

## Saving and crash safety

Each transaction writes two things, always in the order that leaves a duplicate if the server
dies in between:

| Action | First | Second |
|--------|-------|--------|
| deposit (item or coins) | vault file, with the item/coins | character, without them |
| withdraw (item or coins) | character, with the item/coins and the fee paid | vault file, without them |
| `vault take` | immortal's character | vault file |
| `vault put` | vault file | immortal's character |

If the first write fails, the transaction is refused and nothing changes. Withdrawals and
`vault take` check the vault file is writable before handing anything out; if the second write
still fails it is a `SYSERR`, and `vault take` also tells the immortal.

## Code changes (outline)

- `src/mob_progs/banker.{h,cpp}`: program 34, its options parsing, the vault table and file
  format, the `vault` command.
- `src/game_boot_options.{h,cpp}`: the settings file and `gameoptions`.
- `src/spec_ass.cpp`: program 34 in both switch tables and a `spec_pro_message[]` entry;
  matching entries where program 33 is listed (`src/act_wiz.cpp:2964`,
  `src/shapemob.cpp:1284`).
- `src/interpre.cpp`: two new commands, `vault` and `gameoptions`. 138-140 stay as they are.
- `src/objects_json.{h,cpp}`: expose reading and writing a list of object records on its own,
  for the vault file.
- Help: `balance`, `deposit`, `withdraw`, `vault`, `gameoptions`; a banker entry under
  `man shape` and program 34 in the MOB2 29 list. **The helps must say fees are in copper.**
- Docs: a banker section beside the barter vendor docs.

### Build and environment changes (each reviewed by the user individually)

- `src/Makefile`: two new files in the object list.
- `src/CMakeLists.txt`: the same two files, plus new test files.
- A default `lib/misc/game_boot_options.json` is **not** shipped; absence means defaults.

### Depends on

- PR #343 (object version refresh) for the refresh on withdraw and the version number in
  stored records. If the bank is built before #343 merges, that one step is added when it
  does.

## Testing

Unit tests (`src/tests/`):

- Options parsing: every strict and soft case above.
- Fee maths: same day, one day start crossed, cap at `maxdays`, container item count, markup
  rounding up, both daylight-saving changes.
- Side lookup for every playable race, and God.
- Vault file round trip, empty vault, unreadable file left untouched.
- Slot and coin limits, partial coin deposit, limit lowered below contents.
- Write order: a failed first write changes nothing.
- Settings file: missing, missing key, bad value, set then reload.

In game, on a local server (port 4071), scripted as a smoke test and then by hand:

- Every player command at a free banker and a fee banker, both refusal paths on withdraw.
- Two characters of one account on the same side see one vault; a character on another side
  does not.
- Deposit, kill the server, boot: the item is in the vault (and possibly also on the
  character), never gone.
- A container with contents, in and out; contents and weights intact.
- An immortal `vault take` / `vault put` while the account's player is at a banker; the
  player's next `balance` shows the change.
- Banker protection: attack, poison, dust.
- `gameoptions` list and set; reboot; new value in use.
- The account index, account commands and the account smoke test still pass with vault files
  in the account folder.
- A soak run on the final build.

## Out of scope / tabled

- Bank groups (separate vaults per town or per banker group).
- Player-to-player transfers through the bank.

## Decisions made while writing (confirmed by the user, 2026-09-30)

1. **Other races.** Races outside the three lists (for example Easterling, 14) get no side
   and are refused, like immortals.
2. **Allowed ranges** for `gameoptions`: slots 1-100, coin limit 0-100,000 gold, day start
   hour 0-23.
3. **Allowed ranges** for banker options: `fee=` 1-10,000 copper, `maxdays=` 1-365.
4. **`balance` layout:** the Slots column from the proposal page is dropped, since every row
   is always 1 slot.
5. **`give` to a banker** is refused the way vendors refuse it ("I don't take gifts.").
6. **NOBASH reminder:** `/imp` on a banker without nobash shows the same warning vendors get.
7. **Player transaction log:** one log line per player deposit and withdrawal, like the
   vendor trade log, so staff can trace an item. Not on the proposal page.
8. **Worn items** can't be deposited; only loose inventory.
9. **File names:** `vault_light.json`, `vault_dark.json`, `vault_third.json`.

## Changes after the code review (2026-10-01)

Decided item by item with the user after a three-reviewer read of the first build; the sections
above already include them. Findings and decisions: `reports/2026-09-30-player-bank/review-findings.md`
(local). In short: unopenable vault files are unreadable, not empty; nothing is handed out when
the vault file can't be written; `vault take/put` need a connection; cursed items and immortals
(by level) are refused; the `(GC)` line collapses spaces; a damaged settings file is never
replaced by defaults; greetings (`greeting=`, `greeting_other=`); banker and barter-vendor speech
follows the old shopkeepers (a refusal to serve is a say, any other reply a tell); the in-game `linkaccount` command is removed.

## Adversarial review changes (2026-10-02)

Three independent reviewers; each finding verified, then decided item by item with the user.
Findings and decisions: `reports/2026-09-30-player-bank/review-findings.md` (local), section
"ADVERSARIAL REVIEW". `docs/systems/bank.md` and `docs/systems/barter-vendors.md` are current. In
short: the vendor's `buy` no longer leaves a destroyed payment object in the command's targets
(a crash in the merged vendor code); a wand or staff keeps its charges in the vault; a withdraw
is undone when the character save fails (runtime save-result flags); deleted prototypes are
skipped as the rent load skips them; a vault whose read failed is read again on the next use;
`vault take` / `put` log the item that moved; `attacks=yes|no` on vendors and bankers (default
yes, as old shopkeepers), and race aggression is no longer reported as an error; `vault
<account>` with no side is a summary; long plain lines wrap.
