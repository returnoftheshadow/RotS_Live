# Features to Add

## JavaScript Game Scripting Engine

Add a JavaScript scripting engine that runs alongside the current ASIMA-style script system rather than replacing it. The existing trigger entry points, script-number references on mobs/objects, and current world-building workflow should continue to work while JavaScript-backed scripts are introduced incrementally.

Goals:
- Keep the current scripting engine and trigger behavior operational for existing world content.
- Add a JavaScript engine behind the same gameplay trigger concepts: enter, before-enter, die, receive, examine object, hear say/yell, damage, eat, drink, wear, pull, and future trigger types.
- Let builders opt a script into JavaScript explicitly instead of changing the meaning of existing `.scr` files.
- Provide a small, documented game API to scripts instead of exposing raw C/C++ pointers or unrestricted server internals.
- Provide complete builder-facing documentation for the JavaScript API so script authors understand what each class, handle, method, trigger, return value, side effect, and safety limit does before they publish scripts.
- Make script execution deterministic enough for live MUD use: bounded runtime, bounded memory, no filesystem/network/process access from world scripts, and safe failure behavior.
- Preserve current trigger return semantics, especially blocking triggers such as `ON_BEFORE_ENTER`, `ON_DIE`, `ON_DAMAGE`, `ON_WEAR`, and `ON_PULL`.
- Support unit-testable script execution without needing a live socket or full running game.
- Treat room JavaScript support as an explicit design choice: current inspected storage has script vnums on characters/mobs and objects, while `trigger_room_enter()` is effectively a stub. V1 should either add room script storage/loading deliberately or defer room-owned JavaScript triggers while preserving the current room fan-out behavior.

Current scripting surface to integrate with:
- `src/script.h` defines the trigger constants and current script command/parameter model.
- `src/script.cpp` owns `call_trigger(...)`, trigger dispatch, script execution, script-local context state, and continuation support for waiting scripts.
- `src/protos.h` defines `script_head`, `script_data`, `info_script`, and shaping/editor structures.
- `src/db.cpp` loads the current script table and attaches script vnums to mobs/objects.
- `src/shapescript.cpp` implements in-game script shaping and writes `lib/world/scr/*.scr`.
- Existing trigger callers live in movement, communication, combat, object, and look flows, so the new engine should reuse those call sites through a shared dispatcher instead of duplicating trigger plumbing.
- Current room event handling fans out into room occupants and room objects, but room-owned script storage is not currently visible in the inspected structs/loaders.

Engine selection and dependency plan:
- Evaluate an embeddable C/C++ JavaScript runtime during the first implementation slice.
- Prefer a small interpreter-style engine over V8/Node-style embedding so the game server build remains simple.
- Initial candidates:
  - QuickJS/QuickJS-ng: modern JavaScript support, small C embedding surface, runtime/context resource controls.
  - Duktape: compact, portable, C-friendly embedding, older JavaScript feature level.
- Choose and document vendoring/build strategy before production integration:
  - source vendored under a third-party directory, or
  - system package discovered by CMake, with clear build failure messaging.
- Pin the selected engine by exact version or commit, document provenance and license, verify vendored-source checksums where practical, track security advisories, and fail closed if the build links an unexpected runtime/configuration.
- Disable optional shell, module, filesystem, networking, process, and native-extension features in the selected engine configuration.
- Update both CMake and the raw `src/Makefile` path so local and fallback builds stay aligned.

Selected v1 runtime direction:
- Use upstream QuickJS `2026-06-04` as the first embedded runtime target.
- Rationale: the upstream engine is small, embeddable C code, MIT-licensed, and current enough for TypeScript-authored JavaScript output without bringing in a Node/V8 embedding stack.
- Vendored exact release source under `third_party/quickjs`, with provenance, release date, license, source checksum, local configuration flags, and disabled optional features recorded in `third_party/quickjs/README.rots.md`.
- Runtime engine files are wired into both CMake and raw Makefile paths. Shell, compiler, REPL, examples, tests, docs, and QuickJS libc helper files are not built into the server.
- Runtime execution remains disconnected from game trigger dispatch until script registry/loading, package validation, manifest enforcement, and game-handle APIs are implemented.

Proposed script representation:
- Add an engine/language discriminator to script metadata, for example `legacy` vs `javascript`.
- Keep legacy `.scr` loading unchanged.
- Add a JavaScript script file convention under the same world script area or a sibling directory, for example `lib/world/js/<vnum>.js` or `lib/world/scr/<vnum>.js`.
- Add a small metadata file or header syntax only if needed to map script vnums, names, descriptions, host types, and trigger functions.
- Avoid mixing legacy command lists and JavaScript source in the same in-memory `script_data` chain unless a compatibility wrapper makes that safe and clear.
- Decide whether JavaScript scripts occupy the global `script_table`, live in a parallel registry keyed by vnum, or require exclusive vnum ownership across engines.
- Define duplicate-vnum behavior, wrong-host behavior, deleted-file reload behavior, cache lifetime, and free/reload ownership before wiring runtime dispatch.

JavaScript execution model:
- Initial QuickJS runtime wrapper implementation:
  - `src/js_runtime.{h,cpp}` owns JavaScript execution configuration behind a C++ wrapper and creates a fresh QuickJS runtime/context for each evaluation so globals, prototypes, pending jobs, and builder-defined state do not persist across invocations.
  - The wrapper sets memory, stack, blocking, debug-strip, module-loader, and interrupt budgets before each evaluation.
  - The wrapper evaluates strict global scripts only and does not enable module loader, `std`, `os`, filesystem, process, worker, shell, or QuickJS libc helper surfaces.
  - Direct global `eval` and `Function` are removed, and Promise/async results fail closed until async trigger semantics and runtime compilation policy are explicitly designed.
  - Dynamic import is rejected before evaluation as a conservative fail-closed policy until a parser/validator layer exists.
  - Results are normalized so `undefined` and truthy values allow, `false` blocks, and syntax/runtime/interrupt/memory failures return structured error statuses with sanitized single-line diagnostics.
  - Focused runtime tests cover success, allow/block normalization, syntax/runtime errors, diagnostic bounding, infinite-loop interruption, per-evaluation budget reset, memory-limit failure, missing host/OS globals, dynamic/static import rejection, no mutable state persistence across evaluations, Promise/async rejection, post-failure health, and public status strings.
  - Remaining before trigger dispatch: replace source-substring dynamic-import rejection with a parser/validator layer, harden all runtime-code-generation constructor paths, add stack/regexp/proxy/host-callback adversarial budget tests, add a vendored QuickJS hash verification target, and define whether live execution needs an external watchdog beyond the QuickJS interrupt guard.
- Initial server-owned manifest implementation:
  - `src/js_scripting_manifest.{h,cpp}` records every legacy `.scr` trigger and ASIMA/Mudlle call flag currently identified for JavaScript parity or explicit unsupported/deferred behavior.
  - The manifest records trigger kind, legacy numeric value, JavaScript handler name, support status, builder publish status, host eligibility, room-owned publishability, Mudlle call-mask behavior, blocking/handled semantics, exception policy, dispatch ordering, context fields, and notes.
  - The manifest also records an initial deny-by-default host API permission table for dangerous legacy mutation commands, including combat, load/extract, teleport, inventory/equipment mutation, exit mutation, raw kill, XP gain, and wait/continuation commands.
  - All trigger/call-flag entries are currently `deferred`, `reserved`, or `unsupported`; no builder JavaScript trigger is publishable until the runtime, validator, and sandbox are implemented.
  - Focused unit tests in `src/tests/js_scripting_manifest_tests.cpp` guard manifest completeness, duplicate ids, duplicate handler names, reserved/unsupported statuses, host eligibility, room-owned deferral, say/yell compatibility policy, blocking exception policy, Mudlle handled semantics, and the deny-by-default mutation API table.
- Initial server-owned package validator implementation:
  - `src/js_script_package.{h,cpp}` defines the in-memory package shape for compiled JavaScript artifacts before any filesystem loader, live trigger dispatch, or game-handle API exists.
  - Package validation checks server manifest compatibility, package format version, trigger catalog revision, manifest checksum, runtime identity, generated typings version, host eligibility, trigger handler names, duplicate bindings, duplicate package vnums/ids, compiled-JavaScript checksum drift, and bounded machine-readable diagnostics.
  - Publish-mode validation rejects every current manifest trigger because all entries remain deferred, reserved, or unsupported. Internal validation mode exists only to test package shape and future compatibility rules without enabling builder publication.
  - Static source policy rejects source-map references, static/dynamic imports, direct/bracketed `eval`, `Function` and constructor-based code generation, async, Promise, and timer APIs before any JavaScript runtime evaluation.
  - The current checksum is a stable server-computed validation checksum, not final publish-grade cryptographic signing. The publish workflow still needs cryptographic package integrity, scoped credentials, staged activation, rollback, and audit controls.
- Initial server-owned package registry/cache implementation:
  - `src/js_script_registry.{h,cpp}` stores validated package snapshots in memory before filesystem loading, live trigger dispatch, or game-handle APIs exist.
  - Registry replacement validates the full candidate package set, checks server-provided legacy `.scr` vnum ownership, rejects duplicate package ids/vnums, and swaps the active snapshot only after every package and registry-level check passes.
  - Failed replacement leaves the previous snapshot and trigger lookups intact. Empty replacement is controlled by an explicit option so intentional clearing is separate from failed reload behavior.
  - Registry lookup is const and host-aware: callers can find by package vnum/id, find a trigger binding by vnum plus host/kind/legacy value, or find all packages matching a host/kind/legacy trigger for future dispatch wiring.
  - The registry does not execute JavaScript, probe handler exports, derive filesystem paths from package metadata, or expose mutable package references.
- Initial server-owned host API contract implementation:
  - `src/js_api_contract.{h,cpp}` defines static metadata for JavaScript-visible builder API types before QuickJS host bindings or trigger dispatch exist.
  - The contract currently covers read-only handle interfaces for `Character`, `Player`, `Mob`, `GameObject`, `Room`, and `Zone`, plus `TriggerInfo`, `ScriptContext`, `ScriptResult`, and `Script`.
  - Each public member records a TypeScript-shaped signature, return type, nullability, live-handle requirement, side-effect category, permission/status string, and builder-facing documentation text.
  - V1 remains deny-by-default: read-only fields and pure result helpers are planned, while output helpers are deferred and world mutation helpers are explicitly unsupported.
  - Focused tests guard stable metadata, documentation completeness, duplicate symbols, raw C++ type leakage, side-effect permissions, nullability/liveness metadata, trigger-context field coverage against `js_scripting_manifest`, sanitized actor text docs, lookups, and enum string stability.
- Initial fixture-backed game-context execution implementation:
  - `src/js_game_runtime.{h,cpp}` wraps `js_runtime` to execute a trigger body with a generated, deeply frozen `ctx` object that resembles the planned builder API context.
  - The context contains only approved public fixture fields for character/player/mob-style handles, object, room, zone, trigger metadata, actor text, and opaque ids. It does not expose raw C/C++ pointers, live world state, filesystem loading, registry dispatch, output helpers, or mutation APIs.
  - Context objects are converted to null-prototype objects before freezing, object/function prototype constructor paths are hardened during the invocation, non-ASCII fixture bytes are escaped in generated literals, structurally unsafe wrapper-breakout-looking script bodies are rejected, and game-runtime diagnostics are bounded/redacted so thrown actor text is not logged by default.
  - This is the first automated bridge between JavaScript execution and game-shaped context data. It is intentionally not the final live QuickJS host-binding layer; later slices still need C-API or equivalent host-created value injection, liveness checks, real entity adapters, trigger dispatch integration, fixture/API-contract drift checks, and parity tests at the legacy call sites.
  - Focused tests in `src/tests/js_game_runtime_tests.cpp` cover reading context fields, allow/block return semantics, wrapper breakout rejection, mutation rejection on frozen context objects, prototype-pollution attempts, optional-handle nullability, actor/fixture string escaping, inherited runtime limits, state isolation, diagnostic redaction, and absence of raw pointer/process/constructor surfaces.
- Initial live game-struct adapter implementation:
  - `src/js_game_adapter.{h,cpp}` maps real `char_data`, `obj_data`, `room_data`, and `zone_data` values into the read-only `JsGameTriggerContextFixture` shape without executing JavaScript, dispatching triggers, retaining live pointers, or exposing mutation/output helpers.
  - The adapter uses an explicit options object for active character/object pointer sets, world bounds/count, mobile/object index tables, zone metadata, and race names. Missing liveness tables fail closed instead of accepting arbitrary non-null pointers.
  - Adapter output is a bounded snapshot of approved public data only: invocation-local role ids, display names, race text, levels, hit points, resolved mobile/object vnums where supplied, nullable unresolved object vnums, room/zone vnums and names, trigger metadata, and optional actor text.
  - Null, stale, or out-of-bounds inputs are omitted from the generated context rather than dereferenced. Missing index metadata produces unresolved/null vnum fields instead of leaking internal indexes or persistent player/object identifiers.
  - Focused tests in `src/tests/js_game_adapter_tests.cpp` cover character/player/mobile snapshots, fail-closed liveness defaults, stale pointer rejection, object/room/zone snapshots, invalid room/zone bounds, partial context construction, bounded/copied strings, unresolved vnums, object relationship pointer non-use, sentinel preservation on rejection, and ids that avoid pointer-looking/internal type text.
- Add a new script engine facade, likely `script_engine.{h,cpp}`, that exposes:
  - load/compile script by vnum and engine type
  - execute trigger by trigger id and subject context
  - return allow/block/handled status compatible with existing `call_trigger(...)`
  - reset/reload script cache for builder/admin workflows
  - collect/log script diagnostics with script vnum and trigger name
- Update `call_trigger(...)` and the specific `trigger_*` helpers so they can dispatch to either legacy ASIMA scripts or JavaScript scripts attached to the same mob/object/room trigger.
- Preserve ordering rules explicitly when both systems are attached to the same entity:
  - define whether legacy runs before JavaScript, JavaScript before legacy, or only one engine can own a given script vnum
  - fail closed for blocking triggers when script execution errors unless the trigger is explicitly non-blocking
  - document the rule in builder help
- Add a per-invocation context object with stable handles rather than raw pointers:
  - `self`, `actor`, `target`, `object`, `room`, `text`, and trigger metadata as applicable
  - entity handles should validate liveness before every API call
  - scripts should not retain live C/C++ pointers across invocations
- V1 JavaScript must not retain live entity handles or arbitrary mutable JS state across invocations. Any later persistence feature needs a separate audited storage design.
- V1 should either explicitly forbid wait/continuation behavior or design it independently from legacy `SCRIPT_DO_WAIT`; in either case, legacy waiting suppression and `continue_char_script()` behavior must remain unchanged.
- Define a trigger matrix before implementation, including subject mapping, dispatch order, short-circuit behavior, and return semantics:
  - `ON_BEFORE_ENTER`: room-event path iterates room occupants and short-circuits on false.
  - `ON_ENTER`: room-event path runs room handling, then occupants, then objects while the return value remains true.
  - `ON_DAMAGE`: victim script runs first; wielded-object script runs only if the victim path allows damage.
  - `ON_DIE`: target character script can block death.
  - `ON_WEAR` and `ON_PULL`: object scripts can block the action.
  - `ON_EXAMINE_OBJECT`, `ON_RECEIVE`, `ON_EAT`, and `ON_DRINK`: preserve current object/character subject mapping.
  - `ON_HEAR_SAY` and `ON_HEAR_YELL`: make a deliberate compatibility decision because the legacy hear helper checks both sections regardless of which hear trigger entered it.

