# Account Store Layer — Design

Date: 2026-09-05
Status: **Design, approved in discussion. No implementation plan yet.**
Branch: `feat/account-store`, based on `upstream/release-frodo` (e045806).

## What this changes

The account subsystem resolves accounts by walking the `accounts/` directory and parsing every
record it finds. This replaces those walks with an in-memory index, built at boot and maintained on
write. Nothing about the on-disk format changes: same JSON, same filenames, same paths, written at
the same moment in the same order.

## Scope

**In:** account records — lookup, mutation, boot indexing, and the failure policy around them.

**Out, and why:**

- *Character files.* The character side already has an in-memory index (`player_table`, built by
  `build_directory`, db.cpp:966-970). Its one design wart is that the metadata is encoded in the
  filename, which is original CircleMUD-lineage design, is not causing a live defect, and has an
  external consumer (`../bin/autowiz`) whose source is not in this repo. Most of what *looks* like
  character-side slowness is account-side: `save_char` (db.cpp:3221) is slow because
  `find_linked_character_owner_account` scans the account tree, and that scan dies here.
- *The legacy `system("rm")` + `system("cp")` finalize path.* Already dead — db.cpp:3165 calls
  `finalize_player_file_rename`; `finalize_player_file_legacy` survives only in the header and a
  comparison test.
- *Boards, mail, ban, pkill.* Flat files, low churn, no index need, nothing else couples to them.
- *Rate-limiting account creation.* A policy decision, not a storage one. It stops being urgent once
  creation is linear and invisible instead of quadratic and player-facing.
- *The remaining `system()` / `fork()` calls on the pulse loop* (limits.cpp:386, four in db.cpp, the
  sendmail `fork` at account_management.cpp:466). A latency problem that happens to touch the
  filesystem; it belongs in its own piece of work.

## Design

### 1. The index sits behind seams that already exist

Two structural facts make this smaller than it first appears:

- **Writes already funnel through one function.** All 19 `write_account_file` call sites outside the
  tests (17 in `account_management_identity.cpp`, one in `interpre.cpp`, one in
  `account_management_internal.cpp`) go through `account_management_storage.cpp:170`. That is why the current `invalidate_all()` can live in one
  place, and it is where index maintenance goes.
- **Reads already funnel through three resolvers**, each of which already has an enable/disable seam
  from the existing `account_cache`:

  | Lookup | Current implementation | After |
  |---|---|---|
  | by account name | `find_account_file_path_by_account_name` (account_management.cpp:800) — full two-level `readdir` | map lookup |
  | by email | `find_account_by_email_internal` (account_management.cpp:966) — full walk, parses every record | map lookup |
  | by character name | `find_linked_character_owner_account_uncached` (account_management_identity.cpp:903) — full walk | map lookup |

Public signatures do not change. The bodies behind the seams do.

Worth naming explicitly, because it is worse than the findings doc recorded: `read_account_file`
does not derive a path even though `account_file_path_from_email` (account_management.cpp:733)
exists and is what `write_account_file` uses. It calls the name scan. And `resolve_account_storage_key`
(account_management.cpp:744) calls `read_account_file` **to compute a path** — so
`account_character_directory`, the function that answers "where does this character's object file
live", costs a full walk of the account tree before it can return a string.

### 2. What the index holds

Three maps, keys only:

- normalized email -> record path
- normalized account name -> normalized email
- character name -> normalized email

Roughly 100 bytes per entry; under 2MB at 10k accounts. **The index says where a record is, not what
is in it.** Reads still go to disk for the record itself. This is deliberate: it bounds memory on a
box that is already under memory pressure, and it keeps the file the single source of truth, so the
index cannot disagree with a record's *contents* — only about which records exist and where.

The existing whole-record memoization in `account_cache` (`g_account_cache` holds full `AccountData`
copies) is left alone by this work and reconsidered once the index exists.

### 3. Boot

`build_account_native_player_index` (db.cpp:629, called from db.cpp:971) already makes exactly the
pass the index needs. It is not replaced; it is finished — it keeps what it read instead of
discarding it.

That pass is currently quadratic on its own: per linked character it calls
`account_character_player_path(".", account_name, ...)`, which goes through
`resolve_account_storage_key` -> `read_account_file` -> a full tree walk. Cost is about
(total linked characters) x (accounts on disk). Once the record is in hand its path is derivable
directly, so this disappears with no new machinery.

### 4. Failure policy at boot

Today that one function has **four** `exit(1)` sites: unparseable account JSON, an email that does
not match its directory name, a character file that cannot be inspected, and one that cannot be
read. Any one of them, on any one record, and the server does not start.

Replaced with: quarantine the bad record, keep booting, and refuse to boot past **5** failures.

