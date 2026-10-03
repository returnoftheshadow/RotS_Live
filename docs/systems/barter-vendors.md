# Barter vendors

A shopkeeper that sells items for other items (hides, belts, arrowheads…) instead of coins.
Built entirely through data on an ordinary mob: a program number plus a text **options**
field. No vnums in code, no `.shp` file.

## Setting one up

1. On the mob, in the shaping (`shapemob`) editor: set `MOB_SPEC` and program number **33**
   (field 29, "program number"). Field 42 ("options") is only available in **extended**
   editing mode, not simple mode — use `simple` to switch if you're in simple mode.
2. Program 33 only takes effect on a mob with **no hard-coded procedure** (no `.shp` entry,
   no `ASSIGNMOB` in the code). If the mob already has one of those, that procedure runs
   instead and field 29 is just data it reads for its own purposes (for example a
   guildmaster's guild index) — a guildmaster with 33 in field 29 is **not** a vendor, and
   there's no warning about it.
3. On the mob, write the options (field 42), one setting per line, e.g.:
   ```
   store=12345
   hours=6-12,14-20
   list=Ah, a customer! Have a look:
   price 5001 2222x1 3333x2 deduct
   price 5002 3333x4
   ```
   `/42` opens the normal text editor: type each setting as its own line, then `%e` to save
   (`%q` aborts, `%h` lists the editor commands). **Don't use `%f` (format) here:** it joins
   every line into one paragraph and breaks the vendor. If that happens, `%q` to abort (or
   re-enter the settings from scratch). In game, builders find all of this under
   `man shape shopkeeper` (and the field itself under `man shape mob2 42`).
4. In the zone, use ordinary zone commands to load the goods into the store room. An item is
   for sale while at least one copy of it sits in that room.

## Vendor settings

| Setting | Meaning | Missing | Bad |
|---|---|---|---|
| `store=<room vnum>` | The store room; its contents are the stock | Warning; no trading | Warning; no trading |
| `hours=a-b[,c-d…]` | Game hours (0–23) he trades. Overnight (`20-4`) and several windows allowed | Always open | **Strict:** warning; no trading |
| `attacks=yes` or `no` | `no`: he never starts a fight (see "Who a vendor serves, and whether it fights") | `yes`, like an old shopkeeper | Warning; line ignored |
| `list=<message>` | The line shown above the `list` output, as plain text (not spoken) | "What would you like to trade?" (also when empty) | Longer than 78 columns: still shown; `/implement` warns the builder only |
| `price <item vnum> <cur vnum>x<qty> [<cur vnum>x<qty>…] [deduct]` | The cost of one item | Item not sold by this vendor | **Lenient:** that line is skipped with a warning; the rest still work |

**`price` line details**
- `<item vnum>`: the object sold. `<cur vnum>x<qty>`: `qty` copies of object `cur vnum` are
  taken as payment.
- One to four currencies per line; quantities are whole numbers 1–100.
- `deduct`: each purchase hands the buyer **one** copy from the store room itself, so stock
  is limited to the copies the zone loaded, and anything inside that copy goes with it (a
  filled container arrives filled). Without `deduct` the store copy stays, there's no
  purchase limit, and the buyer gets a fresh copy of the item: always empty, since what's
  inside the store copy is never copied.
- A line is bad (skipped, warned) if an item or currency vnum isn't a real object, a quantity
  is outside 1–100, there are more than four currencies, or it doesn't parse.
- Up to 30 price lines per vendor; lines past 30 are skipped with a warning.
- A second line for the same item vnum is skipped with a warning; the first one wins.

`hours=` is bad if it's not in the `a-b` form, an hour is outside 0–23, or a window's start
equals its end. Hours are strict because they tie into the game clock; a vendor with bad
hours doesn't trade at all rather than silently trading around the clock.

Validation only checks that vnums are real objects/rooms. It does **not** check that a zone
ever loads a priced item into the store room: a priced item that isn't in stock simply isn't
listed (not loaded yet, seasonal, sold out).

## Shared store rooms

Several vendors can point at the same store room and price the same item differently (one
for wolf hides, another for leather belts). Each lists only the in-stock items he has a
price line for. With `deduct`, a purchase from any of them removes the copy from the shared
room, so the last copy can be bought from whichever vendor gets there first.

If two vendors stand in the same room as the player (not the store room — the room where
players find them), `list`/`buy` is answered by the first one the room's character list
reaches. The player gets one answer.

## Builder notes

- The store room must be unreachable by players. Anything dropped in or taken out changes
  what's for sale.
- Every copy sold adds to that item's count in the world. Don't sell items that other zones
  load with a world limit. Store-room load lines are best without a world limit, since copies
  players have bought count toward it.
- With `deduct`, the zone's load lines decide how many can be sold and how often stock
  returns.
- To convert an old keeper, remove his `.shp` entry.
- Items flagged invisible in the store room are only listed to buyers who can see invisible
  (`vendor_stock` uses `CAN_SEE_OBJ`), like any other object.
- **Keep him where he can see buyers.** A vendor in a dark room refuses everyone ("I don't
  trade with someone I can't see!"). Keep his room lit.

## Who a vendor serves, and whether it fights

- **Race aggression** (mob editor /11 simple, /20 extended; `specials2.pref` in the source) is
  how a keeper chooses who it serves, exactly as the old shops do: 67 of the 75 shopkeepers in
  the world have it set. A customer of a race the vendor is aggressive to gets "Go away, I
  won't deal with you!" (`IS_AGGR_TO`; its `other_side()` half is always 0 for an NPC).
- The **roleplay flag** (`rp_flag`, /39) is the second check: allowed races, 0 = everyone.
  Anyone else gets "Sorry, I can't serve you!". It only refuses.
- **`attacks=yes|no`** (left out = yes). Race aggression also makes a mob attack those races
  on sight, and so do the AGGR, MEMORY, HELPER and HUNTER flags. With `attacks=yes` a vendor
  behaves like an old shopkeeper: it attacks, and once it has started a fight that player's
  hits land (`damage()` only asks the program while the victim is not already fighting the
  attacker, `fight.cpp`). With `attacks=no` the program answers the mob AI's own turn
  (`SPECIAL_SELF`) with TRUE, so the turn ends there: the vendor never attacks, assists,
  hunts, wanders or picks things up, race aggression is access control only, and it is never
  hurt. (Only its own turn is stopped: an immortal's `force` or a script can still make it
  attack. No player can: recruiting a mob removes its program, and only animals are tamed.) `/implement` warns about SCAVENGER, AGGR, MEMORY, HELPER, BODYGUARD, HUNTER and
  ASSISTANT on such a mob (`<flag> flag with attacks=no`), since they can do nothing; SENTINEL
  is unaffected, a keeper that stays put still stays put. The banker has the same option.
- **Mental attacks are refused like physical ones.** Before it engages, `do_mental`
  (`clerics.cpp`) asks a keeper's program the same `SPECIAL_DAMAGE` question a physical attack
  asks (`fight.cpp`). A keeper answers "Don't even think about it.", is never drawn into the
  mental fight, and the attacker is not left fighting it (an attacker who already was, because
  the keeper stopped fighting them, is stopped, or the refusal would repeat every round). This
  covers the old shopkeepers too. Only keepers are asked (`mob_is_keeper`): many old programs
  (the pale vampire, the dragon) treat any call as one more turn, and must not get one out of
  every mental round aimed at their mob.
- **Keeper protection is one shared piece** (`mob_progs/shopkeeper.h`, "Keeper protection").
  A keeper is a mob whose program serves players: an old shopkeeper, a barter vendor, a
  banker. `keeper_protection()` is the first call in the vendor and banker programs and
  answers the mob's own turn (`attacks=`), an attack, and blinding dust; `mob_is_keeper()` is
  what the rest of the game asks, so no other code names the keeper types. A new keeper type
  adds its program to the list in `mob_is_keeper()` and makes that one call. The old
  `shop_keeper` (`shop.cpp`) keeps its own, older answers and is only listed.
- **One keeper to a room.** The first keeper in the room answers `list` and `buy` (a banker:
  `balance`, `deposit`, `withdraw`), even when its own options are broken; a second one, or an
  old shopkeeper behind it, can't be reached.

## The options field

- Comments: a line starting with `//` is ignored and never produces a warning.
- The stored text can't contain `#` or `~` anywhere, and can't start with `$` — those
  characters would corrupt the mob file when it's saved (the editor's record scanner treats
  a `#` anywhere in the file as the start of the next mob record). What the editor does when
  you finish the text:
  - **Silently changed:** every `#` becomes `+` and every `~` becomes `-` (the same cleaner
    as every other mob, object, room and script text). The mob file writer applies it again
    on save.
  - **Refused**, keeping the old value: a leading `$` (`Options not changed: options can't
    start with $.`), or more than 4,000 characters (`Options not changed: options are too
    long (max 4000 characters).`).
  - Blank lines are dropped, at the start and between settings, because loading the mob
    file drops them anyway. So the text and the line numbers in warnings are the same at
    `/save`, `/implement` and boot. To space settings out, use a `//` comment line.
- `stat` on a loaded copy shows the prototype's current options (what `list`/`buy` use).

## How the vendor speaks

The vendor speaks the way the old shopkeepers (`shop.cpp`) do, through two helpers in
`src/mob_progs/shopkeeper.cpp` that the banker (program 34) shares:

- **A refusal to serve at all is a real `say`** (`vendor_say`), heard by the room: aggressive
  to the buyer, shadow, race check, can't see the buyer, closed, and "I'm not trading right
  now." (bad options).
- **Every other reply is a tell** to the buyer (`vendor_tell`), which nobody else sees:
  "What do you want to buy?", "I don't have that. Try 'list'.", "I have nothing to sell right
  now.", the shortfall, "I don't take gifts." and "Don't even think about it." (attack or
  blinding dust). A shortfall is one tell naming every missing item: "You need 2 x a wolf hide
  (you have 1) and 1 x a leather belt (you have 0)." A tell is one unwrapped line, like every
  tell in the game; it carries up to 1000 characters.
