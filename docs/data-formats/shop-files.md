# Shop files (`.shp`)

**Source files:** `src/shop.cpp` (`shop_data:36`, `boot_the_shops:586`,
`assign_the_shopkeepers:655`); loaded via `db.cpp index_boot(DB_BOOT_SHP)`
**Status:** ✅ format complete (one marker-line detail noted under Open questions)

## Purpose
Shops bind a shopkeeper mobile to a buy/sell inventory, pricing, hours, and a set of
canned messages. Files live in `lib/world/shp/`, listed by that directory's index file.
Unlike the other categories, shop loading does **not** pre-count records — `boot_the_shops`
`realloc`s as it goes (`index_boot:650` skips counting for `DB_BOOT_SHP`).

## Data structure — `struct shop_data` (`shop.cpp:36`)
`MAX_PROD = 5`, `MAX_TRADE = 5` (`shop.cpp:23-24`).

| Field | Meaning |
|-------|---------|
| `producing[5]` | item vnums the shop sells (always in stock); negative = empty slot |
| `profit_buy` | multiplier applied to cost when the **player buys** |
| `profit_sell` | multiplier applied to cost when the **player sells** |
| `type[5]` | item *types* the shop will buy/trade (byte; see object `type_flag`) |
| `no_such_item1/2`, `do_not_buy`, `missing_cash1/2`, `message_buy`, `message_sell` | canned keeper messages |
| `temper1` | keeper reaction when player lacks cash |
| `temper2` | keeper reaction when attacked |
| `keeper` | shopkeeper mob vnum (`assign_the_shopkeepers` sets `mob_index[].func = shop_keeper`) |
| `material` | bitvector of materials the shop buys; `0` = all |
| `in_room` | room vnum where the shop operates |
| `stock_room` | room vnum where goods are stored |
| `open1/close1/open2/close2` | two open/close hour windows (game hours) |

## Format — `boot_the_shops:594-652`
Per record, in order:
```
#<vnum>~
<prod0> <prod1> <prod2> <prod3> <prod4>     (5 ints; item vnums, negative = none)
<profit_buy>
<profit_sell>
<type0> <type1> <type2> <type3> <type4>     (5 ints; tradeable item types)
<no_such_item1>~
<no_such_item2>~
<do_not_buy>~
<missing_cash1>~
<missing_cash2>~
<message_buy>~
<message_sell>~
<temper1>
<temper2>
<keeper>                                     (shopkeeper mob vnum)
<material>                                   (bitvector; 0 = all)
<in_room>
<stock_room>
<open1>
<close1>
<open2>
<close2>
```
The file ends with a record marker of `$` (read as a string; `boot_the_shops:650`).
At assignment time, each `producing[]` item with vnum ≥ 0 is instantiated and placed in the
`stock_room` (`assign_the_shopkeepers:665-669`).

## RotS-specific notes
- `material` (materials the shop will buy) ties into the RotS object `material` field
  (see `world-files.md` object format).
- Two open/close windows (`open1/close1`, `open2/close2`) support split trading hours.
- Numbers/messages are otherwise close to CircleMUD's shop layout, but the exact field
  order above is RotS's and must be matched precisely.

## Open questions
- **Record marker termination.** The `#<vnum>` marker and the `$` terminator are read with
  `fread_string` (`shop.cpp:595`), which requires a trailing `~`. So the marker line is most
  likely written as `#<vnum>~` (and the terminator as `$~` / `$` on its own). No sample
  `.shp` data is available to confirm the exact on-disk punctuation — verify when real data
  or the OLC shop writer is located.
- Units/semantics of `profit_buy`/`profit_sell` (percentage? fixed-point multiplier?) and
  the `temper*` codes — to be pinned down in the Shops system doc from the buy/sell logic.
