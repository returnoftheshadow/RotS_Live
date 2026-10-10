# Account Storage Subsystem — Findings and Proposed Direction

Date: 2026-09-05
Status: **Findings only. Deferred — this is the next thing to work on.** No design or plan written yet.
Found while assessing the risk of the account-level PPC feature (`feat/account-level-ppc`).

## Root cause

**There is no store layer.** `AccountData` is simultaneously the in-memory model, the JSON wire
format, and the on-disk record, and every piece of code that needs an account reaches into the
filesystem and walks it by hand. The directory tree is being used as a database with exactly one
key (normalized email, as the path) and no index.

Every problem below is downstream of that single fact. They are not separate defects.

## Symptoms, all traceable to the root cause

### 1. Three lookups, all linear scans

The storage layout is `accounts/<bucket>/<normalized-email>/account.json`. Email is the only key
the layout encodes. But the code looks accounts up three ways, and every one opens and JSON-parses
*every* account file:

| Lookup | Function | Cached? |
|---|---|---|
| by email | `find_account_by_email_internal` (`account_management.cpp:975`) | **no** |
| by character name | `find_character_owner_account` (`account_management.cpp:902`) | yes, flushed globally |
| by account name | `find_account_file_path_by_account_name` (`account_management.cpp:809`) | yes, flushed globally |
| integrity check | `account_storage_contains_unreadable_records` (`account_management.cpp:1057`) | no |

`read_account_file_by_email` (`account_management_storage.cpp:301`) goes straight to the scan even
though `account_file_path_from_email` already exists and is what `write_account_file` uses to decide
where the record goes. **The read path does not use the one index the layout already has.** That
scan runs on every login attempt, including failed ones.

`find_character_owner_account` is called from `save_char` (`db.cpp:3219`) for every save of a linked
character.

### 2. Account creation is quadratic and unauthenticated

`create_account_for_email` (`account_management_identity.cpp:505`) performs, in order:

1. `account_storage_contains_unreadable_records` — full scan, parses every account file
2. `find_account_by_email_internal` — full scan, parses every account file
3. `create_account` — writes the record, which flushes the global cache

Creating N accounts therefore costs on the order of N² file parses, all on the single-threaded pulse
loop. The path is reachable pre-authentication from the email prompt (`interpre.cpp:3754`), there is
no rate limit, and the record is written **before** email verification — so unverified accounts
persist on disk.

The degradation is permanent and player-facing: every account created raises the cost of every
future login, every save, and every subsequent creation. This is a cheap remote availability attack,
not a theoretical scaling concern.

### 3. Cache invalidation is global

`account_cache::invalidate_all()` clears both memo maps for every account. All 21 `write_account_file`
call sites trigger it. Caching was bolted on *outside* the store, and the store exposes no key
structure to invalidate precisely, so all-or-nothing is the only option available.

### 4. Every field change rewrites the whole record

Read the entire `AccountData`, change one field, serialize it all back. There is no field-level
granularity because the record *is* the file. This is also the source of the read-modify-write
clobber window between concurrent writers.

### 5. Boot is all-or-nothing

`db.cpp:632` scans every account file at startup and calls `exit(1)` on any parse failure. **One
corrupt account file prevents the server from starting at all.** "Is the store valid?" is
implemented as "parse every record", which is the same reason creation pays a full scan.

### 6. Per-linked-character reads at login

`account_has_high_level_linked_character` (`interpre.cpp:2478`) does an uncached
`read_account_character_file` per linked character — up to ~100 file reads for a large account,
on the login path. Partially mitigated by `roster_cache.cpp`.

## Proposed direction

Introduce a real account store: one component that owns where records live, how to find one by any
of its keys, how to mutate one, and what is held in memory. Everything else calls it instead of
walking directories.

The central piece is an **index**: character name -> account, account name -> account, email ->
account. Held in memory, built at boot in one pass, updated by the store on every mutation.

What that fixes, all from the same change:

- Every lookup becomes a map hit. The account count stops mattering — fifty or fifty thousand cost
  the same, so a bot creating junk accounts degrades to wasted disk rather than a degraded game.