- The list, the carry limits and the purchase summary are plain text, as in the old shops.

`say` refuses any mob with intelligence below 6, so a vendor mob needs **INT 6 or higher** or
it can't say why it won't serve. This is warned (see below), not special-cased in `say`. `vendor_tell` sends the same line `tell` does (`<Vendor> tells you '...'`, tell colour) but does
not run the `tell` command, which stays silent for a buyer who has tells turned off (`notell`)
or whom the vendor can't see. The keeper's answer is the only reply the command gets, so it
always arrives; the gift, attack and dust replies are sent before any can-see check.

## Warnings you may see

Reported for every program-33 vendor at boot, and in the shaping editor on `/save`, `/add`,
`/implement`, and `/done` (which saves and implements, and reports once), in the house style
(type + vnum + line, no prose):

```
MOB ERROR: mobile #1234, options line 3: price: bad format - line skipped
MOB ERROR: mobile #1234: store missing - vendor disabled
MOB ERROR: mobile #1234: intelligence below 6 - vendor can't speak
```

The full set of messages you can see:
- `store missing - vendor disabled`
- `bad store - vendor disabled`
- `store room vnum <N> not found - vendor disabled`
- `duplicate store - line ignored`
- `bad hours - vendor disabled`
- `duplicate hours - line ignored`
- `duplicate list - line ignored`
- `price: bad format - line skipped`
- `price: quantity <N> out of range - line skipped`
- `price: more than 4 currencies - line skipped`
- `price: object vnum <N> not found - line skipped` (item or currency)
- `price: duplicate item vnum <N> - line skipped`
- `price: currency vnum <N> listed twice - line skipped`
- `price: more than 30 lines - line skipped`
- `unknown setting - line ignored`
- `intelligence below 6 - vendor can't speak`
- `bad attacks - line ignored`

