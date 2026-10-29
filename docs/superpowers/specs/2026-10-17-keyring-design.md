# Keyring object — design

Date: 2026-10-17. Status: design, awaiting approval. Nothing built.

## Why

Players carry too many keys. Each loose key takes an inventory slot, and the carry
limit (`CAN_CARRY_N`, `utils.h:650`: 5 + Dex/2 + level/2) differs per character, so a
handful of zone keys crowds out real gear. A keyring holds the keys as one item (zero
items when worn on a belt hook) and still lets them work on doors and containers.

## Goals

- New item type **keyring**, made in the object editor like any other object.
- Holds **keys only** (`ITEM_KEY`). Anything else is refused with a short message.
- **Lock and unlock** (doors and containers) find keys on a keyring.
- Can be **worn on a belt hook**; while there, `look in`, `put` and `get` work on it.

## Non-goals / anti-goals

- **No change to bags or any other container.** In particular a *held* container stays
  as it is today: `put`/`get` can't reach it; `look in`/`examine` go through the existing
  crude worn-item match in `generic_find`. Players hide food from eating triggers by holding the
  bag; that must keep working. Anything that changes held-bag behavior is a failure.
- No change to rent, death, weight or carry rules. "Normal rules everywhere."
- No open/close/lock/pick on the keyring itself. Changing what a keyring does by state
  would confuse players.
- No searching inside nested containers (a keyring packed in a bag is not searched).
- No fix to the general worn-item name matching in `generic_find` (see below) for other
  items.

## Behavior (decided with the user)

### The object
- New type constant after `ITEM_LEVER` (25), i.e. `ITEM_KEYRING` 26, with its name in
  every item-type table (consts.cpp `item_types`, `act_info.cpp` type list near 4283,
  `obj2html.cpp`, `shapeobj.cpp` bound check `<= ITEM_LEVER` at 740, `act_wiz.cpp` stat).
- One builder field: **weight capacity**, like a bag's (bag uses `value[0]`). Builders
  can just set it high; since only keys fit, it rarely matters.
- **Always open.** No closed/locked state, no open/close/lock/unlock/pick on it.
- Wear flags are normal: builder may tick TAKE, HOLD, and the existing **belt** flag
  (`ITEM_WEAR_BELT`). Belt hooks (`WEAR_BELT_1..3`) already need a worn waist item
  (`act_obj2.cpp:780`) — unchanged.

### Where the keyring is, and what works

| Keyring is…            | lock/unlock uses its keys | look in / put / get on it |
|------------------------|---------------------------|---------------------------|
| in inventory           | yes                       | yes (like a carried bag)  |
| on a belt hook (worn)  | yes                       | **yes (new)**             |
| held                   | yes                       | same as a held bag today: put/get no; look in/examine via the existing crude match, unchanged |
| inside a bag/container | no                        | as for any item in a bag  |
| in the room            | no                        | as a bag on the floor     |

- **Lock/unlock search order:** loose keys in inventory, held key (today's
  `has_key`, `act_move.cpp:1348`), then keys on keyrings in inventory, held, or on
  belt hooks. `has_key` is the single choke point for door and container lock/unlock,
  so extending it covers every case. Broken keys are skipped as today.
- **Breakable keys** on a keyring break exactly as a loose key does
  (`check_break_key`); the broken key stays on the keyring.