- Global invalidation disappears; the index is the in-memory truth, so there is nothing to flush.
- Creation becomes one index lookup plus one write. The quadratic behaviour is gone.
- Boot's "parse everything" becomes the index build, which can skip and quarantine a bad record
  instead of `exit(1)`.

**Boot already scans every account file today**, so building the index costs nothing that is not
already being paid — it keeps the result instead of discarding it.

### Options considered

- **A. In-memory index, JSON files remain the truth — RECOMMENDED.** Derived state only, so it
  cannot drift from the records. No new dependency. Files stay human-inspectable and rollback-safe.
  Matches how the rest of the MUD works.
- **B. Persisted index file.** Skips the boot pass, but introduces an index that can disagree with
  the records after a crash — trading a cost already paid for a class of consistency bugs.
- **C. Embed SQLite.** Real indexes, transactions, field-level writes, no hand-rolled consistency.
  The right answer in the abstract, but it adds a dependency to a hand-built 32-bit server, changes
  the backup and inspection story, and is a larger migration than the problem currently warrants.
  Note the `mysqld` on the production box belongs to something else; the MUD links no database.

### Genuinely separate

**Rate-limiting account creation is a policy decision, not a storage one.** A perfect index does not
stop someone filling the disk. But it stops being urgent once the cost is linear and invisible
instead of quadratic and player-facing. The existing email-verification throttle is the template to
copy. See also the deferred decision on login rate limiting.

## Relationship to the PPC feature

None blocking. PPC adds one writer on the save path and one field to the record; it does not touch
creation, and its writes are gated by compare-before-write so they occur only on a real settings
change. When the store lands, PPC's write site migrates alongside the other 21. `AccountPreferences`
and the PPC design are unaffected.

The one interaction worth noting: PPC put more content into `account.json`, so a serialization defect
there could reach the boot `exit(1)`. Unknown flag names and unknown colour modes inside the
`preferences` block are already skipped rather than fatal for exactly this reason.

## Deferred follow-ups from the PPC branch — review alongside this work

Two known defects were deliberately left unfixed on `feat/account-level-ppc`. Both are cosmetic or
near-unreachable, and both live in code this project will rewrite anyway, so they were parked to be
reconsidered here rather than patched under a branch that was already verified.

They were deferred for a specific reason worth carrying: **two separate fix waves on that branch each
introduced a new defect** (the sibling preference, added to close a login race, produced a critical
data-loss path; the wave before it produced its own). The branch is currently green under three
adversarial review passes, a code re-review and live server testing. Any further change there would
require re-running all of it to hold the same confidence, for very little gain.

**1. A freshly created alt does not adopt an online sibling's live scheme in its first session.**
`ppc_apply_account_to_character_in` gates the sibling preference on `ch->ppc_account_loaded`, and the
level-0 seeding guard leaves that flag false. So a new character on an account that has no stored
preferences yet keeps its creation defaults for that session even when the player's main is online
with the real scheme. Scope is narrow: it only applies while the account is UNSEEDED — once the
account has preferences, the apply branch sets the flag and sibling adoption works normally. So this
is new-characters-only, during the migration window, and it self-resolves.

The fix means separating "account read failed" from "level-0 seed refused" into distinct signals.
That is exactly the area that produced the critical bug, which is why it was not attempted late in
the branch.

**2. The colour clamp misses a legacy bare-integer slot form.**
`parse_color_setting_value` (`character_json.cpp`) accepts a slot written as a bare integer and
returns before `parse_color_value_object`, so that form skips the range clamp even on the lenient
account path. Reaching it requires a hand-edited `account.json`; no legacy corpus exists, because
account files carrying colour data were introduced by the PPC feature itself. The crash it could
have caused is already closed by the bounds check in `get_color_sequence` (`color.cpp:476`) — that
one was a genuine out-of-bounds read whose regression test segfaulted the binary before the fix.
What remains is an out-of-range value rendering as nothing.

**When picking these up:** re-run the adversarial passes and the live scenarios afterwards. Live
evidence and the full review history from the PPC branch are preserved outside the repo at
`/home/ahumbert/parked/ppc-live-evidence/`.
