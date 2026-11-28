# Adversarial review — account store index branch

Date: 2026-09-08. Target: `upstream/release-frodo..HEAD` (35 commits, 23 files, +6328/-135).
Method: high-effort adversarial code review over every source hunk; the four changed TUs
(`account_index.cpp`, `account_management.cpp`, `db.cpp`, `act_wiz.cpp`) pass `g++ -fsyntax-only`.
Status: **findings recorded, nothing fixed, nothing committed.** Tree clean at 35 commits.

Verification key: [V] = verified by hand in this session. [R] = reviewer's finding, relayed as
written, not independently checked.

## High

### 1. [V] Conversion-loss guard would refuse ~47% of live characters
`src/account_management_internal.cpp:626` — introduced by this branch in `c62c075`.

`FIELD_DIFFERS(specials2)` is a whole-struct `memcmp`, but the JSON path deliberately does not
round-trip that struct byte-for-byte:

- `src/character_json.cpp:1990` masks `PLR_CRASH` out of `act` on load, by design (transient
  inventory-dirty marker, must never be restored from disk).
- `specials2.bad_pws` is never serialized at all, though the legacy loader/saver persist it
  (`src/db.cpp:2369`, `src/db.cpp:3077`).

`neutralize_defaulted_on_unset` (`account_management_internal.cpp:582`) covers only
tactics/shooting/casting and the colour-slot modes — neither of the above.

Measured against `/home/ahumbert/u/rots_live/lib/players`, 6,906 character records carrying the
keys (33,916 files scanned), by two independent methods that agree:

    act with PLR_CRASH (bit 64) set:  3260   (47%)
    bad_pws != 0:                      169

Effect: those characters hit `account addchar`, get "field 'specials2' does not survive the round
trip", and can never be migrated. `PLR_CRASH` is set on any inventory change
(`src/handler.cpp:1225`), so this is the ordinary state of a saved character, not an edge case.
`bad_pws` is incremented then saved at `src/interpre.cpp:3127-3128` — the state of exactly the
offline character an immortal would migrate. `two_handed` has the same shape (raw `int` from
`KEY_INT` in, coerced to 0/1 on readback).

Fix shape: field-by-field comparison mirroring the known-lossy transforms, not `memcmp` over the
struct. The error message should also name something the operator can act on.

### 2. [R] Boot proceeds with an empty authoritative index
`src/db.cpp:755` — `build_account_native_player_index` returns silently when
`for_each_account_record_on_disk` fails: no log, no mudlog, no `exit(1)`, while `boot_db` has
already called `account_index::set_enabled(true)`. On any `opendir("accounts")` failure other than
ENOENT (EACCES after a manual SFTP deploy, EMFILE/ENFILE at boot) the index comes up empty and
authoritative. `find_account_by_email_internal`'s fast path then answers every login with
"No account exists for that email address." — the exact string `src/interpre.cpp:3030` compares
against to offer account creation. Every player on the box is told their account does not exist and
invited to register. The old scan returned a distinct "Failed to open accounts directory ..." and
would not have offered it.

Fix shape: refuse to boot, or leave the index disabled, rather than proceed empty-and-authoritative.

### 3. [R] Unreadable bucket directory strands the accounts inside it
`src/account_management_storage.cpp:387` — a bucket directory that will not `opendir()` is visited
as ONE unreadable record keyed by the bucket path, so the accounts inside are neither indexed nor
quarantined by email. `create_account`/`create_account_for_email` now skip
`account_storage_contains_unreadable_records` when the index is authoritative
(`account_management_identity.cpp:480`, `:558`), and `is_quarantined(email)` cannot match a
path-shaped bucket key. Those players are told no account exists and offered a new one. Their
characters are absent from `g_characters`, so `find_owner_email_by_character` reports them unlinked
and `save_char` takes the "refusing legacy fallback for account-native character" branch — nothing
is saved for any of them, log-only, for the life of the process. The escape hatch inside
`account_storage_contains_unreadable_records` is unreachable; its own comment says both callers skip
the walk.

## Medium

### 4. [R] Index upsert sits after the retirement steps
`src/account_management_storage.cpp:273` — the `upsert` (line 299) is after the two post-`rename`
retirement failures. If `std::remove` of the stale or legacy flat file fails with anything but
ENOENT, `account.json` is already in place but the index and `account_cache` still hold the pre-write
keys. A character just linked by that write is missing from `g_characters`, so `save_char` resolves
it as unlinked and refuses the legacy fallback — silent total save loss until reboot. Upsert/
invalidate belong immediately after the successful `rename`.