Legacy `.scr` trigger inventory requiring JavaScript equivalents:
- The `.scr` trigger system uses the following `ON_*` trigger constants. These are separate from the ASIMA/Mudlle mobile program call-mask flags listed below, but both systems need JavaScript parity planning because builders currently use both behaviors.
- `ON_ENTER` (`11`):
  - Create a JavaScript trigger equivalent for character/mob scripts reacting to another character entering the room.
  - Create a JavaScript trigger equivalent for object scripts reacting when a character enters the room containing the object.
  - Preserve current room-event ordering: room hook first, then room occupants other than the entering character, then room contents, short-circuiting while return remains true.
  - JavaScript context should include at least `self`, `actor`, `room`, trigger metadata, and host type.
- `ON_BEFORE_ENTER` (`12`):
  - Create a JavaScript trigger equivalent for character/mob scripts that can block another character from entering the room.
  - Preserve blocking behavior: false/block prevents entry.
  - Preserve current fan-out across room occupants other than the entering character, short-circuiting on the first block.
  - JavaScript context should include at least `self`, `actor`, `room`, trigger metadata, and host type.
- `ON_BEFORE_DIE` (`13`):
  - This trigger is defined in `src/script.h` but the inspected legacy dispatch path does not call it, and the header comment marks it as `implemented??`.
  - Do not expose it as a supported JavaScript trigger until the product decision is made to either implement it deliberately or keep it reserved/unsupported.
  - The trigger manifest should list it as reserved/unsupported with a clear diagnostic if a builder attempts to publish it.
- `ON_DIE` (`14`):
  - Create a JavaScript trigger equivalent for character/mob scripts on the dying target.
  - Preserve blocking behavior: false/block prevents death.
  - Current `call_trigger()` receives the killer as `subject2`, but `trigger_char_die()` does not pass that killer into legacy script context; decide whether JavaScript v1 preserves that limitation or deliberately adds a typed optional `killer` context field.
  - JavaScript context should include at least `self`, optional `killer` if supported, trigger metadata, and host type.
- `ON_RECEIVE` (`15`):
  - Create a JavaScript trigger equivalent for character/mob scripts when the scripted character receives an object.
  - Preserve current context mapping: receiver is `self`, giver/actor is the second character, received object is the object context.
  - JavaScript context should include at least `self`, `actor`, `object`, trigger metadata, and host type.
- `ON_EXAMINE_OBJECT` (`16`):
  - Create a JavaScript trigger equivalent for object scripts when a character examines the object.
  - Preserve current object-host behavior and return semantics used by the look/examine path.
  - JavaScript context should include at least `self`/`object`, `actor`, trigger metadata, and host type.
- `ON_HEAR_SAY` (`17`):
  - Create a JavaScript trigger equivalent for character/mob scripts when another character says text the scripted character hears.
  - Preserve or explicitly replace the current legacy compatibility behavior where `trigger_char_hear()` checks both `ON_HEAR_SAY` and `ON_HEAR_YELL` script sections regardless of which hear trigger entered the helper.
  - JavaScript context should include at least `self`, `speaker`, sanitized/heard `text`, trigger metadata, and host type.
- `ON_DAMAGE` (`18`):
  - Create a JavaScript trigger equivalent for character/mob scripts on the victim before damage is applied.
  - Create a JavaScript trigger equivalent for wielded-object scripts after the victim script allows damage.
  - Preserve blocking behavior: false/block prevents the damage from being applied and prevents downstream weapon-object trigger dispatch when the victim script blocks.
  - JavaScript context should include at least victim/`self`, `attacker`, optional weapon object for object-host dispatch, trigger metadata, and host type.
- `ON_EAT` (`19`):
  - Create a JavaScript trigger equivalent for object scripts when a character eats the object.
  - Preserve current object-host context mapping.
  - JavaScript context should include at least `self`/`object`, `actor`, trigger metadata, and host type.
- `ON_DRINK` (`20`):
  - Create a JavaScript trigger equivalent for object scripts when a character drinks from the object.
  - Preserve current object-host context mapping.
  - JavaScript context should include at least `self`/`object`, `actor`, trigger metadata, and host type.
- `ON_WEAR` (`21`):
  - Create a JavaScript trigger equivalent for object scripts before a character wears/equips the object.
  - Preserve blocking behavior: false/block prevents the wear action.
  - JavaScript context should include at least `self`/`object`, `actor`, trigger metadata, target wear slot when available, and host type.
- `ON_PULL` (`22`):
  - Create a JavaScript trigger equivalent for object scripts before a character pulls the object as a lever.
  - Preserve blocking behavior: false/block prevents the pull action.
  - JavaScript context should include at least `self`/`object`, `actor`, trigger metadata, and host type.
- `ON_HEAR_YELL` (`23`):
  - Create a JavaScript trigger equivalent for character/mob scripts when another character yells text the scripted character hears.
  - Preserve or explicitly replace the current legacy compatibility behavior where `trigger_char_hear()` checks both hear-say and hear-yell script sections for both hear entry points.
  - JavaScript context should include at least `self`, `speaker`, sanitized/heard `text`, trigger metadata, and host type.
- Every active ASIMA trigger above must appear in the server-owned JavaScript trigger manifest with:
  - legacy numeric id
  - JavaScript handler name
  - host eligibility
  - blocking/non-blocking behavior
  - dispatch ordering
  - context fields and nullability
  - whether the trigger is implemented, reserved, unsupported, or deferred in the current engine version
  - parity tests proving the JavaScript trigger reaches the same gameplay call sites as the ASIMA trigger

ASIMA/Mudlle mobile-program call flags requiring JavaScript parity decisions:
- ASIMA/Mudlle programs use `SPECIAL_*` call flags from `src/interpre.h` and `CALL_MASK(host)` rather than the `.scr` `ON_*` constants.
- The builder-facing ASIMA documentation currently describes the `I` call-mask bits for command, self, and enter-room only, but the engine can pass additional call flags through the generic special dispatcher. JavaScript planning must decide whether each flag gets a supported JavaScript equivalent, remains hard-coded-special-only, or is explicitly unsupported for builder scripts.
- `SPECIAL_COMMAND` (`1`):
  - Create a JavaScript equivalent for command-handler behavior when a player command is checked against specials in the room, inventory/equipment, or target path.
  - Context should include `self`/host, `actor`, command id/name, raw/sanitized argument text, target data when available, source room, and trigger metadata.
  - Preserve return semantics where true/handled blocks normal command flow.
- `SPECIAL_SELF` (`2`):
  - Create a JavaScript equivalent for mobile heartbeat/self activity.
  - Context should include `self`, current room, tick/heartbeat metadata where available, and trigger metadata.
  - Resource budgets need to be especially strict because this can run from periodic mobile activity.
- `SPECIAL_ENTER` (`4`):
  - Create a JavaScript equivalent for special-procedure enter-room behavior when a character enters a room containing the host or when movement code targets the entrant.
  - Context should include `self`/host, `actor`/entrant, entering direction/reverse direction command id, room, and trigger metadata.
  - Preserve return semantics where true/handled can block or consume the special path depending on the current caller.
- `SPECIAL_DELAY` (`8`):
  - Create a JavaScript parity decision for delayed ASIMA continuation behavior caused by the ASIMA `d` command.
  - V1 JavaScript may deliberately not support continuations, but the manifest and docs must say so explicitly and the offline runner should reject scripts that try to use delayed continuation APIs until they exist.
  - If supported later, continuation state must be independent from legacy `continue_char_script()` and must validate handles after the delay.
- `SPECIAL_TARGET` (`16`):
  - Decide whether JavaScript supports target-special behavior from command targeting paths.
  - If supported, context should include `self`/target host, `actor`, command id/name, argument text, `targ1`, `targ2`, target types, and trigger metadata.
  - If deferred, the manifest should mark it unsupported for builder-authored JavaScript even though hard-coded specials can receive it.
- `SPECIAL_DAMAGE` (`32`):
  - Decide whether JavaScript supports special-procedure damage hooks in addition to the `.scr` `ON_DAMAGE` trigger.
  - If supported, context should distinguish this call flag from `.scr` `ON_DAMAGE`, include attacker/victim/target data, and preserve the special dispatcher's handled/blocking semantics.
  - Add parity tests for combat call sites that invoke `special(..., SPECIAL_DAMAGE, ...)`.
- `SPECIAL_DEATH` (`64`):
  - Decide whether JavaScript supports special-procedure death hooks in addition to the `.scr` `ON_DIE` trigger.
  - If supported, context should include dying character, killer/actor when available, target data from the waiting structure, and trigger metadata.
  - Add parity tests for death call sites that invoke `special(..., SPECIAL_DEATH, ...)`.
- `SPECIAL_NONE` (`0`):
  - This is defined as a no-callflag/default path and is not enabled by `CALL_MASK`; do not expose it as a builder JavaScript trigger unless a specific existing hard-coded-special behavior is being ported.
- Every ASIMA/Mudlle call flag above must appear in the server-owned JavaScript trigger manifest with:
  - legacy callflag value
  - JavaScript handler name if supported
  - builder support status: supported, hard-coded-special-only, reserved, unsupported, or deferred
  - host eligibility, initially mobile-only unless a broader special-procedure bridge is deliberately designed
  - context fields and nullability
  - handled/blocking semantics
  - parity-test requirements for each supported call site

Minimal JavaScript API v1:
- Initial server-owned host API contract implementation:
  - `src/js_api_contract.{h,cpp}` defines static metadata for builder-visible API types before any QuickJS host binding or trigger dispatch exists.
  - The contract records schema/API revision, checksum, generated typings/documentation versions, public class/interface/namespace names, members, TypeScript type strings, return types, nullability, liveness requirements, side-effect category, permission status, and documentation text.
  - Public v1 handle types are planned as read-only invocation-local handles: `Character`, `Player`, `Mob`, `GameObject`, `Room`, `Zone`, `ScriptContext`, and `TriggerInfo`.
  - Pure return helpers are planned through `ScriptResult.allow()` and `ScriptResult.block()`.
  - Output helpers and world-mutation helpers are explicitly deferred or unsupported in the contract until permission, recursion, liveness, and audit behavior are implemented and tested.
- Read-only helpers:
  - character name, level, race, room, NPC/player state, hit points, rank where already exposed to legacy scripts
  - object name/vnum and room name/vnum
  - current trigger name/type and spoken text for hear triggers
- Controlled action helpers must be deny-by-default. V1 should start with read-only helpers plus a small audited output/action allowlist, then expand command-by-command only after security and liveness tests exist.
- Initial allowed mutation/output candidates:
  - send to char
  - send to room
  - send to room except actor
  - say/emote/yell through existing command helpers where safe
- Defer high-risk actions such as raw kill, extract char/object, direct stat writes, exit mutation, broad teleport/movement, load mob/object, forced combat/social/wear/remove, and inventory transfer until each has a specific permission model, liveness checks, rollback/partial-mutation policy, and unit coverage.
- Return helpers:
  - `return true` / `return false` for blocking trigger semantics
  - explicit `Script.allow()` / `Script.block()` wrappers if the embedded engine needs a normalized return type
- Defer broad mutation APIs until after v1 so the initial bridge is auditable.

Builder-facing JavaScript API documentation:
- Treat API documentation as a required deliverable for every JavaScript scripting slice, not as optional release polish.
- Generate or version-lock the public documentation from the same server-owned API/trigger manifest and TypeScript definition source used by the Electron client so docs, typings, and runtime allowlists cannot drift silently.
- Documentation generation must be manifest-driven and deterministic:
  - source all public documentation from the server-owned trigger manifest, API contract metadata, package/runtime metadata, fixture schema metadata, diagnostic catalog, and generated TypeScript declarations
  - do not maintain separate hand-authored reference tables for trigger names, handler signatures, API members, support status, permissions, side effects, resource limits, or diagnostic codes
  - every generated documentation section must declare its source metadata owner: `js_scripting_manifest`, `js_api_contract`, manifest/export metadata, diagnostic catalog, fixture schema metadata, package/runtime metadata, or generated TypeScript declarations
  - hand-authored prose is allowed only for named migration and operational guide files; those files must have separate documentation checksums and cannot define API/trigger facts
  - emit checked-in generated artifacts for long-form markdown, compact hover text, in-game help source, CLI reference output, example index, diagnostic catalog, and documentation coverage reports
  - generated docs artifacts should use explicit paths such as `lib/text/generated/js/docs/api.md`, `lib/text/generated/js/docs/hover.json`, `lib/text/generated/js/docs/cli-reference.json`, `lib/text/generated/js/docs/diagnostics.json`, `lib/text/generated/js/docs/examples.json`, and `lib/text/generated/js/docs/coverage.json`
  - include `validation_manifest_checksum`, `api_contract_checksum`, `trigger_manifest_checksum`, `typings_checksum`, `documentation_checksum`, `fixture_schema_checksum`, generator version, and generated-at server build/revision metadata in every generated documentation artifact header
  - sort generated sections by stable manifest ids, not display names, so diffs remain reviewable and deterministic
- Documentation should cover every exposed public API item:
  - global script helpers and namespace layout
  - trigger function names, host eligibility, blocking behavior, context fields, and return semantics
  - character, player, mob, object, room, zone, and script-result handle classes/interfaces
  - every readable property, method, argument, return type, thrown/returned diagnostic, side effect, permission requirement, and resource-budget impact
  - handle liveness rules, invalid-handle behavior, null/absent context fields, extraction behavior, and cross-invocation lifetime rules
  - output/action helpers, what they emit, who can see the output, whether they can trigger other scripts, and what limits apply
  - unsupported APIs and deliberately absent capabilities so builders do not infer that raw server commands, filesystem access, network access, timers, persistence, dynamic imports, or direct pointer/state mutation are available
- Required documentation fields for each public trigger entry:
  - stable trigger id, legacy kind/value, JavaScript handler name, host eligibility, publishability, support status, blocking/handled return semantics, dispatch ordering, exception/fail-open/fail-closed policy, context fields with TypeScript types and nullability, required permissions, resource-budget category, offline runner support, fixture requirements, example ids, and stable diagnostic codes
- Required documentation fields for each public API type/member:
  - stable type/member id, display name, TypeScript declaration, parameters, return type, nullability, liveness requirement, side-effect category, permission status, support status, resource-budget impact, possible diagnostics, safe usage notes, unsupported alternatives if relevant, example ids, and generated hover summary
- Required documentation fields must be structured and individually validated, not just non-empty prose:
  - each entry needs summary, lifecycle/liveness notes, nullability rules, side-effect description, permission requirement, return semantics, diagnostic code links, example links, related trigger/API links, support status, and unsupported/deferred reason when applicable
  - structured context fields must render with field id, role, TypeScript type, nullability, liveness requirement, host availability, support status, and documentation id; prose-only context strings are not sufficient source data
