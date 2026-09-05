# Core Server-Health Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 11 previously-identified, independently-verified bugs across the MSDP protocol, socket/connection layer, combat reentrancy, and NPC scripting — bundled as one "core server-health" pass since they're all connection/latency/messaging/protocol/reentrancy defects found across several prior audit sessions.

**Architecture:** No new subsystems. Every task is a small, surgical, localized fix to existing code. Tasks are grouped (MSDP / Socket-Infra / Combat-Reentrancy / Scripting) but each is independently committable and independently testable — nothing here depends on another task's code being present first.

**Tech Stack:** C++ (32-bit, `src/Makefile`), one Rust proxy crate (`proxy/`), one POSIX shell script (`autorun`).

**Base branch:** `release-frodo` (the current upstream release branch — never `master`). This plan was originally drafted and its 13 candidate bugs verified against a different local branch; it was then re-verified line-by-line against `release-frodo`'s actual code before any task was dispatched, because `release-frodo` includes a large intervening "Account management" commit (`f26d314`, #274) that rewrote `protocol.cpp`, `ranger.cpp`, `utils.h`, `structs.h`, `script.cpp`, and `db.cpp`. That re-verification found **two of the original 13 items already fixed** by that commit — see "Already fixed upstream" below. The remaining 11 tasks were all confirmed against the actual current code before this plan was finalized.

## Global Constraints

