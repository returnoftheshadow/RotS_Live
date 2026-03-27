# Features to Add

I'd currently like to add an account management system to the game. Accounts should now be email-first: at login the player is prompted for an account name, and that account name is the player's email address. If the account does not exist yet, the login flow should offer to create it and set a secure password with a minimum of 8 characters, upper and lower case, and a number. After logging into the account, the player should land in an account menu where they can list their characters, add an existing legacy character, create a new character, reset the account password, or play a linked character. When they add a pre-existing character it should verify the legacy character password, transform the character file, character object, and character exploit file to json, and store it in a new directory linked back to the account. The account files should also be in json and stored in the similar fashion of how player files are stored now with the alphabet being split up.

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
10. Reset password flow:
   - prompt for existing account password
   - prompt for new password
   - prompt to confirm the new password
11. Play character flow:
   - select a linked character
   - enter the world using the current character login behavior after selection

## Execution Breakdown

Current repo/storage notes:
- Character files are currently stored in `lib/players/<bucket>/<name>`.
- Character exploit files are currently stored in `lib/exploits/<bucket>/<name>.exploits`.
- Character object save data is currently stored in `lib/plrobjs/<bucket>/`.
- Existing player data is split into alphabet buckets (`A-E`, `F-J`, `K-O`, `P-T`, `U-Z`, `ZZZ`), so account storage should likely follow the same pattern for consistency.

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

- Added a standalone account-management module with JSON read/write support for accounts and migrated character snapshots.
- Added secure account password hashing and verification using `libcrypt`.
- Added bucketed account storage under `lib/accounts/...` and account-linked character snapshot storage under `lib/account_characters/...`.
- Added file-backed account creation, authentication, password reset, block/unblock, and character-link helpers with focused unit coverage.
- Added admin account-management commands for showing accounts, blocking/unblocking, resetting passwords, linking characters, and forcing migration.
- Added player-side `linkaccount` support with a masked password prompt instead of raw command-line password entry.
- Added transitional live login support for account authentication and linked-character selection.
- Added email validation, email-based account lookup, email-based authentication, and account creation from an email address as groundwork for the new email-first login flow.
- Added duplicate-link protection across accounts, migration-first linking to avoid stale partial links, duplicate-email detection that fails closed, and resilient email lookup when an unrelated account file is corrupt.
- Added account verification metadata, emailed verification-code generation, 15-minute expiry tracking, and confirmation helpers.
- Added outbound verification email delivery through the local `sendmail` interface.
- Updated the live login flow so new and pending accounts email a verification code, accept `RESEND` / `CANCEL`, and require code entry before entering the account menu.
- Hid verification-code entry from snoops the same way other secret prompts are masked.
- Added persistent verification-attempt tracking, verification-code invalidation after too many bad tries, and resend cooldown protection for emailed codes.
- Hardened account creation to fail closed if stored account records are unreadable, so email uniqueness cannot be bypassed through corrupted account data.
- Added account-snapshot refresh helpers so linked characters can self-heal a missing migration snapshot and refresh account storage from current legacy files.
- Added migration-restore helpers so account-selected play can reconstruct legacy player/object/exploit files from account storage before loading the character.
- Added snapshot-decoding helpers and a direct player-text load path so account-backed character selection can parse player data from account storage without first recreating the legacy player file.
- Updated runtime flows so account-created characters are immediately linked and migrated, account-selected play requires account snapshot readiness and restores from account storage before loading, and normal character saves refresh linked account snapshots.
- Hardened account-backed cutover behavior so corrupt existing snapshots self-heal from legacy files, duplicate linked-character ownership fails closed, runtime support-file restore validates snapshot identity before writing, and malformed snapshot player text is rejected safely during direct account-backed load.
- Cut exploit history one step farther away from legacy login-time restore by removing exploit-file restoration from account-backed play, teaching exploit reads to fall back to account snapshots when the runtime file is absent, and seeding new runtime exploit files from snapshot data when gameplay appends fresh records.
- Expanded and kept passing focused `AccountManagement` unit coverage for the foundation work, including verification-code success, invalid-code, expired-code, resend-cooldown, and pending-auth cases.
- Added regression coverage for corrupt snapshot rebuilds, duplicate linked-character ownership, snapshot-identity restore mismatches, malformed player-text decoding, exploit-history fallback/append behavior when the legacy exploit file is missing, corrupt runtime exploit-file self-healing, temp-file conflict failure, and preserving exploit history across snapshot refreshes.
- Ran `make test` and confirmed the full C++ unit test suite passes locally at 240/240.
- Ran the required `Magus` quality review and `Vincent` security review for this exploit-history cutover slice, then addressed their findings before finalizing the pass.

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
  - Define JSON schema for migrated character snapshots and how they reference account ownership.
  - Decide whether migrated data lives under the account directory or under separate bucketed directories with back-references.

- [x] Build serialization/deserialization support in the server:
  - Add helpers for reading/writing account JSON safely.
  - Add helpers for exporting existing character file, object save file, and exploit file into JSON.
  - Add validation and error handling for missing/malformed JSON files.
  - Ensure writes are atomic enough to avoid partial migrations.
  - Add unit tests for valid reads/writes, malformed input, missing files, and partial-write safeguards where testable.