- Required documentation fields for diagnostics and limits:
  - stable diagnostic code, severity, whether it can appear during typecheck/offline run/package/stage/activation/live execution, sanitized message template, remediation guidance, redaction policy, and linked examples
  - memory, runtime/instruction, stack, output/action, recursion, package-size, source-size, and fixture-size limits with enforcement phase and failure behavior
- Documentation should include builder-oriented examples for each supported trigger:
  - minimal allow/block examples
  - read-only inspection examples
  - allowed output/action examples
  - examples showing how to handle missing actors, objects, rooms, and invalid handles
  - examples for local fixture tests and expected assertions in the offline runner
- Example validation requirements:
  - every example must have a manifest-linked example id, supported host type, trigger binding, expected return behavior, required fixture schema version, and expected diagnostics/output assertions
  - examples must be stored as machine-readable fixtures with explicit mode: `typecheck-only`, `offline-run`, `negative-typecheck`, `negative-validate`, or `documentation-only`
  - runnable example fixtures must declare expected allow/block/error status, expected diagnostic code, expected emitted output/action budget behavior, unsupported/deferred status where relevant, and fixture inputs
  - TypeScript examples must compile against the generated `rots.d.ts` with strict settings and the supported TypeScript version range
  - runnable examples must execute through the offline runner or server-side test harness where practical and assert return value, diagnostics, resource-limit behavior, and allowed emitted output
  - negative examples must prove unsupported/deferred/reserved APIs, wrong host types, stale manifest checksums, invalid handles, and unsafe source-policy constructs fail with stable diagnostic codes
  - CI must fail when an example references an API, trigger, context field, diagnostic code, enum literal, or fixture field absent from the generated manifest/typings
  - examples that are documentation-only must be explicitly marked non-runnable with a reason and still pass syntax/type validation when possible
- Documentation should include a migration guide for builders familiar with legacy ASIMA-style scripts:
  - mapping legacy trigger concepts to JavaScript trigger functions
  - differences in return values, ordering, wait/continuation behavior, and say/yell compatibility
  - examples of legacy patterns that should not be recreated in JavaScript because the new API is deny-by-default
- Documentation should include operational guidance:
  - script file/project layout
  - TypeScript authoring and compiled JavaScript publishing flow
  - local compile/test/package/publish steps
  - staged vs live script states, activation, rollback, and common rejection reasons
  - how to read syntax/runtime diagnostics, resource-limit errors, manifest mismatch errors, and publish validation failures
- Documentation should be available in multiple builder-facing places:
  - checked-in markdown/reference docs for review and long-form explanation
  - generated TypeScript doc comments and editor hover text in the Electron app
  - in-game help entries for the supported JavaScript script layout, trigger names, return values, and high-level API rules
  - CLI help/reference output for offline validation and publishing commands
- In-game help generation requirements:
  - generate help source from the same documentation metadata used for markdown and TypeScript hover docs, then format it for existing MUD help constraints such as line width, section size, and terminal-safe text
  - write generated help topics under an explicit read-only generated path such as `lib/text/generated/js/help/`, with one topic per stable help topic id and a generated index file
  - generated help topics must include header metadata with documentation checksum, manifest checksum, generator version, topic id, source metadata ids, and generated artifact version
  - generated JavaScript help must not overwrite hand-maintained general help files directly; runtime help should either read generated topics read-only or copy them through an explicit build/install step
  - include high-level topics for JavaScript project layout, trigger handlers, context fields, return values, resource limits, diagnostics, offline testing, package validation, staging, activation, rollback, and unsupported capabilities
  - include per-trigger help entries only for authorable triggers; reserved/unsupported/deferred entries should appear in compatibility/reference help but not as normal builder-authoring commands
  - in-game help must include manifest/documentation version and checksum metadata so immortals can identify stale help after reloads or deployments
  - generated help text must avoid raw source snippets, local paths, live player names/speech, account identifiers, auth tokens, and server-local filenames
- CLI help/reference output should consume the generated docs index or compact CLI reference artifact, not a separate hand-authored command reference, so CLI help cannot drift from markdown, hover docs, and in-game help.
- Add documentation quality gates:
  - CI fails when a public API manifest entry or TypeScript declaration lacks documentation text
  - docs examples compile under the generated TypeScript definitions
  - runnable examples execute in the offline runner where practical
  - documentation generated from the manifest includes the manifest checksum/API version it describes
  - stale documentation is rejected when the API/trigger manifest changes without regenerated docs
- Documentation drift CI requirements:
  - add generator/check targets such as `make js-docs`, `make js-docs-check`, and CMake equivalents that regenerate markdown, TypeScript doc comments, hover summaries, in-game help source, CLI reference output, diagnostic catalog, and example validation reports
  - CI must fail when generated docs, in-game help, CLI reference output, diagnostic catalog, example index, or TypeScript doc comments differ from server metadata
  - CI must include tamper tests that alter one generated artifact at a time and assert the check target fails with the artifact name and stable diagnostic code
  - CI must include stale-doc fixtures for missing documentation id, stale documentation checksum, changed trigger signature, changed API member signature, changed nullability/liveness, changed permission/side-effect status, changed diagnostic code, changed resource limit, changed fixture schema, changed API docs without regenerated markdown, changed trigger notes without updated in-game help, changed example source without updated expected diagnostics, tampered generated help topic, and docs-only prose changes
  - docs-only prose changes must update `documentation_checksum` and generated docs while leaving `validation_manifest_checksum` unchanged; validation-relevant metadata changes must update the validation checksum and fail stale docs until regenerated
  - in-game help, markdown, TypeScript hovers, CLI reference output, and Electron help panels must be checked for matching documentation ids and checksums
  - generated documentation release notes must be derived from a machine-readable documentation diff summary so builder-facing release notes cannot claim unsupported compatibility
- Documentation checksum blocking rules:
  - documentation checksum mismatches fail docs CI, generated artifact checks, in-game help publication, and release-note generation
  - documentation checksum mismatches warn in Electron editor hovers/help panels and in-game help metadata, but do not grant or deny server scripting capabilities by themselves
  - package creation and offline run should warn on stale docs but block only when typings, fixture schema, runtime, or validation manifest compatibility is also stale/incompatible
  - stage and activation decisions are controlled by validation manifest, package, runtime, permission, and live-state checks; docs checksum is audit/display metadata unless documentation is being published
  - stale generated docs must never be used to infer support for an API/trigger that the validation manifest marks unsupported, deferred, reserved, or host-ineligible
- Keep documentation safe to publish: examples, diagnostics, and generated reference output must not include live player data, account identifiers, private logs, local filesystem paths, credentials, or raw production speech.
- Documentation safety tests must scan generated markdown, TypeScript doc comments, hover JSON, in-game help, CLI reference output, examples, fixture docs, diagnostic catalogs, editor config, and documentation release-note summaries for account ids, local absolute paths, source snippets, server filenames, raw player speech, credentials, auth tokens, raw C++ identifiers, and live log fragments.
- Documentation compatibility summaries must include docs version/checksum changes so release notes cannot claim documentation/API compatibility that generated metadata does not advertise.
- Documentation trust boundaries:
  - downloaded documentation, manifests, and compatibility summaries must come from authenticated server endpoints; cached/offline copies need provenance metadata with server identity, manifest checksum, documentation checksum, compatibility table revision, generated server revision, and optional signature/MAC status
  - cached or checked-in docs may support offline reading and editing, but the client must not treat them as publish authority without a fresh server compatibility check
  - generated diagnostics shown in docs, examples, Electron panels, CLI output, or in-game help must be bounded and redacted: no full source text, absolute local paths, account ids, auth tokens, live player text, private speech, descriptor data, server-local filenames, or uncontrolled control characters
  - documentation examples must use synthetic fixture names and stable fake ids only; examples that intentionally demonstrate rejection must not include realistic secrets or production-like private text
- Documentation generation acceptance tests required before Electron implementation:
  - table-drive documentation coverage over every manifest trigger, ASIMA/Mudlle call flag, API type, API member, context field, enum/literal domain, diagnostic code, resource limit, fixture schema field, and publish workflow state
  - require exact artifact coverage for markdown reference, generated TypeScript doc comments, hover/help JSON, in-game help topics, CLI reference output, diagnostic catalog, example index, compatibility summary, and release-note metadata
  - verify every generated artifact carries the same manifest checksum, trigger/API checksum pair, documentation checksum, typings checksum, runtime identity, engine ABI version, fixture schema version, and generator version where applicable
  - verify stale artifacts fail independently: changed API docs without markdown regeneration, changed TypeScript declaration comments without hover regeneration, changed trigger docs without in-game help regeneration, changed diagnostic text without CLI reference regeneration, changed example expectation without example index regeneration, and changed compatibility table without release-note summary regeneration
  - verify docs-only changes update `documentation_checksum` but do not change `validation_manifest_checksum`, while validation-relevant metadata changes update the validation checksum and force documentation regeneration before the check target passes
  - verify examples compile/run by mode: compile-positive TypeScript examples, compile-negative unsupported API examples, offline-run examples with fixture assertions, offline-run negative examples for invalid handles/resource limits, and documentation-only examples with explicit non-runnable reasons
  - verify generated in-game help has a topic for each supported trigger and public API type, includes concise high-level help for return values/diagnostics/resource limits, and keeps unsupported/deferred entries visible only as unavailable compatibility/reference entries
  - verify Electron help/search index entries link back to stable documentation ids and surface the same support status/reason codes as local validation and server validation
  - verify stale documentation diagnostics identify the artifact id, expected checksum/version, and actual checksum/version without dumping absolute paths, source text, account ids, auth tokens, private player text, local server filenames, or raw C++ symbols
  - verify generated release notes and compatibility summaries are derived from the same machine-readable documentation diff and compatibility table, including tests that tampered release-note compatibility claims fail the docs check
  - verify unsupported, deferred, reserved, host-ineligible, wrong-host, wrong-kind, and room-owned-nonpublishable cases keep the same stable reason code across generated docs, TypeScript comments, local validator docs, offline runner docs, package validation docs, staging docs, and activation docs
  - verify raw Makefile and CMake paths expose equivalent docs generation/check targets so generated documentation drift cannot be missed by one build path

Sandbox and safety requirements:
- Disable or omit JavaScript access to filesystem, sockets, process execution, native module loading, timers, dynamic imports, and host environment data.
- Do not register QuickJS-style `std`/`os` modules, host module loaders, native extension hooks, or finalizers that can touch game pointers after extraction.
- Freeze or minimize global objects, and define the policy for `eval`, `Function`, dynamic import, and runtime code compilation before enabling builder scripts.
- Set memory and instruction/runtime limits per invocation, plus a per-game-tick script budget.
- Guard recursive trigger entry and script-induced action loops with a maximum depth and action/output budget.
- Define partial-mutation behavior for exceptions, timeouts, and memory failures. Blocking triggers should either run precondition-only JavaScript before mutation or have documented compensation behavior.
- Validate all game handles before use and fail gracefully if an entity is extracted during script execution.
- Log script errors with vnum, engine type, trigger type, sanitized error class/location, and stable entity ids/vnums without raw player speech, account email, descriptor data, full source text, or arbitrary argument values by default.
- Ensure a JavaScript crash/exception cannot corrupt descriptor state, world lists, object lists, or combat state.

Builder/admin workflow:
- Extend script listing/inspection commands so immortals can see engine type and JavaScript source metadata.
- Add a reload path for JavaScript scripts that does not require a full reboot.
- Add help text for JavaScript script layout, supported trigger function names, return semantics, and API surface.
- Add diagnostics that point builders at syntax/runtime errors with line/column information when the selected engine supports it.
- Preserve existing shape/edit flows for legacy scripts; add JavaScript editing only if it can be done cleanly without destabilizing the current editor.
- Treat reload/source paths as a trust boundary: canonicalize paths under the JavaScript script root, allow numeric-vnum filenames only unless a stricter metadata format is chosen, reject symlinks, enforce max source size, check zone/builder permissions, write atomically, and keep the last known good compiled script active or explicitly disabled according to a documented reload policy.

Electron TypeScript authoring client:
- Build a companion Electron app for builders to author game scripts locally in TypeScript, test them offline, and publish approved compiled JavaScript artifacts to the server.
- Treat TypeScript as an authoring language only. The game server should execute compiled JavaScript and should never require a TypeScript compiler at runtime.
- The primary editor experience should look and behave like Visual Studio Code rather than a custom text box:
  - use a VS Code-style activity/sidebar, file explorer, tabbed editor, problems panel, output/terminal panels, command palette, status bar, theme support, and keyboard shortcuts where practical
  - support IntelliSense for all generated game scripting APIs, trigger context objects, enum/literal domains, fixtures, package metadata, and script-result helpers
  - provide hover documentation, go-to-definition, find references where practical, signature help, inline diagnostics, quick fixes, symbol search, rename support, formatting, and semantic highlighting through an LSP-compatible TypeScript language service configuration
  - load generated TypeScript declarations, trigger manifests, fixture schemas, and docs into the editor workspace so completions and diagnostics match the server-owned manifest/API contract exactly
  - keep editor diagnostics separate from server publish authority: local IntelliSense/LSP feedback helps builders iterate, but the server still revalidates compiled JavaScript and package metadata before activation
- Build the reusable builder workflow before the Electron shell:
  - define the project layout, manifest format, compiler settings, validator, offline runner, package format, and publish protocol first
  - expose that workflow through a CLI so CI and server-side validation can run it without Electron
  - make Electron a UI over the shared CLI/libraries rather than the owner of validation logic
  - define generated-file ownership, formatter/linter commands, local Git-friendly review behavior, and conflict handling between local files and live/staged server versions
- CLI-first builder workflow requirements before Electron implementation:
  - create a shared builder core library plus a `rots-script` CLI; Electron may shell out to the CLI or call the same library APIs, but must not contain separate compiler, validator, runner, packaging, or publish authority logic
  - keep command behavior deterministic and CI-friendly: every command must support machine-readable JSON output, stable diagnostic codes, non-interactive mode, bounded diagnostics, and no ANSI/progress noise unless explicitly requested
  - start with these commands: `init`, `manifest sync`, `typecheck`, `build`, `validate`, `fixture run`, `package`, `publish stage`, `publish activate`, `publish rollback`, `status`, `diff`, `docs`, and `doctor`
  - every command must define stable exit-code classes: success, local validation failure, stale manifest, stale live state, permission failure, server rejection, transport failure, and internal tool error
  - command JSON must include command name, schema version, request id where applicable, input artifact ids, output artifact ids, diagnostics, warnings, and whether any local or server state changed
  - require `--project`/workspace-root arguments to resolve under the local project root; commands must reject path traversal, symlink escapes for generated/package output, absolute paths in package metadata, and shell-command interpolation from project files
  - CLI command execution must not run arbitrary project scripts, npm lifecycle hooks, TypeScript transformers, postinstall commands, or fixture-defined commands unless a future explicit allowlist and prompt/audit model is added
  - all local cache and generated output writes must be atomic and recoverable: write to temp files under the project cache/output tree, fsync where practical, rename into place, and preserve the previous generated package on failure
