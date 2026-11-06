# Account Save/Load Pipeline — Performance Findings

**Date:** 2026-06-29 (initial measurement).
**Source:** the `savebench` implementor command (`src/savebench.cpp` → `src/save_benchmark.cpp`) added on `feature/savebench-port`. See the design in `2026-06-29-savebench-port-design.md`.

## Methodology

- Measured in-game with **`savebench 500`** on a level-100 implementor (`drelibench`, an account-linked clone of `Drelidan`) against its **real** on-disk account/character files.
- Each stage is timed independently over 500 iterations; the report gives min/avg/max µs and each stage's share of the per-direction total. `read_account_file` succeeds here (a real account is present), so these are *live* numbers — unlike the offline gtest, where the account-directory `readdir` scan degrades to a fast miss under emulation.
- **Environment caveat — these are i386-under-QEMU numbers.** The MUD is a 32-bit i386 binary run via QEMU emulation on an arm64 host. **Treat the *relative* breakdown (where the time goes) as representative and the *absolute* µs as a loose upper bound** — native i386 (the live server) will be meaningfully faster, especially on the compute-heavy JSON parse/serialize stages.
- The instrumentation overhead is negligible on the live path: per direction the sum of stages equals the end-to-end single-pass total (2118 == 2118 µs SAVE, 3726 == 3726 µs LOAD), so the breakdown reconciles exactly.

## Measured results (avg µs per character, 500 iterations)

### SAVE pipeline — total ≈ 2118 µs/char

| Stage | min | avg | max | share |
|---|---:|---:|---:|---:|
| **S2 `read_account_file`** | 1057 | **1217** | 5690 | **57.5%** |
| S4 `serialize_character_to_json` | 327 | 338 | 545 | 16.0% |
| S5 `write_text_file_atomically` | 212 | 327 | 753 | 15.4% |
| S3 `character_data_from_store` | 153 | 167 | 475 | 7.9% |
| other (validate/mkdir/path/owner/index) | 0 | 69 | 0 | 3.3% |

### LOAD pipeline — total ≈ 3726 µs/char (L1–L4; L5 `store_to_char` is offline-only)

| Stage | min | avg | max | share |
|---|---:|---:|---:|---:|
| **L3 `deserialize_character_from_json`** | 1680 | **1811** | 5009 | **48.6%** |
| **L1 `read_account_file`** | 1060 | **1151** | 1556 | **30.9%** |
| L4 `apply_character_data_to_store` | 542 | 594 | 718 | 15.9% |
| L2 `read_text_file` | 50 | 58 | 209 | 1.6% |
| other (validate/mkdir/path/owner/index) | 0 | 112 | 0 | 3.0% |

## Interpretation — two bottlenecks

1. **`read_account_file` dominates and is paid in *both* directions** — 57.5% of SAVE and 30.9% of LOAD. It re-reads and re-parses `account.json` (an `opendir`/`readdir` resolution plus a full JSON parse) on **every** save and load, even though the owner→account link is invariant for a given character within a session. **This is redundant work**, which makes it the highest-value optimization target: caching the resolved owner→account link per character would remove ~57% of save cost and ~31% of load cost outright.

2. **`deserialize_character_from_json` is the LOAD bottleneck** — 48.6%, and ~5× the cost of the *serialize* side (S4, 338 µs). The hand-rolled JSON reader is far more expensive than the writer. If load latency becomes a concern, the parser is the place to look. The file read itself (L2, 58 µs) is trivial — the cost is CPU in the parse, not disk I/O.

3. **Disk I/O and the in-memory char↔store transforms are cheap.** The atomic write (S5, 327 µs), the file read (L2, 58 µs), `character_data_from_store` (S3, 167 µs), and `apply_character_data_to_store` (L4, 594 µs) are all minor. The cost is `read_account_file` + JSON parsing, not the filesystem.

4. **LOAD (~3.7 ms) is ≈1.75× SAVE (~2.1 ms)**, driven entirely by deserialize ≫ serialize.

## Implications for the deferred consistent-snapshot autosave

The point of this benchmark was to size the cost of a save-all snapshot before tightening cadence. The data says:

- A snapshot pass pays the SAVE path per connected player (~2.1 ms/char under QEMU; less native). With `read_account_file` being **57% of that and redundant**, **caching the owner→account link should come *before* any cadence reduction** — it roughly halves per-player save cost and is pure upside.
- Order of operations for the next branch: **(1) add an owner→account-link cache** (the dominant, redundant cost in both directions) → **(2) re-measure** → **(3) only then** de-gate `Crash_save_all` to save-all and/or lower the cadence below 240s, sized against the now-lower per-player cost.
- The single-threaded snapshot runs inline on the 4-pulse/sec loop (250 ms/pulse). At the current 240s cadence the per-pass cost is comfortably absorbed; the link cache is what makes a *tighter* cadence over many players safe.

## Open follow-ups for more precise numbers

- Re-run on **native i386** (the live server, not QEMU) for absolute latencies that reflect production.
- Run immediately **after a fresh login** (cold FS cache) vs. warm, to bound the I/O-cache variance on `read_account_file`/`read_text_file`.
- Measure with a **heavier character** (full inventory/affects) — `serialize`/`deserialize` scale with character size, so the parse share of LOAD may grow.