### 5. [R] Two records sharing one email silently drop the first's claims
`src/account_index.cpp:258` — `g_owned_characters` is keyed by email alone. On upserting the second
record, `erase_owned_keys(email)` (line 231) withdraws the FIRST record's account-name and character
claims without recording contention. That record then fails `find_path_by_account_name` with "Failed
to open account file for account 'X'" and its characters resolve as unlinked, where the old
`find_account_file_path_by_account_name` / `find_character_owner_account` scans resolved both by
name. The email-contention set exists to make this visible; the name/character claims are dropped
silently instead.

## Low

- [R] `src/account_index.cpp:416` — `is_quarantined()` normalizes its argument, but directory-layout
  records are quarantined under the raw `record.directory_entry_name`, never re-normalized. A record
  filed under a non-lowercase/untrimmed directory name is quarantined under a key `is_quarantined`
  can never produce, so "the quarantined record's address stays reserved" silently does not hold and
  `create_account_for_email` proceeds. `is_quarantined_record_key` exists for this; the email door
  does not use it consistently.
- [R] `src/account_index.cpp:491` — `size()` counts quarantined entries, so the boot log
  ("N account(s) indexed") and the `account index` header over-report by the quarantine count.
- [R] `src/account_index.h:197` — dangling doc comment for the declaration removed in `e9e7db4`,
  followed by an empty `//` and the namespace close; reads as a missing declaration.
- [R] `src/account_index.cpp:577` — `join_claimants()` is defined in an anonymous namespace and never
  called (the `act_wiz.cpp` listing inlines its own join). Invisible only because the Makefile builds
  with `-w`.

## Checked and found correct

The `deserialize_account_from_json` normalization the owner-name fast path relies on
(`account_management_storage.cpp:151`) is byte-identical to `upsert`'s; `is_directory_bucket_entry`'s
switch to `classification.directory_layout` is safe at its only call site (reached only after a
successful record read); the double `half_chop(buf, ...)` in `do_account index` is redundant but
harmless; `affected[].next` and struct padding do not perturb the conversion `memcmp` (both sides are
value-initialized and filled field-wise); the colour-slot round trip is covered by
`convert_old_colormask` before the comparison runs.

## Outcome — all nine fixed, 2026-09-08 (uncommitted)

Each fixed test-first: test written, watched fail for the expected reason, then the fix. Working
tree only, nothing committed. 11 files, +344/-77.

| # | Finding | Fix |
|---|---------|-----|
| 1 | Conversion guard refuses ~47% of live characters | `neutralize_known_lossy_transforms()` in `account_management_internal.cpp` — compares `act` without `PLR_CRASH` and ignores the unserialized `bad_pws`. 2 tests. |
| 2 | Empty index stays authoritative after a failed boot walk | `db.cpp` distinguishes "no accounts directory yet" (fine) from a real failure, and on a real failure logs, mudlogs, clears and disables the index so the resolvers fall back to scanning. 1 test. |
| 3 | Unreadable bucket lets its accounts be registered over | `email_bucket_is_quarantined()` in `account_management.cpp`, consulted by both registration guards. 1 test. |
| 4 | Index upsert sat after the retirement steps | Cache flush + upsert moved to immediately after the successful `rename`. 1 test. |
| 5 | Second record at one address wiped the incumbent's claims | `upsert` keeps both records' character claims when the address is disputed and carries the union in `g_owned_characters`. 1 test. |
| L1 | `is_quarantined()` could not match a directory record | `account_index_quarantine_key` normalizes the (email-shaped) directory name; path-shaped keys still untouched. 1 test. |
| L2 | `size()` over-reported in the boot log | Boot line now reads "N record(s) indexed, M of them quarantined". `size()`'s contract and its test are unchanged. |
| L3 | Dangling doc comment in `account_index.h` | Removed. |
| L4 | Dead `join_claimants()` | Removed. |

Two corrections to the review itself, both verified in the source:

- Its `two_handed` sub-claim under finding 1 is **wrong**. `sanitize_persisted_combat_state`
  (`db.cpp:2275`) already coerces the field to 0/1 on the legacy load path (`db.cpp:2566`), so it
  cannot mismatch. Not fixed, because there is nothing to fix.