- CLI project layout:
  - use a stable root marker such as `rots.script.project.json` with project schema version, package id, script vnum, host type, zone, server manifest checksum, fixture schema checksum, TypeScript settings checksum, and generated artifact ownership metadata
  - source lives under `src/` as TypeScript only; generated TypeScript declarations, manifest snapshots, docs, fixture schemas, and editor settings live under `generated/` and are marked read-only/generated in headers
  - compiled JavaScript and sanitized sourcemaps live under `dist/`; package bundles and local validation reports live under `packages/`; local fixtures and expected results live under `fixtures/`; local CLI cache lives under `.rots-cache/`
  - generated files must be reproducible and reviewable: stable formatting, stable ordering, no timestamps in validation artifacts, no local absolute paths, no hostnames/usernames, and no builder account ids in checked-in files
  - Git-friendly defaults should check in `src/`, `fixtures/`, project metadata, and deterministic generated declarations/manifest snapshots; ignore `.rots-cache/`, transient diagnostics, auth state, temporary publish receipts, and unsanitized local logs
  - project templates must include checked-in sample fixtures and expected assertions for each supported host type, plus negative examples for unsupported/deferred triggers that prove the local validator returns the same reason code as server validation
  - fixture and package metadata must never include live player speech, account emails, passwords, auth tokens, local filesystem paths, raw logs, server-local filenames, or production descriptor/session data
- TypeScript compiler settings:
  - pin a supported TypeScript version range in the manifest and CLI lockfile; Electron/LSP must use the same compiler version selected by the CLI
  - use `strict`, `noImplicitAny`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, `isolatedModules`, `noEmitOnError`, and deterministic emit settings
  - compile to plain JavaScript accepted by the embedded server runtime, with a fixed target/module format selected by the server manifest; do not emit runtime helpers, dynamic imports, bundled dependencies, or Node/Electron/browser globals
  - prohibit custom transformers, plugins that execute project code, compiler options that inject imports, and polyfills that require timers, filesystem, network, process, native modules, or dynamic code generation
  - sourcemaps are optional and disabled by default for publish; when enabled for local debugging or authorized server review, reject `sourcesContent`, absolute source paths, private path segments, and source-map URLs in compiled JavaScript
  - the validator must compare TypeScript source metadata, compiled JavaScript bytes, sourcemap policy, generated typings checksum, and package metadata before package creation
  - static validation tests must include mismatched TypeScript source vs compiled JavaScript, stale generated typings, stale manifest cache, changed handler names, missing handler exports, unsupported host contexts, and obfuscated source-policy attempts
- Local validator and offline runner:
  - `rots-script validate` must run the same static package policy as the server where practical: manifest compatibility, trigger host eligibility, unsupported/deferred/reserved reason codes, source policy, size limits, checksum checks, fixture schema compatibility, and diagnostic redaction
  - `rots-script fixture run` must execute compiled JavaScript with the same runtime identity, resource limits, trigger context shape, return normalization, sandbox policy, and read-only fixture semantics as the server harness
  - offline execution is advisory only; it may prove a package is locally plausible, but it cannot grant publish, staging, activation, or rollback permission
  - local diagnostics must carry phase, stable code, artifact id, trigger/package id, expected/actual checksum where relevant, and sanitized message; diagnostics must not echo full source text, private fixture text, absolute paths, credentials, account ids, or auth headers
  - fixture tests must be machine-readable and include mode, host type, trigger binding, fixture schema version, expected allow/block/error result, expected diagnostic codes, expected output/action budget behavior, and unsupported/deferred status where relevant
  - fixture tests must cover stale-but-editable fixture schema, stale-and-non-runnable fixture schema, removed enum values, renamed context fields, nullability changes, liveness changes, malformed assertions, oversized fixture strings, and unsupported trigger bindings
  - runner regression tests must prove failed fixture execution leaves compiled artifacts, package bundles, and local status cache unchanged unless an explicit update command is run
  - Electron problems panels, hover warnings, and run results must read these CLI/library diagnostics directly instead of reinterpreting server/client mismatch rules
- Package format:
  - package bundles are deterministic JSON with a canonical field order and a `packageFormatVersion`, `packageId`, `packageVersionId`, `scriptVnum`, `hostType`, `zone`, `manifestChecksum`, `triggerCatalogRevision`, `apiContractVersion`, `runtimeIdentity`, `generatedTypingsChecksum`, `compiledJavaScriptChecksum`, `sourcePolicyChecksum`, trigger bindings, and validation report reference
  - compiled JavaScript bytes are the artifact the server validates and executes; TypeScript source, docs, local reports, and sourcemaps are optional review artifacts and must not change what the server executes
  - source and sourcemaps must be separately permissioned for upload/view/download; server logs and diagnostics should reference package ids and digests, not source snippets or local filenames
  - package creation must fail when source, compiled JavaScript, manifest snapshot, generated typings, fixture schema, or validation report disagree; every mismatch gets a stable reason code
  - package validation must reject mismatches between package metadata and compiled JavaScript checksum, TypeScript source checksum, validation report digest, fixture schema checksum, manifest checksum, trigger catalog revision, runtime identity, host type, package vnum, handler names, and sourcemap policy
  - package mismatch tests must assert rejected packages cannot update local staged status, overwrite the prior package artifact, or be submitted for activation without a fresh successful package command
  - package bundles must not include executable install hooks, dependencies, native modules, local cache files, credentials, hidden dotfile auth state, crash reports, or unsanitized TypeScript compiler traces
- Publish protocol and server authority:
  - use a two-step stage/activate protocol over an authenticated server channel; generic file transfer to the live JavaScript package directory is not a valid publish path
  - publish requests include package metadata, compiled JavaScript bytes, optional sanitized source/sourcemap artifacts, local validation report, manifest checksum, compiled artifact checksum, base live checksum, client nonce, and requested permissions
  - the server recomputes canonical package digest, compiled JavaScript checksum, source policy result, manifest compatibility, package validation, builder permission, zone/vnum ownership, live/staged conflict state, and static/runtime policy before accepting a staged package
  - client-supplied checksums, compatibility claims, local validation reports, TypeScript typings versions, fixture results, and Electron UI state are advisory and never authoritative
  - activation is a separate request that names the exact staged package digest/version id, base live checksum, activation nonce/id, required permission, and expected live script slot; the server must reject stale staged digests, changed live checksums, revoked permissions, incompatible manifests, and replayed activation ids
  - rollback is also server-authorized and names an immutable prior package digest/version id; it must follow the same permission, ownership, audit, and live-state checks as activation
  - every publish, stage, activate, rollback, reject, and source-view action must produce a sanitized audit record with request id, package id/version, actor, account id where allowed by policy, zone/vnum, remote endpoint class, current live digest, staged digest, decision code, and bounded diagnostics
  - credentials must be short-lived and scoped by capability; the CLI stores persistent auth only through OS credential storage, falls back to memory-only credentials when unavailable, supports explicit logout/revocation, and must never write tokens into project files, packages, logs, validation reports, crash dumps, or Electron IPC payloads
  - publish protocol tests must cover replayed request id, expired request id, stale staged digest, stale live checksum, permission revoked between stage and activate, manifest changed between stage and activate, stage accepted but activation rejected, rollback target deleted, rollback target incompatible, and retry after transport failure
- Local, staged, and live conflict workflow:
  - `status` must fetch current server manifest compatibility, current live checksum, current staged package digest, package owner, permissions, and any pending activation/rollback locks before publish actions
  - `diff` must compare local source, compiled JavaScript, package metadata, staged package digest, and live package digest without exposing server-private source unless the user has source-view permission
  - package stage must require a matching `base_live_checksum`; if live changed after the local edit began, the CLI/Electron must show a conflict and require rebase/rebuild/revalidate before staging
  - activation must require the exact staged digest the user reviewed; if another builder stages or activates a package for the same vnum/host, activation fails with a stable conflict code and the local package remains unchanged
  - local cache state must never be treated as server truth; stale cached staged/live metadata can be used for display only and must be refreshed before stage, activate, rollback, or source-view
  - conflict resolution should offer explicit choices: pull latest metadata, keep editing locally, rebuild against latest manifest, restage new package, activate reviewed staged package if still current, or rollback to an authorized prior digest
  - failed stage/activation/rollback must preserve local source, local package artifacts, server live package, and previous staged/live metadata; partial server writes must be recoverable through immutable package/version records
  - state-machine tests must assert rejected publish leaves staged and live unchanged, activation failure preserves last live package and metadata, rollback failure preserves current live package, stale local cache cannot activate an older staged package, concurrent builders cannot activate the wrong digest, and conflict resolution never mutates local source without explicit command intent
- Electron over shared CLI/libraries:
  - Electron provides VS Code-style editing, problems/output panels, fixture controls, status/diff views, and publish/activation UI, but all compile, validate, run, package, publish, conflict, and auth state-machine behavior must come from the shared CLI/core library
  - Electron must display the exact CLI/server diagnostic code and artifact id for every problem; it may add UI explanation but cannot downgrade a server error to a warning or convert an unsupported API into an allowed API
  - Electron can cache manifests, docs, typings, fixture schemas, and status snapshots for offline editing, but must label provenance and refresh server state before package creation, stage, activation, rollback, or source viewing
  - Electron IPC messages should use typed schemas over explicit commands such as compile, validate, run fixture, package, status, stage, activate, rollback, and credential actions; IPC must not expose arbitrary command execution or arbitrary filesystem read/write
  - editor/LSP cache invalidation must use the CLI-generated manifest/typings/fixture/runtime checksums so Electron and non-Electron CLI runs see the same drift behavior
- CLI-first builder workflow acceptance tests required before Electron implementation:
  - project-layout fixture tests for valid projects, missing manifests, generated-file ownership violations, absolute/local paths, traversal, stale generated files, dirty build outputs, and cache-only manifests
  - compiler fixture tests for strict type failures, unsupported APIs, forbidden globals, forbidden imports/code generation, deterministic output, sourcemap policy, and TypeScript/LSP version drift
  - validator tests for every package manifest field, trigger binding, host eligibility, checksum mismatch, unsupported/deferred/reserved trigger, fixture schema drift, package-size/source-size limit, and diagnostic redaction
  - offline runner tests for allow/block/error normalization, fixture liveness, null handles, resource limits, output/action budgets, unsupported host APIs, hostile fixtures, and golden server parity
  - package format tests for deterministic bundle bytes, duplicate/unknown fields, malformed archives, path traversal, symlinks, oversized entries, wrong checksums, optional source/sourcemap policy, and local validation report mismatch
  - publish protocol tests for stage success, stage rejection rollback, activation success, activation precondition failure, stale staged digest, stale live checksum, rollback success/failure, replayed request id, retry/idempotency, and concurrent builder conflicts
  - state-machine tests proving rejected publish leaves staged/live unchanged, activation failure preserves the last live package and metadata, rollback failure preserves current live package, stale local cache cannot activate an older staged package, and conflict resolution never mutates local source without explicit command intent
  - Electron integration tests proving the UI is a consumer of shared CLI/library state, not a second validator with divergent rules
- Create a server-owned API/trigger manifest as a first implementation artifact. The manifest should include:
  - `schema_version`
  - `api_version`
  - `engine_abi_version`
  - trigger catalog revision
  - runtime feature flags
  - compatibility ranges
  - manifest checksum
  - generated TypeScript package version
  - host types, trigger names, trigger implementation status, blocking behavior, context fields, return semantics, and publish eligibility
- Publishing should require an exact or explicitly compatible manifest checksum, not just a loose API version string.
- The manifest schema must be treated as a server-owned contract with deterministic serialization:
  - use stable field names, stable enum string values, sorted object/array ordering where applicable, and no timestamps, local paths, build-machine data, or nondeterministic formatting in the canonical manifest
  - include a top-level schema identifier, manifest kind, schema version, trigger catalog revision, API contract version, engine ABI version, runtime identity, runtime feature flags, generated typings version, generated documentation version, and canonical manifest checksum
  - add or expose these metadata fields from the server manifest/export before Electron implementation: `schema_id`, `manifest_kind`, `schema_version`, `api_contract_version`, `engine_abi_version`, `trigger_catalog_revision`, `runtime_name`, `runtime_version`, `runtime_feature_flags`, `generated_typings_version`, `generated_documentation_version`, `fixture_schema_version`, `package_format_version`, `validation_manifest_checksum`, `trigger_manifest_checksum`, `api_contract_checksum`, `typings_checksum`, `documentation_checksum`, `fixture_schema_checksum`, and `compatibility_table_revision`
  - include per-trigger entries with stable id, legacy kind/value, handler name, host eligibility, publishability, blocking/handled semantics, exception policy, dispatch ordering, context fields with nullability, and support status
  - include per-API entries with stable type/member ids, TypeScript signatures, documentation ids, permission/side-effect status, nullability/liveness rules, and support status
  - structured trigger context metadata must live in the server source metadata, not be parsed from prose: each context field needs a stable field id, role name, TypeScript type, nullability, host availability, liveness requirement, documentation id, and support status
  - include compatibility ranges separately from exact current versions so a client can explain stale-manifest problems before attempting publish, while the server remains the final authority
- Manifest provenance and authenticity:
  - manifests downloaded by Electron must come from authenticated server endpoints and include server identity, server build/revision id, generated-at timestamp for display only, manifest checksum, compatibility table revision, and transport/auth context in the local cache metadata
  - checked-in or cached manifests may support offline editing, but the client must label their provenance and must not treat them as publish authority without a fresh server compatibility check
  - if manifests are exported for sharing or long-lived offline use, support an optional server signature or MAC over the canonical manifest plus compatibility summary; invalid or missing signatures should warn for editing and fail staging/activation
  - local manifest provenance metadata must not be included in the canonical validation checksum because server validation recomputes the canonical checksum from server-owned metadata
- Compatibility rules for Electron/offline tooling:
  - the Electron client may open projects with older compatible manifests for editing, but local validation must warn when the manifest checksum is not the current server checksum
  - local offline execution may run only when runtime name, runtime version, engine ABI version, manifest schema version, trigger catalog revision, API contract version, generated typings version, and fixture schema version are exact or explicitly listed in the server compatibility table
  - publish/stage requests must include the exact manifest checksum, trigger catalog revision, API contract version, generated typings version, runtime identity, package format version, and compiled artifact checksum used by the client
  - the server must recompute the manifest checksum, compiled artifact checksum, static source policy result, package validation result, and registry compatibility result; client-supplied compatibility claims are advisory only
  - unsupported, deferred, reserved, host-ineligible, or room-owned-nonpublishable manifest entries must fail with stable diagnostic codes during local validation and server validation
  - downgrades are rejected unless the server advertises a specific compatibility window for that older manifest checksum and the package does not use newer APIs or triggers
  - only the server's current compatibility table can authorize staging or activation; the Electron client may display compatibility hints, but cannot decide that an older manifest/runtime/typings set is publishable
  - activation requests must bind manifest compatibility to live state: current live checksum, staged package digest, builder permission, package vnum/host, server compatibility window, and activation id must all match server state at activation time
  - generated TypeScript package versions are derived from the server API/trigger manifest metadata, not manually edited in the Electron client, and they are diagnostic metadata only: server validation must never grant capability because a package claims a typings version