- **Belt-worn interaction:** `look in`, `put <key> <keyring>`, `get <key> <keyring>`
  find a keyring on a belt hook by a proper keyword match (the object's name list,
  normal `isname` style). Today's worn-item search in `generic_find`
  (`handler.cpp:2443`) is a crude prefix match against the whole name line with no
  `N.` numbering — the user confirmed in game it can't reliably target a held pack.
  The keyring must not depend on it. Only belt-worn keyrings get the new lookup; held
  ones do not. *(Builder's call, superseded 2026-10-21 — see below:)* for these three
  commands, try the belt-hook keyring keyword match first, then fall back to today's
  search, so the crude worn match can't grab a different worn item.
  *(User decisions, 2026-10-17/18, after the high reviews:)*
  - The belt keyring answers to any keyword, including a 3-4 letter start ("keyr"), like
    an item in the inventory, in put, get, look in and examine.
  - No sight check on the belt step: it is found in the dark or while blind, as worn gear
    and the inventory are. Putting a key in still needs sight (as for bags).
  - *(User decisions, 2026-10-21, after the third high review:)* the belt keyring comes
    **last**: inventory, then the room, then belt keyrings, in put, get, look in and
    examine, so a bag or pouch that shares a keyword (leather, iron) wins. "N.name" counts
    on to the belt after the longer of the inventory and room match lists (the old search
    takes the Nth in the inventory, else the Nth in the room), so every belt keyring has a
    number (adversarial review 2026-10-21). Other worn gear is still found before the
    inventory in look in / examine (live behaviour for all worn items, filed on #421), but a
    belt keyring also waits behind the rest of the worn gear: with "leather keyring" on belt
    1 and "leather pouch" on belt 2, `look in leather` shows the pouch. One shared search serves all four commands: `generic_find` with the
    `FIND_OBJ_BELT_KEYRING` option (`handler.cpp`, whole-branch review 2026-10-21), wrapped by
    `find_container`; every other command leaves the option off and searches as before. Examine's contents step
    searches again with it (2026-10-21, second adversarial review: reusing the object from
    the description step read freed memory when an ON_EXAMINE_OBJECT script destroyed it).
  - Wording: a key goes *on* a keyring ("You put a brass key on an iron keyring.", "... won't
    fit on ..."); bags and quivers keep "in"; the staff OBJ log line is unchanged.
  - `get <x> all` leaves worn things alone, as before keyrings. `get <x> all.<name>`
    searches the inventory, then the room, then belt keyrings. Held items are not searched.
  - A get from a container with hands that fill up partway says "You can't hold any more
    items." once and stops (before: the message was built and never sent).
    A container on the ground (a corpse) keeps its old message, one "You can't carry that
    many items." line per item left, so a player looting their own corpse sees what is still
    in it (user decision 2026-10-21).
  - `put <x> <keyring>` looks among keys in the inventory first, so `put key keyring`
    picks a key, not the keyring; bags and quivers keep the old lookup exactly (quiver
    behavior is a group decision).
  - The first word of put/get is only ever searched for in the inventory (put) or in the
    named container (get); contents of other containers are never searched.

### Normal rules (unchanged code paths, but must include the new type)
- **Carried weight:** all bookkeeping is in `obj_to_obj` / `obj_from_obj`
  (`handler.cpp:1803` onward), type-independent, and walks up to the carrier's
  `IS_CARRYING_W`. Correct for carried and belted keyrings.
- **Worn weight — keyrings fixed, quiver deferred (user decision 2026-10-17, after the
  adversarial review):** equip/unequip add/subtract the item's weight at that moment, so
  changing a worn container's contents used to drift the wearer's `worn_weight` /
  `encumb_weight` (dodge and OB penalties) until next login (`recalc_worn_weight`,
  `objsave.cpp:613`, `828`). With a belted keyring, wear empty / put all / remove / get all
  repeated drove them negative (a bonus). Now, when a worn keyring's weight changes through
  `obj_to_obj` / `obj_from_obj`, `update_worn_keyring_weight` (`handler.cpp`) takes off the
  ring's old contribution and adds its new one (the shared `worn_encumb_weight` formula
  that equip/unequip/`recalc_worn_weight` also use), so the stored numbers always equal
  what wearing the ring as it is now would add. The ranger back quiver (and any other
  worn container) keeps the old drift: it is a long-standing norm that needs group
  approval (arrows may need a weight rebalance). Deferred to the group.
- **Item count:** keyring counts as one carried item; zero when worn. Keys inside never
  count.
- **Rent:** keys are unrentable (`objsave.cpp:1381`) and get extracted at rent; the
  keyring itself stays. `extract_obj` unlinks a contained key without going through
  `obj_from_obj`, so the ring's weight, `IS_CARRYING_W` and `worn_weight` /
  `encumb_weight` are all left as they were (still consistent with each other). The
  character is leaving, and `recalc_worn_weight` rebuilds them at next login.
- **Death:** keys get pulled out of containers into the corpse (`fight.cpp:661`
  `parse_container`); a belt-flagged keyring is itself wearable, so if it's inside a bag
  it gets moved to the corpse whole. *(Builder's call, settled:)* keys stay on the ring;
  the ring is at the corpse's top level, so nothing is hidden. A keyring that's carried
  or on a belt goes to the corpse the normal way with its keys. Random-item removal
  (`fight.cpp:609`) skips keyrings like it skips containers.

## Left to the builder

- How to share the "container or keyring" test. There are ~38 `ITEM_CONTAINER`
  checks; **not all should include keyrings** (corpses `value[3]==1`, quivers,
  `limits.cpp` corpse decay, `fight.cpp:706` corpse creation, `ranger.cpp`,
  open/close/lock/pick paths in `act_move.cpp` 1221/1330/1395/1462 must *not* accept a
  keyring). Decide per site; use one shared helper where the answer is "yes".
- Exact refusal message for non-keys (≤78 columns, game's own wording style).
- Whether `examine` shows contents for a keyring the same way it does for a bag
  (`act_info.cpp:1553`) — follow the bag.
- Object editor layout for the one capacity field.
- Shop/keeper item-type handling: treat like any other new type; no special rules.

## Testing

Unit tests for `has_key` (inventory / held / belt keyring / bagged keyring not found /
broken key skipped), key-only refusal, and weight before/after put/get on carried and
belted keyrings. Then an in-game pass on a local test server: build a keyring in the
editor, hang it on a belt, put/get/look in, unlock a door and a chest with a key on it,
hold it and confirm look-in/put/get refuse but unlock works, hold a bag and confirm it
behaves as before, rent and reconnect, die and check the corpse.
