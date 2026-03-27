# Repository Guidelines

## Project Structure & Module Organization
- src/: C/C++ game server sources, headers, and build scripts (`Makefile`, `CMakeLists.txt`).
- bin/: Built server binary (`ageland`) and backup (`ageland~`).
- lib/: Runtime data (players, world, text, etc.); many subpaths are git-ignored.
- build/: CMake build artifacts and test scaffolding; not checked in.
- proxy/: Rust workspace member (`cargo` crate) for proxy/CLI utilities.
- release-notes/, game design docs/, code documentation/: Docs and release history.

## Planning Workflow
- Before starting feature work, always read `FEATURES.md` and `WIP.md` if they exist.
- Treat `FEATURES.md` as the current feature scope, breakdown, and implementation checklist.
- Treat `WIP.md` as the current execution log and update it during feature work with the current task, recent progress, and next step.
- If feature scope changes during discussion, update `FEATURES.md` before implementing.
- If active work changes during implementation, update `WIP.md` before continuing.

## Build, Test, and Development Commands
- Configure: `make configure` — generates the CMake build tree in `build/`.
- Bootstrap data: `make setup` — creates required runtime directories/files under `lib/`, `log/`, and `bin/`.
- Build: `make build` — compiles C/C++ sources to `bin/ageland`.
- Test: `make test` — builds and runs the GoogleTest-based C++ unit tests.
- Run: `make run` — builds and starts the server in the background on port `3791`.
- Clean: `make clean` — removes build outputs from the configured tree.
- Raw CMake fallback: `cmake -S src -B build -DCMAKE_CXX_COMPILER=g++ && cmake --build build --target ageland`
- Rust proxy: `cargo build -p proxy` | `cargo test -p proxy` | `cargo run -p proxy -- --help`.

## Coding Style & Naming Conventions
- Formatter: run `cmake --build build --target format` (or `cd src && make format`) using WebKit style. Prefer the repo-provided target over local defaults; CI expects formatted diffs.
- .clang-format: present for IDEs; indentation 4 spaces; column limit ~100.
- Filenames: lower_snake_case for `.cpp`/`.h` (e.g., `act_comm.cpp`, `protocol.h`).
- C/C++: functions/variables lower_snake_case; constants UPPER_SNAKE_CASE; types TitleCase where applicable.
- Rust (proxy): follow `rustfmt` defaults; module/file lowercase with underscores.

## Testing Guidelines
- C/C++: add or update unit tests in `src/tests/` when working in covered areas, run them via `make test`, and also perform smoke tests by building and running locally. Verify server boots, accepts connections, and changed features behave as expected.
- C/C++ test style: prefer behavior-oriented GoogleTest names that read clearly in CTest output, such as `ReturnsConfiguredWeaponType` instead of terse names like `WeaponType`. Use readable assertions like `EXPECT_TRUE` when appropriate, and add concise failure messages that explain the expectation and include important domain values when a failure would otherwise be cryptic.
- Do not modify production code solely to accommodate tests. Prefer test fixtures, helper builders, dependency-free coverage, and existing public behavior. Only introduce a production seam for testability when it is also a legitimate design improvement, and call that out explicitly.
- New code: add unit tests for newly written code when the surrounding module supports them, and document any gaps when tests are not practical.
- Rust: write unit/integration tests in `proxy/`; run with `cargo test -p proxy` and keep coverage reasonable.

## Review Workflow
- Before finalizing any non-trivial change set, maintain two review subagents in parallel: `Magus` as the quality engineer reviewer and `Vincent` as the security engineer reviewer.
- Reuse the same reviewer pair across successive changes by sending them updated diff context, instead of spawning a fresh pair for every round. Only replace a reviewer when it has been closed, becomes unavailable, or its context is no longer reliable. If either reviewer must be replaced, assign the replacement the same role name so the workflow stays consistent.
- The quality engineer should focus on regressions, correctness, maintainability, test quality, developer ergonomics, and documentation gaps.
- The security engineer should focus on trust boundaries, unsafe execution paths, secrets or data exposure, command safety, and build/test workflow risks.
- Give both reviewers the relevant changed files or diff context, ask for findings first with severity, file references, and concrete recommendations, and do not treat the work as complete until their feedback has been reviewed.
- Address their findings in code or docs when appropriate, or explicitly document why a recommendation is being deferred.

## Commit & Pull Request Guidelines
- Commits: concise, imperative subject (<=72 chars). Reference issues/PRs, e.g., "ranger: fix stun timing (#255)".
- Scope small, logically grouped changes; include short body for context when needed.
- PRs: describe changes, link issues, list validation steps (build/run commands), and note data/world impacts. Include logs/screens where useful.
- Do not commit generated binaries or runtime data (`bin/`, `build/`, many `lib/` paths are git-ignored).

## Security & Configuration
- World files live in a separate repo; keep `lib/world/` and player data out of commits.
- Never check in PII or live server logs (`log/`). Use local testing accounts and sanitized samples.