- Compatibility table shape:
  - include a server-owned `compatibility` section with revision, generated server version, accepted schema/API/runtime/typings/docs/fixture/package ranges, exact allowed manifest checksums, exact allowed trigger/API checksums, deprecation reason, expiration/removal version when known, and booleans for edit, typecheck, offline-run, stage, activate, rollback, and source-view eligibility
  - every compatibility entry must be closed, explicit, and monotonic; no client-defined ranges, wildcards, open-ended downgrade windows, or publish/activation permissions without exact checksum allowlists
  - removals and breaking changes must expire older publish/activation windows deliberately while still allowing read-only editing/source-view where safe
- Generated TypeScript package identity and versioning:
  - use a stable package id/name such as `@rots/scripting-api`, generated declaration path such as `generated/rots.d.ts`, and editor import path that the Electron/LSP workspace controls
  - record supported TypeScript compiler version range, emitted module target, declaration format, generator version, and cache key in the manifest/export metadata
  - version bump rules: breaking API/trigger removal, renamed handler, changed TypeScript signature, changed enum literal meaning, stricter nullability, stricter liveness, or permission/side-effect tightening bumps the major version
  - additive publishable API/trigger/context fields bump the minor version
  - docs-only changes bump the documentation checksum and may bump a patch/documentation version without changing validation compatibility
  - deferred/unsupported explanatory text changes do not imply publish compatibility and must not change the compact validation manifest checksum unless a validation-relevant status changes
  - Electron must invalidate LSP/type caches when typings checksum, TypeScript compiler compatibility, fixture schema checksum, runtime identity, or manifest checksum changes
- Manifest checksum policy:
  - compute `validation_manifest_checksum` over the canonical compact manifest that drives server validation and offline runner selection
  - exclude explanatory prose that is not used for validation unless the prose is part of the generated documentation checksum
  - store separate checksums for trigger manifest, API contract, combined builder manifest, generated TypeScript declarations, generated documentation, and fixture schema so drift diagnostics can point to the mismatched artifact
  - never include TypeScript source text, compiled script source text, builder account identifiers, local absolute paths, server secrets, live player data, or audit tokens in manifest checksum input
  - expose checksum values as read-only metadata in generated files and package manifests, then verify them server-side before staging or activation
  - `validation_manifest_checksum` blocks offline run, stage, and activation when incompatible; `typings_checksum` blocks local typecheck/package creation when mismatched; `documentation_checksum` blocks documentation CI and warns in the editor; `fixture_schema_checksum` blocks offline execution when incompatible; none of these checks are bypassed by client-supplied package metadata
- Manifest drift CI gate:
  - add deterministic generator/check commands such as `make js-builder-artifacts`, `make js-builder-artifacts-check`, and CMake equivalents that write or verify the compact manifest, TypeScript declarations, API reference docs, editor/LSP config, compatibility summary, and fixture schema from the same server-owned metadata
  - checked-in generated artifacts should live under an explicit generated builder-artifacts tree, for example `lib/text/generated/js/manifest.compact.json`, `lib/text/generated/js/rots.d.ts`, `lib/text/generated/js/api.md`, `lib/text/generated/js/editor-config.json`, `lib/text/generated/js/fixture.schema.json`, and `lib/text/generated/js/compatibility.json`
  - CI must regenerate those artifacts and fail if any checked-in generated artifact differs from the source metadata
  - CI must cover CMake and raw Makefile paths for generator and check targets, matching the repo's dual-build discipline
  - CI must run JSON parse/round-trip tests, checksum stability tests, enum string stability tests, documentation coverage tests, TypeScript compiler smoke tests, and negative type tests for unsupported/deferred APIs
  - CI must include stale-client fixtures: older compatible manifest, older incompatible manifest, newer manifest, wrong runtime, wrong typings version, wrong trigger catalog revision, wrong API checksum, wrong documentation checksum, and mismatched package checksum
  - release notes for builder tooling must list manifest schema/API/typing/documentation version changes and whether the change is backward compatible for editing, offline validation, staging, and activation
  - generated fixture schemas and cached offline test bundles must follow the same sensitive-data exclusion policy as manifests: no player account identifiers, private speech, local absolute paths, auth tokens, live logs, server-local filenames, or source snippets
- Manifest compatibility acceptance tests must be table-driven before Electron implementation:
  - for each compatibility dimension, cover exact match, older-compatible, older-incompatible, newer-unknown, malformed, missing, and tampered values with stable diagnostic codes
  - dimensions must include combined manifest checksum, trigger catalog revision, API contract version/checksum, engine ABI version, runtime name/version, generated typings version/checksum, generated documentation version/checksum, fixture schema version/checksum, package format version, and compiled artifact checksum
  - workflow states must be tested separately: open for editing, local typecheck, offline execution, package creation, stage request, activation request, rollback request, and source-view request
  - stale compatible manifests may open for editing with warnings, but offline execution must require runtime/schema compatibility, staging must reject unsupported newer APIs, and activation must fail if the staged digest, live base checksum, or server manifest checksum changed after staging
  - compatibility ranges must be closed and explicit: reject malformed ranges, open-ended ranges, overlapping contradictory ranges, downgrade windows without checksum allowlists, and combinations where the API version is compatible but the trigger catalog/runtime/fixture schema is not
  - generated TypeScript package version and checksum must change when any public trigger handler signature, API type/member signature, enum domain, nullability, liveness, permission, or side-effect status changes
  - generated documentation checksum must change when builder-facing API/trigger documentation changes, while non-validation prose must not change the compact validation manifest checksum
  - drift tests must intentionally tamper with generated `rots.d.ts`, compact manifest JSON, API docs markdown, editor/LSP config, fixture schema, and package metadata, then prove the drift command fails and names the mismatched artifact
  - checksum exclusion tests must prove timestamps, local absolute paths, builder identity, source comments, docs prose excluded from validation, and machine-local formatting do not affect the compact manifest checksum, while validation-relevant trigger/API/package fields do
  - unsupported, deferred, reserved, host-ineligible, room-owned-nonpublishable, wrong-host, and wrong-kind cases must produce the same stable reason code across TypeScript generation, local validator, offline runner, server package validator, staging, and activation
  - fixture drift tests must cover older editable fixtures, older non-runnable fixtures, removed enum values, renamed context fields, changed nullability, changed host liveness semantics, missing required fields, and fixture schema migration/rejection diagnostics
  - stale-manifest diagnostics must name the mismatched artifact and expected/actual version or checksum, but must not include absolute paths, source text, account identifiers, auth tokens, live player text, or server-local filenames
  - the generator should emit a machine-readable compatibility summary used by docs/release notes so release compatibility claims cannot drift from the manifest compatibility table
- Mark room-owned triggers and unresolved host types as unsupported/non-publishable in the manifest until the server-side room storage/dispatch policy is resolved. TypeScript can expose read-only room context where valid without implying room script authoring is supported.
- Package the TypeScript API definitions with the client and keep them generated from, or version-locked to, the server-side JavaScript host API contract:
  - strongly typed trigger context classes/interfaces
  - strongly typed character, player, mob, object, room, zone, and script-result handles
  - method/property documentation matching the server allowlist
  - trigger function signatures for every supported trigger
  - literal/enum types for trigger names, host types, directions, wear slots, races, positions, and other stable domain values exposed to scripts
- The client should load the complete trigger catalog from a server-exported manifest or a checked-in API manifest, including trigger names, host eligibility, blocking behavior, context fields, return semantics, and whether the trigger is implemented in v1.
- Regenerated TypeScript definitions, trigger manifests, and server host API allowlists must agree in CI; drift should fail the build.
- The client should provide a local project model:
  - script source files in TypeScript
  - generated compiled JavaScript output
  - script metadata such as vnum, name, description, host type, zone, allowed triggers, engine version, and API version
  - local fixtures for player/mob/object/room/zone state
  - expected test assertions for output messages, return values, diagnostics, and allowed state changes
- The client should include an offline runner that uses the same script facade contract as the server where practical:
  - compile TypeScript to JavaScript
  - execute compiled JavaScript through the same selected JavaScript runtime as the server, preferably via a shared native/wasm runner or the same validation binary used server-side
  - run any supported trigger against local fixtures
  - show messages sent to actor/room, return allow/block value, diagnostics, resource-limit failures, and host API calls
  - simulate handle liveness, extraction, invalid handles, and action/output budgets
  - support fixture libraries for common objects, mobs, rooms, zones, and player states
- Define the TypeScript compiler target, module format, forbidden transforms/polyfills, sourcemap policy, and allowed built-ins before client implementation starts.
- Define the LSP/editor configuration before client implementation starts, including TypeScript version, generated declaration roots, workspace layout, path aliases, diagnostic severity mapping, formatter/linter integration, and how manifest/API drift invalidates editor caches.
- Define a versioned fixture schema generated from the same manifest, including fixture schema version, manifest checksum, world/build revision metadata, enum domains, and handle-liveness semantics.
- The offline runner must not be the authority for production safety. Publishing must send a package to the server, and the server must revalidate before activation.
- Publishing workflow:
  - authenticate the builder/admin against a server-side publishing endpoint or command using a dedicated publish credential/session with short-lived scoped tokens.
  - require TLS or equivalent channel security, server-side rate limits, replay protection, audit ids, explicit logout/revocation behavior, and CSRF/session-binding protections if HTTP is used.
  - upload compiled JavaScript, TypeScript source if desired for review, metadata, API version, manifest checksum, engine version, package manifest, and local validation report.
  - upload to a staged server location first, not directly into the live script directory.
  - server computes canonical digests over staged artifacts, metadata, optional source, and validation reports; the server must not trust a checksum supplied by the Electron client.
  - server creates an immutable package id/version id and records the server-computed digest, manifest checksum, author, zone, vnum, host type, and base live checksum.
  - package manifests should be signed or authenticated where practical and include replay protection/provenance data.
  - server verifies builder permissions, zone ownership, vnum/host type, source size, metadata shape, package integrity, API/manifest compatibility, syntax/compile success under the server runtime, sandbox configuration, and trigger manifest compatibility.
  - server rejects packages that use APIs outside the allowlist or target unsupported triggers.
  - server rejects dynamic code-generation/import behavior according to the sandbox policy, including `eval`, `Function`, dynamic import, and bundled runtime dependencies unless explicitly approved.
  - server validates the exact compiled JavaScript bytes it will execute, regardless of TypeScript source, sourcemaps, local validation reports, or client-side checksums.
  - successful publish stores the package as staged; activation/reload is a separate explicit step that preserves the last known good compiled script on failure.
  - distinguish capabilities for upload/stage, activate live, rollback live, view source, and administer other builders' scripts.
  - activation must re-check permissions, zone/vnum ownership, current live checksum, manifest compatibility, and exact staged package digest.
  - all publish, activate, rollback, and reject actions should be logged with request/package ids, builder identity, actor account id, connection/source identifier where appropriate, script vnum, zone, staged/live digest, decision reason code, and sanitized diagnostics.
- The client should support rollback-aware publishing:
  - always fetch current live/staged script versions before publish, activation, or rollback
  - compare local compiled output against staged/live checksums
  - include `base_live_checksum`, package digest, manifest checksum, and activation preconditions in publish/activation requests
  - allow an authorized builder/admin to request activation or rollback through the server validation path
  - use immutable version directories, per-vnum locking, atomic live-pointer swaps, rollback to a named prior digest, retention/garbage-collection policy, and documented crash/partial-failure behavior
  - never write directly to `lib/world/js` over generic file transfer as the normal workflow
- Keep the reusable validator/runner available outside Electron as a CLI so CI and server-side validation can run the same checks without the desktop app.
- Client implementation should avoid bundling secrets in project files. Persistent authentication tokens must live in OS credential storage; if credential storage is unavailable, the client should fall back to memory-only tokens or require reauthentication. Do not store passwords at rest. Tokens need TTL/refresh behavior, logout clearing, and must not appear in logs, crash reports, exported packages, or local validation reports.
- Harden the Electron app:
  - `contextIsolation: true`
  - `sandbox: true`
  - `nodeIntegration: false`
  - no `remote`
  - strict Content Security Policy
  - block unexpected external navigation/window creation
  - validate IPC schemas
  - avoid arbitrary local file execution
  - harden custom protocol handling
  - require signed auto-update packages if auto-update is added
- Client supply-chain controls:
  - commit lockfiles
  - pin Electron, TypeScript, bundler, and runner dependencies
  - run dependency review/vulnerability scanning
  - prefer reproducible package builds where practical
  - code-sign desktop builds
  - disallow install-time scripts unless explicitly reviewed and approved
- Package privacy rules:
  - compiled JavaScript, optional TypeScript source, sourcemaps, diagnostics, and fixtures must not include account emails, passwords, verification codes, private player data, live logs, absolute local paths, auth headers, or raw source snippets in server logs
  - sourcemaps are allowed only if sanitized according to policy; `sourcesContent` should be rejected unless explicitly approved
- Client testing requirements:
  - TypeScript type tests proving unsupported methods/properties fail at compile time
  - CI regeneration tests proving TypeScript definitions, trigger manifest, manifest checksum, and server host API allowlist match
  - documentation generation tests proving every public manifest entry, TypeScript declaration, trigger, context field, class, property, and method has builder-facing documentation
  - documentation example tests proving checked-in and generated examples compile, and run through the offline runner where practical
  - negative TypeScript tests for removed, renamed, unsupported, or host-ineligible methods/triggers
  - trigger catalog loading, stale-manifest handling, manifest checksum compatibility, and API/engine version checks
  - golden offline/server parity tests that run the same compiled package, manifest version, and fixture through both the offline runner and server validator, then diff return value, emitted messages, diagnostics, resource-limit behavior, and allowed state changes
  - fixture validation for required context fields plus hostile fixtures: wrong host type, missing nested fields, invalid enum values, duplicate vnums, dangling exits, stale object references, extracted actor referenced by later assertions, malformed expectations, and fixture schema-version migration/rejection
  - offline runner tests for every supported trigger signature
  - manifest-driven negative tests for every unimplemented or host-ineligible trigger, proving type generation, runner selection, package validation, and server publish validation fail with the same diagnostic code
  - compile-to-JavaScript artifact tests, including sourcemap/source-reference policy
  - package mismatch tests where TypeScript source, compiled JavaScript, sourcemap, local validation report, manifest checksum, and server-computed digest intentionally disagree
  - negative package tests for absolute local paths, `sourcesContent`, player names/log snippets, private filesystem paths, bundled dependencies, dynamic code generation, and obfuscated unsupported API calls
  - publish package validation tests for server-computed digest, metadata, API version, manifest checksum, permissions, unsupported APIs, server rejection handling, replay protection, expired/revoked tokens, builder without zone ownership, stage-without-activate permission, admin rollback permission, and token expiry mid-flow
  - publish/activation state-machine tests proving rejected publish leaves staged/live unchanged, activation failure restores compiled cache and metadata, rollback failure does not corrupt live, stale staged digest refuses activation, and concurrent builders cannot activate the wrong package
  - UI workflow tests for edit, compile, run fixture, review diagnostics, package, publish staged, activate, rollback, stale manifest banner, compile failure preserving editor state, sanitized server rejection display, activation disabled on checksum/live mismatch, staged-vs-live conflict resolution, rollback confirmation, offline mode with cached manifest, and no secrets in exported packages or UI logs