- Finding 5's fix is NOT "fail closed on both records", which was the first thing considered. Before
  the index, the email lookup refused but the separate character scan resolved characters on BOTH
  records; refusing both would have been a second behaviour change. The fix restores the old
  behaviour instead. The account-NAME lookup still refuses via the contested email — that divergence
  predates this work and is documented at `account_index.cpp`'s `find_path_by_account_name`.

### Enabling change worth knowing about

`build_account_native_player_index` was file-local in `db.cpp`'s anonymous namespace and could not be
tested at all. It is now defined past that namespace and declared in `db.h`. This is a production
change beyond the fix itself and was not in the review.

### Test-suite interaction (NOT a defect in this work)

With the six new tests present, the full run SIGSEGVs at test 492 in
`InterpreAccountMenu.SelectingSameLinklessActiveCharacterReconnectsExistingBody` instead of at
`UnlockSelect*`. Bisected:

- clean tree, `--gtest_filter='-InterpreAccountMenu.UnlockSelect*'`: exit 1, 778 run, 10 failures.
- **production changes only, tests reverted**: exit 1, 778 run, the same 10 failures. No crash.
- production changes + the new tests: exit 139 at 492.
- both reconnect tests filtered: exit 1, **784 run, the same 10 failures**, all six new tests pass.

So the added tests perturb heap layout and the pre-existing, layout-dependent reconnect segfault
lands on a neighbouring test in the same suite. Practical consequence: the workaround filter now
needs two exclusions, not one. See memory `test-suite-segfault-unlockselect`; that work stays tabled.

## Second review, 2026-09-09 — five more findings, all fixed (uncommitted)

Re-reviewed after the fixes above. Five findings; two were regressions introduced by those fixes.
Tree now: 13 files, +527/-86, still uncommitted.

| # | Finding | Fix |
|---|---------|-----|
| F1 | `db.cpp` — five letter buckets exist and each unreadable one quarantines as ONE record, so a chmod accident across `accounts/` gives exactly 5, which does not exceed the threshold of 5. Boot proceeded with an authoritative, EMPTY index. | New `unreadable_bucket` flag on `AccountRecordOnDisk`, counted in the boot visitor; ANY unreadable bucket now logs, mudlogs, clears and disables the index, same as the whole-directory failure. 1 test |
| F2 | `act_wiz.cpp:3199,3205` — two bare `sprintf` into the shared `buf1` with unbounded reader output, 20 lines above a `snprintf` whose comment names the hazard. | Both bounded with `snprintf`. Mechanical, no test |
| F3 | `account_index.cpp:237` — **regression from finding 5's fix.** The disputed path skipped `erase_owned_keys` but still overwrote `g_entries[email]`, discarding the incumbent's account name so its key could never be withdrawn. Once the dispute settled it resolved again, to a record no longer carrying that name. | New `g_owned_account_names` mirrors `g_owned_characters`, so both key kinds are withdrawn the same way. 1 test |
| F4 | `db.cpp:641` — `g_unreadable_character_files_at_boot` only ever incremented. Latent until the `db.h` export above made the function callable more than once. | Both boot counters reset at function entry. No test — see below |
| F5 | `account_management_identity.cpp` — the write-time occupancy guard inspects `final_path` (always the directory layout), so a legacy FLAT record that went unreadable after boot read as "address free" and a second record was written at the same email, disputing it permanently. | Both registration guards now refuse when the index already holds the address, readable or not. 1 test |

### Judgement calls

- **F4 has no test.** The first attempt asserted equal log output across two runs with an empty
  `accounts/`, where the counter never increments — it passed without the fix. Producing a real
  increment needs an account record with a linked character whose character file exists but does not
  parse; that fixture costs more than a log-line count is worth. Removed rather than kept as a test
  that proves nothing. F2 is likewise mechanical.
- **F5's test first passed for the wrong reason.** `create_account` had left a readable record at the
  directory path, so the write-time guard refused on that instead of the flat one. Rewritten to leave
  only the corrupted flat record, which then failed correctly.

### Crash attribution, re-checked

The wandering segfault moved again — test 492 → 495
(`SelectingSameActivePlayingCharacterUsurpsExistingDescriptor`), still inside the same reconnect
cluster. Re-bisected exactly as before, with the same result: **production changes alone run clean —
exit 1, 778 tests, the same 10 pre-existing failures, no crash.** With all new tests present and the
three known crash victims excluded: exit 1, **786 tests, the same 10 failures**, every new test
passing. Each test added to this binary shifts which member of that cluster the pre-existing
layout-dependent bug lands on; chasing it with more filter exclusions is not a fix. ASan on
`ageland_tests` remains the real next step, and that work stays tabled.