- [~] Implement account creation:
  - Add the creation flow directly to the login prompt when an email account is missing.
  - Enforce unique email-based account identifiers.
  - Enforce password complexity: minimum 8 chars, at least one uppercase letter, one lowercase letter, and one number.
  - Store passwords securely using one-way hashing plus salt.
  - Add unit tests for email normalization/uniqueness checks, password complexity, and account creation success/failure paths.
  Status: backend helpers and the login-prompt create-on-miss flow are wired into `nanny()`, new accounts are created as unverified, account creation immediately sends a verification code by email, and creation now fails closed if account storage is unreadable; remaining work is broader interactive smoke coverage.

- [~] Implement account authentication:
  - Add login prompts/state transitions for email-first account lookup and password entry.
  - Validate credentials against stored account JSON.
  - Add failure handling, lockout/throttling considerations, and clear player messaging.
  - Prevent blocked accounts from authenticating or entering the game according to the chosen policy.
  - Add unit tests for successful login, failed login, blocked-account handling, and password verification behavior.
  Status: the live login prompt is now email-first, authenticates against account JSON, sends emailed verification codes for pending accounts, requires a valid unexpired code before entering the account menu, rate-limits resend attempts, and invalidates codes after repeated failures; remaining work is deeper interactive smoke coverage.

- [~] Build the account menu workflow:
  - Add an account menu shown immediately after successful login or account creation.
  - Add a simple "list characters" option that prints the linked character names.
  - Add a "play character" option that selects one linked character and enters the world.
  - Add an "add existing character" option that prompts for legacy character name and legacy character password.
  - Add a "create new character" option that bridges into the existing character-creation flow under the authenticated account.
  - Add a "reset password" option that prompts for old password, new password, and confirmation.
  - Add unit tests for account-menu helper logic where practical and smoke test the full menu flow locally.
  Status: the live menu flow is now wired into `nanny()` with all requested options and sits behind verified-email gating; remaining work is better automated coverage and local socket-level smoke testing.

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
  - Convert all three into JSON and store them in the new account-linked format.
  - Record linkage metadata so the account can list/select the character later.
  - Verify the legacy character's existing password before migrating/linking it through the account menu flow.
  - Add unit tests for successful migration, duplicate-link prevention, missing legacy file handling, password verification, and rollback/error behavior where practical.
  Status: migration/link helpers, duplicate-link protection, rollback protection, legacy-character password verification, snapshot refresh helpers, and immediate migration for account-created characters are now in place; remaining work is the final sanitized migrated player JSON and any deeper operator-facing migration policy.

- [~] Update runtime character selection flow:
  - After account login, enter the account menu instead of dropping directly to character selection.
  - Allow selecting a linked character from the account menu to enter the world.
  - Define behavior for accounts with zero linked characters from the menu.
  - Preserve compatibility with current descriptor/login state machinery.
  - Add unit tests for account-with-no-characters, valid character selection, and invalid/unlinked character selection paths where practical.
  Status: account-authenticated login now lands in the account menu, handles zero-character accounts, can play linked characters from there, requires account snapshot readiness for linked-character play, loads player data directly from account storage, restores only the still-needed object support file before account-backed play, serves exploit history from account storage when no runtime exploit file exists, preserves account-backed exploit history across ordinary saves, self-heals corrupt snapshots and corrupt runtime exploit files, and now fails closed on duplicate ownership or snapshot-identity mismatches; remaining work is fully retiring the object support-file dependency and adding deeper interactive smoke coverage.

- [ ] Handle migration and backward compatibility:
  - Decide whether new characters are created directly under accounts.
  - Decide how renamed/deleted characters affect linked account data.
  - Add guardrails to prevent duplicate links or partial conversions.
  - Plan how legacy files are retained, archived, or retired after successful migration.
  - Define how admin-added character links interact with legacy standalone character login rules.

- [ ] Add validation and tests:
  - Treat unit tests as part of each implementation slice, not a final pass-only task.
  - Unit tests for password validation and account schema parsing.
  - Unit tests for bucket/path resolution for accounts.
  - Unit tests for legacy-to-JSON migration helpers.
  - Unit tests for blocked-account behavior.
  - Unit tests for admin permission checks and admin account actions.
  - Smoke test the login/account/character-selection flow locally.
  - Smoke test administrator workflows for block, password reset, character listing, and character linking.

- [ ] Document the feature:
  - Update help text/admin notes for account creation and linking.
  - Document administrator account-management commands/workflows.
  - Document new file locations and migration behavior for operators.

## Suggested Delivery Order

1. Finalize account schema and migration rules.
2. Implement account JSON storage helpers plus their unit tests.
3. Implement password validation and secure hashing plus their unit tests.
4. Implement account creation/login flow plus their unit tests.
5. Build the account menu, password reset flow, and simple character listing.
6. Implement character link + migration flow plus their unit tests.
7. Wire play-character and new-character creation into the account menu flow plus unit tests where practical.
8. Implement administrator account-management tools plus their unit tests.
9. Add docs, smoke tests, and close any remaining test gaps.
