# Account Store Index — Manual Test Checklist

For the merge of `feat/account-store` into `release-frodo` (merge commit `03f84a8`).
Design: `docs/superpowers/specs/2026-09-05-account-store-index-design.md`.

**What changed, in one line:** account lookups no longer walk `accounts/` — an in-memory index
answers them. Nothing on disk changed: same JSON, same filenames, same paths, same write order.

**What did NOT change, and is worth knowing before you start:** the JSON, the filenames, the paths,
and the order writes happen in. If you see a difference in any of those, that is a bug, not the
feature.

---

## Most of this is now scripted

Two server+client smoke scripts cover the client-observable items, run against a booted server:

```
export ROTS_SMOKE_EMAIL=... ROTS_SMOKE_PASSWORD=... ROTS_SMOKE_CHARACTER=...
python3 tools/smoke_account_index.py [port]                # sections 2, 3, 4, 5 (except the drill)
python3 tools/smoke_account_index_quarantine.py [port] [quarantined-email]
```

The fixture comes from the environment, not from a default: the scripts are in the repo and the
fixture password is not. `ROTS_SMOKE_CHARACTER` must be at `LEVEL_GRGOD` or above, since the
`account` command is immortal-only. On this machine the values are the `clauded3bugbot` fixture;
they are recorded in memory rather than here.

Both were run green in a throwaway worktree on 2026-09-06 against the merged tree, in exactly the
form committed: 19/19 on a healthy tree and 7/7 against a genuinely corrupted record.

The scripts are a floor, not a substitute — they prove the paths still work, not that the game feels
right. Section 1 (boot) and the quarantine drill's corrupt/reboot/restore steps stay manual, because
they need a server restart against a doctored account tree; the second script asserts the state
*after* you have done that.

---

## Before you start

- **Quit every character all the way out of the game between selection tests.** A character left
  in-game makes a later selection reconnect to it, which fakes the result and has burned smoke tests
  on this codebase before.
- **Do not run the quarantine drill (section 5) against live player data.** Use a copy.

---

## 1. Boot

- [ ] Server boots. The log shows `Account index: N account(s) indexed.`
- [ ] `N` matches `find lib/accounts -name account.json | wc -l`.
- [ ] No `record(s) quarantined` line on a healthy tree.

`log()` writes to **stderr**, not to `log/syslog` — read it wherever the port's stderr goes (under
`scripts/rots-docker.sh boot`, `docker logs <container>`).

## 2. Login

- [ ] Login by email address works, and feels instant.
- [ ] A wrong password is rejected; the right one then works.
- [ ] An **unknown** email says `No account exists for that email address.` and offers to create an
      account. This exact string is compared by `interpre.cpp` — if the wording changed, something is
      wrong.
- [ ] Account creation at a fresh address works end to end.

## 3. Roster and characters

- [ ] The roster lists the same characters it did before, in the same order.
- [ ] Selection by number works. Selection by full name works (case-insensitive).
- [ ] The sort/filter keys still behave (`a`/`l`/`c`/`s`, `w`/`r`/`t`/`m`).
- [ ] Link a character — it appears on the roster.
- [ ] Delete a character — it disappears, and stays gone across a logout and back in.

## 4. Saving — the one to be fussy about

- [ ] Play a linked character, quit cleanly, and confirm the save landed in the **account**
      directory (`accounts/<bucket>/<email>/<name>.character.json`), by modification time.
- [ ] The legacy `players/` file for that character was NOT written instead.

This is the path where a wrong answer is silently destructive, so check the file, not just that the
game said "saved".

## 5. Immortal surface (`LEVEL_GRGOD`)

- [ ] `account index` — reports the count and lists nothing quarantined on a healthy tree.
- [ ] `account index frobnicate` — prints usage rather than silently showing the summary.

### Quarantine drill — on a COPY of the account tree, never live

**"Quarantine" is in-memory only.** Nothing is moved, copied, renamed or written. The file stays
exactly where it is; the index simply refuses to resolve it and reserves its address. The state
lasts only for the life of the process — reboot and the boot walk decides again from scratch.

- [ ] Corrupt one `account.json` (e.g. `echo 'not json' > …`), boot.
- [ ] The server **boots** rather than exiting, and logs `1 record(s) quarantined` plus a per-record
      line naming the file and why.
- [ ] `account index` lists that record.
- [ ] Creating an account at a **different** address still works. (A quarantined record must not
      block creation game-wide.)
- [ ] Creating an account at the **quarantined** address is refused. (Its address is reserved — this
      is what stops a new account being written over a real player's record.)
- [ ] Confirm the corrupt file is still in place, unmoved and unmodified — quarantine touches no
      file.
- [ ] Restore the record; confirm byte-identical; reboot; `account index` reports nothing
      quarantined.

### Unreadable-since-boot drill — on a COPY, never live

- [ ] Boot with a healthy tree, then corrupt one `account.json` **while the server is running**.
- [ ] Log in as that account's owner. The login path writes the record, which now refuses.
- [ ] The syslog gets one line naming the file and the parser's reason, and online immortals see it
      mudlogged. Log in again: it is **not** repeated — the report is once per record.
- [ ] `account index` lists it under `UNREADABLE SINCE BOOT`, separately from quarantined records.
- [ ] That account's characters still resolve and still save. This is report-only; it withdraws
      nothing.
- [ ] Restore the file and log in again. The next successful write clears the mark, with no reboot.

## 6. If something is wrong in production

`account index` names every record the server could not read, with its path and the reason. Since
nothing on disk changed and quarantine moves no file, the record is where it has always been and can
be inspected or repaired in place. A repaired record needs a reboot to leave QUARANTINE; one merely
marked unreadable-since-boot clears as soon as the server reads it successfully again -- the next
login against that account is enough -- or on the next successful write to it.

---

## Notes for whoever reads this later

- Boot refuses to continue past **5** unusable records. That threshold is a bug detector, not a
  corruption tolerance: the write path cannot produce a torn file, so many bad records at once means
  we shipped something, and stopping before players write on top of it is the recoverable outcome.
- Production has **no** legacy flat account records (`accounts/<bucket>/<name>.json`) — verified 0 on
  the live tree, 2026-09-05. The code still handles them because the retained fallback scans do.
- The fallback scans are meant to live for one release and then be deleted, taking the legacy-flat
  handling with them.
