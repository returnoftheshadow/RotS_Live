# Features to Add

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
8. Replace new-character legacy birth writes with direct account-native JSON persistence for `character.json` / `objects.json` / `exploits.json`.
9. Implement administrator account-management tools plus their unit tests.
10. Add docs, smoke tests, and close any remaining test gaps.
