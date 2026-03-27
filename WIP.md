# Work In Progress

## Current Status
- In progress.
- The emailed verification-code slice is implemented and hardened, and the account-authoritative cutover is now past the first direct-load stage with exploit history no longer depending on login-time legacy-file restore.
- The latest exploit-history cutover slice is complete, validated, and reviewer-cleared.

## Current Task
- Handoff point: the next session should start on the remaining account-authoritative cutover for the legacy object support-file dependency.

## Recent Progress
- Read `FEATURES.md`.
- Broke the feature request into smaller implementation slices.
- Added administrator account-management requirements to the feature plan.
- Mapped the current login flow in `src/interpre.cpp`.
- Confirmed existing player/exploit/object save directory layout under `lib/`.
- Confirmed there is no existing JSON library already wired into the repo.
- Added a standalone `account_management` module.
- Added bucketed account-path helpers, JSON serialization/deserialization, and file persistence helpers.
- Added secure account password hashing/verification using `libcrypt`.
- Built the test binary and passed the focused `AccountManagement` unit-test suite.
- Added admin-friendly helpers for blocking/unblocking accounts, password resets, and character linking.
- Expanded the focused unit-test suite to cover admin helper behavior.
- Added file-backed account creation, authentication, and admin workflows.
- Added account-linked character snapshot storage and default legacy path helpers for `players`, `plrobjs`, and `exploits`.
- Expanded the focused unit-test suite to cover migration snapshot behavior and default legacy path migration.
- Addressed review feedback around safer account creation, least-privilege file permissions, generic auth failures, and optional legacy object/exploit files.
- Added the immortal `account` command for account lookup, block/unblock, password reset, character linking, and character migration.
- Added the player `linkaccount <account>` command with a masked password prompt so account credentials do not travel through normal command logging.
- Added login-state support for `account <name>` at the name prompt, account password authentication, and linked-character selection before entering the normal game menu.
- Added account-selection helper coverage so linked-character selection rules and prompt formatting are unit tested.
- Added identifier validation before account-file access, cross-account character ownership checks, and migration-first linking so failed migrations do not leave stale account links behind.
- Rebuilt the test binary and passed the focused `AccountManagement` unit-test suite with 33 tests.
- Captured the new target workflow in `FEATURES.md`: email-first login, create-account-on-miss, authenticated account menu, legacy-character password verification during linking, menu-driven password reset, and play/create-character entry points.
- Added account-layer email validation, email-based account lookup, email-based authentication, and account creation from an email address so the login flow can move off the transitional account-name prompt.
- Tightened email lookup to fail closed if duplicate account files claim the same normalized email address.
- Rebuilt the test binary and passed the focused `AccountManagement` unit-test suite with 39 tests.
- Switched the live login prompt from character-name-first to account-email-first.
- Added login states for create-account confirmation, account password creation/confirmation, account menu, legacy-character linking, account password reset, and account-backed new-character entry.
- Wired `nanny()` so an email can authenticate an existing account or create a new one directly from the login prompt.
- Added the authenticated account menu flow for listing linked characters, playing a linked character, linking an existing legacy character via legacy password verification, creating a new character, resetting the account password, and logging out.
- Kept automatic post-creation linking in place so a character created from the account menu is associated back to the authenticated account.
- Split account-password entry off from the legacy 10-character player-password buffer so account creation and resets can accept longer passwords without inheriting the old character limit.
- Added linked-character owner lookup plus account-menu preflight checks so account-created characters cannot reuse names already claimed in account storage.
- Added rollback behavior so if post-creation account linking fails, the newly created character is immediately marked deleted instead of being left behind as an orphaned legacy character.
- Removed the hidden `legacy <character>` login bypass so the public login surface now follows the email-first account workflow instead of offering a parallel legacy path.
- Added account email-verification metadata to the JSON account schema plus admin verify/unverify helpers and commands.
- Changed newly created email-first accounts to remain pending until an administrator verifies the email address.
- Gated account authentication so unverified accounts cannot enter the account menu, and added player-facing messaging that the account is awaiting verification.
- Expanded the focused account suite to cover verification metadata, pending-verification auth failures, admin verify/unverify flows, and the verified-account migration path.
- Replaced the admin-only verification stopgap with emailed verification codes delivered through the local `sendmail` interface.
- Added verification-code hashing, persistence, resend support, and a 15-minute expiry window.
- Updated `nanny()` so new and pending accounts transition into a verification-code prompt, support `RESEND` and `CANCEL`, and only enter the account menu after successful code confirmation.
- Marked verification-code entry as secret input so snoops do not see emailed codes.
- Added persistent verification-attempt tracking, invalidated emailed codes after too many bad tries, and added resend cooldown protection.
- Fixed the account-creation handoff so a newly created account that becomes verified immediately routes into the account menu instead of getting stuck at the code prompt.
- Hardened create-on-miss to fail closed if existing account records are unreadable, preventing email uniqueness bypass through corrupt storage.
- Expanded the focused account suite to 49 passing tests, then 50 with the unreadable-record regression coverage folded into the full test suite.
- Re-ran `make test` and confirmed the full C++ unit test suite passes locally at 224/224.
- Added helpers to ensure a linked character has an account snapshot and to refresh linked snapshots from current legacy files.
- Updated account-character selection so linked characters must be account-snapshot-ready before play and will self-heal a missing snapshot by migrating current legacy files when possible.
- Updated new account-created characters to link and migrate immediately instead of only linking.
- Hooked normal player saves to refresh linked account snapshots so account storage tracks ongoing character changes.
- Added helpers to restore legacy player/object/exploit files back out of account snapshots.
- Updated account-backed play so the selected linked character is restored from account storage before the legacy runtime loader runs.
- Expanded the focused account suite to 55 passing tests.
- Added direct player-text loading from account snapshots, so account-backed play no longer has to recreate the legacy player file before character load.
- Narrowed runtime restoration during account-backed play to the still-legacy support files.
- Expanded the focused account suite to 57 passing tests.
- Hardened linked-character owner lookup to fail closed when multiple accounts claim the same character.
- Updated snapshot readiness so a corrupt existing migration snapshot is rebuilt from current legacy files instead of blocking account-backed play.
- Added snapshot-identity validation before restoring runtime support files out of account storage.
- Hardened direct player-text loading so malformed snapshot data is rejected safely instead of walking past buffer boundaries.
- Added regression coverage for duplicate ownership, corrupt-snapshot rebuilds, snapshot-identity mismatches, and malformed player-text decoding.
- Changed account-backed play so it no longer restores the legacy exploit file during login; stale runtime exploit files are removed instead.
- Added exploit-history helpers that read from account snapshots when the legacy exploit file is missing and seed new runtime exploit files from snapshot history when gameplay appends new records.
- Preserved existing exploit history during linked-character snapshot refreshes when the runtime exploit file is intentionally absent, so ordinary saves no longer erase account-backed history.
- Hardened exploit-history loading so malformed runtime exploit files are removed and rebuilt from account snapshots instead of poisoning later reads/appends.
- Hardened exploit writes to fail closed on pre-existing temp paths using secure temp-file creation.
- Updated `print_exploits()` and runtime exploit writes to use the new fallback path, and fixed runtime account-snapshot refresh calls to use the live data-directory root.
- Added focused regression coverage for exploit-history snapshot fallback, snapshot-seeded append behavior, corrupt runtime exploit-file fallback, temp-file conflict failure, and preserving snapshot exploit history across refreshes.
- Rebuilt the server and passed the focused account suite at 61 tests, focused db-loader coverage at 5 tests, and the full unit suite at 240/240.
- Sent the exploit-history cutover slice through `Magus` and `Vincent`, addressed their findings, and got a clean final reviewer pass from both.

## Next Step
- Continue the cutover so the legacy object support file stops being the remaining runtime substrate during account-backed play.
- Map exactly where `Crash_load()` and related object/runtime flows still require `plrobjs/...`, then decide whether to load object state directly from account snapshots or add a narrower account-aware fallback similar to the exploit-history path.
- Add smoke coverage for the new interactive login/menu flow and tighten any rough edges uncovered by real socket-level testing.
- Before production use, replace the current raw legacy snapshot capture with structured/sanitized migration for player data so legacy password/host fields are not carried forward verbatim.

## Last Validation
- `cmake --build build --target ageland -j4`
- `./bin/tests '--gtest_filter=AccountManagement*'`
- `./bin/tests '--gtest_filter=DbLoader*'`
- `make test`
- Result: all green, including `240/240` full tests.

## Reviewer Status
- `Magus`: clean final review on the exploit-history cutover slice.
- `Vincent`: clean final security review on the exploit-history cutover slice.