## Live red-green validation, 2026-09-09

Five of the fourteen findings have a failure mode a player or an operator can actually see on the
wire. Each was staged against a booted local server twice: once on a build with the working-tree
fixes **stashed out** (RED — prove the danger reproduces), once with them in (GREEN). Harness:
`testing/danger/` (untracked, local-only), driven by `testing/danger/run_all.sh <red|green>`, which
restores `lib/accounts` + `lib/players` from a tarball before and after every scenario.

Findings 4 (upsert ordering) and F4 (counter reset) are NOT here and cannot be: 4 needs a
`std::remove` failure, F4 needs a character file that parses on one path and not the other. Both
stay unit-test-only. Finding 5/F3 (two records at one email) is not here either — which record the
walk reaches second is `readdir` order, so a live test of it would assert on something the
filesystem chooses. Also unit-test-only, deliberately.

| Scenario | Finding | RED (fixes stashed out) | GREEN (fixed) |
|---|---|---|---|
| S1 `s1_conversion_guard` | 1 | `kurst` (act=64) and `ologr` (act=524352) both refused: *"field 'specials2' does not survive the round trip"*. Control `orcen` (act=0) migrated. 5/5 | both migrate, control still migrates. 5/5 |
| S2 `s2_accounts_unreadable` | 2 | boot log carries **no account-index line at all**; login answers `No account exists for that email address.` + `Create one? (Y/N):`. 3/3 | `Failed to open accounts directory './accounts': Permission denied. The index is NOT authoritative; account lookups fall back to scanning.`; the player is never told the account is missing and never offered registration. 4/4 |
| S3 `s3_buckets_unreadable` | F1 | `5 record(s) quarantined` then `5 account(s) indexed` — five does not *exceed* the limit of five, so it booted authoritative and empty; same lockout + registration offer. 3/3 | `5 account bucket(s) could not be read. The index is NOT authoritative...`; no lockout, no offer. 3/3 |
| S4 `s4_bucket_registration` | 3, F5 | `1 record(s) quarantined`, `12 account(s) indexed`; incumbent told no account exists, registration ran, **the fixture's `account.json` was overwritten on disk**. 5/5 | `1 account bucket(s) could not be read. The index is NOT authoritative...`; registration not offered, **record byte-identical on disk**. 4/4 |
| S5 `s5_quarantine_key` | L1 | corrupt record quarantined under raw `CLAUDEd3bugbot@example.com`; `is_quarantined()` could not match, **a fresh record was written at the reserved lowercase address**. 4/4 | registration refused, **no record at the reserved address**. 4/4 |

Two harness notes for whoever re-runs this:

- The container has no `sendmail`, so a registration that SUCCEEDS closes with *"sendmail reported a
  delivery failure with exit code 127"* rather than *"Account created"* — the record is already on
  disk by then. S4 and S5 therefore judge registration by what reached the disk, not by the closing
  message. An assertion that reads only the text will pass on a refusal and a success alike.
- S4 uses `chmod 0300` on the bucket, not `0000`. `0000` blocks the write too, which hides the
  destructive half of the finding behind a permission error; `0300` (write + search, no read) is the
  state that actually reproduces the overwrite.

Both binaries are kept at `bin/ageland.red` and `bin/ageland.green` for re-runs. `bin/ageland` was
left as the green build.

## S6 — migration fidelity follow-up, 2026-09-09

S1 proved the guard no longer *refuses* a PLR_CRASH character. It did not prove the migrated
character is correct, and that distinction matters for finding 1 alone: its fix widens what the
guard tolerates rather than narrowing what the server does. `testing/danger/s6_migration_fidelity.py`
closes it, green build only (the red build refuses the migration outright, so it never produces a
file to inspect). **8/8.**

Oracle: the retired legacy player file, which turns out to be a keyed TEXT format carrying every
field by name — not the binary `char_file_u` dump the never-written scanner tool was scoped around.
That makes a field-by-field comparison a plain parse, and removes the reason that tool existed.

- All **26** mapped scalar fields identical: name, sex, race, bodytype, language, hometown, weight,
  height, idnum, level, alignment, sp_to_learn, mini_lvl, max_mini_lv, rerolls, birth, played,
  last_logon, title, load_room, wimpy, freeze_lvl, morale, owner, rp_flag, spec.
- Migrated `kurst` appears on the roster at its legacy level (90), enters the game, lands in a room
  with a working prompt, and `score` renders.