Testing plan:
- Add focused unit tests around the new script engine facade:
  - loading valid JavaScript script metadata/source
  - rejecting missing, malformed, or oversized scripts
  - executing simple allow/block triggers
  - mapping each supported legacy trigger id to the correct JavaScript handler name
  - exception handling and fail-open/fail-closed behavior per trigger category
  - memory/runtime limit behavior where testable
  - handle-liveness checks after extracted characters/objects
- Add trigger-dispatch matrix tests:
  - table-driven coverage for every blocking trigger with legacy allow plus JavaScript block, legacy block plus JavaScript allow, JavaScript exception, missing handler, and mixed char/object chains
  - exact downstream invocation assertions for `ON_BEFORE_ENTER`, `ON_ENTER`, `ON_DAMAGE`, `ON_DIE`, `ON_WEAR`, and `ON_PULL`
  - movement before-enter, movement enter fan-out, death blocking, damage victim-vs-weapon ordering, object examine, receive, eat/drink, wear/pull, and say/yell behavior through the existing call sites where practical
  - explicit say/yell compatibility tests once the legacy double-check behavior is either preserved or corrected
- Add API wrapper tests:
  - send-to-char/room output capture
  - read-only character/object/room property access
  - action wrappers preserving existing command safety
  - invalid vnum/entity/room inputs fail without mutating world state
  - mid-invocation extraction of `self`, `actor`, `target`, objects, worn weapons, and room occupants followed by further API calls fails safely without use-after-free, skipped iteration, or duplicated iteration
- Add coexistence regression tests:
  - legacy scripts still execute unchanged
  - JavaScript scripts execute from the same trigger call sites
  - defined ordering when both script systems are present
  - blocking trigger return values still affect movement, death, damage, wear, and pull flows
  - same-vnum legacy/JavaScript conflicts follow the documented policy
  - same-entity multiple-engine attachment follows the documented ordering policy if supported
- Add malformed metadata/layout tests:
  - duplicate vnum, unknown engine type, missing metadata, source without metadata, metadata without source, wrong host type, no matching handler, non-function handler, oversized metadata/source, deleted source followed by reload, and stale compiled cache behavior
- Add sandbox/resource tests:
  - filesystem/network/process/module access is unavailable
  - `while(true)` interruption, memory exhaustion, runtime code-generation policy, and redacted diagnostics
  - recursive host-action loops such as hear-say causing say, damage causing hit, wear causing wear, enter causing teleport/re-enter, and object enter causing load/move
  - host API loops that repeatedly emit output or allocate/mutate C-side game state are bounded and leave no leaked mobs/objects after timeout
- Add reload/cache tests:
  - successful reload switches from old JavaScript source to new source
  - malformed reload keeps the last good compiled script or disables it per documented policy
  - reload during an active invocation does not invalidate the current frame
  - legacy wait continuation still resumes correctly after JavaScript cache reload
- Add null/invalid subject tests for the new facade and any hardened dispatcher paths:
  - null subject/actor/object where feasible, invalid room index or `NOWHERE`, actor without descriptor, NPC actor, object not in room/inventory/equipment, and empty weapon slot during damage
- Add build tests/coverage:
  - CMake includes the selected engine and new source files
  - raw `src/Makefile` includes the selected engine and new source files
  - no generated binaries or world runtime data are committed
- Run `make test` for all implementation slices.
- Run targeted manual smoke tests on a local server with at least one mob, object, and room JavaScript trigger before finalizing the integration.

Open product/implementation decisions before coding:
- Which JavaScript engine should be selected for v1, and whether it is vendored or system-linked.
- Exact on-disk JavaScript script layout and whether `.js` files use one exported `triggers` object, named global functions, or a small module wrapper.
- Whether a single script vnum can contain both legacy and JavaScript behavior or whether engine type is exclusive per script vnum.
- Ordering and error policy when multiple scripts or engines respond to the same trigger.
- Whether JavaScript scripts can keep persistent per-script state, or whether v1 follows the current script model where script-local data is lost after execution.
- Which legacy script commands are in the v1 JavaScript API and which are deferred.
- Builder permissions for creating/reloading JavaScript scripts.

Suggested delivery order:
1. Select the embedded JavaScript engine and add a minimal build-only integration behind a compile-time feature flag if needed.
2. Introduce the script engine facade and unit-testable trigger context model without changing existing legacy behavior.
3. Add JavaScript script loading/metadata support and cache/reload plumbing.
4. Wire JavaScript dispatch into `call_trigger(...)` while preserving all legacy script paths.
5. Implement the v1 safe JavaScript game API wrappers.
6. Add coexistence and blocking-trigger regression tests.
7. Add builder/admin listing, reload diagnostics, and help text.
8. Run full validation and smoke-test representative room, mob, and object JavaScript triggers.

## MSDP Unit Test Coverage Requirements

Add broad unit-test coverage for the game's MSDP implementation. The goal is to test as much of the current MSDP behavior as practical without depending on a live server socket, live telnet client, or full interactive smoke flow.

Requirements:
- Cover the protocol core in `src/protocol.cpp`, including MSDP negotiation, subnegotiation parsing, command handling, configurable variables, reporting state, dirty-state flushing, and output formatting.
- Cover the game-facing MSDP update paths in `src/comm.cpp` and `src/act_move.cpp`, including periodic character updates and room updates.
- Prefer focused C++ unit tests in `src/tests/` that can run through `make test`.
- Build reusable test helpers for descriptors, protocol state, output capture, fake player characters, rooms, and MSDP packet parsing so individual tests are readable and do not require a real network connection.
- Include negative and boundary coverage for malformed or truncated MSDP packets, unknown commands/variables, oversized values, invalid configurable values, write-once client identity variables, and disabled or missing protocol state.
- Verify the actual bytes or parsed structure emitted for MSDP arrays, tables, scalar variables, `REPORT` / `UNREPORT` / `RESET` behavior, `LIST` responses, and ATCP fallback where applicable.
- Include regression coverage for known suspicious or fragile paths as tests expose them, especially room updates and string escaping/sanitization.
- Keep smoke tests as a complement only; this feature should primarily be unit-test driven.

I'd currently like to add an account management system to the game. Accounts should now be email-first: at login the player is prompted for an account name, and that account name is the player's email address. If the account does not exist yet, the login flow should offer to create it and set a secure password with a minimum of 8 characters, upper and lower case, and a number. After logging into the account, the player should land in an account menu where they can list their characters, add an existing legacy character, create a new character, reset the account password, or play a linked character. When they add a pre-existing character it should verify the legacy character password, transform the character file, character object, and character exploit file to json, and store it in a new directory linked back to the account. The account files should also be in json and stored in the similar fashion of how player files are stored now with the alphabet being split up. New characters should not be born into the legacy file layout at all: their character data, object data, and exploit history should be written directly into the new JSON-backed account storage from the start. Character state should live in `character.json`, while object state and exploit history should each live in their own JSON files so they can be maintained independently, and the account file should reference those separate per-character files so it still shows what is linked. The old single-file character snapshot idea is transitional only and should not be the final design.

## Desired Login Workflow

1. Prompt for account name, which is the player's email address.
2. If the account exists, prompt for the account password and authenticate it.
3. If the account does not exist, offer to create it and collect/confirm the new password.
4. After account creation, send an email verification code to the account address and require the player to enter it before the account is trusted.
5. If the account exists but is still unverified, authenticate the password, send or resend a verification code, and prompt for that code instead of entering the account menu yet.
6. Verification must be by an emailed code that expires 15 minutes after it is issued.
7. After successful authentication and email verification, enter an account menu with these options:
   - list linked characters
   - add an existing character
   - create a new character
   - reset the account password
   - play a linked character
   - quit/back out
8. List linked characters as a simple list of the character names tied to the account.
9. Add existing character flow:
   - prompt for character name
   - prompt for that character's existing legacy password
   - if the password is correct, migrate the legacy data into account-linked JSON storage and attach it to the account
   - once migration succeeds and the account-owned JSON character storage is safely written, delete the old legacy player, object, and exploit files for that character
10. Reset password flow:
   - prompt for existing account password
   - prompt for new password
   - prompt to confirm the new password
11. Play character flow:
   - select a linked character
   - enter the world using the current character login behavior after selection
12. New-character storage rule:
   - a newly created character should be written directly to account-native JSON storage for character data, object data, and exploit data
   - each character should have its own dedicated `character.json` file containing the character state; that data must not be combined into a shared or multi-character JSON document
   - each character should likewise have its own dedicated `objects.json` file and `exploits.json` file
   - the account file should reference that character's separate files so an operator can inspect the account file and see exactly which assets are linked
   - legacy `lib/players`, `lib/plrobjs`, and `lib/exploits` files should be treated as migration/backward-compatibility inputs, not the authoritative home for newly created account characters
13. Active account-session/reconnect rule:
   - if an authenticated account already has a character still connected to the game, the account menu should show which linked character is currently active
   - this must cover both linkless characters left in-game after a socket disconnect and a second connection to the same account while another descriptor is actively playing
   - while the active character is not over level 91, the account must not be able to enter the game as a different character
   - treat "over level 91" as `GET_LEVEL(active_character) > 91`; level 91 and below remain restricted
   - if any currently active linked character on the account is over level 91, the account may select any linked character even if another active linked character is level 91 or below
   - the account should still be able to resume or reconnect to that same active character through the existing character reconnect behavior
   - if the active character is over level 91, selecting another linked character remains allowed
14. Administrator active-session unlock rule:
   - high-level account administrators need an `account unlockselect <email-or-account>` style command for cases where a low-level linked character is stuck active and blocks the account from selecting another character
   - the unlock should be account-scoped, runtime-only, and one-shot so it fixes the stuck-session case without permanently weakening the active-session guard
   - the unlock should allow linked-character selection only; it must not unlock account-menu new-character creation
   - the command should only grant an unlock when the account currently has a restricting active linked character session, and it should be logged like the other immortal account-management commands

## Execution Breakdown

Current repo/storage notes:
- Character files are currently stored in `lib/players/<bucket>/<name>`.
- Character exploit files are currently stored in `lib/exploits/<bucket>/<name>.exploits`.
- Character object save data is currently stored in `lib/plrobjs/<bucket>/`.
- Existing player data is split into alphabet buckets (`A-E`, `F-J`, `K-O`, `P-T`, `U-Z`, `ZZZ`), so account storage should likely follow the same pattern for consistency.
- Updated target rule: newly created account characters should persist directly into account-owned JSON storage and should not require a legacy-format birth write before they can be played.
- Updated storage rule: each character should have its own separate `character.json`, `objects.json`, and `exploits.json` files, with references from the account file, instead of being bundled into one monolithic document or any shared multi-character JSON file.
- Updated cutover rule: replace the transitional single-file character snapshot layout with separate per-character JSON assets plus account-owned references to them.
- Updated path rule: keep all account-owned files directly under `lib/accounts/<bucket>/<normalized_email>/`, and prefix character-owned asset filenames with the character slug instead of nesting them under a per-character directory.
- Updated schema rule: define `character.json` from the post-load runtime character/player structs rather than from the raw legacy save-file text, so migrated and newly created characters share the same canonical shape.
- Updated schema rule: preserve profession allocation points, profession coeffs, and other important persisted point/coefficient data in `character.json`; these are gameplay-relevant and must survive the cutover.
- Updated terminology rule: use `mystic` in the new JSON schema/docs where the legacy codebase still uses `cleric` identifiers internally.
- Updated schema rule: omit `pretitle` and `prompt` from the new `character.json` schema; they are not needed in the account-native character persistence format.

Proposed implementation slices:
1. Define the account data model and file layout.
2. Add JSON read/write support for accounts and migrated character assets with unit tests.
3. Add account creation and password validation flow with unit tests.
4. Add account login/authentication flow with unit tests.
5. Add administrator account-management tools with unit tests.
6. Add character linking/migration flow for pre-existing characters with unit tests.
7. Update game login/menu flow so players choose an account, then a linked character, with unit tests where practical.
8. Add final migration/smoke-test coverage and fill any remaining test gaps.

## Completed So Far