**At use:** a vendor with a missing/bad `store=` or bad `hours=` refuses to trade ("I'm not
trading right now.") every time, and logs the `bad options - vendor disabled` warning the
**first** time someone tries to buy or list, so a player can't flood the log. It logs again
after its options are next saved (`/save`, `/implement`) or the server reboots. Price-line
warnings never fire during play, only at boot/save/implement.

On a `-s` (no specials) boot there are no vendors at all, like every other special.

## Rolling back to a server without vendors

A server binary from before this feature can't handle vendor mobs. The same goes for mob
files: once a mob with options is saved on test, don't copy that mob file to a server still
running an older binary. Before rolling back:
1. Remove every options block from the mob files. The old binary can't read them, and the
   failure isn't predictable: its loader reads the next word into a 10-character buffer, so
   an options line like `store=12345` overruns it. The boot may stop with `SYSERR: Format
   error in mob file`, or crash, or misread the file.
2. On every vendor mob, change field 29 off 33 (or clear `MOB_SPEC`). Otherwise the old
   binary boots but crashes the first time a player looks at a room holding the vendor (it
   reads `spec_pro_message[33]`, past the end of its table).

## What players see

- `list` starts with the vendor's `list=` line ("What would you like to trade?" by default),
  like a coin shop's "You can buy:", then shows every in-stock, priced item once, with its cost(s); `(N left)` only on
  `deduct` items. The cost column starts at column 44 or earlier (names wrap at 38), so a
  currency short description of up to 28 characters keeps every line within 78 columns; a
  longer one makes that line longer and the player's client wraps it.
- `buy <number>` is a list number only when the argument is all digits. Anything else is a
  keyword: `buy belt` is the first listed item matching `belt`, `buy 2.belt` the second.
- `buy` checks every currency is covered in the player's **loose inventory** — not inside a
  bag, not worn, and not a currency item that is itself a container with something inside
  it (that copy is skipped so its contents can never be destroyed). If anything is short,
  nothing is taken and the player is told what's needed and what he has. Then it checks the
  player can carry the item, counting the payment as already handed over (paying 3 items
  for 1 frees 2 slots).
- A completed purchase prints one currency per line: `You hand over:` / `  N x <name>` / `You
  now have <item>.` It is also written to the server log: `VENDOR: <player> buys <item>
  (<vnum>) from mobile #<vnum>, paid N x <currency> (<vnum>), …`. The bought item counts as
  handled by a player, so later `get`s of it are logged like any other.
- Giving the vendor an item is refused ("I don't take gifts.") — exactly the cases where
  `give` would hand it to him.
- Attacking him (melee, spells, skills) is cancelled; he says "Don't even think about it."
  and never fights back (unless he started the fight himself — see "Who a vendor serves, and
  whether it fights"). Poison can
  still be cast on him, but its damage each tick is cancelled too (silently). Dust aimed at
  him is refused outright, because a blinded vendor couldn't see anyone to trade with.
- Bash: the damage is cancelled and he stays standing, but he still picks up the bash state
  (a short delay, then "has recovered from a bash"). Harmless; trading still works.
