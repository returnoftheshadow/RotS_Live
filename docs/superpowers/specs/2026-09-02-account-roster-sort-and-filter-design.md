# Account Roster Sorting, Coefficient Filtering, and Summary Cache

**Date:** 2026-09-02
**Status:** Design, awaiting review
**Branch target:** `release-frodo`

## Problem

The "Play a linked character" roster (`CON_ACCTSLCT`) renders linked characters in
`account.json` insertion order — the order they were linked, nothing else. With the cap now at
200 (PR #289), a player with a large roster reads 100 lines of unordered text to find one
character.

Two things are needed: a way to order the roster, and a way to narrow it.

A third, non-negotiable constraint comes out of PR #289: **whatever is displayed must be
selectable, and whatever is selectable must be displayed.** The cap bug existed because the
display bound and the selection bound were the same constant but the failure mode was silent.
Re-ordering and filtering both change which character sits at which number, so display and
selection must be driven from one ordered list, never computed twice.

## Scope

In scope:

1. Four sort orders on the roster, chosen by the player.
2. Four coefficient filters that narrow the roster to characters whose highest profession
   coefficient is the chosen one.
3. Persisting the sort choice on the account.
4. An in-memory roster summary cache, without which sorting makes the existing per-render cost
   substantially worse.

Out of scope: paging, the `Character number or name:` prompt's other behaviour, the account menu
itself, and anything about character creation.

## Interface

All input happens at the existing `Character number or name:` prompt. No new screen, no new
connection state.

```
199) [ 45 WdE] Legolas      200) [ 30 Dwf] Gimli

200 characters displayed.

Sort: (A)-Z  (L)evel  ra(C)e  (S)ide      Show only: (W)arrior (R)anger (T)mystic (M)age
0) Back to Account Menu.

Character number or name:
```

| key | action |
|---|---|
| `a` | sort by name, A–Z |
| `l` | sort by level, highest first |
| `c` | sort by race |
| `s` | sort by side |
| `w` | show only characters whose highest coefficient is Warrior |
| `r` | ... Ranger |
| `t` | ... Mystic (cleric) |
| `m` | ... Mage |
| `0` | back to the account menu (unchanged) |
| number / name | select a character (unchanged) |

`w`/`r`/`t`/`m` follow the profession-coefficient letters players already use. That fixes `r` to
Ranger, which is why the race sort is `c` rather than `r`.

### Why single letters are unambiguous

Character names are a minimum of 3 characters, enforced independently by `valid_name`
(`ban.cpp:278`, against `MIN_NAME_LENGTH` = 3 in `structs.h:75`) and by `is_valid_account_name`
(`account_management_identity.cpp:24`), the latter applied to every entry in `characters[]` when
an account file is parsed. A one-letter name cannot reach this roster, so no precedence rule
between "sort key" and "character name" is required.

### Ordering within each sort

| sort | order |
|---|---|
| `a` name | case-insensitive ascending |
| `l` level | descending, highest first |
| `c` race | ascending by race index (`RACE_GOD`, Human, Dwarf, Wood, Hobbit, High, Beorning, Uruk, …), so races cluster in their canonical order |
| `s` side | gods, then lights, then darks, then third side, per `other_side_num` (`handler.cpp:152`) |

Race index order does **not** cleanly group sides — the third side (Magus 15, Haradrim 18) is
interleaved with the darks (Orc 13, Easterling 14, Undead 16, Olog-hai 17, Troll 20) — which is
why race and side are separate sorts rather than one.

All sorts are stable, and ties break by the character's position in `account.characters` so a
redraw never reshuffles equal keys.

Characters that fail to load (`readable == false`, rendered `[ ?? ???]`) have no level, race, or
coefficients. They sort **last** under every ordering, and are excluded by every coefficient
filter. They remain selectable by name and by their displayed number.

### Filter behaviour

- Filters and sorts compose: a filter narrows the set, the active sort orders what remains.
- Pressing an active filter's letter again clears it. Only one filter is active at a time;
  pressing a different letter replaces the current one.
- With a filter active, the footer reports the filtered count and that a filter is on, so a
  player can never mistake a filtered roster for their whole roster:
  `12 of 200 characters shown (Warrior).  Press W to clear.`
- **Numbering follows the displayed list.** With a filter active, `1` selects the first
  *displayed* character. This is the PR #289 invariant: one ordered list drives both rendering
  and selection.

### Highest-coefficient rule

A character matches filter *P* when its coefficient for *P* is greater than or equal to its
coefficient for every other profession. Ties therefore appear under every tied letter, as
requested.

Comparison uses the derived coefficient (`get_prof_coof`, `char_utils.cpp:363`), not the raw
stored value, because the race adjustments — Orc `*2/3`, Uruk-mage `-100` — change the ordering
between professions for those races. Raw values are stored; the adjustment is applied when
comparing.

## Persistence

Add one field to `AccountData`:

```c
std::string roster_sort;   // "", "name", "level", "race", "side"
```

Empty means "never chosen" and preserves today's insertion order, so existing accounts are
unaffected until the player picks something.

This is cheap and needs no migration. `parse_account_property` ends in
`return reader->skip_value(...)` (`account_management.cpp:1372`), so unknown keys are skipped
rather than rejected: existing files without the key load and take the default, and an older
binary reading a newer file ignores it. `ACCOUNT_SCHEMA_VERSION` stays at 1.

**The filter is not persisted.** It is a transient narrowing, and persisting it risks a player
logging in to what looks like a truncated roster.

### When the write happens

The sort is written when the player leaves the roster, not on each keypress.

This matters more than it looks. `write_account_file` calls `account_cache::invalidate_all()`
(`account_management_storage.cpp:248`), which clears **both** cache maps globally — every
account, not just the writer's. The second map, `g_owner_cache`, is keyed by character name and
is consumed by `save_char` (`db.cpp:3219`) on every save. A miss there is not a lookup but a full
scan that reads and parses **every** `account.json` on disk, and it deliberately does not stop at
the first match so it can detect two accounts claiming one character
(`account_management.cpp:742`).

So each account write makes the next save for every logged-in character do a full-corpus scan.
That is acceptable for the things that write today — create, link, verify, block, password reset —
because they happen once or twice in a character's life. It is not acceptable for a key a player
can press repeatedly.

## Roster summary cache

### Why it is required, not an optimisation

`format_account_character_short_entry` (`account_management.cpp:102`) calls
`read_account_character_file` per row. That reads and deserialises the character's ~4 KB JSON to
extract **two bytes**: `level` and `race`. The display name does not come from it — it comes from
`account.characters[index]`, already in the cached `account.json`.

Measured with the real deserializer at production flags (`-m32 -O0`, matching `Makefile:25`,
which sets no `-O` at all): **308 µs per character**. A pulse is 250 ms (`OPT_USEC`,
`comm.cpp:47`).

| roster | parse cost | share of one pulse |
|---|---|---|
| 100 characters | 31 ms | 12% |
| 200 characters | 62 ms | 25% |

Sorting makes this strictly worse in a way the display cap does not bound: to order the roster
you must read **every** character before choosing which to show. And the cost repeats on every
redraw — which now includes every sort keypress, every filter keypress, every empty Enter, and
every mistyped name.

There is a second, larger amplification. `read_account_character_file` calls `read_account_file`
first (`account_management_assets.cpp:52`). That is memoised, but after any flush it is not, and
a 200-character `account.json` is ~47 KB. Two hundred cold re-parses is on the order of 700 ms —
roughly three pulses with the game frozen.

### Design

Keyed by account name, holding one summary per linked character:

```c
struct RosterSummary {
    unsigned char level;              // char_file_u.level
    unsigned char race;               // char_file_u.race; also yields side
    bool readable;                    // false -> render "[ ?? ???]" without re-parsing
    short prof_coof[MAX_PROFS + 1];   // raw square_root[] index; real domain 0..170
};                                    // sizeof == 14 at 32-bit
```

Name is absent — it comes from `account.characters[]`. Side is absent — it derives from race via
`other_side_num` (`handler.cpp:152`).

`prof_coof` is `short` rather than a byte because nothing validates the range on load
(`character_json.cpp:575` reads it into an `int` with no clamp), and a byte would silently
truncate a corrupt value into a plausible-looking wrong sort position. Values are clamped when
filling the cache.

`readable` matters: characters that fail to deserialise already render as `[ ?? ???]`, and
without caching the failure the most expensive rows would be the useless ones. There is a known
live population of these (oversized `description` fields).

### Measured footprint

Actual `VmRSS` at 32-bit:

| shape | entries | RSS |
|---|---|---|
| whole live world (~5.5k active characters) | 5,500 | **~0.3 MB** |
| 100 concurrent accounts × 150 characters | 15,000 | 0.27 MB |
| 2,000 accounts × 150 (well beyond real) | 300,000 | 4.3 MB |

Caching every active character in the game costs about a third of a megabyte. Live has 5,591
active player files across `A-E`/`F-J`/`K-O`/`P-T`/`U-Z`; the 28,308 in `ZZZ` are deleted and
never appear on a roster.

### Invalidation

**Per character, on that character's save — not a global flush.**

`account_cache` uses a deliberate coarse `invalidate_all()`, which is correct there because
account mutations are rare. Character saves are not rare; autosave fires constantly. Copying that
pattern would keep this cache permanently cold and defeat its purpose.

The cache is a memo of on-disk character state, so it is dropped for exactly the character whose
file was written, leaving every other entry intact.

## Testing

Following PR #289's pattern: watch each test fail first, and for guard tests prove they can fail
by temporarily breaking the thing they guard.

Unit:

1. Each sort orders correctly, including ties.
2. Sort is stable, so equal keys keep a deterministic order rather than shuffling between renders.
3. Each filter selects exactly the characters whose highest derived coefficient is that
   profession, and a tied character appears under every tied letter.
4. Race adjustments are applied before comparison — an Orc and a Uruk mage land where the derived
   values put them, not the raw ones.
5. Filter plus sort compose.
6. **Selection matches display under every sort and filter**: for each ordering, selecting `k`
   returns the character rendered at row `k`. This is the PR #289 invariant and the most
   important test here.
7. Pressing an active filter's letter clears it.
8. A character named `Zzz` is still selectable by name while a filter is active.
9. Sort persists across a reload; an account with no stored sort keeps insertion order.
10. Cache: a hit returns the same summary as a cold read; a character's save drops only that
    character's entry; an unreadable character is cached as unreadable and not re-parsed.
11. The existing `RendersAFullRosterWithinTheOutputBuffer` guard still holds for the longest
    footer (filter active, so the footer carries the extra "N of M" text).

Wire, via `testing/`:

12. Boot with an account over 100 characters; exercise each sort and filter key; confirm the
    rendered order, that the footer counts match, and that selecting a number after sorting and
    after filtering loads the character shown at that row.

Baseline the suite on a clean tree first. The full run aborts at
`InterpreAccountMenu.UnlockSelectAllowsOneDifferentLinkedCharacterSelectionAndConsumesAtEntry` on
pristine `release-frodo` — pre-existing, verified by stashing and re-running.

## Files

| file | change |
|---|---|
| `account_management_types.h` | `roster_sort` field on `AccountData` |
| `account_management_storage.cpp` | serialise / parse the field |
| `account_management.cpp` | ordering and filtering; roster renders from the ordered list |
| `account_management_identity.cpp` | `select_linked_character` resolves against the same ordered list |
| `account_management_presentation.cpp` | footer, sort/filter legend |
| `interpre.cpp` | handle the eight keys at `CON_ACCTSLCT`; write the sort on leaving |
| new: `roster_cache.{h,cpp}` | the summary cache |
| `db.cpp` / save path | drop a character's summary on save |
| `src/CMakeLists.txt`, `src/Makefile` | build the new file |

## Risks

**Ordering must be computed once.** If rendering and selection each sort independently they can
disagree — the PR #289 failure in a new form. One function returns the ordered, filtered list;
both callers consume it.

**Cache staleness shows as a wrong roster.** A missed invalidation means a stale level or race,
and under a coefficient filter a character could vanish from the list. Selection resolves against
the same list, so a stale entry cannot make a character unselectable while it is displayed — but
it could hide one. The save-path hook is the thing to get right.

**`clang-format` must not be run over these files.** It reorders `account_management.cpp`'s
fragment `#include`s (`assets` ahead of `internal`, against the comment above them) and breaks the
build, and it churns unrelated lines in the test files. Format by hand, matching `-style=WebKit`.

## Deferred

- Paging. At 200 the cap is comfortable — a 200-row roster is 5,616 bytes of the 16 KB descriptor
  buffer, with the silent-drop cliff at 585 — so paging is not needed yet.
- Ordering the roster *by* a coefficient's value (e.g. "everyone by mage coefficient, highest
  first"). Note this is **not** the `w`/`r`/`t`/`m` filters, which are in scope — it is a fifth
  and sixth sort order, and it would largely restate what filtering by highest coefficient
  already surfaces. Same for sorting by profession *level*.
- Secondary sort keys.
- The `-O0` production build. `Makefile:25` sets no `-O` flag, which is why the per-character
  parse is 308 µs rather than the 84 µs the same code takes at `-O2`. Turning optimisation on
  would help this path and many others, but `-fstrict-aliasing` is already in `MYFLAGS` and only
  becomes active at `-O2`, which is a real risk in a codebase that type-puns through struct
  pointers. Its own change, with its own testing.
- The 7 live characters with 2-character names cannot be linked to an account at all, because
  `is_valid_account_name` rejects them when the account file is parsed. Pre-existing, unrelated,
  worth its own look.