- Added a standalone account-management module with JSON read/write support for accounts and transitional migrated character storage.
- Added secure account password hashing and verification using `libcrypt`.
- Added bucketed account storage under `lib/accounts/...` and account-linked character storage under `lib/account_characters/...`.
- Added file-backed account creation, authentication, password reset, block/unblock, and character-link helpers with focused unit coverage.
- Added admin account-management commands for showing accounts, blocking/unblocking, resetting passwords, linking characters, and forcing migration.
- Added player-side `linkaccount` support with a masked password prompt instead of raw command-line password entry.
- Added transitional live login support for account authentication and linked-character selection.
- Added email validation, email-based account lookup, email-based authentication, and account creation from an email address as groundwork for the new email-first login flow.
- Added duplicate-link protection across accounts, migration-first linking to avoid stale partial links, duplicate-email detection that fails closed, and resilient email lookup when an unrelated account file is corrupt.
- Added account verification metadata, emailed verification-code generation, 15-minute expiry tracking, and confirmation helpers.
- Added outbound verification email delivery through the local `sendmail` interface.
- Added a configurable `ROTS_SENDMAIL_COMMAND` override plus a more robust live mail-delivery subprocess path so local smoke testing can capture verification emails without changing the feature behavior.
- Added fallback resolution for versioned legacy player-save filenames, so freshly created characters can be migrated into account storage immediately instead of only legacy characters that happen to live at the old unsuffixed path.
- Updated the live login flow so new and pending accounts email a verification code, accept `RESEND` / `CANCEL`, and require code entry before entering the account menu.
- Hid verification-code entry from snoops the same way other secret prompts are masked.
- Added persistent verification-attempt tracking, verification-code invalidation after too many bad tries, and resend cooldown protection for emailed codes.
- Hardened account creation to fail closed if stored account records are unreadable, so email uniqueness cannot be bypassed through corrupted account data.
- Added account-storage refresh helpers so linked characters can self-heal missing migrated character storage and refresh account storage from current legacy files.
- Added migration-restore helpers so account-selected play can reconstruct legacy player/object/exploit files from account storage before loading the character.
- Added asset-decoding helpers and a direct player-text load path so account-backed character selection can parse player data from account storage without first recreating the legacy player file.
- Updated runtime flows so account-created characters are immediately linked and migrated, account-selected play requires account-owned character storage readiness and restores from account storage before loading, and normal character saves refresh linked account storage.
- Updated migration/backfill flows so legacy-character migration now writes authoritative account-owned `character.json` immediately from decoded legacy player data, and account-backed selection now re-reads `character.json` after migration/backfill instead of decoding `migration.player_file` directly at runtime.
- Updated account-backed selection so the direct authoritative `character.json` fast path also loads account-owned object-save bytes before staging `Crash_load()`, which keeps already-account-native characters from dropping equipment when they enter the world without needing migration fallback first.
- Hardened account-backed cutover behavior so corrupt existing migrated character storage self-heals from legacy files, duplicate linked-character ownership fails closed, runtime support-file restore validates character-storage identity before writing, and malformed stored player text is rejected safely during direct account-backed load.
- Cut exploit history one step farther away from legacy login-time restore by removing exploit-file restoration from account-backed play, teaching exploit reads to fall back to account-owned character storage when the runtime file is absent, and seeding new runtime exploit files from stored account data when gameplay appends fresh records.
- Expanded and kept passing focused `AccountManagement` unit coverage for the foundation work, including verification-code success, invalid-code, expired-code, resend-cooldown, and pending-auth cases.
- Added regression coverage for corrupt migrated-character rebuilds, duplicate linked-character ownership, character-storage identity restore mismatches, malformed player-text decoding, exploit-history fallback/append behavior when the legacy exploit file is missing, corrupt runtime exploit-file self-healing, temp-file conflict failure, and preserving exploit history across account-storage refreshes.
- Ran `make test` and confirmed the full C++ unit test suite passes locally at 240/240.
- Expanded the proxy-backed Python smoke harness to cover account-menu new-character creation, reconnect, and account-backed play-character selection.
- Ran the required `Magus` quality review and `Vincent` security review for this exploit-history cutover slice, then addressed their findings before finalizing the pass.
- Added a shared `character_json` foundation module and focused unit coverage for profession points/coeffs, symbolic player/preference/affected flag arrays, structured affect state, `mystic` profession naming, and `char_file_u` conversion helpers as groundwork for the account-native `character.json` cutover.
- Expanded the shared `character_json` groundwork so it now round-trips a broader slice of normalized `char_file_u` state, including identity/physical fields, temporary and rolled abilities, point data, conditions, timers, talks, skills, hide flags, and array-capacity validation for applying JSON back into the stored character form.
- Hardened the shared `character_json` reader/apply path so malformed JSON now fails closed on out-of-range narrowed values, truncated fixed-width arrays, and overlong fixed-buffer strings instead of silently truncating or wrapping stored character state.
- Tightened the shared JSON/parser boundary further so parsed integers fail before out-of-range narrowing, fixed-width arrays are capped while parsing, embedded NUL bytes are rejected for fixed-buffer character strings, and oversized `affects` arrays are rejected before they can accumulate unboundedly.
- Updated the planned `character.json` shape so `skills` and `talks` are now represented as named key/value JSON objects rather than positional arrays, while the serializer still translates those objects back into the legacy fixed arrays for runtime compatibility.
- Added a shared `exploits_json` module plus focused unit coverage for exploit-history binary/JSON round-trip behavior, malformed binary-length rejection, and fixed-width string validation.
- Added account-layer helpers to write/read/check/remove per-character `exploits.json` files in the flat account directory layout.
- Updated new-character introduction so account-created characters now create an account-owned `exploits.json` during their initial account-link flow, with rollback cleanup if linking fails.
- Updated legacy migration so successful migrations now seed canonical account-owned `exploits.json` immediately when legacy exploit data is valid, write an empty default `exploits.json` when the legacy exploit file is absent, and fail closed when legacy exploit bytes are malformed.
- Updated exploit-history runtime flows so linked characters now prefer account-owned `exploits.json`, refresh it directly when new exploit records are written, and retire stale legacy runtime exploit files after successful account-native reads/writes.
- Added focused regression coverage proving corrupt authoritative account-owned `exploits.json` fails closed even when a stale legacy runtime exploit file is still present.
- Sanitized the transitional `.migration.json` artifact so it no longer persists raw legacy player-file bytes at rest; object/exploit transitional data remains available where still needed, while legacy player password/host content is no longer carried forward in the on-disk migration metadata.
- Stopped treating `.migration.json` as a routine persisted artifact during successful migration/refresh flows; ordinary migration now retires any leftover snapshot file, `ensure_character_migration(...)` no longer depends on it in the normal account-native path, and exploit-history refresh now falls back to authoritative account-native `exploits.json` data instead of the old snapshot file.

## Todo List

- [ ] Confirm product decisions before coding:
  - Account identifier rules: login should be email-first, so confirm whether email is the only account identifier or whether a separate display name still exists.
  - Email rules: normalization, uniqueness, and whether verification is required.
  - Email ownership proof: email-first accounts should not become the authoritative identity without verification or operator approval, otherwise unused email addresses can be squatted by whoever creates the account first.
  - Password storage approach: hashed/salted format for accounts; do not reuse current reversible character password handling.
  - Migration policy: whether a linked character remains playable through the old login path or becomes account-only.
  - Recovery/admin flows: how password resets, duplicate-email cases, and account unlinking should work.
  - Admin permissions: which immortals/admin levels can view, block, reset, or modify accounts.
  - Blocking semantics: whether blocked accounts are prevented from login entirely, character selection only, or specific actions.

- [x] Design the new on-disk account structure:
  - Add `lib/accounts/<bucket>/<account>.json` or equivalent.
  - Define JSON schema for account data: email-based account identifier, normalized email, password hash, linked characters, created/updated timestamps, and status flags.
  - Include administrative metadata such as block status, block reason, blocked-by, blocked-at, last password reset info, and audit history if needed.
  - Define JSON schema for account-owned character metadata and how it references separate `character.json` / `objects.json` / `exploits.json` assets under account-owned storage.
  - Decide whether migrated data lives under the account directory or under separate bucketed directories with back-references.
  Update: use `lib/accounts/<bucket>/<normalized_email>/account.json` for the account record, and keep character-owned files in that same directory with names prefixed by the character slug, such as `<character_slug>.character.json`, `<character_slug>.objects.json`, and `<character_slug>.exploits.json`.

- [x] Build serialization/deserialization support in the server:
  - Add helpers for reading/writing account JSON safely.
  - Add helpers for exporting existing character file, object save file, and exploit file into JSON.
  - Store character data, object data, and exploit data in separate JSON files so each asset can be maintained independently.
  - Add validation and error handling for missing/malformed JSON files.
  - Ensure writes are atomic enough to avoid partial migrations.
  - Add unit tests for valid reads/writes, malformed input, missing files, and partial-write safeguards where testable.

- [~] Implement account creation:
  - Add the creation flow directly to the login prompt when an email account is missing.
  - Enforce unique email-based account identifiers.
  - Enforce password complexity: minimum 8 chars, at least one uppercase letter, one lowercase letter, and one number.
  - Store passwords securely using one-way hashing plus salt.
  - Add unit tests for email normalization/uniqueness checks, password complexity, and account creation success/failure paths.
  Status: backend helpers and the login-prompt create-on-miss flow are wired into `nanny()`, new accounts are created as unverified, account creation immediately sends a verification code by email, mail delivery now supports a configurable local capture command for smoke testing, and creation fails closed if account storage is unreadable; remaining work is broader interactive smoke coverage around edge cases.

- [~] Implement account authentication:
  - Add login prompts/state transitions for email-first account lookup and password entry.
  - Validate credentials against stored account JSON.
  - Add failure handling, lockout/throttling considerations, and clear player messaging.
  - Prevent blocked accounts from authenticating or entering the game according to the chosen policy.
  - Add unit tests for successful login, failed login, blocked-account handling, and password verification behavior.
  Status: the live login prompt is now email-first, authenticates against account JSON, sends emailed verification codes for pending accounts, requires a valid unexpired code before entering the account menu, rate-limits resend attempts, invalidates codes after repeated failures, and now has local smoke coverage for create-account, verify, login, password reset, and re-login flows; remaining work is deeper interactive smoke coverage for linking and play-character paths.

- [~] Build the account menu workflow:
  - Add an account menu shown immediately after successful login or account creation.
  - Add a simple "list characters" option that prints the linked character names.
  - Add a "play character" option that selects one linked character and enters the world.
  - Add an "add existing character" option that prompts for legacy character name and legacy character password.
  - Add a "create new character" option that bridges into the existing character-creation flow under the authenticated account.
  - Change post-creation persistence so newly created characters are written directly into account-native JSON `character.json` / `objects.json` / `exploits.json` storage instead of being born in the legacy file layout and migrated afterward.
  - Add a "reset password" option that prompts for old password, new password, and confirmation.
  - Add unit tests for account-menu helper logic where practical and smoke test the full menu flow locally.
  Status: the live menu flow is now wired into `nanny()` with all requested options and sits behind verified-email gating; a local proxy-backed smoke test now covers account creation, emailed verification, verified-account login, character listing, password reset, logout, and re-login with the new password, and remaining work is broader automated coverage plus deeper socket-level smoke coverage for link/play paths. That smoke run now lives outside `make test` and should be run manually via `make smoke-account` when validating account/login/authentication changes.
  Update: socket-level smoke coverage now also covers creating a new character from the account menu, reconnecting, and entering the world through account-backed character selection.
  Update: account-created characters now write an account-native `character.json` as part of their initial account-link path, but object/exploit birth storage still needs the same direct-account-native cutover.

- [~] Implement administrator account management:
  - Add admin-visible commands or menu tools for account lookup.
  - Add a way to view all characters linked to an account.
  - Add a way to link/add a character to an account as an administrator.
  - Add a way to block or unblock an account.
  - Add a way to reset an account password securely.
  - Record audit information for sensitive admin actions where practical.
  - Add permission checks and clear logging for account-management actions.
  - Add unit tests for permission checks, block/unblock behavior, password reset behavior, character listing, and admin-driven character linking.
  Status: admin commands and helper tests are in place, including account email verify/unverify support; menu/help/doc polish is still pending.

- [~] Implement character linking for existing characters:
  - Verify ownership/authentication rules for linking an existing character.
  - Read the current character file from `lib/players`.
  - Read the current object save data from `lib/plrobjs`.
  - Read the current exploit history from `lib/exploits`.
  - Convert all three into JSON and store them as separate account-owned `character.json` / `objects.json` / `exploits.json` files.
  - After successful account-owned character storage is written and linked, delete the old legacy player/object/exploit files for that migrated character.
  - Record linkage metadata in the account-owned character record so the account can list/select the character later and so the account file clearly shows which `character.json` / `objects.json` / `exploits.json` files are linked.
  - Verify the legacy character's existing password before migrating/linking it through the account menu flow.
  - Add unit tests for successful migration, duplicate-link prevention, missing legacy file handling, password verification, and rollback/error behavior where practical.
  Status: migration/link helpers, duplicate-link protection, rollback protection, legacy-character password verification, storage refresh helpers, immediate migration for account-created characters, immediate account-owned `objects.json` / `exploits.json` creation, and sanitization of the persisted transitional `.migration.json` player payload are now in place; remaining work is the last migration-policy cleanup plus removing the temporary dependency on legacy-format birth writes for newly created account characters.

- [~] Update runtime character selection flow:
  - After account login, enter the account menu instead of dropping directly to character selection.
  - Allow selecting a linked character from the account menu to enter the world.
  - Define behavior for accounts with zero linked characters from the menu.
  - Preserve compatibility with current descriptor/login state machinery.
  - Add unit tests for account-with-no-characters, valid character selection, and invalid/unlinked character selection paths where practical.
  Status: account-authenticated login now lands in the account menu, handles zero-character accounts, can play linked characters from there, requires account-owned character storage readiness for linked-character play, loads player data directly from account storage, clears stale runtime object/exploit files before account-backed play, loads object/alias/follower save bytes from account storage when the runtime object file is absent, serves exploit history from account storage when no runtime exploit file exists, preserves account-backed exploit history across ordinary saves, self-heals corrupt account-owned character storage and corrupt runtime exploit files, and now fails closed on duplicate ownership or storage-identity mismatches; remaining work is deeper interactive smoke coverage and the remaining migration-policy cleanup.
  Update: account-menu new-character creation now smoke-tests cleanly against the account-backed play path because migration resolves the real versioned player-save filename written by fresh character creation.
  Update: account-backed selection now prefers direct `character.json` load and only falls back to migration when the authoritative account-native character file is absent.

- [x] Implement active account-session/reconnect guard:
  - Add an account-scoped active-session lookup for the live descriptor list:
    - match the authenticated normalized account on descriptors with the same account identity
    - ignore the current descriptor and unauthenticated/login-in-progress descriptors
    - include descriptors in `CON_PLYNG` so a second account connection sees an already-playing character
    - include descriptors in `CON_LINKLS` so a reconnect after a dropped socket sees the linkless character still in-game
    - require a live non-NPC character and verify that character is still linked to the authenticated account before treating it as the account's active character
    - return the active character name, level, connection state (`playing` vs `linkless`), and whether selecting a different character is allowed
  - Update the account menu display:
    - show the currently active linked character when one exists, including enough state for the player to understand whether it is still playing or linkless
    - keep the normal linked-character count and menu options visible
    - do not show the level-91 lock hint in the account menu when no over-level-91 choice is relevant; keep the menu focused on which character is active
  - Gate account-menu actions while an active character is level 91 or below:
    - allow listing linked characters, password reset, and logout
    - allow selecting/resuming the same active character so the existing reconnect/usurp path can take over that body
    - reject selecting any different linked character with a clear message and return to the account menu or account character prompt
    - reject account-menu new-character creation because it would enter the game as a different character
    - allow adding/linking an existing character while restricted because it changes the account roster but does not enter the game as another character
  - Preserve high-level exception behavior:
    - when any active linked character on the account is over level 91, keep the existing ability to select any linked character
    - still show the active character in the account menu so the player understands that another body is in-game
  - Keep existing same-character reconnect semantics intact:
    - do not duplicate live `char_data` records for the same active character
    - continue to close or usurp the old descriptor using the current `complete_existing_character_login(...)` / `CON_SLCT` reconnect logic
    - preserve account fields on the new descriptor so returning to the account menu still works after reconnect
  - Add focused unit coverage, with `Bazarat` pressure-testing the cases:
    - account menu shows an active linked character for a `CON_PLYNG` descriptor on the same account
    - account menu shows an active linked character for a `CON_LINKLS` descriptor on the same account
    - active sessions from another account, unauthenticated descriptors, NPCs, and unlinked characters are ignored
    - a level-91-or-below active character blocks selection of a different linked character
    - the same active character can still be selected to reconnect
    - an over-level-91 active character does not block selecting a different linked character
    - a mixed active-session account with one over-level-91 character and one lower-level character remains unrestricted
    - restricted accounts cannot create a new character from the account menu
    - selected-character failures leave descriptor state clean and do not strand staged account/object data
  - Add smoke/e2e coverage after the unit path is stable:
    - extend `make smoke-account` or add a targeted proxy-backed flow with two simultaneous connections to the same account
    - prove a second login sees the active character and cannot enter a different low-level character
    - prove reconnecting the same linkless account-backed character succeeds without corrupting account-native character/object/exploit storage
  Status: account-menu display and linked-character selection now scan live descriptors for same-account linked characters in `CON_PLYNG` and `CON_LINKLS`, show the active character in the account menu without a level-91 lock hint, block different-character selection and new-character creation while all active linked characters are level 91 or below, recheck stale character-menu and creation-wizard states before entering/birthing a character, preserve same-character reconnect/usurp behavior, and allow different-character selection when any active linked character on the account is over level 91. Focused unit coverage pins playing vs linkless display, absence of the menu lock hint, false-positive descriptor filtering, the level 91/92 boundary, mixed active-session high-level override, same-character usurp and linkless reconnect, side-effect-free blocking, stale-state races, and allowed list/reset/link/logout actions. The proxy-backed account smoke now includes a two-connection guard flow that proves a second login sees the active character, blocks selecting a different low-level character, and can reconnect the same active character.