### The guard's field list is narrower than `char_file_u`

`first_lossy_conversion_field` compares 22 members. `char_file_u` has 26. The four it never
mentions, and what each is actually worth:

| Member | In the JSON? | Verdict |
|---|---|---|
| `player_index` | no | Runtime player-table index, not persisted state. Correctly excluded. |
| `prof` | **no** | **Zero for all 5,591 characters in the live tree** (measured). The multi-profession `profs` array is the live system and `player.prof` is vestigial — `GET_PROF` has only 7 call sites. Nothing is lost. |
| `pwd` | **no** | Handled by design: `interpre.cpp:2340` writes `kAccountOnlyPasswordMarker` into an account-native character's descriptor rather than leaving a blank, so the legacy `CRYPT` comparisons cannot match. Not a hole. |
| `host` | **no** | Last-logon host. Genuinely dropped on migration. Forensic/ban context only. |

So the guard passing is a stronger statement than it looks: every field it lists is byte-identical,
and the only differences that can pass unseen are the two the neutralizers mask deliberately
(`act`'s PLR_CRASH bit, `bad_pws`), plus `host`. No third silent difference exists.

**Recommendation: no code change.** The one thing worth having is a comment on
`first_lossy_conversion_field` naming the four members outside its view and why each is acceptable,
so a later reader does not mistake "the guard passed" for "every member of the struct round-trips".

### Harness note

`PRF_MSDP` is a per-character preference (bit 24). A legacy character does not have it, so a
connection playing one receives **no MSDP variables at all**. Two earlier S6 assertions read
`CHARACTER_NAME`/`LEVEL` over MSDP and failed while the transcript plainly showed the character in
the game — the harness was wrong, not the server. Entry is asserted on the in-band text and the
roster's own level column instead. Any future scenario driving a non-fixture character must do the
same.

## S7/S8 — 50-character spot check against real live data, 2026-09-09

Finding 1 was a claim about the SHAPE of the live population (47% of characters carry PLR_CRASH),
and one fixture cannot speak to that. 50 characters were copied out of
`/home/ahumbert/u/rots_live/lib` — 10 per letter bucket, seeded random (`seed=20260909`), with
their `.obj` and `.exploits` companions — joining 5 legacy characters already in the dev tree for a
corpus of **55**. Composition: levels 1–92; 45 carrying PLR_CRASH, 5 without; 8 with `bad_pws != 0`;
`prof = 0` for all 55, consistent with the whole-tree measurement.

`testing/danger/s7_corpus_fidelity.py`, green build:

- **54 of 55 migrated.**
- **Every one of the 54 matched the legacy file on every mapped scalar field.** No mismatches at
  all, across ~1,400 field comparisons.
- 1 refused: `revilo`.

### `revilo` — a character the game plays but the migration will not take

`testing/danger/s8_revilo.py` captured the message verbatim:

    Truncated objects data while reading follower object record.

It is not the character file — it is `plrobjs/P-T/revilo.obj`, whose follower section ends without
its sentinel. Verified `cmp`-identical to the live source, so the truncation is in the live data,
not the copy. Ruled out: missing `.exploits` (tolerated — `read_file_bytes` returns success on
ENOENT for a non-required asset), long description (57 bytes), the unknown legacy key `bank_gold`
(the loader's switch has no erroring default; unrecognised keys are ignored).

The two readers disagree about the same bytes:

| Reader | Behaviour on the truncation |
|---|---|
| `objsave.cpp` `Crash_follower_load` (the live game) | `read_crashsave_record` logs `SYSERR: truncated crashsave data ...`, closes the file and **returns**. The character loads; only the trailing follower data is dropped. Non-fatal. |
| `objects_json.cpp:419` (the migration converter) | `read_pod` returns false, which propagates and **aborts the whole migration**. Fatal. |

So `revilo` logs in and plays today, and can never be moved into an account. That is the same
end state as finding 1 — a character permanently barred from migration — reached by a different
route, and it is NOT introduced by this branch: `objects_json.cpp` predates it. The conversion
guard is not involved.

`revilo` is an older on-disk variant in other ways too (carries `bank_gold`, lacks
`rp_flag`/`retiredon`/`color`), which is a hint about where such files come from.

**Scale is unmeasured.** One failure in 55 is not a rate. Counting it properly means running the
real `objects_json.cpp` parser over all 5,591 live `.obj` files — which is the standalone C++ tool
that keeps getting scoped and never written, now with a concrete payoff. Not done; not proposed as
part of this branch's work.

### Corrections to the harness, not the server

- S7's "50 expected" assertion is wrong: the dev tree already held 5 legacy characters beyond
  kurst/ologr/orcen, so the corpus is 55. The count assertion, not the corpus, is the defect.
- S7's refusal-reason extraction captured the prompt (`>`) instead of the server's message, which is
  why S8 had to exist at all. Any future scenario reading an error off the wire must skip the
  prompt line.
- `run.sh`'s restore was widened to `plrobjs/` and `exploits/`: migration retires a character's
  player, object AND exploits files, so a restore covering only `players/` leaves the corpus
  half-consumed for the next run.

## Live-tree scan of migration blockers, 2026-09-09

The S8 `revilo` finding was one character in a sample of 55, which is not a rate. Scanned the whole
live tree with `tools/scan/scan_live_migration.cpp` — a standalone program that calls the REAL
parser the migration calls (`objects_json::legacy_object_save_data_from_binary`, the same entry
point as `account_management_internal.cpp:389`), not a reimplementation. Built in the 32-bit
container so struct layout matches the server. Read-only bind mount of
`/home/ahumbert/u/rots_live/lib`; nothing written.

**Validated against ground truth before being trusted:** run over the dev corpus it reproduced the
server's verdict exactly — 55 parse, 1 refused, `revilo`, same message.

### Result: 76 of 5,592 characters (1.36%) cannot be migrated

| Refusal reason | count |
|---|---|
| Truncated objects data while reading follower object record | 37 |
| Truncated objects data while reading board point | 20 |
| Truncated objects data while reading top-level object record | 12 |
| Truncated objects data while reading rent data | 6 |
| Truncated objects data while reading follower record | 1 |

By size: 46 are substantial files (1k–8.5k) genuinely truncated mid-record, 22 are under 1k, **6 are
zero-byte** `.obj` files and 2 are under 100 bytes. Levels 13–100, median 30; 42 at level 30+, 6 at
level 50+.

### By year of last logon — this is an old-character problem

| Year | characters | cannot migrate | % |
|---|---|---|---|
| 1998 | 7 | 7 | **100%** |
| 1999 | 26 | 19 | **73%** |
| 2000 | 40 | 5 | 12.5% |
| 2001–2013 | ~1,600 | 15 | ~0.9% |
| 2014 | 116 | 11 | 9.5% |
| 2015–2019 | 617 | 1 | 0.2% |
| 2020 | 356 | 14 | 3.9% |
| 2021–2024 | 1,033 | 4 | 0.4% |
| **2025** | **738** | **0** | **0%** |
| **2026** | **848** | **0** | **0%** |

Nobody who has logged in during 2025 or 2026 is affected. The most recent affected character is
`nog` (level 42, 2024). The 1998–1999 cluster is near-total.

### The description blocker — corrected

An earlier draft of this section claimed the blocker "does not exist" because the longest on-disk
description over all 5,593 legacy files is 509 bytes, none over 511. **That was the wrong
criterion.** The mechanism is in-memory inflation, not on-disk size: `file_to_string()` appends a
`\r` after every 98-byte `fgets` chunk, so a 509-byte description becomes ~514 in memory and blows
the 511 cap. An on-disk measurement cannot see it, and the stored note's count of 297 affected
characters was never contradicted.

The blocker is nonetheless absent **here**, for a different reason: `KEY_LONG_STR` in this repo
already truncates the field and skips to the `~` instead of failing (committed; it is not part of
this branch's working diff). That is why `kurst` — the exemplar of that bug — migrated cleanly in
S6. Live's deployed `src/db.cpp` still carries the original unbounded loop, which writes past the
512-byte field into the rest of `char_file_u`.

So: not a migration blocker against this tree; still a live-server hazard until the repo is
deployed. The `desc_bytes` column in the scanner CSV measures on-disk length and must not be read
as an answer to this question.

### Recommendation

The mismatch is real and worth closing: `Crash_follower_load` keeps the character and drops the
trailing data, `objects_json.cpp` aborts the whole migration over the same bytes. Making the
converter as tolerant as the game — stop at the truncation, keep what parsed, log what was dropped —
would unblock all 76.

**Not on this branch.** `objects_json.cpp` is untouched by the account index work and this is not a
regression it introduced. It is a separate change against a separate decision, and the 0% figure for
2025–2026 says it is not urgent.