The threshold is a bug detector, not a corruption tolerance. The write path cannot produce a torn
file — `write_account_file` checks the `fwrite` length and the `fclose`, removes the temp on either
failure, and only then renames — so disk-full or a crash mid-write leaves the previous record
intact. The realistic causes of an unreadable record are ours and they hit many records at once: a
serialization change, or a `normalize_email` change (which is what the mismatch check actually
detects). One bad record is a
genuine one-off and should not take the game down for everyone. Six at once means we shipped
something, and stopping before players log in and write on top of it is the recoverable outcome.

Two properties this must keep:

- **Quarantine is an in-memory mark, not a filing operation.** Nothing is moved, copied, renamed or
  written; the record stays on disk exactly as it is, with its email still occupied in the index,
  marked bad. The state lasts only for the life of the process — the next boot decides again from
  the files as they stand. Otherwise the address reads as "no account exists" and the next person to enter it
  could create a fresh account over a real player's record — turning a recoverable parse error into
  actual data loss.
- **Quarantine is loud.** A `mudlog` at boot plus a wizard-visible list of what was quarantined, so
  it is triaged rather than left to rot. The same applies after boot: a record that becomes
  unreadable while the server runs is logged, mudlogged once, and listed — but only *marked*, never
  quarantined, since withdrawing a live player's keys over a transient read error would cost them
  every save until the next reboot.

### 5. Write path and drift

The single new failure mode this design introduces is index drift: if a write succeeds and the index
update does not run, a lookup resolves to the wrong record. That is worse than a slow scan, because
it is silent.

The mitigation is structural. `write_account_file` already ends with `invalidate_all()` on success,
after the rename, only when everything worked; that line becomes the index update, in the same place
under the same condition. Three keys are maintained there, not one — email, account name, and the
character-name keys, which are the mutable ones (link, rename, delete) and where drift would
actually surface. Deletion is the other mutation and does not run through that function; the
account-aware delete path gets the same treatment at the point its removal succeeds.

On top of that: the store can rebuild the index from disk on demand, and a wizard command runs a
rebuild and reports any disagreement with the live index. Drift becomes something checkable on
purpose after a deploy rather than something a player reports. Automatic periodic verification was
considered and deferred — on-demand covers the deploy case, which is when drift would be introduced.

### 6. What does not change

- On-disk format, filenames, paths, write ordering.
- Public function signatures in `account_management_*.h`.
- Whole-record reads: the index resolves *where*, the disk still supplies *what*.
- Character storage, in any respect.

## Verification

**The existing suite is the primary net.** 305 tests on this branch: `account_management_tests`
(174), `interpre_account_menu_tests` (117), `account_cache_tests` (9), `roster_cache_tests` (5).
`account_ppc_tests` (53) is not on this branch; it arrives with the PPC rebase and applies from then
on. Because the resolvers keep their signatures, these must pass
**unchanged**. A test that needs editing to go green is a behaviour change to justify, not a test to
fix.

**New coverage** — index build, key maintenance across link/rename/delete, quarantine and the
threshold, an email staying reserved while quarantined, and a record that goes unreadable after boot
being marked and reported without withdrawing any key.

**One constraint shapes how those are written:** per the note in `account_cache.h`, the on-disk
`readdir` scan does not resolve under QEMU i386 emulation, which is why the cache carries
`set_backing_resolvers_for_testing`. The index build takes the same treatment — an injectable
enumerator, so tests drive it from records in memory rather than a real directory.

**Live verification is where this is actually confirmed.** Boot on real fixtures, then login,
create, link, delete, roster, and PPC, confirming the account tree is no longer read on the hot
paths. Baseline the gtest run on a clean tree first: the full run has pre-existing failures and has
historically aborted partway.

## Rollout

The same `set_enabled()` pattern the cache already uses. Index on by default, with a flag that falls
back to the existing scans, so a problem on the live port is one setting rather than a redeploy.
Keep the fallback for one release, then delete it — two live paths past that point is how drift
hides.

## Accepted risks

1. **Drift is silent where a slow scan was merely slow.** Mitigated structurally (single chokepoint)
   and detectably (rebuild-and-compare), not eliminated.
2. **The blast radius is every account-facing feature** — login, creation, verification, password
   reset, deletion, roster, PPC — most shipped within the last month, and the regressions land on
   the login path. The unchanged-tests rule is the main guard.
3. **Quarantine trades a loud failure for a quiet one** on a single bad record. Accepted knowingly;
   the threshold and the wizard-visible list are the counterweight.
4. **Verification leans on manual passes**, because CI has no world files and the unit suite is
   already red at the unit-test step.
5. **No player-visible benefit.** Invisible if it works, very visible if it does not.

## Sequencing against PR #294 (PPC)

PPC will merge *after* this. It rewrites the same files, so it pays the rebase, and rebasing it
means re-running its three adversarial passes and live scenarios. To keep that cost down, the
store's account API is shaped so PPC's single write site maps onto it directly (PPC takes the call
site count to 21, which is the number the findings doc records). The two PPC
follow-ups parked in the findings doc are picked up during that rebase, which is where the findings
doc always intended them.