- [x] Add administrator account-selection unlock:
  - Add `account unlockselect <email-or-account>` to the existing high-level account-management command surface.
  - Reuse the current account identifier lookup so either email or internal account name works.
  - Grant only a runtime, account-scoped, one-shot linked-character selection unlock.
  - Refuse to grant an unlock if the account does not currently have a restricting active linked character session.
  - Make linked-character selection and stale account-backed character-menu entry honor the unlock.
  - Keep account-menu new-character creation and stale character birth blocked even when an unlock is pending.
  - Consume the unlock when the account uses it to pass the final account-backed character-menu entry guard.
  - Log the administrative grant and add focused unit coverage for command behavior, unlock consumption, non-use when no restriction exists, and the new-character non-bypass.
  Status: `account unlockselect <email-or-account>` now resolves accounts by email or internal account name, grants a runtime-only account-scoped one-shot linked-character selection unlock only when the account currently has a restricting active linked character session, lets the early linked-character selection prompt and final account-backed character-menu entry guard honor that pending unlock, consumes it at final entry, and leaves account-menu new-character creation plus stale account-backed birth blocked. Immortal help documents the command and its one-use selection-only scope.

- [ ] Handle migration and backward compatibility:
  - New characters created through the account flow must be created directly under account-owned JSON storage, not legacy player/object/exploit files.
  - Decide how renamed/deleted characters affect linked account data.
  - Add guardrails to prevent duplicate links or partial conversions.
  - On successful migration of a legacy character into account-owned JSON storage, delete the old legacy player/object/exploit files instead of retaining or archiving them in place.
  - Define how admin-added character links interact with legacy standalone character login rules.
  - Keep `player_table` as a unified boot-time index for both legacy characters and account-native characters; account-native-only characters should be indexed at startup, not only when selected through the account flow.
  - Fail closed if the same normalized character name appears in both legacy storage and account-native storage, or in multiple account-native records, because character identities should never be duplicated across stores.
  Status: boot-time `player_table` indexing now scans both legacy player files and account-owned `character.json` files, account-native name-based loads now resolve through the shared `player_table`, and duplicate names fail closed during startup; remaining work is deleting legacy files after successful migration, finishing the direct-authority `objects.json` / `exploits.json` cutover, and closing the remaining rename/delete policy decisions.

- [~] Replace legacy runtime persistence with account-native JSON persistence for new characters:
  - Define the authoritative JSON schema for character state, object state, and exploit history for newly created account characters.
  - Base `character.json` on the normalized post-load runtime character/player structs, not on the raw legacy save-file text layout.
  - Include profession/class points, coeffs, and other gameplay-relevant point/coefficient fields in `character.json`.
  - Use `mystic` terminology in the schema/docs for the profession represented internally by legacy `PROF_CLERIC` fields.
  - Store those three assets in separate per-character `character.json`, `objects.json`, and `exploits.json` files under account-owned storage instead of a single bundled file or any shared multi-character JSON file.
  - Record references to those separate JSON files in the account-owned character metadata so the account file can show what is linked.
  - Remove the remaining transitional single-file character snapshot layout once the separate per-character JSON assets are in place.
  - Write new-character saves directly into account-owned JSON storage instead of legacy `players`, `plrobjs`, and `exploits` paths.
  - Update load/save/runtime flows so account-selected play reads and writes the JSON-backed form directly for new characters.
  - Update boot-time player indexing and name-based character loading so both legacy characters and account-native characters populate and resolve through the same `player_table`.
  - Keep legacy file ingestion only for migrated pre-existing characters until the full cutover is complete.
  - When a pre-existing legacy character is migrated successfully, remove its legacy on-disk player/object/exploit files so account-owned JSON storage becomes the only authoritative copy.
  - Add unit tests and smoke coverage proving a newly created character can be created, saved, reloaded, and played without depending on the legacy on-disk format.
  Status: the shared `character_json` module is now in place, account-layer helpers can read/write/remove per-character `character.json` files in the flat account directory layout, new account-created characters now write `character.json` during initial linking, legacy-character migration now also writes/backfills authoritative `character.json` from decoded legacy player data, migration now prefers a valid versioned player save over a stale flat file when both exist, retires that stale flat artifact during successful migration, cleans up newly written account-native outputs again if stale-flat retirement fails, and no longer persists raw legacy player-file bytes into the transitional `.migration.json` artifact at rest. Account-backed selection now prefers direct `character.json` load, no longer re-reads the migration snapshot just to clear runtime support files after fallback migration, now succeeds when a valid authoritative `character.json` exists even if the old migration snapshot is corrupt, and now fails closed if only the transitional snapshot remains while the authoritative `character.json` is missing. Ordinary saves now refresh account-native character files when they already exist, linked characters now repair a missing `character.json` directly from current store state instead of reviving the old snapshot-refresh path once migration has retired their legacy files, and unreadable account records now fail closed instead of letting account-native saves fall back to legacy player files. Boot-time player indexing/name-based loading now also include account-native characters, and the legacy boot scan now ignores flat player artifacts when a valid versioned sibling exists so pre-migration stale-flat data no longer causes duplicate-name startup failures. The `character_json` boundary is also tighter now, with required top-level section enforcement, explicit rejection of legacy `cleric` in favor of `mystic`, complete `flags` / `professions` object enforcement, and fail-closed parsing for incomplete structured affects. The `objects.json` cutover is also now further along with a shared `objects_json` module, account-owned object-file read/write helpers, loader preference for account-native object files, object-save refresh after crash/rent/idle writes, default empty `objects.json` creation for new account-born characters, immediate account-owned `objects.json` creation during migration when legacy object data is valid or absent, focused loader coverage proving account-backed login can equip staged objects, and migration-parity coverage proving legacy object payloads and the resulting account-owned `objects.json` decode to the same structure; the `exploits.json` cutover now has a shared `exploits_json` module, account-owned exploit-file read/write helpers, default empty `exploits.json` creation for new account-born characters, immediate account-owned `exploits.json` creation during migration when legacy exploit data is valid or absent, direct account-native exploit read/write preference for linked characters, and fail-closed coverage for corrupt authoritative exploit JSON. Remaining work is closing the remaining migration-policy/test gaps and continuing to run the smoke harness manually via `make smoke-account`, which still has a known telnet prompt-detection flake during some runs.

- [ ] Add validation and tests:
  - Treat unit tests as part of each implementation slice, not a final pass-only task.
  - Unit tests for password validation and account schema parsing.
  - Unit tests for bucket/path resolution for accounts.
  - Unit tests for legacy-to-JSON migration helpers.
  - Unit tests for blocked-account behavior.
  - Unit tests for admin permission checks and admin account actions.
  - Smoke test the login/account/character-selection flow locally.
  - Smoke test administrator workflows for block, password reset, character listing, and character linking.
  Status: focused unit coverage now includes unified legacy/account-native boot indexing, the shared `objects_json` and `exploits_json` round-trip modules, account-owned `objects.json` / `exploits.json` read/write helpers, account-backed object-save fallback, direct account-native exploit read/write preference, corrupt-authoritative-exploit fail-closed behavior, runtime-support-file clearing behavior, configurable verification-email delivery, versioned-player migration for freshly created characters, explicit precedence coverage proving a valid versioned legacy player save beats a stale flat file during migration, coverage proving that same migration still succeeds when the stale flat file is unreadable, boot-index coverage proving startup indexing prefers the versioned legacy save over a flat artifact even before migration runs, boot-index coverage proving successful migration also retires the stale flat artifact before startup indexing runs, direct account-native `character.json` file read/write/remove behavior, migration-time/backfill-time `character.json` hydration from real legacy player saves, cleanup-on-failure coverage proving stale-flat retirement failure removes partially written account-native outputs instead of leaving a duplicate boot hazard behind, direct account-native `character.json` plus `objects.json` staged-login coverage for equipped items, required-top-level-section enforcement for `character.json`, fail-closed nested `identity` / `progression` / `abilities` / `points` / `conditions` / `timers` / `perception` / `state` parsing in `character.json`, explicit missing-field regressions for each of those nested `character.json` sections, restore-path coverage proving mismatched migration identity does not overwrite stale runtime legacy files, coverage proving snapshot-only state no longer repairs a missing authoritative `character.json`, coverage proving corrupt snapshots do not block an already-authoritative `character.json`, legacy-file retirement immediately after successful migration into account-owned storage, rollback restoration when a later legacy retirement step fails after earlier files have already been removed, linked-character object/exploit loaders now using account-native JSON first and only still-present runtime legacy files second instead of decoding migration snapshot payloads, structured account-owned file inspection so unreadable authoritative character/object/exploit JSON fails closed instead of being misclassified as missing, runtime-legacy fallback coverage when account-native object/exploit JSON is absent, authority-order coverage proving account-native object/exploit JSON wins over conflicting runtime legacy data, fail-closed malformed-authoritative-object-JSON coverage that preserves the stale runtime file, fail-closed unreadable-authoritative-object/exploit coverage that preserves stale runtime files, save-path coverage proving already account-native linked characters do not attempt legacy snapshot refresh after migration retirement, that linked saves can repair a missing `character.json` directly from current store state, and that unreadable account records do not revive legacy player-file saves for account-native characters, legacy `cleric` rejection in favor of `mystic`, duplicate named `talks` rejection, unknown affected/hide flag rejection, missing structured-affect-field rejection, stored-object-path validation, narrowed `objects.json` field-range validation, empty/default `objects.json` round-trip coverage, required-top-level-section enforcement for `objects.json`, alias/follower truncation coverage, missing nested object/alias/follower field rejection in `objects.json`, missing nested object-affect field rejection in `objects.json`, stale verification-code rejection after resend, verified-account re-verification safety, and conflicting old/new-layout duplicate email record rejection. Focused `AccountManagement` is green at `100` tests, focused `DbLoader` is green at `28` tests, `make test` is now back to C++ unit-test coverage only and passes at `354/354`, and the proxy-backed Python smoke flow should be run manually via `make smoke-account` as a required separate validation step for account/login/authentication changes. Remaining work is broader interactive smoke coverage for legacy-character linking, the last migration-policy cleanup, and eventually tightening the flaky prompt-detection in the manual smoke harness.

- [ ] Upgrade player colors to support true color selection:
  - Keep the current per-category color slots, including `magic` and `weather`, but replace the legacy “small integer only” assumption with a richer internal color model.
  - Define each stored color selection as a mode-aware value:
    - `default`
    - `ansi16`
    - `truecolor`
  - Preserve backward compatibility for older saved characters:
    - existing integer color values should load as `ansi16`
    - missing color data should still default safely
    - old clients should still receive usable downgraded output
  - Centralize color rendering so every colorized output path goes through one renderer that knows:
    - the selected color slot
    - the player’s configured foreground/background values
    - the client’s supported color capability
    - the required fallback behavior
  - Support true color escape generation using standard terminal sequences:
    - foreground: `ESC[38;2;R;G;Bm`
    - background: `ESC[48;2;R;G;Bm`
    - full reset at the end of colored segments: `ESC[0m`
  - Introduce terminal-capability-aware fallback rules:
    - no-color clients receive plain text
    - ANSI-only clients receive nearest supported ANSI colors
    - if an intermediate 256-color tier is added later, true color may downgrade to nearest 256-color before ANSI
  - Extend account-native `character.json` color persistence from named integers to structured named objects.
  - Proposed schema shape for future account-native color data:
    - `foreground` and `background` should be stored independently per slot
    - `background` should be optional and default to `default`
    - example:
      ```json
      "colors": {
        "magic": {
          "foreground": { "mode": "truecolor", "r": 180, "g": 80, "b": 255 },
          "background": { "mode": "default" }
        },
        "weather": {
          "foreground": { "mode": "truecolor", "r": 90, "g": 170, "b": 255 },
          "background": { "mode": "truecolor", "r": 10, "g": 20, "b": 35 }
        }
      }
      ```
  - Keep JSON deserialization backward compatible with the current integer form during the transition.
  - For legacy text player-file compatibility:
    - account-native JSON should remain authoritative
    - legacy save compatibility should keep only the nearest ANSI fallback if needed
    - true color should not require the legacy file format to become authoritative again
  - Expand the `color` command UX without breaking existing syntax:
    - keep `color <slot> <legacy-color>` working
    - add forms like:
      - `color magic fg hex #B450FF`
      - `color magic bg hex #0A1423`
      - `color weather fg rgb 90 170 255`
      - `color magic bg default`
    - validate RGB ranges and hex format strictly
  - Update no-argument `color` output so it shows readable current values, for example:
    - `magic: truecolor fg #B450FF bg default`
    - `weather: truecolor fg #5AAAFF bg #0A1423`
    - `chat: ansi bright magenta`
  - Recommended rollout order:
    1. introduce the internal color model and centralized renderer
    2. add backward-compatible JSON read/write for the new schema
    3. expose true color selection in the `color` command for foreground values
    4. wire more message families through the centralized renderer
    5. add optional background-color support after foreground behavior is proven stable
  - Recommended implementation boundary for v1:
    - design the model for both foreground and background now
    - implement foreground first
    - treat background as advanced/optional follow-up work even though the schema should already support it
  - Unit tests for:
    - ANSI legacy color migration into the new model
    - true color JSON read/write
    - invalid RGB and hex rejection
    - exact escape-sequence rendering
    - capability downgrade fallback
    - no-color plain-text fallback
  - Regression coverage for:
    - older characters still loading correctly
    - existing color categories like `magic` and `weather` continuing to work
    - spellcasting and other migrated message families still rendering correctly after the renderer centralization

- [ ] Document the feature:
  - Update help text/admin notes for account creation and linking.
  - Document administrator account-management commands/workflows.
  - Document new file locations, the separate `character.json` / `objects.json` / `exploits.json` asset layout, and migration behavior for operators.

## Suggested Delivery Order

1. Finalize account schema and migration rules.
2. Implement account JSON storage helpers plus their unit tests.
3. Implement password validation and secure hashing plus their unit tests.
4. Implement account creation/login flow plus their unit tests.
5. Build the account menu, password reset flow, and simple character listing.
6. Implement character link + migration flow plus their unit tests.
7. Wire play-character and new-character creation into the account menu flow plus unit tests where practical.
8. Implement the active account-session/reconnect guard so second logins and linkless reconnects cannot branch into another low-level character.
9. Replace new-character legacy birth writes with direct account-native JSON persistence for `character.json` / `objects.json` / `exploits.json`.
10. Implement administrator account-management tools plus their unit tests.
11. Add docs, smoke tests, and close any remaining test gaps.