- **No automated C/C++ test runner exists in this repo** (`CLAUDE.md`): `src/CMakeLists.txt` doesn't build the `tests/` subdirectory or link GTest. Every task's verification step is therefore **build + targeted manual smoke test** (connect, exercise the exact changed behavior, watch the log), not a unit test run — this deliberately deviates from the skill's default pytest-shaped step structure.
- **Format ONLY the file(s) this task touches — never run the bare `make format` target.** `src/Makefile`'s `format` target runs `clang-format -i -style=WebKit *.cpp` and `*.h` across **every** file in `src/`, unconditionally. Confirmed live during Task 1: it reformatted 38 unrelated files and — critically — reordered the hand-ordered `#include "account_management_*.cpp"` fragment-includes in `account_management.cpp` (a deliberate, comment-documented "internal fragment must come before public fragments" ordering used because these are raw code fragments stitched into one translation unit, not real headers), which broke the build. Instead, format only the touched file(s) directly: `cd src && clang-format -i -style=WebKit <exact file(s) you changed>`.
- **Build via Docker, not native `make all`, in this environment.** Native `cd src && make all` compiles cleanly but fails at the final link step here (`cannot find -lcrypt` — the host has the i386 *runtime* `libcrypt1:i386` but not the i386 *dev* package that provides the unversioned `libcrypt.so` symlink `-lcrypt` needs). Use `scripts/rots-docker.sh compile` instead (needs `scripts/rots-docker.sh build` once first, to build the i386 toolchain image — already done for this worktree as of this plan's Task 1). **If a native `make all` was run first** (e.g. by accident, or on a different task/session), run `cd src && make clean` before the next `scripts/rots-docker.sh compile` — otherwise the Makefile's timestamp-based dependency tracking skips recompiling unchanged files, the container relinks stale host-built `.o` files against its own libstdc++/glibc, and link fails with `undefined reference to std::__throw_bad_array_new_length()` / `__libc_single_threaded` (an ABI mismatch between the two toolchains, not a real code problem — confirmed by reproducing and then clearing it with `make clean`).
- Booting the server for smoke tests additionally needs world files at `lib/world/` (present in this worktree — carried over from the original checkout) and `scripts/rots-docker.sh boot` / `cd src && make run`.
- Rust proxy: `cargo build -p proxy` / `cargo test -p proxy`.
- One commit per task. Do not batch multiple tasks into one commit — if a reviewer rejects one fix, the others must still land independently.
- This is a live production MUD (`docs/Running the Game.md`) — none of these changes deploy themselves; this plan only covers this repo. Production rollout is a separate, manual step outside this plan's scope.

**Already fixed upstream (confirmed in `release-frodo`'s current code — do not touch, no task exists for these):**
- **MSDP per-pulse freeze** (`msdp_update()` in `comm.cpp` using `return` instead of `continue` on `in_room == NOWHERE`): the current code (`comm.cpp:594`) reads `if (desc->character->in_room < 0 || desc->character->in_room > top_of_world) { continue; }` — already correct, and even hardened with an upper-bound check that didn't exist in the version originally audited.
- **Blocking `system("rm"/"cp")` in `save_player()`**: `save_player()` (`db.cpp:2953`) now calls `write_player_text()` + `finalize_player_file_rename()` (`src/player_file_finalize.cpp`), which uses `std::filesystem::rename()` (atomic, publishes the new file first) plus a directory-scan cleanup of stale versioned files — no `system()` call anywhere in the live path. A `finalize_player_file_legacy()` function still exists with the old `system()` pattern but has zero callers (dead code).

**Out of scope (explicitly excluded from this bundle, do not fix here):**
- `combat_dying_recovery_stays_engaged` — confirmed correct design intent (recovery-from-incap should stay engaged), not a bug.
- `msdp_bReport` defaulting to `true` — confirmed correct design intent (opt-in-by-default, opt-out via `UNREPORT`/`RESET`, which already works); left unchanged.
- `msdp_room_data_not_bundled_with_text` — informational only (separate buffered-vs-immediate write paths by design), not a bug.
- `get_hike_bonus` stat-hiking cliff — explicitly excluded by user as a separate "misc" item, not part of this server-health bundle.
- `check_autowiz()`'s `system()` call (`limits.cpp:373-383`), `move_char_deleted()`'s `system("mv ...")` (`db.cpp`), and the rename-flow's `system("rm"/"mv")` calls — same blocking-syscall anti-pattern as the already-fixed `save_player()` item above, but low-frequency (immortal-only actions) vs. that item's every-few-minutes autosave hit. Noted as a candidate follow-up, not included here to keep this bundle bounded. `finalize_player_file_legacy()` (dead code, same file as the fixed `save_player()` path) is also left alone — nothing calls it.
- Full delay-queue redesign for the double-delay bug (Task 10) — only a minimal reentrancy-correctness patch is in scope; a real fix (replacing the single-slot `ch->delay` with a proper queue) is a separate, larger effort.

---

## File Structure

| File | Tasks touching it | Responsibility |
|---|---|---|
| `src/comm.cpp` | 3, 4, 5, 6 | Socket accept/read/write plumbing, pre-login idle sweep |
| `src/act_move.cpp` | 1, 2, 9 | MSDP room update, `check_simple_move()` trigger firing |
| `src/act_offe.cpp` | 9 | `do_flee()` |
| `src/ranger.cpp` | 9 | `on_windblast_hit()` (second flee-shaped call site) |
| `src/protocol.cpp` | 2 | MSDP wire-format send functions |
| `src/utils.h` | 10 | `WAIT_STATE_BRIEF`/`WAIT_STATE_FULL` macros |
| `src/skill_timer.cpp` | 8 | `skill_timer::update_skill_timer()` |
| `src/script.cpp` | 11 | `get_next_command()` |
| `autorun` | 7 | Boot/restart loop (untracked shell script, still lives in this repo dir) |
| `proxy/src/main.rs` | 3 | Rust WS/TCP bridge |

---

## Group A — MSDP protocol (Tasks 1-2)

### Task 1: Fix MSDP room-update inverted guard

**Files:**
- Modify: `src/act_move.cpp:587` (inside `msdp_room_update()`)

**Interfaces:** None — one-line condition fix.

- [ ] **Step 1: Make the fix**

In `src/act_move.cpp`, inside `msdp_room_update()`:

```cpp
    if (ch->in_room >= 0) {
        return;
    }
```

becomes:

```cpp
    if (ch->in_room < 0) {
        return;
    }
```

`NOWHERE` is `-1`. The current guard bails out whenever the character is in a *valid* room (the normal case) and only proceeds when `in_room` is negative — meaning the full ROOM table (VNUM/NAME/EXITS/TERRAIN) essentially never gets sent on ordinary movement, and on the rare occasion `in_room` really is negative, the function falls through and indexes `world[-1]` (undefined behavior) further down. Flipping the condition makes it bail out only on the genuinely-invalid case and proceed for real rooms, matching every other `NOWHERE` guard in the codebase (e.g. `one_mobile_activity()` in `mobact.cpp`).

- [ ] **Step 2: Format**

```bash
cd src && clang-format -i -style=WebKit act_move.cpp
```

- [ ] **Step 3: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 4: Smoke test**

Connect with an MSDP client capable of showing raw variable state (e.g. Mudlet with `display(msdp.ROOM)`), or a client that renders a room-list/map from MSDP `ROOM` data. Walk through a few rooms with actual exits (including doors/one-way exits if easy to find) and confirm `ROOM.EXITS`, `ROOM.NAME`, `ROOM.VNUM`, and `ROOM.TERRAIN` update on every move — not just the bare `ROOM_NAME`/`ROOM_VNUM` from the generic per-pulse `msdp_update()` sweep, which was previously the *only* ROOM data clients ever saw on a normal move.

- [ ] **Step 5: Commit**

```bash
git add src/act_move.cpp
git commit -m "fix(msdp): correct inverted NOWHERE guard in msdp_room_update()

Guard checked in_room >= 0 (bail on valid rooms) instead of in_room < 0
(bail on the actual invalid case), so the ROOM MSDP table never updated
on normal movement."
```

---

### Task 2: Extend MSDP sanitization to `MSDPSendPair`/`MSDPSendList` and the `TERRAIN` field

**Files:**
- Modify: `src/protocol.cpp:1225-1313` (`MSDPSendPair`, `MSDPSendList`)
- Modify: `src/act_move.cpp:633` (TERRAIN field in `msdp_room_update()`)

**Interfaces:** None — internal hardening, no signature changes. `MSDPSanitizeValue()` (`src/protocol.cpp`) already exists and is reused as-is.

This is defense-in-depth, not an active-exploit fix: today every live caller of `MSDPSendPair`/`MSDPSendList` passes compile-time string literals (`SERVER_ID`+`MUD_NAME`, `LIST` command tables), and `SoundSend()` (the one function that *would* pass a caller-supplied trigger string) has zero call sites anywhere in the codebase — it's dead code. Still worth closing so a future caller (e.g. if `SoundSend` is ever wired up, or a new feature calls `MSDPSendPair` with room/player data) doesn't reopen an MSDP/telnet injection path.

- [ ] **Step 1: Sanitize `MSDPSendPair`**

In `src/protocol.cpp`, `MSDPSendPair()`:

```cpp
void MSDPSendPair(descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != NULL && apValue != NULL) {
        protocol_t* pProtocol = apDescriptor ? apDescriptor->pProtocol : NULL;
        if (pProtocol == NULL)
            return;

        /* Should really be replaced with a dynamic buffer */
        int RequiredBuffer = strlen(apVariable) + strlen(apValue) + 12;
```

Add sanitization right after the `pProtocol == NULL` early-return, before `RequiredBuffer` is computed from the raw `apValue`:

```cpp
void MSDPSendPair(descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != NULL && apValue != NULL) {
        protocol_t* pProtocol = apDescriptor ? apDescriptor->pProtocol : NULL;
        if (pProtocol == NULL)
            return;

        std::string SanitizedValue = MSDPSanitizeValue(apValue);
        apValue = SanitizedValue.c_str();

        /* Should really be replaced with a dynamic buffer */
        int RequiredBuffer = strlen(apVariable) + strlen(apValue) + 12;
```

The rest of the function is unchanged (it already only reads `apValue`, never writes through it).

- [ ] **Step 2: Sanitize `MSDPSendList`**

Same pattern in `src/protocol.cpp`, `MSDPSendList()`:

```cpp
void MSDPSendList(descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != NULL && apValue != NULL) {
        protocol_t* pProtocol = apDescriptor ? apDescriptor->pProtocol : NULL;
        if (pProtocol == NULL)
            return;

        /* Should really be replaced with a dynamic buffer */
        int RequiredBuffer = strlen(apVariable) + strlen(apValue) + 12;
```

becomes:

```cpp
void MSDPSendList(descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != NULL && apValue != NULL) {
        protocol_t* pProtocol = apDescriptor ? apDescriptor->pProtocol : NULL;
        if (pProtocol == NULL)
            return;

        std::string SanitizedValue = MSDPSanitizeValue(apValue);
        apValue = SanitizedValue.c_str();

        /* Should really be replaced with a dynamic buffer */
        int RequiredBuffer = strlen(apVariable) + strlen(apValue) + 12;
```

`MSDPSendList` later mutates its own local `MSDPBuffer` copy (converting spaces to `MSDP_VAL`), not `apValue` itself, so reassigning `apValue` to the sanitized copy up front is safe.

- [ ] **Step 3: Sanitize the TERRAIN field**

In `src/act_move.cpp`, inside `msdp_room_update()`:

```cpp
    extern char* sector_types[];
    msdp_room += sector_types[world[ch->in_room].sector_type];
```

becomes:

```cpp
    extern char* sector_types[];
    msdp_room += MSDPSanitizeValue(sector_types[world[ch->in_room].sector_type]);
```

(matches the `NAME` field a few lines above it in the same function, which already calls `MSDPSanitizeValue()`.)

- [ ] **Step 4: Format**

```bash
cd src && clang-format -i -style=WebKit protocol.cpp act_move.cpp
```

- [ ] **Step 5: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 6: Smoke test**

Connect with an MSDP-capable client. Send `LIST SENDABLE_VARIABLES`, `LIST COMMANDS`, and `SEND SERVER_ID` and confirm the responses are byte-identical to before the change (all compile-time constants, sanitization should be a no-op on them since none contain control characters, quotes, or backslashes). Walk into a few different terrain types (confirmed via `docs/` or in-game `look`) and confirm `ROOM.TERRAIN` still renders correctly (plain sector-type names have no characters `MSDPSanitizeValue` would alter, so output should be unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/protocol.cpp src/act_move.cpp
git commit -m "harden(msdp): sanitize MSDPSendPair/MSDPSendList and the TERRAIN field

MSDPSetString() already ran values through MSDPSanitizeValue(), but
MSDPSendPair/MSDPSendList and msdp_room_update()'s TERRAIN field didn't.
No live caller currently passes attacker/player-controlled text through
these paths, but close the gap for defense-in-depth."
```

---

## Group B — Socket and infra (Tasks 3-7)

### Task 3: Add `TCP_NODELAY` to the game socket accept path and the proxy

**Files:**
- Modify: `src/platdef.h` (add `<netinet/tcp.h>` include)
- Modify: `src/comm.cpp` (`pnew_connection()`)
- Modify: `proxy/src/main.rs` (`GameAddr::connect()`, `handle_tcp()`, `handle_ws()`)

**Interfaces:** None — additive `setsockopt`/`set_nodelay` calls only.

`TCP_NODELAY` is absent from both the game's socket-accept path and the Rust proxy (confirmed by grepping `src/` and `proxy/src/` for `TCP_NODELAY`/`setsockopt`/`set_nodelay`/`nodelay` — zero hits anywhere). Without it, Nagle's algorithm can add real round-trip latency to small, interactive MUD packets, compounded by the proxy hop.

- [ ] **Step 1: Add the header include**

In `src/platdef.h`, add alongside the existing network includes:

```cpp
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/param.h>
```

- [ ] **Step 2: Set `TCP_NODELAY` on accepted game sockets**

In `src/comm.cpp`, `pnew_connection()`:

```cpp
SocketType pnew_connection(SocketType s)
{
    struct sockaddr_in isa;
    socklen_t i;
    SocketType t;

    i = sizeof(isa);
    if ((t = accept(s, (struct sockaddr*)(&isa), &i)) < 0) {
        perror("Accept");
        return (0); // probably incorrect..
    }
    sprintf(buf, "Socket %d connected.", t);
    mudlog(buf, NRM, LEVEL_IMPL, TRUE);

    return (t);
}
```

becomes:

```cpp
SocketType pnew_connection(SocketType s)
{
    struct sockaddr_in isa;
    socklen_t i;
    SocketType t;

    i = sizeof(isa);
    if ((t = accept(s, (struct sockaddr*)(&isa), &i)) < 0) {
        perror("Accept");
        return (0); // probably incorrect..
    }

    {
        int opt = 1;
        if (setsockopt(t, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt)) < 0) {
            perror("setsockopt TCP_NODELAY");
        }
    }

    sprintf(buf, "Socket %d connected.", t);
    mudlog(buf, NRM, LEVEL_IMPL, TRUE);

    return (t);
}
```

- [ ] **Step 3: Set `TCP_NODELAY` on both proxy legs**

In `proxy/src/main.rs`, `GameAddr::connect()`:

```rust
impl GameAddr {
    async fn connect(&self) -> Result<TcpStream, Report> {
        Ok(TcpStream::connect((self.hostname.as_str(), self.port)).await?)
    }
}
```

becomes:

```rust
impl GameAddr {
    async fn connect(&self) -> Result<TcpStream, Report> {
        let stream = TcpStream::connect((self.hostname.as_str(), self.port)).await?;
        stream.set_nodelay(true)?;
        Ok(stream)
    }
}
```

In `handle_tcp()`, set it on the client-side stream too:

```rust
async fn handle_tcp(game: GameAddr, mut stream: TcpStream, addr: SocketAddr) -> Result<(), Report> {
    let addr = match addr {
        SocketAddr::V4(addr) => addr,
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    // Connect to the game
    let mut game = game.connect().await?;
```

becomes:

```rust
async fn handle_tcp(game: GameAddr, mut stream: TcpStream, addr: SocketAddr) -> Result<(), Report> {
    let addr = match addr {
        SocketAddr::V4(addr) => addr,
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    stream.set_nodelay(true)?;

    // Connect to the game
    let mut game = game.connect().await?;
```

In `handle_ws()`, set it on the client-side stream before it's wrapped by `accept_hdr_async`:

```rust
async fn handle_ws(
    game: GameAddr,
    stream: TcpStream,
    addr: SocketAddr,
    cloudflare: bool,
) -> Result<(), Report> {
    let mut addr = match addr {
        SocketAddr::V4(addr) => *addr.ip(),
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    let mut ws = tokio_tungstenite::accept_hdr_async(stream, |req: &Request, res| {
```

becomes:

```rust
async fn handle_ws(
    game: GameAddr,
    stream: TcpStream,
    addr: SocketAddr,
    cloudflare: bool,
) -> Result<(), Report> {
    let mut addr = match addr {
        SocketAddr::V4(addr) => *addr.ip(),
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    stream.set_nodelay(true)?;

    let mut ws = tokio_tungstenite::accept_hdr_async(stream, |req: &Request, res| {
```

- [ ] **Step 4: Format and build the C++ side**

```bash
cd src && clang-format -i -style=WebKit comm.cpp && cd .. && scripts/rots-docker.sh compile
```

- [ ] **Step 5: Build and test the proxy**

```bash
cargo build -p proxy
cargo test -p proxy
```

- [ ] **Step 6: Smoke test**

Boot the server without the proxy (`cd src && make run`, plain `telnet localhost 1024`) and confirm login/movement/combat still work normally — `TCP_NODELAY` shouldn't be observable functionally, only as (likely imperceptible without packet capture) reduced latency on small writes. Separately, run the proxy (`cargo run -p proxy -- --help` for flags, then point it at the running server) and connect through it via both the raw TCP path and the WebSocket path; confirm both still connect, log in, and relay input/output correctly.

- [ ] **Step 7: Commit**

```bash
git add src/platdef.h src/comm.cpp proxy/src/main.rs
git commit -m "perf(net): enable TCP_NODELAY on game socket accept and both proxy legs

Nagle's algorithm was unconditionally enabled everywhere in the stack
(game accept path, proxy-to-client, proxy-to-game), adding avoidable
latency to small interactive packets."
```

---

### Task 4: Don't disconnect on a momentary `EAGAIN`/`EWOULDBLOCK` write

**Files:**
- Modify: `src/comm.cpp:1612-1638` (`write_to_descriptor()`)

**Interfaces:** None — `write_to_descriptor()`'s signature and success/failure return contract (`0`/`1` = ok, `-1` = fatal) are preserved; only which cases count as fatal changes.

Note: `comm.cpp` also has an unrelated sibling function, `write_to_descriptor_new()` (`comm.cpp:1577`), explicitly marked in a comment as replaced/unused ("old version replaced with above April 2001 - Fingolfin") — it has the same EAGAIN-as-fatal bug but is dead code (nothing calls it). Only `write_to_descriptor()` (the live one) needs this fix.

- [ ] **Step 1: Make the fix**

In `src/comm.cpp`:

```cpp
int write_to_descriptor(int desc, char* txt)
{
    int sofar, thisround, total;

    total = strlen(txt);
    sofar = 0;

    if (desc <= 0) {
        return 0;
    }

    try {
        do {
            thisround = write(desc, txt + sofar, total - sofar);
            if (thisround < 0) {
                perror("Write to socket");
                return (-1);
            }
            sofar += thisround;
        } while (sofar < total);
    } catch (...) {
        vmudlog(NRM, "Exception in write_to_descriptor");
        return -1;
    }

    return (0);
}
```

becomes:

```cpp
int write_to_descriptor(int desc, char* txt)
{
    int sofar, thisround, total;

    total = strlen(txt);
    sofar = 0;

    if (desc <= 0) {
        return 0;
    }

    try {
        do {
            thisround = write(desc, txt + sofar, total - sofar);
            if (thisround < 0) {
                if ((errno == EAGAIN || errno == EWOULDBLOCK) && sofar == 0) {
                    /* Kernel send buffer is momentarily full and nothing from this
                       call has gone out yet, so the caller's output buffer (still
                       fully intact -- process_output() only clears it after a
                       successful write) can simply be retried whole next pulse
                       instead of disconnecting the player. A mid-flight EAGAIN
                       after a partial write is not handled here (would risk
                       resending already-sent bytes) and still falls through to
                       the existing fatal-disconnect path below. */
                    return 0;
                }
                perror("Write to socket");
                return (-1);
            }
            sofar += thisround;
        } while (sofar < total);
    } catch (...) {
        vmudlog(NRM, "Exception in write_to_descriptor");
        return -1;
    }

    return (0);
}
```

This targets the documented "lag then disconnect" case: a client on a slow link accumulates a full kernel send buffer across several pulses, then the *next* `process_output()` flush's very first `write()` call hits `EWOULDBLOCK` before any bytes of that flush have gone out (`sofar == 0`) — previously fatal, now safely deferred to the next pulse.

- [ ] **Step 2: Format**

```bash
cd src && clang-format -i -style=WebKit comm.cpp
```

- [ ] **Step 3: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 4: Smoke test**

Hard to trigger `EWOULDBLOCK` deliberately without a throttled/simulated slow link. Minimum bar: boot the server, connect normally, confirm ordinary output (room descriptions, combat spam, a big `who`/`score` listing) still displays correctly with no duplication or truncation — this exercises the unchanged success path. If you have a way to throttle a test connection (e.g. `tc qdisc` rate-limiting on loopback, or a client-side artificial slow-consumer), trigger a large burst of output (e.g. an AoE spell room-full of combat text) against the throttled connection and confirm the connection survives (no disconnect) rather than the previous "lag then disconnect" behavior.

- [ ] **Step 5: Commit**

```bash
git add src/comm.cpp
git commit -m "fix(net): don't disconnect on a momentary EAGAIN/EWOULDBLOCK write

write_to_descriptor() treated any negative write() return as fatal,
including EAGAIN/EWOULDBLOCK on a non-blocking socket whose send buffer
was momentarily full -- disconnecting players during output bursts on
slow links instead of retrying next pulse."
```

---

### Task 5: Cap `process_input()`'s read-loop iterations per call

**Files:**
- Modify: `src/comm.cpp:1672-1732` (`process_input()`)

**Interfaces:** None — internal loop-bound only, function signature/behavior for well-behaved input is unchanged.

Note: `process_input()` now starts with a `finish_proxy_header_if_ready(t)` call (proxy-header handling was reworked to be non-blocking as part of the account-management commit) before reaching the read loop below — that addition is unrelated to this fix and doesn't need to change.

- [ ] **Step 1: Make the fix**

In `src/comm.cpp`, inside `process_input()`, the read loop currently reads:

```cpp
    sofar = flag = 0;
    begin = strlen(t->buf);

    /* Read in some stuff */
    do {
        char inbuf[2048];
        thisround = read(t->descriptor, inbuf, sizeof(inbuf));
```

and ends:

```cpp
    } while (!ISNEWL(*(t->buf + begin + sofar - 1)));
```

Add an iteration counter and cap:

```cpp
    sofar = flag = 0;
    begin = strlen(t->buf);

    /* Read in some stuff */
    int read_iterations = 0;
    const int MAX_READ_ITERATIONS_PER_CALL = 8; /* ~16KB/call at 2048 bytes/iteration --
                                                    caps how long one connection can
                                                    monopolize a single pulse when it
                                                    keeps sending data with no newline */
    do {
        char inbuf[2048];
        thisround = read(t->descriptor, inbuf, sizeof(inbuf));
```

and:

```cpp
    } while (!ISNEWL(*(t->buf + begin + sofar - 1)) &&
             ++read_iterations < MAX_READ_ITERATIONS_PER_CALL);
```

No other change is needed: if the loop exits early because the cap was hit (no newline seen yet), the existing code immediately below the loop already handles "no complete line in the buffer yet" correctly —

```cpp
    /* if no pnewline is contained in input, return without proc'ing */
    for (i = begin; !ISNEWL(*(t->buf + i)); i++)
        if (!*(t->buf + i))
            return (0);
```

— it just returns `0` (nothing to process yet), and the next pulse's call to `process_input()` picks up where `t->buf` left off (`begin = strlen(t->buf)` at the top) and keeps draining the socket. So the cap only limits how much of *one pulse* a single flooding/paste-without-newline connection can consume, without losing or corrupting any data.

- [ ] **Step 2: Format**

```bash
cd src && clang-format -i -style=WebKit comm.cpp
```

- [ ] **Step 3: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 4: Smoke test**

Boot the server. From one connection, paste or script a send of several multi-KB chunks with no newline terminator (e.g. `python3 -c "import socket,time; s=socket.create_connection(('localhost',1024)); s.sendall(b'A'*20000)"` with no trailing `\n`) while a second, normal connection is actively moving/chatting. Before the fix, the flooding connection's `process_input()` call could keep reading/scanning across many 2048-byte chunks in one call, delaying the second connection's input/output for that pulse; after the fix, confirm the second connection continues to feel responsive (prompt/output keeps flowing every ~250ms) while the flood is in progress. Then send a trailing newline on the flooding connection and confirm the (now very long, truncated per existing `MAX_STRING_LENGTH` clamping) line is still processed without a crash.

- [ ] **Step 5: Commit**

```bash
git add src/comm.cpp
git commit -m "fix(net): cap process_input()'s read-loop iterations per call

The do-while read loop had no iteration or byte cap, only exiting on a
newline or EWOULDBLOCK -- a connection that kept sending data with no
newline (flood, huge paste, or a buggy client) could keep one
process_input() call spinning through many read() cycles, stalling
every other player for that pulse (single-threaded select loop)."
```

---

### Task 6: Fix pre-login connection leak (`SO_KEEPALIVE` + idle-timeout sweep)

**Files:**
- Modify: `src/comm.cpp` (`pnew_connection()` for `SO_KEEPALIVE`; new `check_pre_login_idle()` function + call site in the pulse loop)

**Interfaces:**
- Produces: `void check_pre_login_idle();` — a new free function in `comm.cpp`, called once per minute from the main pulse loop. Not called from anywhere outside this file.

Note: the pulse loop's autosave cadence (`Crash_save_all()`) was reworked into an `AutosaveTimer`-based scheduler as part of the account-management commit — there is no longer a `pulse % (60*4)` "one minute" block to piggyback on for that reason. This task adds its own independent one-minute block instead, so it doesn't touch or depend on the autosave scheduler at all.

- [ ] **Step 1: Add `SO_KEEPALIVE` on accepted sockets**

In `src/comm.cpp`, `pnew_connection()` — same function touched by Task 3, so if Task 3 already landed, add this alongside the `TCP_NODELAY` block; if applying independently, add fresh:

```cpp
SocketType pnew_connection(SocketType s)
{
    struct sockaddr_in isa;
    socklen_t i;
    SocketType t;

    i = sizeof(isa);
    if ((t = accept(s, (struct sockaddr*)(&isa), &i)) < 0) {
        perror("Accept");
        return (0); // probably incorrect..
    }

    {
        int opt = 1;
        if (setsockopt(t, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt)) < 0) {
            perror("setsockopt KEEPALIVE");
        }
    }

    sprintf(buf, "Socket %d connected.", t);
    mudlog(buf, NRM, LEVEL_IMPL, TRUE);

    return (t);
}
```

- [ ] **Step 2: Add a forward declaration**

Near the other forward declarations at the top of `src/comm.cpp` (e.g. alongside `SocketType pnew_connection(SocketType s);` at line 214):

```cpp
void check_pre_login_idle(); /* below, in this file */
```

- [ ] **Step 3: Add the sweep function**

Add this new function in `src/comm.cpp`, near `msdp_update()`:

```cpp
void check_pre_login_idle()
{
    const int PRE_LOGIN_IDLE_TIMEOUT = 15 * 60; /* seconds; covers name/password/menu entry only --
                                                     logged-in players are handled by check_idling() */
    descriptor_data *point, *next_point;

    for (point = descriptor_list; point; point = next_point) {
        next_point = point->next;

        if (point->character) {
            continue; /* has a char_data -- check_idling() covers this one */
        }

        if (time(0) - point->last_input_time > PRE_LOGIN_IDLE_TIMEOUT) {
            close_socket(point);
        }
    }
}
```

- [ ] **Step 4: Call it once a minute from the pulse loop**

In `src/comm.cpp`, right after the `msdp_update();` call in the main pulse loop:

```cpp
        msdp_update();

        // Periodic point-in-time crash-save snapshot cadence, driven by the configurable seconds
```

becomes:

```cpp
        msdp_update();

        if (!(pulse % (60 * 4))) /* one minute */
        {
            check_pre_login_idle();
        }

        // Periodic point-in-time crash-save snapshot cadence, driven by the configurable seconds
```

- [ ] **Step 5: Format**

```bash
cd src && clang-format -i -style=WebKit comm.cpp
```

- [ ] **Step 6: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 7: Smoke test**

Boot the server. Open a raw `telnet localhost 1024` connection and stop at the "By what name do you wish to be known?" prompt (don't type anything). Confirm the connection is still open after a minute (under the 15-minute timeout). This test can't practically wait 15 real minutes end-to-end; instead, temporarily lower `PRE_LOGIN_IDLE_TIMEOUT` to e.g. `20` seconds in a local build, confirm the connection is closed by the server (not by the client) shortly after 20 seconds of inactivity at the name prompt, then revert the constant back to `15 * 60` before committing. Separately, confirm a connection that *does* proceed to log in and start playing is unaffected (its `character` pointer becomes non-null, so `check_pre_login_idle()` skips it — `check_idling()`'s existing 28-minute logic takes over).

- [ ] **Step 8: Commit**

```bash
git add src/comm.cpp
git commit -m "fix(net): reap idle pre-login connections, add SO_KEEPALIVE

Connections stuck at the name/password/menu prompts (no char_data yet)
were invisible to check_idling() (which only walks character_list) and
had no SO_KEEPALIVE, so a client that went silently dead at the network
level (dropped wifi, sleep, mobile handoff) held its fd forever."
```

---

### Task 7: Add crash-loop backoff to `autorun`

**Files:**
- Modify: `autorun` (untracked shell script at repo root)

**Interfaces:** None — shell script, no code interface.

- [ ] **Step 1: Make the fix**

In `autorun`:

```sh
  # Run the game.
  bin/ageland $FLAGS $PORT #>> syslog 2>&1

  # Make our daily player/object/exploit file backups.
```

becomes:

```sh
  # Run the game.
  bin/ageland $FLAGS $PORT #>> syslog 2>&1

  # Brief backoff before restarting, so a fast crash-loop doesn't
  # immediately re-trigger the full boot-time world-load memory spike
  # back-to-back with zero cooldown.
  sleep 5

  # Make our daily player/object/exploit file backups.
```

- [ ] **Step 2: Smoke test**

Since this is a shell script rather than compiled code, "build" doesn't apply — just verify the syntax and behavior directly:

```bash
sh -n autorun   # syntax check only, doesn't run it
```

Then, in a scratch copy (don't run this against the real `lib/`/`bin/` unless you intend to actually boot), confirm the loop structure: temporarily replace `bin/ageland $FLAGS $PORT` with a stub like `false` in a copy of the script, run it in the foreground, confirm each iteration is now visibly spaced ~5 seconds apart (`date` printed via the existing debug lines, or add a temporary `echo`), then discard the scratch copy.

- [ ] **Step 3: Commit**

```bash
git add autorun
git commit -m "fix(ops): add 5s backoff before autorun restarts the game process

The restart loop had zero delay between a crash and the next
bin/ageland invocation, so a crash-loop could repeatedly re-trigger the
full boot-time world-load memory spike with no cooldown."
```

---

## Group C — Combat / reentrancy (Tasks 8-10)

### Task 8: Fix `skill_timer` erase-then-skip bug

**Files:**
- Modify: `src/skill_timer.cpp:41-51` (`skill_timer::update_skill_timer()`)

**Interfaces:** None — internal loop fix, no signature change.

- [ ] **Step 1: Make the fix**

In `src/skill_timer.cpp`:

```cpp
void skill_timer::update_skill_timer()
{
    for (int i = 0; i < m_skill_timer.size(); ++i) {
        auto& data = m_skill_timer[i];
        if (data.counter > 0) {
            data.counter -= 1;
        } else {
            m_skill_timer.erase(m_skill_timer.begin() + i);
        }
    }
}
```

becomes:

```cpp
void skill_timer::update_skill_timer()
{
    for (int i = 0; i < m_skill_timer.size();) {
        auto& data = m_skill_timer[i];
        if (data.counter > 0) {
            data.counter -= 1;
            ++i;
        } else {
            m_skill_timer.erase(m_skill_timer.begin() + i);
        }
    }
}
```

Moving `++i` into the `counter > 0` branch means that after an erase, `i` stays put and re-examines whatever element the vector just shifted into that slot on the *next* loop check, instead of skipping over it via the `for` loop's own unconditional increment.

- [ ] **Step 2: Format**

```bash
cd src && clang-format -i -style=WebKit skill_timer.cpp
```

- [ ] **Step 3: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 4: Smoke test**

Boot the server. Use two skills on the same character back-to-back such that both have active cooldowns tracked by `skill_timer` at the same time, with one entry expiring before the other (e.g. a short-cooldown skill and a longer-cooldown skill used in the same tick). Confirm both cooldowns count down and clear at their expected tick, with neither skill's cooldown appearing to take one tick longer than it should (the bug's symptom: an entry immediately following an erased one silently skips one decrement).

- [ ] **Step 5: Commit**

```bash
git add src/skill_timer.cpp
git commit -m "fix(combat): don't skip the element shifted into an erased slot

update_skill_timer() erased an expired entry then unconditionally
advanced i via the for loop's own increment, skipping whatever entry
the vector shifted into that slot -- that entry missed a decrement
for one tick."
```

---

### Task 9: Fix `do_flee`/`on_windblast_hit` double-firing `ON_BEFORE_ENTER`

**Files:**
- Modify: `src/act_move.cpp` (`check_simple_move()` — add trigger-skip suppression)
- Modify: `src/act_offe.cpp` (`do_flee()` — set the suppression before calling `do_move()`)
- Modify: `src/ranger.cpp` (`on_windblast_hit()` — same fix, second identical call-site pattern)

**Interfaces:**
- Produces: `extern char_data *g_skip_next_before_enter_for;` — a file-scope global in `act_move.cpp`, consumed once by `check_simple_move()` and set by any caller that already fired the trigger itself via its own prior `check_simple_move()` call and is about to invoke `do_move()` for the identical transition.

Both `do_flee()` and `on_windblast_hit()` call `check_simple_move()` directly to test whether a forced move succeeds (which fires `ON_BEFORE_ENTER` as a side effect), and then — on success — call `do_move()` for the same direction, which **internally calls `check_simple_move()` again**, firing `ON_BEFORE_ENTER` a second time for the identical room transition. If the room's trigger has any side effect (message, resource decrement, damage, spawn), it happens twice per successful flee/windblast instead of once.

`do_move()` is an `ACMD`-macro-generated function; changing its signature would ripple across every other command handler, so the fix uses a narrow, self-resetting suppression flag instead of a new parameter. Because `do_flee()`/`on_windblast_hit()` only reach their `do_move()` call when their *own* `check_simple_move()` call already returned "allowed" (`die == 0`), the second firing inside `do_move()` is always redundant when this flag is set — it can be skipped without changing what the *first* firing decided.

- [ ] **Step 1: Add the suppression flag and consume it in `check_simple_move()`**

In `src/act_move.cpp`, near the top of the file (with the other file-scope statics/globals):

```cpp
/* Set by a caller that already fired ON_BEFORE_ENTER via its own
   check_simple_move() call and is about to invoke do_move() for the exact
   same character+transition (e.g. do_flee(), on_windblast_hit()) -- lets
   check_simple_move() skip re-firing the trigger for that one redundant
   internal call instead of running it (and any side effects) twice. */
char_data *g_skip_next_before_enter_for = nullptr;
```

In `check_simple_move()`:

```cpp
    if (call_trigger(ON_BEFORE_ENTER, room_to, ch, 0) == FALSE)
        return 1; //  Trigger doesn't allow them to enter the new room
```

becomes:

```cpp
    if (g_skip_next_before_enter_for == ch) {
        g_skip_next_before_enter_for = nullptr;
    } else if (call_trigger(ON_BEFORE_ENTER, room_to, ch, 0) == FALSE) {
        return 1; //  Trigger doesn't allow them to enter the new room
    }
```

- [ ] **Step 2: Set the flag in `do_flee()` before its `do_move()` call**

In `src/act_offe.cpp`, near the top of the file (below the existing forward declaration of `check_simple_move`, `int check_simple_move(struct char_data* ch, int cmd, int* move_cost, int mode);`):

```cpp
extern char_data *g_skip_next_before_enter_for;
```

In `do_flee()`:

```cpp
                send_to_char("You flee head over heels.\n\r", ch);
                act("$n flees head over heels!", FALSE, ch, 0, 0, TO_ROOM);
                do_move(ch, dirs[attempt], 0, attempt + 1, SCMD_FLEE);
                return;
```

becomes:

```cpp
                send_to_char("You flee head over heels.\n\r", ch);
                act("$n flees head over heels!", FALSE, ch, 0, 0, TO_ROOM);
                g_skip_next_before_enter_for = ch;
                do_move(ch, dirs[attempt], 0, attempt + 1, SCMD_FLEE);
                return;
```

- [ ] **Step 3: Set the flag in `on_windblast_hit()` before its `do_move()` call**

In `src/ranger.cpp`, near the top of the file (below the existing forward declaration of `check_simple_move`, matching Step 2's pattern):

```cpp
extern char_data *g_skip_next_before_enter_for;
```

In `on_windblast_hit()`:

```cpp
                send_to_char("A wave of thunderous force sweeps you out!", ch);
                act("$n gets sweep out from the wave of thunderous force!", FALSE, ch, 0, 0,
                    TO_ROOM);

                do_move(ch, dirs[attempt], 0, attempt + 1, SCMD_FLEE);
                return;
```

becomes:

```cpp
                send_to_char("A wave of thunderous force sweeps you out!", ch);
                act("$n gets sweep out from the wave of thunderous force!", FALSE, ch, 0, 0,
                    TO_ROOM);

                g_skip_next_before_enter_for = ch;
                do_move(ch, dirs[attempt], 0, attempt + 1, SCMD_FLEE);
                return;
```

- [ ] **Step 4: Format**

```bash
cd src && clang-format -i -style=WebKit act_move.cpp act_offe.cpp ranger.cpp
```

- [ ] **Step 5: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 6: Smoke test**

Find or temporarily rig a room with an `ON_BEFORE_ENTER` mudlle/script trigger that has a visible, countable side effect (a message, or — best — a limited-use counter/resource that decrements on entry; `docs/data-formats/mudlle-and-scripts.md` documents the trigger system if you need to author a quick test trigger). Stand in an adjacent room, engage a mob in combat, and flee into the triggered room. Before the fix, the trigger's side effect fires twice per successful flee (visible as a doubled message, or a counter dropping by 2 instead of 1); after the fix, it fires exactly once. Separately confirm a **normal** (non-flee, non-windblast) move into the same room still fires the trigger exactly once (unaffected by this change, since `g_skip_next_before_enter_for` is only ever set by `do_flee`/`on_windblast_hit`, and consumed-and-reset on the very next `check_simple_move()` call regardless of whether it matched). If a ranger with the windblast spell is available for testing, repeat the same room-trigger test by having the ranger windblast a target into the triggered room.

- [ ] **Step 7: Commit**

```bash
git add src/act_move.cpp src/act_offe.cpp src/ranger.cpp
git commit -m "fix(movement): don't double-fire ON_BEFORE_ENTER on flee/windblast

do_flee() and on_windblast_hit() each call check_simple_move() directly
to test a forced move, then call do_move() for the same transition on
success -- do_move() internally calls check_simple_move() again, firing
ON_BEFORE_ENTER a second time. Any trigger side effect (message,
resource decrement, damage) ran twice per successful flee/windblast.

do_move() is ACMD-macro-generated so its signature can't easily grow a
skip-trigger parameter without touching every command handler; instead
a narrow, self-resetting flag lets the caller's own already-fired
trigger result stand instead of re-firing it."
```

---

### Task 10: Fix double-delay reentrancy in `WAIT_STATE_BRIEF`/`WAIT_STATE_FULL`

**Files:**
- Modify: `src/utils.h:482-562` (`WAIT_STATE_BRIEF`, `WAIT_STATE_FULL` macros)

**Interfaces:** None — macro-internal fix, no call-site changes anywhere (`spell_pa.cpp`, `act_offe.cpp`, `ranger.cpp`, `olog_hai.cpp`, etc. all keep calling these macros exactly as before).

**Root cause:** when a new delay's priority is `>=` the pending delay's priority, both macros call `complete_delay(ch)` — which synchronously runs the *old* pending command via `command_interpreter(ch, "", &ch->delay)` — then unconditionally call `abort_delay(ch)` and overwrite `ch->delay` with the *new* action's data. If the old command's synchronous completion itself calls `WAIT_STATE_BRIEF`/`FULL` again (e.g. queuing a follow-up recovery delay), that reentrant call succeeds and links a fresh delay onto `ch->delay` — which the outer macro's subsequent `abort_delay(ch)` + overwrite then silently destroys with no message, since `complete_delay()`'s first line unconditionally zeroes `ch->delay.wait_value`, masking the fact that something new was written there moments later.

This is the **minimal correctness patch** (not a full delay-queue redesign): detect the reentrant case and back off the same way the existing "else" branch already does for a rejected lower-priority delay, instead of silently destroying the reentrant delay.

- [ ] **Step 1: Fix `WAIT_STATE_BRIEF`**

In `src/utils.h`:

```cpp
#define WAIT_STATE_BRIEF(ch, cycle, commd, subcommd, prir, new_flag)                    \
    do {                                                                                \
        char_data* tmpch;                                                               \
        if (ch->delay.wait_value != 0) {                                                \
            if (prir >= ch->delay.priority) {                                           \
                ch->delay.subcmd = -1;                                                  \
                complete_delay(ch);                                                     \
                abort_delay(ch);                                                        \
            } else {                                                                    \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch); \
                log("double delay?\n");                                                 \
                break;                                                                  \
            }                                                                           \
        }                                                                               \
```

becomes:

```cpp
#define WAIT_STATE_BRIEF(ch, cycle, commd, subcommd, prir, new_flag)                    \
    do {                                                                                \
        char_data* tmpch;                                                               \
        if (ch->delay.wait_value != 0) {                                                \
            if (prir >= ch->delay.priority) {                                           \
                ch->delay.subcmd = -1;                                                  \
                complete_delay(ch);                                                     \
                if (ch->delay.wait_value != 0) {                                        \
                    /* complete_delay() reentrantly queued a new delay (e.g. a        \
                       follow-up recovery action) while running the just-finished     \
                       command -- don't silently clobber it with this action. */      \
                    send_to_char("Possible bug - double delay. Please notify Imps.\n", ch); \
                    log("double delay (reentrant queue during complete_delay)?\n");    \
                    break;                                                             \
                }                                                                      \
                abort_delay(ch);                                                        \
            } else {                                                                    \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch); \
                log("double delay?\n");                                                 \
                break;                                                                  \
            }                                                                           \
        }                                                                               \
```

(the remainder of the macro, from `ch->delay.wait_value = cycle;` onward, is unchanged)

- [ ] **Step 2: Fix `WAIT_STATE_FULL`**

Same pattern in `src/utils.h`:

```cpp
#define WAIT_STATE_FULL(ch, cycle, commd, subcommd, prir, flag, dgt, argument, new_flag, data_type) \
    do {                                                                                            \
        struct char_data* tmpch;                                                                    \
        if (ch->delay.wait_value != 0) {                                                            \
            if (prir >= ch->delay.priority) {                                                       \
                ch->delay.subcmd = -1;                                                              \
                complete_delay(ch);                                                                 \
                abort_delay(ch);                                                                    \
            } else {                                                                                \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch);             \
                printf("double delay?\n");                                                          \
                break;                                                                              \
            }                                                                                       \
        }                                                                                           \
```

becomes:

```cpp
#define WAIT_STATE_FULL(ch, cycle, commd, subcommd, prir, flag, dgt, argument, new_flag, data_type) \
    do {                                                                                            \
        struct char_data* tmpch;                                                                    \
        if (ch->delay.wait_value != 0) {                                                            \
            if (prir >= ch->delay.priority) {                                                       \
                ch->delay.subcmd = -1;                                                              \
                complete_delay(ch);                                                                 \
                if (ch->delay.wait_value != 0) {                                                    \
                    send_to_char("Possible bug - double delay. Please notify Imps.\n", ch);         \
                    printf("double delay (reentrant queue during complete_delay)?\n");               \
                    break;                                                                          \
                }                                                                                   \
                abort_delay(ch);                                                                    \
            } else {                                                                                \
                send_to_char("Possible bug - double delay. Please notify Imps.\n", ch);             \
                printf("double delay?\n");                                                          \
                break;                                                                              \
            }                                                                                       \
        }                                                                                           \
```

(the remainder of the macro is unchanged)

- [ ] **Step 3: Format**

```bash
cd src && clang-format -i -style=WebKit utils.h
```

- [ ] **Step 4: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 5: Smoke test**

Reproduce the documented trigger: have a mob or player begin casting a spell (priority 30, `spell_pa.cpp`), then bash them (priority 80, `act_offe.cpp`) while the cast is still pending, on a spell whose completion queues its own follow-up delay if one exists in this codebase (check `docs/systems/combat-loop.md` / `spell_pa.cpp` for a spell with a post-cast recovery delay to use as the test case — if none queues a follow-up delay, this specific reentrant path can't be exercised end-to-end in-game, in which case fall back to confirming the ordinary case: bash-interrupts-cast still correctly force-completes the cast and applies the bash stun with no crash, log spam, or the new "double delay (reentrant queue...)" message appearing when no reentrant queuing actually happened). If a genuine reentrant case is reproducible, confirm the interrupting bash's stun is now the one that's dropped (visible via the existing double-delay message) rather than the recovery delay silently vanishing with zero indication.

- [ ] **Step 6: Commit**

```bash
git add src/utils.h
git commit -m "fix(combat): don't silently clobber a reentrantly-queued delay

WAIT_STATE_BRIEF/FULL's interrupt path ran complete_delay(ch) (which
can reentrantly queue a brand-new delay via a follow-up action) then
unconditionally called abort_delay(ch) and overwrote ch->delay with the
interrupting action's data, silently destroying whatever the reentrant
completion had just queued. Detect that case and back off the same way
the existing lower-priority rejection already does, rather than
clobbering it with no indication anything happened.

Minimal correctness patch -- the underlying single-slot ch->delay
design is unchanged; a real fix would replace it with a proper delay
queue, which is out of scope here."
```

---

## Group D — NPC scripting (Task 11)

### Task 11: Fix `get_next_command()` double-advance in the Scripts interpreter

**Files:**
- Modify: `src/script.cpp:760-773` (`get_next_command()`)

**Interfaces:** None — same signature (`script_data* get_next_command(script_data* curr)`), same external behavior contract (returns the command immediately following the matching `END`/`END_ELSE_BEGIN`), only the internal traversal is fixed. All existing call sites (`script.cpp:1047-1049`, every `SCRIPT_IF_*` false-branch, etc.) are unaffected.

- [ ] **Step 1: Make the fix**

In `src/script.cpp`:

```cpp
script_data* get_next_command(script_data* curr)
{

    curr = curr->next;
    for (; (curr) && ((curr->command_type != SCRIPT_END) && (curr->command_type != SCRIPT_END_ELSE_BEGIN));
         curr = curr->next)
        if (curr->command_type == SCRIPT_BEGIN)
            curr = get_next_command(curr);

    if (curr)
        return curr->next;
    else
        return 0;
}
```

becomes:

```cpp
script_data* get_next_command(script_data* curr)
{

    curr = curr->next;
    while (curr && curr->command_type != SCRIPT_END && curr->command_type != SCRIPT_END_ELSE_BEGIN) {
        if (curr->command_type == SCRIPT_BEGIN) {
            curr = get_next_command(curr);
        } else {
            curr = curr->next;
        }
    }

    if (curr)
        return curr->next;
    else
        return 0;
}
```

The original `for` loop's own `curr = curr->next` increment ran unconditionally every iteration, including right after the `if (curr->command_type == SCRIPT_BEGIN) curr = get_next_command(curr);` branch had *already* advanced `curr` past the nested block's matching `END` — a double-advance that could skip the real terminating `END`/`END_ELSE_BEGIN` of the current level and scan into unrelated following script content. Restructuring as a `while` loop with the advance only in the non-`BEGIN` branch means a nested block's recursive result is used as-is, with no extra advance layered on top.

- [ ] **Step 2: Format**

```bash
cd src && clang-format -i -style=WebKit script.cpp
```

- [ ] **Step 3: Build**

```bash
scripts/rots-docker.sh compile
```

- [ ] **Step 4: Smoke test**

Per the original diagnosis, this needs a deeply-nested script to reproduce: author (or find, per `docs/data-formats/mudlle-and-scripts.md` for the Scripts/`shapescript` OLC format) a test script with a nested `if/begin...end` as the last statement before the *enclosing* block's own `END` — the specific shape that reproduces the miss. Test both:
1. The false-`if` skip path: trigger the script so the outer condition is false and the scan must skip over the nested `begin...end`, then confirm execution correctly resumes at the statement immediately after the *enclosing* block's `END` (not one further, and not stuck inside content that should've been skipped).
2. The `SCRIPT_END_ELSE_BEGIN` path: same nested shape but reached via an `if/begin...end_else_begin...end` structure, confirming the `else` branch's own scan-past-skipped-content lands correctly too.

Watch `log/syslog` during both runs for any script-related errors, and confirm no previously-working simpler (non-nested) scripts regress — spot check a couple of existing non-nested `if/begin/end` scripts in `lib/world/scr*` still behave identically.

- [ ] **Step 5: Commit**

```bash
git add src/script.cpp
git commit -m "fix(scripts): stop get_next_command() from double-advancing past nested blocks

The for loop's own curr = curr->next increment fired unconditionally
every iteration, including right after the recursive get_next_command()
call for a nested SCRIPT_BEGIN had already advanced curr past that
block's own matching END -- skipping one extra command and sometimes
running past the current level's real terminating END/END_ELSE_BEGIN,
read by players as script sections being silently ignored."
```

---

## Self-Review Notes

- **Spec coverage:** 11 of the original 13 agreed items have an active task here (Tasks 1-2 = MSDP, 3-7 = socket/infra, 8-10 = combat/reentrancy, 11 = scripting). The other 2 (MSDP per-pulse freeze, `save_player()`'s blocking `system()` calls) were confirmed already fixed by an intervening upstream commit during re-verification against `release-frodo` and are documented under "Already fixed upstream" so no task silently reinvents them. The three explicitly-excluded design-intent items (dying-recovery, bReport default, room-data-not-bundled) and the deliberately-deferred full delay-queue redesign are called out in Global Constraints / Out of scope.
- **Second call site found during planning:** Task 9 originally only covered `do_flee()` per the original audit, but grepping for `check_simple_move` during planning surfaced an identical pattern in `ranger.cpp`'s `on_windblast_hit()` (a forced-flee spell effect) — folded into the same task since it's the same bug, same fix shape, same suppression flag.
- **Re-verified against actual `release-frodo` code, not just the original audit:** every remaining task's before/after code blocks were re-read from the actual current file content in this worktree (not carried over from the original audit's line numbers), since the intervening account-management commit changed `protocol.cpp`, `ranger.cpp`, `utils.h`, `script.cpp`, and `comm.cpp` significantly. Two hook points needed adjustment as a result: Task 2's `MSDPSendPair`/`MSDPSendList` insertion point now accounts for a new `if (pProtocol == NULL) return;` early-out that didn't exist before; Task 6's pre-login idle sweep no longer piggybacks on the old `pulse % (60*4)` autosave block (replaced by an `AutosaveTimer` scheduler) and instead adds its own independent one-minute block.
- **Type/name consistency check:** `g_skip_next_before_enter_for` (Task 9) is declared once in `act_move.cpp` and referenced via matching `extern` declarations in both `act_offe.cpp` and `ranger.cpp` — same name, same type, in all three places. `check_pre_login_idle()` (Task 6) is declared and defined with matching signatures. No placeholder steps, "TBD"s, or "similar to Task N" shorthand remain — every step has concrete before/after code.
