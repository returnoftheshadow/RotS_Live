# Account Forgot-Password Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a player who has forgotten their account password reset it themselves, via an emailed code offered at the moment they exhaust their five password attempts.

**Architecture:** Four new fields on `AccountData` hold a hashed, expiring reset code, kept separate from the existing email-verification code so the two cannot be cross-used. Two new functions in `account_management_identity` start and complete a reset, both keyed by email because the caller is unauthenticated. Four new connection states in `interpre.cpp`'s `nanny()` drive the menu → code → password → confirm sequence, and a new absolute-deadline field on `descriptor_data`, swept once a second, closes connections that stall.

**Tech Stack:** C++17 (built 32-bit), GoogleTest, Docker toolchain via `scripts/rots-docker.sh`.

**Spec:** `docs/superpowers/specs/2026-08-28-account-forgot-password-design.md` — read it before starting; it records *why* several of these choices are what they are.

## Global Constraints

- **Build:** `scripts/rots-docker.sh compile`. Never a native `make all` on this host — it fails at link with `cannot find -lcrypt`. If a native build was ever attempted, run `cd src && make clean` first or you get bogus ABI-mismatch link errors.
- **Tests:** `scripts/rots-docker.sh test --gtest_filter='<Suite>.<Pattern>'`.
- **Known-failing baseline:** `AccountManagement.FormatsOutOfRangeSummaryTimestampsAsInvalid` fails on this branch before any of your changes. Do not try to fix it; do not treat it as a regression.
- **Formatting:** NEVER run `make format` — it is repo-wide and reorders `account_management.cpp`'s hand-ordered `#include "account_management_*.cpp"` fragment includes, which breaks the build. Format only files you touched, by exact path: `cd src && clang-format -i -style=WebKit <file>`. **Never run clang-format on `account_management.cpp`** for the same reason; match the surrounding style by hand there.
- **`account_management_*.cpp` are code fragments, not translation units.** They are `#include`d into `account_management.cpp` in a deliberate order. Add functions to the same fragment as their neighbours.
- **Attempt caps are deliberately uniform at 5** — password attempts, verification-code attempts, reset-code attempts. Define new constants in terms of existing ones, never as bare literals.
- **Never reveal whether an address has an account.** Any branch that behaves differently for "no such account" in a way the player can observe is a bug, including via timing of state transitions.
- Commit after each task with the message given in its final step.

---

### Task 1: Reset-code fields on `AccountData`

**Files:**
- Modify: `src/account_management_types.h`
- Modify: `src/account_management_storage.cpp` (serializer, ~line 111)
- Modify: `src/account_management.cpp` (`parse_account_property`, ~line 1288)
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `AccountData::password_reset_code_hash` (`std::string`), `::password_reset_code_sent_at` (`long`), `::password_reset_code_expires_at` (`long`), `::password_reset_attempt_count` (`int`); constants `account::PASSWORD_RESET_WINDOW_SECONDS` (`long`), `account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS` (`long`), `account::MAX_PASSWORD_RESET_ATTEMPTS` (`int`).

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/account_management_tests.cpp`:

```cpp
TEST(AccountManagement, RoundTripsPasswordResetCodeMetadataThroughAccountJson)
{
    account::AccountData original_account = make_account();
    original_account.password_reset_code_hash = "reset-code-hash";
    original_account.password_reset_code_sent_at = 1700030000;
    original_account.password_reset_code_expires_at = 1700030900;
    original_account.password_reset_attempt_count = 2;

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(
        account::serialize_account_to_json(original_account), &parsed_account, &error_message))
        << error_message;

    EXPECT_EQ(parsed_account.password_reset_code_hash, "reset-code-hash");
    EXPECT_EQ(parsed_account.password_reset_code_sent_at, 1700030000);
    EXPECT_EQ(parsed_account.password_reset_code_expires_at, 1700030900);
    EXPECT_EQ(parsed_account.password_reset_attempt_count, 2);
}

TEST(AccountManagement, DefaultsPasswordResetCodeMetadataWhenAccountJsonOmitsIt)
{
    const std::string legacy_json = "{\n"
                                    "  \"version\": 1,\n"
                                    "  \"account_name\": \"alpha-admin\",\n"
                                    "  \"normalized_email\": \"player@example.com\"\n"
                                    "}\n";

    account::AccountData parsed_account;
    std::string error_message;
    ASSERT_TRUE(account::deserialize_account_from_json(legacy_json, &parsed_account, &error_message)) << error_message;

    EXPECT_TRUE(parsed_account.password_reset_code_hash.empty());
    EXPECT_EQ(parsed_account.password_reset_code_sent_at, 0);
    EXPECT_EQ(parsed_account.password_reset_code_expires_at, 0);
    EXPECT_EQ(parsed_account.password_reset_attempt_count, 0);
}

TEST(AccountManagement, KeepsResetAndVerificationAttemptCapsUniform)
{
    EXPECT_EQ(account::MAX_PASSWORD_RESET_ATTEMPTS, account::MAX_EMAIL_VERIFICATION_ATTEMPTS);
    EXPECT_EQ(account::PASSWORD_RESET_WINDOW_SECONDS, account::EMAIL_VERIFICATION_WINDOW_SECONDS);
    EXPECT_EQ(account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS, account::EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*PasswordReset*:AccountManagement.KeepsResetAndVerificationAttemptCapsUniform'`
Expected: compile failure — `'struct account::AccountData' has no member named 'password_reset_code_hash'` and `'MAX_PASSWORD_RESET_ATTEMPTS' is not a member of 'account'`.

- [ ] **Step 3: Add the constants and fields**

In `src/account_management_types.h`, after `MAX_FAILED_LOGIN_HOST_LENGTH`:

```cpp
// Deliberately defined in terms of the verification constants rather than repeating the literals:
// five password attempts, five verification attempts, five reset-code attempts.
static constexpr long PASSWORD_RESET_WINDOW_SECONDS = EMAIL_VERIFICATION_WINDOW_SECONDS;
static constexpr long PASSWORD_RESET_RESEND_COOLDOWN_SECONDS = EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS;
static constexpr int MAX_PASSWORD_RESET_ATTEMPTS = MAX_EMAIL_VERIFICATION_ATTEMPTS;
```

In `struct AccountData`, after `failed_login_last_host`:

```cpp
    std::string password_reset_code_hash;
    long password_reset_code_sent_at = 0;
    long password_reset_code_expires_at = 0;
    int password_reset_attempt_count = 0;
```

- [ ] **Step 4: Serialize the new fields**

In `src/account_management_storage.cpp`, the `failed_login_last_host` line is currently last and has no trailing comma. Give it one and append the four new fields:

```cpp
    output << "  \"failed_login_last_host\": \"" << json_utils::escape_json_string(account.failed_login_last_host) << "\",\n";
    output << "  \"password_reset_code_hash\": \"" << json_utils::escape_json_string(account.password_reset_code_hash) << "\",\n";
    output << "  \"password_reset_code_sent_at\": " << account.password_reset_code_sent_at << ",\n";
    output << "  \"password_reset_code_expires_at\": " << account.password_reset_code_expires_at << ",\n";
    output << "  \"password_reset_attempt_count\": " << account.password_reset_attempt_count << "\n";
```

- [ ] **Step 5: Parse the new fields**

In `src/account_management.cpp`, in `parse_account_property`, after the `failed_login_last_host` clause and before the final `return reader->skip_value(error_message);`:

```cpp
        if (key == "password_reset_code_hash")
            return reader->parse_string(&account->password_reset_code_hash, error_message);
        if (key == "password_reset_code_sent_at")
            return reader->parse_long(&account->password_reset_code_sent_at, error_message);
        if (key == "password_reset_code_expires_at")
            return reader->parse_long(&account->password_reset_code_expires_at, error_message);
        if (key == "password_reset_attempt_count")
            return reader->parse_integer(&account->password_reset_attempt_count, error_message);
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*PasswordReset*:AccountManagement.KeepsResetAndVerificationAttemptCapsUniform'`
Expected: 3 tests PASS.

- [ ] **Step 7: Run the whole account suite for regressions**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*'`
Expected: all pass except the known-failing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 8: Commit**

```bash
cd src && clang-format -i -style=WebKit account_management_types.h account_management_storage.cpp tests/account_management_tests.cpp && cd ..
git add src/account_management_types.h src/account_management_storage.cpp src/account_management.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): add reset-code fields to the account record

Separate from the email-verification code fields so a code mailed for one
purpose cannot be used for the other. Absent keys keep the struct defaults, so
existing account files parse unchanged."
```

---

### Task 2: `start_password_reset`

**Files:**
- Modify: `src/account_management.cpp` (add `send_password_reset_email` beside `send_verification_email`, ~line 388)
- Modify: `src/account_management_identity.h`, `src/account_management_identity.cpp`
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: Task 1's fields and constants.
- Produces:
  ```cpp
  bool account::start_password_reset(const std::string& root_directory, const std::string& email,
      long sent_at, long* code_expires_at, std::string* error_message = nullptr);
  ```
  Returns `false` only on a genuine internal failure (write or mail error for a real account). Returns `true` — writing and mailing nothing — when the address is malformed, has no account, or is inside the resend cooldown. **`*code_expires_at` is always set**, even when nothing was sent, so callers cannot leak account existence through the deadline they stamp.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/account_management_tests.cpp`:

```cpp
TEST(AccountManagement, StartPasswordResetIgnoresAddressesWithoutAnAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    long code_expires_at = 0;

    EXPECT_TRUE(account::start_password_reset(temp_directory.path(), "nobody@example.com", 1700002000, &code_expires_at, &error_message)) << error_message;

    EXPECT_EQ(code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);
    struct stat file_info {};
    EXPECT_NE(stat(account::account_file_path(temp_directory.path(), "nobody@example.com").c_str(), &file_info), 0);
}

TEST(AccountManagement, StartPasswordResetStoresAHashedCodeAndMailsIt)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path,
        "#!/bin/sh\n"
        "cat > \""
            + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long code_expires_at = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &code_expires_at, &error_message)) << error_message;

    EXPECT_EQ(code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_FALSE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_sent_at, 1700002000);
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS);
    EXPECT_EQ(stored_account.password_reset_attempt_count, 0);

    const std::string captured_mail = read_file_contents(capture_path);
    EXPECT_NE(captured_mail.find("To: player@example.com"), std::string::npos);
    EXPECT_NE(captured_mail.find("Subject: RotS account password reset code"), std::string::npos);
    EXPECT_NE(captured_mail.find("Password reset code: "), std::string::npos);
    // The plaintext code must never be what we stored.
    EXPECT_EQ(captured_mail.find(stored_account.password_reset_code_hash), std::string::npos);
}

TEST(AccountManagement, StartPasswordResetSuppressesASecondCodeInsideTheCooldown)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long first_expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &first_expiry, &error_message)) << error_message;

    account::AccountData after_first;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_first, &error_message)) << error_message;

    long second_expiry = 0;
    const long inside_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS - 1;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", inside_cooldown, &second_expiry, &error_message)) << error_message;

    account::AccountData after_second;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_second, &error_message)) << error_message;

    // The first code is untouched and still the one that works.
    EXPECT_EQ(after_second.password_reset_code_hash, after_first.password_reset_code_hash);
    EXPECT_EQ(after_second.password_reset_code_sent_at, 1700002000);
    EXPECT_EQ(second_expiry, after_first.password_reset_code_expires_at);
}

TEST(AccountManagement, StartPasswordResetIssuesAFreshCodeOnceTheCooldownLapses)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &expiry, &error_message)) << error_message;
    account::AccountData after_first;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_first, &error_message)) << error_message;

    const long past_cooldown = 1700002000 + account::PASSWORD_RESET_RESEND_COOLDOWN_SECONDS;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", past_cooldown, &expiry, &error_message)) << error_message;
    account::AccountData after_second;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_second, &error_message)) << error_message;

    EXPECT_NE(after_second.password_reset_code_hash, after_first.password_reset_code_hash);
    EXPECT_EQ(after_second.password_reset_code_sent_at, past_cooldown);
}

TEST(AccountManagement, StartPasswordResetLeavesAPendingEmailVerificationCodeAlone)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&created_account, 1700001500, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created_account, &error_message)) << error_message;

    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(root, "player@example.com", 1700002000, &expiry, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_EQ(stored_account.verification_code_hash, created_account.verification_code_hash);
    EXPECT_EQ(stored_account.verification_code_expires_at, created_account.verification_code_expires_at);
    EXPECT_NE(stored_account.password_reset_code_hash, stored_account.verification_code_hash);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.StartPasswordReset*'`
Expected: compile failure — `'start_password_reset' is not a member of 'account'`.

- [ ] **Step 3: Add the reset email body**

In `src/account_management.cpp`, immediately after `send_verification_email` (~line 398), inside the same anonymous namespace:

```cpp
    bool send_password_reset_email(const AccountData& account, const std::string& reset_code, std::string* error_message)
    {
        std::ostringstream body;
        body << "A password reset was requested for your RotS account.\n\n";
        body << "Email: " << account.normalized_email << "\n";
        body << "Password reset code: " << reset_code << "\n";
        body << "This code is valid for 15 minutes.\n\n";
        body << "If you did not request this reset, you can ignore this email. Your password has\n";
        body << "not been changed.";

        return send_email_message(account.normalized_email, "RotS account password reset code", body.str(), error_message);
    }
```

Do **not** run clang-format on this file; match the surrounding indentation by hand.

- [ ] **Step 4: Declare the function**

In `src/account_management_identity.h`, after the `clear_account_login_failures` declaration:

```cpp
// Begin a forgot-password reset for the account at this address. Malformed addresses, addresses
// with no account, and repeat requests inside the resend cooldown all return true having written
// and mailed nothing -- a failed request must never reveal whether an account exists. *code_expires_at
// is always set (to the real pending expiry when one exists, otherwise to a synthetic one), so the
// caller's timeout is identical in every case.
bool start_password_reset(const std::string& root_directory, const std::string& email, long sent_at, long* code_expires_at, std::string* error_message = nullptr);
```

- [ ] **Step 5: Implement it**

In `src/account_management_identity.cpp`, immediately before `bool start_email_verification(`:

```cpp
bool start_password_reset(const std::string& root_directory, const std::string& email, long sent_at, long* code_expires_at, std::string* error_message)
{
    // Set unconditionally up front: every early return below must leave the caller with the same
    // observable deadline, or the timeout becomes an account-existence oracle.
    if (code_expires_at != nullptr)
        *code_expires_at = sent_at + PASSWORD_RESET_WINDOW_SECONDS;

    if (!is_valid_email(email, nullptr)) {
        set_error(error_message, "");
        return true;
    }

    AccountData stored_account;
    if (!find_account_by_email_internal(root_directory, email, &stored_account, nullptr)) {
        set_error(error_message, "");
        return true;
    }

    if (stored_account.password_reset_code_sent_at != 0
        && sent_at < stored_account.password_reset_code_sent_at + PASSWORD_RESET_RESEND_COOLDOWN_SECONDS) {
        // Inside the cooldown the previous code is still pending and still works, so a player who
        // reconnected mid-flow can finish with the code already in their inbox.
        if (code_expires_at != nullptr && stored_account.password_reset_code_expires_at != 0)
            *code_expires_at = stored_account.password_reset_code_expires_at;
        set_error(error_message, "");
        return true;
    }

    const std::string generated_code = generate_numeric_verification_code();
    if (generated_code.empty()) {
        set_error(error_message, "Failed to generate a password reset code.");
        return false;
    }

    std::string reset_salt;
    if (!generate_hash_for_secret(generated_code, &stored_account.password_reset_code_hash, &reset_salt, error_message))
        return false;

    stored_account.password_reset_code_sent_at = sent_at;
    stored_account.password_reset_code_expires_at = sent_at + PASSWORD_RESET_WINDOW_SECONDS;
    stored_account.password_reset_attempt_count = 0;
    stored_account.updated_at = sent_at;

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (!send_password_reset_email(stored_account, generated_code, error_message))
        return false;

    set_error(error_message, "");
    return true;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.StartPasswordReset*'`
Expected: 5 tests PASS.

- [ ] **Step 7: Commit**

```bash
cd src && clang-format -i -style=WebKit account_management_identity.h account_management_identity.cpp tests/account_management_tests.cpp && cd ..
git add src/account_management.cpp src/account_management_identity.h src/account_management_identity.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): start a forgot-password reset by email

Mails a hashed, 15-minute code. Unknown addresses and repeat requests inside the
60-second cooldown write and mail nothing but are indistinguishable to the
caller, including in the expiry they report back."
```

---

### Task 3: Verifying and completing a reset

**Files:**
- Modify: `src/account_management_identity.h`, `src/account_management_identity.cpp`
- Test: `src/tests/account_management_tests.cpp`

**Interfaces:**
- Consumes: Task 1's fields and constants, Task 2's stored code.
- Produces:
  ```cpp
  bool account::verify_password_reset_code(const std::string& root_directory, const std::string& email,
      const std::string& reset_code, long attempted_at, std::string* error_message = nullptr);

  bool account::complete_password_reset(const std::string& root_directory, const std::string& email,
      const std::string& reset_code, const std::string& new_password, long reset_at,
      AccountData* account, std::string* error_message = nullptr);
  ```
  Both return `false` for every failure — no account, no pending code, expired code, wrong code, or (for the completing call) a password the policy rejects. On the fifth wrong code both clear the stored code. **A successful check never touches the attempt counter and never consumes the code**, which is what makes it safe to verify at the code prompt and again at completion.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/account_management_tests.cpp`:

```cpp
namespace {

// Issues a reset code and returns the plaintext by capturing the outgoing mail.
std::string issue_reset_code(const std::string& root, const std::string& capture_path, long sent_at)
{
    long expiry = 0;
    std::string error_message;
    EXPECT_TRUE(account::start_password_reset(root, "player@example.com", sent_at, &expiry, &error_message)) << error_message;

    const std::string captured_mail = read_file_contents(capture_path);
    const std::string marker = "Password reset code: ";
    const size_t code_offset = captured_mail.find(marker);
    EXPECT_NE(code_offset, std::string::npos) << "Expected a reset code in the captured mail.";
    if (code_offset == std::string::npos)
        return "";
    return captured_mail.substr(code_offset + marker.size(), 6);
}

} // namespace

TEST(AccountManagement, CompletePasswordResetChangesThePasswordAndClearsResetState)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    // Give it failed logins and an unverified address, both of which a completed reset should settle.
    ASSERT_TRUE(account::record_account_login_failure(root, "player@example.com", "attacker.example.com", 1700001500, &error_message)) << error_message;

    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    account::AccountData reset_account;
    ASSERT_TRUE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002100, &reset_account, &error_message)) << error_message;

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;

    EXPECT_TRUE(account::verify_password("BrandNew1", stored_account.password_hash));
    EXPECT_FALSE(account::verify_password("ValidPass1", stored_account.password_hash));
    EXPECT_EQ(stored_account.password_reset_by, "forgot-password");
    EXPECT_EQ(stored_account.password_reset_at, 1700002100);
    EXPECT_TRUE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_sent_at, 0);
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 0);
    EXPECT_EQ(stored_account.password_reset_attempt_count, 0);
    EXPECT_TRUE(stored_account.email_verified);
    EXPECT_EQ(stored_account.failed_login_count, 0);
    EXPECT_TRUE(stored_account.failed_login_last_host.empty());
}

TEST(AccountManagement, CompletePasswordResetCountsWrongCodesAndInvalidatesAtTheCap)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    for (int attempt = 1; attempt < account::MAX_PASSWORD_RESET_ATTEMPTS; ++attempt) {
        EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", "000000", "BrandNew1", 1700002000 + attempt, nullptr, &error_message));
        account::AccountData in_progress;
        ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &in_progress, &error_message)) << error_message;
        EXPECT_EQ(in_progress.password_reset_attempt_count, attempt);
        EXPECT_FALSE(in_progress.password_reset_code_hash.empty());
    }

    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", "000000", "BrandNew1", 1700002090, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(stored_account.password_reset_code_hash.empty());
    EXPECT_EQ(stored_account.password_reset_code_expires_at, 0);
    // The real code is dead too, so a reconnect cannot resume with it.
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002095, nullptr, &error_message));
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetRejectsAnExpiredCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    const long after_expiry = 1700002000 + account::PASSWORD_RESET_WINDOW_SECONDS + 1;
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", after_expiry, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetRejectsAddressesWithoutAnAccount)
{
    TemporaryDirectory temp_directory;
    std::string error_message;
    EXPECT_FALSE(account::complete_password_reset(temp_directory.path(), "nobody@example.com", "000000", "BrandNew1", 1700002000, nullptr, &error_message));
}

TEST(AccountManagement, CompletePasswordResetRejectsAnEmailVerificationCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;

    std::string verification_code;
    ASSERT_TRUE(account::prepare_email_verification_code(&created_account, 1700001500, &verification_code, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_file(root, created_account, &error_message)) << error_message;

    // No reset has been started, so a verification code must not stand in for one.
    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", verification_code, "BrandNew1", 1700002000, nullptr, &error_message));

    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}

TEST(AccountManagement, CompletePasswordResetRejectsAPasswordFailingPolicy)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_FALSE(account::complete_password_reset(root, "player@example.com", reset_code, "short", 1700002100, nullptr, &error_message));

    // The code survives so the player can retry with a better password.
    account::AccountData stored_account;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &stored_account, &error_message)) << error_message;
    EXPECT_FALSE(stored_account.password_reset_code_hash.empty());
    EXPECT_TRUE(account::verify_password("ValidPass1", stored_account.password_hash));
}
```

```cpp
TEST(AccountManagement, VerifyPasswordResetCodeAcceptsWithoutConsumingTheCode)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_TRUE(account::verify_password_reset_code(root, "player@example.com", reset_code, 1700002050, &error_message)) << error_message;

    account::AccountData after_verify;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_verify, &error_message)) << error_message;
    EXPECT_EQ(after_verify.password_reset_attempt_count, 0);
    EXPECT_FALSE(after_verify.password_reset_code_hash.empty());

    // The code still completes the reset afterwards -- checking it did not spend it.
    EXPECT_TRUE(account::complete_password_reset(root, "player@example.com", reset_code, "BrandNew1", 1700002100, nullptr, &error_message)) << error_message;
}

TEST(AccountManagement, VerifyPasswordResetCodeCountsWrongCodesLikeTheCompletingCall)
{
    TemporaryDirectory temp_directory;
    const std::string root = temp_directory.path();
    const std::string capture_path = root + "/captured-mail.txt";
    const std::string command_script_path = root + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData created_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(root, "alpha-admin", "player@example.com", "ValidPass1", 1700001000, &created_account, &error_message)) << error_message;
    const std::string reset_code = issue_reset_code(root, capture_path, 1700002000);
    ASSERT_FALSE(reset_code.empty());

    EXPECT_FALSE(account::verify_password_reset_code(root, "player@example.com", "000000", 1700002050, &error_message));

    account::AccountData after_verify;
    ASSERT_TRUE(account::read_account_file(root, "alpha-admin", &after_verify, &error_message)) << error_message;
    EXPECT_EQ(after_verify.password_reset_attempt_count, 1);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*PasswordResetCode*:AccountManagement.CompletePasswordReset*'`
Expected: compile failure — `'complete_password_reset' is not a member of 'account'`.

- [ ] **Step 3: Declare both functions**

In `src/account_management_identity.h`, after `start_password_reset`:

```cpp
// Check a reset code WITHOUT consuming it, so the player learns a code is wrong at the code prompt
// rather than after typing a new password twice. A wrong code costs an attempt exactly as the
// completing call would; a correct one changes nothing at all.
bool verify_password_reset_code(const std::string& root_directory, const std::string& email, const std::string& reset_code, long attempted_at, std::string* error_message = nullptr);

// Finish a forgot-password reset. Fails for an unknown address, a missing or expired code, a wrong
// code, or a password the policy rejects. The fifth wrong code clears the stored code so a
// reconnect cannot resume against it. On success the reset state is cleared, the address is marked
// verified (mailbox control was just proven) and the failed-login tally is reset.
bool complete_password_reset(const std::string& root_directory, const std::string& email, const std::string& reset_code, const std::string& new_password, long reset_at, AccountData* account, std::string* error_message = nullptr);
```

- [ ] **Step 4: Implement the shared check and both functions**

In `src/account_management_identity.cpp`, immediately after `start_password_reset`. The shared
helper is what guarantees the two entry points cannot drift apart in how they count attempts:

```cpp
namespace {

// Shared by verify_password_reset_code and complete_password_reset so the two can never disagree
// about what counts as a failure. Loads the account, checks the code, and charges an attempt on
// mismatch (clearing the code at the cap). Writes only on mismatch -- a correct code leaves the
// stored record untouched.
bool check_password_reset_code(const std::string& root_directory, const std::string& email, const std::string& reset_code, long attempted_at, AccountData* stored_account, std::string* error_message)
{
    if (!is_valid_email(email, nullptr)) {
        set_error(error_message, "That reset code is invalid.");
        return false;
    }

    if (!find_account_by_email_internal(root_directory, email, stored_account, nullptr)) {
        set_error(error_message, "That reset code is invalid.");
        return false;
    }

    if (stored_account->password_reset_code_hash.empty() || stored_account->password_reset_code_expires_at == 0) {
        set_error(error_message, "That reset code is invalid.");
        return false;
    }

    if (attempted_at > stored_account->password_reset_code_expires_at) {
        set_error(error_message, "That reset code has expired.");
        return false;
    }

    const std::string trimmed_code = trim_copy(reset_code);
    if (trimmed_code.empty() || !verify_password(trimmed_code, stored_account->password_reset_code_hash)) {
        ++stored_account->password_reset_attempt_count;
        stored_account->updated_at = attempted_at;

        const bool exhausted = stored_account->password_reset_attempt_count >= MAX_PASSWORD_RESET_ATTEMPTS;
        if (exhausted) {
            stored_account->password_reset_code_hash.clear();
            stored_account->password_reset_code_sent_at = 0;
            stored_account->password_reset_code_expires_at = 0;
        }

        write_account_file(root_directory, *stored_account, nullptr);
        set_error(error_message, exhausted ? "Too many invalid reset codes." : "That reset code is invalid.");
        return false;
    }

    set_error(error_message, "");
    return true;
}

} // namespace

bool verify_password_reset_code(const std::string& root_directory, const std::string& email, const std::string& reset_code, long attempted_at, std::string* error_message)
{
    AccountData stored_account;
    return check_password_reset_code(root_directory, email, reset_code, attempted_at, &stored_account, error_message);
}

bool complete_password_reset(const std::string& root_directory, const std::string& email, const std::string& reset_code, const std::string& new_password, long reset_at, AccountData* account, std::string* error_message)
{
    AccountData stored_account;
    if (!check_password_reset_code(root_directory, email, reset_code, reset_at, &stored_account, error_message))
        return false;

    // Validate the password only after the code is known good, so a rejected password never tells
    // an attacker their guessed code was right.
    if (!is_valid_password(new_password, error_message))
        return false;

    if (!reset_account_password(&stored_account, new_password, "forgot-password", reset_at, error_message))
        return false;

    stored_account.password_reset_code_hash.clear();
    stored_account.password_reset_code_sent_at = 0;
    stored_account.password_reset_code_expires_at = 0;
    stored_account.password_reset_attempt_count = 0;

    // Receiving the code proves control of the address, which also unsticks an account that never
    // finished verification.
    if (!stored_account.email_verified)
        verify_email(&stored_account, "forgot-password", reset_at);

    // These failures were the player's own, on the way to this reset; leaving them would greet them
    // with a warning about themselves.
    stored_account.failed_login_count = 0;
    stored_account.failed_login_last_at = 0;
    stored_account.failed_login_last_host.clear();

    if (!write_account_file(root_directory, stored_account, error_message))
        return false;

    if (account != nullptr)
        *account = stored_account;

    set_error(error_message, "");
    return true;
}
```

Note `verify_email` resets `updated_at` to `reset_at`, which is what we want.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*PasswordResetCode*:AccountManagement.CompletePasswordReset*'`
Expected: 8 tests PASS.

- [ ] **Step 6: Run the whole account suite**

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*'`
Expected: all pass except the known-failing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 7: Commit**

```bash
cd src && clang-format -i -style=WebKit account_management_identity.h account_management_identity.cpp tests/account_management_tests.cpp && cd ..
git add src/account_management_identity.h src/account_management_identity.cpp src/tests/account_management_tests.cpp
git commit -m "feat(account): verify and complete a forgot-password reset

Both entry points share one check, so they cannot drift apart in how they count
attempts. Checking a code does not consume it, which lets the code prompt reject
a wrong code immediately instead of after the player types a new password twice.

Five wrong codes invalidate the code outright, so reconnecting buys no fresh
attempts against it. Success also marks the address verified and clears the
failed-login tally the player themselves ran up."
```

---

### Task 4: Connection-state deadlines

**Files:**
- Modify: `src/structs.h` (new `CON_` defines after `CON_ACCTVERIFY` ~line 2000; `descriptor_data` ~line 2027)
- Modify: `src/comm.cpp` (new `check_state_deadlines()` near `check_pre_login_idle()` ~line 691; call site in the `pulse % 4` block ~line 1124; init in `new_descriptor` ~line 1518)
- Modify: `src/comm.h` (declaration)
- Test: `src/tests/interpre_account_menu_tests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `CON_ACCTPWDFAIL` (40), `CON_ACCTFORGOTCODE` (41), `CON_ACCTFORGOTNEW` (42), `CON_ACCTFORGOTCNF` (43); `descriptor_data::state_deadline` (`time_t`, absolute, `0` = none); `void check_state_deadlines(time_t now)` declared in `comm.h`.

This is a generic mechanism — any state can stamp a deadline. Tasks 5–7 use it. The state constants
are defined here rather than with the states that use them so this task's timeout messages can name
them and the task stands alone.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/interpre_account_menu_tests.cpp`, inside the existing anonymous namespace (before the closing `} // namespace`):

```cpp
TEST(InterpreAccountMenu, StateDeadlineClosesTheConnectionOnceItPasses)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data* descriptor = allocate_descriptor();
    descriptor->descriptor = 7;
    descriptor->connected = CON_ACCTPWD;
    descriptor->state_deadline = 1700002000;
    descriptor_list = descriptor;

    check_state_deadlines(1700002000);

    EXPECT_EQ(descriptor->connected, CON_CLOSE);
    EXPECT_EQ(descriptor->state_deadline, 0);
    EXPECT_NE(std::string(descriptor->output).find("Timed out."), std::string::npos);
}

TEST(InterpreAccountMenu, StateDeadlineLeavesConnectionsAloneBeforeItPasses)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data* descriptor = allocate_descriptor();
    descriptor->descriptor = 7;
    descriptor->connected = CON_ACCTPWD;
    descriptor->state_deadline = 1700002000;
    descriptor_list = descriptor;

    check_state_deadlines(1700001999);

    EXPECT_EQ(descriptor->connected, CON_ACCTPWD);
    EXPECT_EQ(descriptor->state_deadline, 1700002000);
    EXPECT_EQ(std::string(descriptor->output), "");
}

TEST(InterpreAccountMenu, StateDeadlineIgnoresDescriptorsWithoutOne)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data* descriptor = allocate_descriptor();
    descriptor->descriptor = 7;
    descriptor->connected = CON_ACCTMENU;
    descriptor->state_deadline = 0;
    descriptor_list = descriptor;

    check_state_deadlines(1900000000);

    EXPECT_EQ(descriptor->connected, CON_ACCTMENU);
    EXPECT_EQ(std::string(descriptor->output), "");
}

TEST(InterpreAccountMenu, StateDeadlineNamesTheExpiredCodeInTheForgotPasswordStates)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data* descriptor = allocate_descriptor();
    descriptor->descriptor = 7;
    descriptor->connected = CON_ACCTFORGOTNEW;
    descriptor->state_deadline = 1700002000;
    descriptor_list = descriptor;

    check_state_deadlines(1700002001);

    EXPECT_EQ(descriptor->connected, CON_CLOSE);
    EXPECT_NE(std::string(descriptor->output).find("That reset code has expired."), std::string::npos);
}
```

Add the declaration to the `extern`/free-function block at the top of the file, beside `int process_input(struct descriptor_data* t);`:

```cpp
void check_state_deadlines(time_t now);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.StateDeadline*'`
Expected: compile failure — `'struct descriptor_data' has no member named 'state_deadline'`, and an undefined reference to `check_state_deadlines`.

- [ ] **Step 3: Add the connection states and the descriptor field**

In `src/structs.h`, after `#define CON_ACCTVERIFY 39`:

```cpp
#define CON_ACCTPWDFAIL 40
#define CON_ACCTFORGOTCODE 41
#define CON_ACCTFORGOTNEW 42
#define CON_ACCTFORGOTCNF 43
```

In `struct descriptor_data`, immediately after `int bad_pws;`:

```cpp
    time_t state_deadline; /* absolute time the current connection state expires; 0 = none */
```

- [ ] **Step 4: Initialize it for new connections**

In `src/comm.cpp`, in `new_descriptor`, beside `pnewd->bad_pws = 0;`:

```cpp
    pnewd->state_deadline = 0;
```

- [ ] **Step 5: Implement the sweep**

In `src/comm.cpp`, immediately after `check_pre_login_idle()`:

```cpp
/*
** Close connections whose current state carried an absolute deadline. Distinct from
** check_pre_login_idle(), which measures idleness from last input -- a deadline here is fixed when
** the state is entered, so typing at a prompt cannot extend it.
*/
void check_state_deadlines(time_t now)
{
    descriptor_data *point, *next_point;

    for (point = descriptor_list; point; point = next_point) {
        next_point = point->next;

        if (point->state_deadline == 0 || now < point->state_deadline)
            continue;

        point->state_deadline = 0;

        // The three forgot-password states are all bounded by the code's own expiry, so name the
        // real reason rather than a generic timeout.
        if (point->connected == CON_ACCTFORGOTCODE || point->connected == CON_ACCTFORGOTNEW
            || point->connected == CON_ACCTFORGOTCNF)
            SEND_TO_Q("\n\rThat reset code has expired.\n\r", point);
        else
            SEND_TO_Q("\n\rTimed out.\n\r", point);

        STATE(point) = CON_CLOSE;
    }
}
```

`CON_CLOSE` descriptors are swept later in the same `game_loop` iteration, after output is written, so the message flushes before the socket closes.

- [ ] **Step 6: Declare it**

In `src/comm.h`, beside the other free-function declarations:

```cpp
void check_state_deadlines(time_t now);
```

- [ ] **Step 7: Call it once a second**

In `src/comm.cpp`'s `game_loop`, in the existing `if (!(pulse % 4))` block (one second), after the skill-timer update:

```cpp
            check_state_deadlines(time(0));
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.StateDeadline*'`
Expected: 4 tests PASS.

- [ ] **Step 9: Commit**

```bash
cd src && clang-format -i -style=WebKit structs.h comm.cpp comm.h tests/interpre_account_menu_tests.cpp && cd ..
git add src/structs.h src/comm.cpp src/comm.h src/tests/interpre_account_menu_tests.cpp
git commit -m "feat(comm): absolute per-state connection deadlines

Swept once a second. Unlike check_pre_login_idle(), which measures from last
input, a deadline set here is fixed when the state is entered, so typing at a
prompt cannot extend it indefinitely."
```

---

### Task 5: The post-exhaustion menu

**Files:**
- Modify: `src/interpre.cpp` (`CON_ACCTPWD` five-strike branch ~line 3119; new `CON_ACCTPWDFAIL` case in `nanny()`)
- Test: `src/tests/interpre_account_menu_tests.cpp`

**Interfaces:**
- Consumes: Task 2's `start_password_reset`, Task 4's `state_deadline` and `CON_` states.
- Produces: `ACCOUNT_RESET_MENU_TIMEOUT_SECONDS` (90) in `interpre.cpp`; `show_account_reset_menu(struct descriptor_data* d)`.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/interpre_account_menu_tests.cpp`:

```cpp
TEST(InterpreAccountMenu, FifthWrongAccountPasswordOffersTheResetMenuInsteadOfDisconnecting)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTPWD;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "host.example.com");

    char wrong_password[] = "WrongPass1";
    for (int attempt = 0; attempt < 5; ++attempt) {
        descriptor.connected = CON_ACCTPWD;
        nanny(&descriptor, wrong_password);
    }

    EXPECT_EQ(descriptor.connected, CON_ACCTPWDFAIL);
    const std::string output = descriptor.output;
    EXPECT_NE(output.find("1) Reset your account password"), std::string::npos);
    EXPECT_NE(output.find("0) Disconnect"), std::string::npos);
    EXPECT_NE(descriptor.state_deadline, 0);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ResetMenuChoiceZeroDisconnects)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTPWDFAIL;
    descriptor.state_deadline = 1900000000;
    char choice[] = "0";

    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);
    EXPECT_EQ(descriptor.state_deadline, 0);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ResetMenuRedrawsOnInvalidInputWithoutExtendingTheDeadline)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTPWDFAIL;
    descriptor.state_deadline = 1900000000;
    char choice[] = "banana";

    nanny(&descriptor, choice);

    EXPECT_EQ(descriptor.connected, CON_ACCTPWDFAIL);
    EXPECT_EQ(descriptor.state_deadline, 1900000000);
    EXPECT_NE(std::string(descriptor.output).find("1) Reset your account password"), std::string::npos);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ResetMenuChoiceOneAdvancesToTheCodePromptForAnUnknownAddress)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTPWDFAIL;
    descriptor.state_deadline = 1900000000;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "nobody@example.com");
    char choice[] = "1";

    nanny(&descriptor, choice);

    // Identical to the real-account path: same message, same next state, a deadline from the code.
    EXPECT_EQ(descriptor.connected, CON_ACCTFORGOTCODE);
    EXPECT_NE(std::string(descriptor.output).find("If an account exists for that address"), std::string::npos);
    EXPECT_NE(descriptor.state_deadline, 0);
    EXPECT_NE(descriptor.state_deadline, 1900000000);

    close(descriptor.descriptor);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.*ResetMenu*:InterpreAccountMenu.FifthWrong*'`
Expected: all 4 FAIL. The states exist (Task 4) but nothing drives them — the fifth wrong password still sets `CON_CLOSE`, and `CON_ACCTPWDFAIL` input falls through `nanny()`'s switch leaving `connected` unchanged.

- [ ] **Step 3: Add the menu renderer**

In `src/interpre.cpp`, in the same anonymous-namespace block as `show_account_menu` (before `bool account_has_restricting_active_linked_session`):

```cpp
// How long the post-exhaustion reset menu stays open. Provisional -- already far more generous than
// the instant disconnect a fifth wrong password causes today.
static constexpr int ACCOUNT_RESET_MENU_TIMEOUT_SECONDS = 90;

void show_account_reset_menu(struct descriptor_data* d)
{
    SEND_TO_Q("\n\r"
              "1) Reset your account password\n\r"
              "0) Disconnect\n\r"
              "\n\r"
              "This menu will close in 90 seconds.\n\r"
              "Choice: ",
        d);
}
```

- [ ] **Step 4: Replace the five-strike disconnect**

In `src/interpre.cpp`, in the `CON_ACCTPWD` wrong-password branch, replace:

```cpp
                if (++(d->bad_pws) >= 5) {
                    SEND_TO_Q("Invalid account credentials... disconnecting.\n\r", d);
                    STATE(d) = CON_CLOSE;
                } else {
```

with:

```cpp
                if (++(d->bad_pws) >= 5) {
                    SEND_TO_Q("Invalid account credentials.\n\r", d);
                    d->state_deadline = time(0) + ACCOUNT_RESET_MENU_TIMEOUT_SECONDS;
                    show_account_reset_menu(d);
                    STATE(d) = CON_ACCTPWDFAIL;
                } else {
```

- [ ] **Step 5: Handle the menu**

In `src/interpre.cpp`, in `nanny()`, immediately after the `case CON_ACCTVERIFY:` block:

```cpp
    case CON_ACCTPWDFAIL: /* password attempts exhausted -- offer a reset */
        for (; isspace(*arg); arg++)
            continue;

        if (*arg == '0') {
            d->state_deadline = 0;
            SEND_TO_Q("Goodbye.\n\r", d);
            STATE(d) = CON_CLOSE;
            return;
        }

        if (*arg == '1') {
            long code_expires_at = 0;
            mudlog_account_event(d, "Account password reset requested");
            // The return value is deliberately not branched on: whether a code was sent, suppressed
            // by the cooldown, or skipped because no account exists must not be observable here.
            account::start_password_reset(kAccountStorageRoot, d->account_email, time(0), &code_expires_at, nullptr);

            d->bad_pws = 0;
            d->state_deadline = code_expires_at;
            SEND_TO_Q("\n\rIf an account exists for that address, a reset code has been sent to it.\n\r"
                      "The code is valid for 15 minutes.\n\r"
                      "\n\rReset code: ",
                d);
            STATE(d) = CON_ACCTFORGOTCODE;
            return;
        }

        show_account_reset_menu(d);
        return;
```

Note the deadline is **not** re-stamped on the redraw — that is the point of an absolute deadline.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.*ResetMenu*:InterpreAccountMenu.FifthWrong*'`
Expected: 4 tests PASS.

- [ ] **Step 7: Commit**

```bash
cd src && clang-format -i -style=WebKit interpre.cpp tests/interpre_account_menu_tests.cpp && cd ..
git add src/interpre.cpp src/tests/interpre_account_menu_tests.cpp
git commit -m "feat(account): offer a password reset when attempts are exhausted

The fifth wrong password now opens a 90-second menu instead of disconnecting
outright. The menu, its message, and the state it leads to are identical whether
or not the address has an account behind it."
```

---

### Task 6: The reset-code prompt

**Files:**
- Modify: `src/interpre.cpp` (new `CON_ACCTFORGOTCODE` case)
- Test: `src/tests/interpre_account_menu_tests.cpp`

**Interfaces:**
- Consumes: Task 3's `verify_password_reset_code`, Task 4's `state_deadline` and `CON_` states.
- Produces: a descriptor in `CON_ACCTFORGOTNEW` with the verified reset code held in `d->account_character_name`.

**Why the code is held on the descriptor rather than consumed here:** `verify_password_reset_code`
checks it without spending it, and `complete_password_reset` (Task 7) re-checks it when the new
password is applied. A successful check never touches the attempt counter, so checking twice costs
the player nothing — and this way a wrong code is reported at the code prompt instead of after they
have typed a new password twice.

**Why `account_character_name` and not `account_password`:** `d->account_password` is needed to hold
the new password across `CON_ACCTFORGOTNEW` → `CON_ACCTFORGOTCNF`, so the code needs its own slot.
`account_character_name` is a `MAX_INPUT_LENGTH` scratch field already used for pending account
actions and is unused in this flow.

- [ ] **Step 1: Add the two test helpers this file is missing**

`src/tests/interpre_account_menu_tests.cpp` already has `write_text_file`, `ScopedEnvironmentVariable`,
`ScopedWorkingDirectory`, and `TemporaryDirectory`, but it does **not** have `make_file_executable` or
`read_file_contents` — the tests below need both to capture the mailed reset code. Copy them from
`src/tests/account_management_tests.cpp` into this file's existing anonymous namespace, beside
`write_text_file`:

```cpp
std::string read_file_contents(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr) << "Expected test helper to open fixture file: " << path;
    if (file == nullptr)
        return "";

    std::string contents;
    char buffer[256];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            EXPECT_EQ(std::ferror(file), 0) << "Expected test helper to read fixture file cleanly: " << path;
            break;
        }
    }

    EXPECT_EQ(std::fclose(file), 0);
    return contents;
}

void make_file_executable(const std::string& path)
{
    ASSERT_EQ(chmod(path.c_str(), 0700), 0)
        << "Expected test helper to mark fixture file executable: " << path;
}
```

Ensure `<sys/stat.h>` is included (it already is, for `mkdir`).

- [ ] **Step 2: Write the failing tests**

Append to `src/tests/interpre_account_menu_tests.cpp`.

**Every test that calls `account::start_password_reset` MUST set `ROTS_SENDMAIL_COMMAND` first.** The
docker test image has no MTA, so an unmocked call execs a nonexistent sendmail, exits 127, and the
`ASSERT_TRUE` fails. Tests that need the code capture it with a script; tests that only need the send
to succeed can use the file's existing lighter idiom, `ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", "/bin/true");`.

```cpp
TEST(InterpreAccountMenu, CorrectResetCodeAdvancesToTheNewPasswordPrompt)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    const std::string capture_path = temp_directory.path() + "/captured-mail.txt";
    const std::string command_script_path = temp_directory.path() + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(".", "player@example.com", time(0), &expiry, &error_message)) << error_message;
    const std::string captured_mail = read_file_contents(capture_path);
    const std::string marker = "Password reset code: ";
    const size_t code_offset = captured_mail.find(marker);
    ASSERT_NE(code_offset, std::string::npos);
    std::string mailed_code = captured_mail.substr(code_offset + marker.size(), 6);

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTCODE;
    descriptor.state_deadline = expiry;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");

    std::vector<char> code_buffer(mailed_code.begin(), mailed_code.end());
    code_buffer.push_back('\0');
    nanny(&descriptor, code_buffer.data());

    EXPECT_EQ(descriptor.connected, CON_ACCTFORGOTNEW);
    EXPECT_EQ(std::string(descriptor.account_character_name), mailed_code);
    EXPECT_NE(std::string(descriptor.output).find("New account password:"), std::string::npos);
    // The code's own expiry keeps bounding the connection through the password prompts.
    EXPECT_EQ(descriptor.state_deadline, expiry);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, WrongResetCodeIsReportedImmediatelyAndRePrompts)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", "/bin/true");

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(".", "player@example.com", time(0), &expiry, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTCODE;
    descriptor.state_deadline = expiry;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    char wrong_code[] = "000000";

    nanny(&descriptor, wrong_code);

    EXPECT_EQ(descriptor.connected, CON_ACCTFORGOTCODE);
    EXPECT_NE(std::string(descriptor.output).find("Reset code: "), std::string::npos);
    EXPECT_TRUE(std::string(descriptor.account_character_name).empty());

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, FifthWrongResetCodeDisconnects)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", "/bin/true");

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    long expiry = 0;
    ASSERT_TRUE(account::start_password_reset(".", "player@example.com", time(0), &expiry, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTCODE;
    descriptor.state_deadline = expiry;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");

    char wrong_code[] = "000000";
    for (int attempt = 0; attempt < account::MAX_PASSWORD_RESET_ATTEMPTS; ++attempt) {
        if (descriptor.connected != CON_ACCTFORGOTCODE)
            break;
        nanny(&descriptor, wrong_code);
    }

    EXPECT_EQ(descriptor.connected, CON_CLOSE);
    EXPECT_NE(std::string(descriptor.output).find("Too many invalid reset codes."), std::string::npos);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ResetCodePromptDisconnectsOnEmptyInput)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTCODE;
    char empty_input[] = "";

    nanny(&descriptor, empty_input);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);

    close(descriptor.descriptor);
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.*ResetCode*'`
Expected: all 4 FAIL — `CON_ACCTFORGOTCODE` falls through `nanny()`'s switch, so `connected` never changes.

- [ ] **Step 4: Handle the code prompt**

In `src/interpre.cpp`, in `nanny()`, after the `CON_ACCTPWDFAIL` case:

```cpp
    case CON_ACCTFORGOTCODE: /* reset code from the email */
        echo_on(d->descriptor);

        for (; isspace(*arg); arg++)
            continue;

        if (!*arg) {
            close_socket(d);
            return;
        }

        {
            std::string error_message;
            // Checked without being consumed: complete_password_reset re-checks it when the new
            // password is applied. A correct code never charges an attempt, so checking twice is
            // free -- and a wrong one is reported here rather than after two password prompts.
            if (!account::verify_password_reset_code(kAccountStorageRoot, d->account_email, arg, time(0), &error_message)) {
                *d->account_character_name = '\0';

                if (error_message.find("Too many") != std::string::npos
                    || error_message.find("expired") != std::string::npos) {
                    d->state_deadline = 0;
                    SEND_TO_Q(("\n\r" + error_message + "\n\rPlease reconnect and try again.\n\r").c_str(), d);
                    STATE(d) = CON_CLOSE;
                    return;
                }

                SEND_TO_Q(("\n\r" + error_message + "\n\rReset code: ").c_str(), d);
                return;
            }
        }

        strncpy(d->account_character_name, arg, sizeof(d->account_character_name) - 1);
        d->account_character_name[sizeof(d->account_character_name) - 1] = '\0';

        SEND_TO_Q("\n\rNew account password: ", d);
        echo_off(d->descriptor);
        STATE(d) = CON_ACCTFORGOTNEW;
        return;
```

The generic "That reset code is invalid." message is identical whether the address has an account,
has no pending code, or simply got the digits wrong — the same non-enumeration rule as everywhere
else in this flow.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.*ResetCode*'`
Expected: 4 tests PASS.

- [ ] **Step 6: Commit**

```bash
cd src && clang-format -i -style=WebKit interpre.cpp tests/interpre_account_menu_tests.cpp && cd ..
git add src/interpre.cpp src/tests/interpre_account_menu_tests.cpp
git commit -m "feat(account): prompt for the mailed reset code

Verified without being consumed, so a wrong code is reported at this prompt
rather than after the player has typed a new password twice. The prompt lives
exactly as long as the code does."
```

---

### Task 7: New password, confirmation, and completion

**Files:**
- Modify: `src/interpre.cpp` (`CON_ACCTFORGOTNEW`, `CON_ACCTFORGOTCNF` cases)
- Test: `src/tests/interpre_account_menu_tests.cpp`

**Interfaces:**
- Consumes: Task 3's `complete_password_reset`, Task 6's verified code held in `d->account_character_name`.
- Produces: nothing downstream — this terminates the flow at `CON_CLOSE`.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/interpre_account_menu_tests.cpp`:

```cpp
TEST(InterpreAccountMenu, ForgotPasswordHappyPathResetsThePasswordAndDisconnects)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    const std::string capture_path = temp_directory.path() + "/captured-mail.txt";
    const std::string command_script_path = temp_directory.path() + "/capture-sendmail.sh";
    write_text_file(command_script_path, "#!/bin/sh\ncat > \"" + capture_path + "\"\n");
    make_file_executable(command_script_path);
    ScopedEnvironmentVariable sendmail_override("ROTS_SENDMAIL_COMMAND", command_script_path);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_verify_email(".", "acct", "test", 1700010201, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTPWDFAIL;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "host.example.com");

    char request[] = "1";
    nanny(&descriptor, request);
    ASSERT_EQ(descriptor.connected, CON_ACCTFORGOTCODE);

    const std::string captured_mail = read_file_contents(capture_path);
    const std::string marker = "Password reset code: ";
    const size_t code_offset = captured_mail.find(marker);
    ASSERT_NE(code_offset, std::string::npos);
    std::string mailed_code = captured_mail.substr(code_offset + marker.size(), 6);

    std::vector<char> code_buffer(mailed_code.begin(), mailed_code.end());
    code_buffer.push_back('\0');
    nanny(&descriptor, code_buffer.data());
    ASSERT_EQ(descriptor.connected, CON_ACCTFORGOTNEW);

    char new_password[] = "BrandNew1";
    nanny(&descriptor, new_password);
    ASSERT_EQ(descriptor.connected, CON_ACCTFORGOTCNF);

    char confirm_password[] = "BrandNew1";
    nanny(&descriptor, confirm_password);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);
    EXPECT_NE(std::string(descriptor.output).find("Please log in again with your new password."), std::string::npos);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("BrandNew1", reloaded_account.password_hash));
    EXPECT_TRUE(reloaded_account.password_reset_code_hash.empty());

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ForgotPasswordRejectsAPasswordFailingPolicyWithoutAdvancing)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTNEW;
    char weak_password[] = "short";

    nanny(&descriptor, weak_password);

    EXPECT_EQ(descriptor.connected, CON_ACCTFORGOTNEW);
    EXPECT_NE(std::string(descriptor.output).find("New account password:"), std::string::npos);

    close(descriptor.descriptor);
}

TEST(InterpreAccountMenu, ForgotPasswordMismatchedConfirmationReturnsToTheNewPasswordPrompt)
{
    ScopedDescriptorListReset descriptor_list_reset;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTNEW;

    char new_password[] = "BrandNew1";
    nanny(&descriptor, new_password);
    ASSERT_EQ(descriptor.connected, CON_ACCTFORGOTCNF);

    char mismatch[] = "Different1";
    nanny(&descriptor, mismatch);

    EXPECT_EQ(descriptor.connected, CON_ACCTFORGOTNEW);
    EXPECT_NE(std::string(descriptor.output).find("Passwords don't match"), std::string::npos);

    close(descriptor.descriptor);
}

// Defence in depth: the code was already checked at the prompt, but completion must re-check rather
// than trust whatever the descriptor is carrying.
TEST(InterpreAccountMenu, ForgotPasswordCompletionReVerifiesTheCodeRatherThanTrustingTheDescriptor)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedDescriptorListReset descriptor_list_reset;
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    account::AccountData stored_account;
    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "acct", "player@example.com", "ValidPass1", 1700010200, &stored_account, &error_message)) << error_message;

    descriptor_data descriptor = make_descriptor();
    descriptor.descriptor = open("/dev/null", O_WRONLY);
    ASSERT_GE(descriptor.descriptor, 0);
    descriptor.connected = CON_ACCTFORGOTNEW;
    std::snprintf(descriptor.account_email, sizeof(descriptor.account_email), "%s", "player@example.com");
    std::snprintf(descriptor.account_character_name, sizeof(descriptor.account_character_name), "%s", "000000");

    char new_password[] = "BrandNew1";
    nanny(&descriptor, new_password);
    char confirm_password[] = "BrandNew1";
    nanny(&descriptor, confirm_password);

    EXPECT_EQ(descriptor.connected, CON_CLOSE);

    account::AccountData reloaded_account;
    ASSERT_TRUE(account::read_account_file(".", "acct", &reloaded_account, &error_message)) << error_message;
    EXPECT_TRUE(account::verify_password("ValidPass1", reloaded_account.password_hash));

    close(descriptor.descriptor);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.ForgotPassword*'`
Expected: all 4 FAIL — `CON_ACCTFORGOTNEW` and `CON_ACCTFORGOTCNF` fall through `nanny()`'s switch, leaving `connected` unchanged.

- [ ] **Step 3: Handle the new-password and confirmation states**

In `src/interpre.cpp`, in `nanny()`, after the `CON_ACCTFORGOTCODE` case:

```cpp
    case CON_ACCTFORGOTNEW: /* new password after a verified reset code */
        echo_on(d->descriptor);

        for (; isspace(*arg); arg++)
            continue;

        if (!*arg || strlen(arg) > MAX_ACCOUNT_PASSWORD_LENGTH) {
            SEND_TO_Q("\n\rIllegal password.\n\rNew account password: ", d);
            *d->account_password = '\0';
            echo_off(d->descriptor);
            return;
        }

        {
            std::string error_message;
            if (!account::is_valid_password(arg, &error_message)) {
                SEND_TO_Q(("\n\r" + error_message + "\n\rNew account password: ").c_str(), d);
                *d->account_password = '\0';
                echo_off(d->descriptor);
                return;
            }
        }

        strncpy(d->account_password, arg, MAX_ACCOUNT_PASSWORD_LENGTH);
        d->account_password[MAX_ACCOUNT_PASSWORD_LENGTH] = '\0';
        SEND_TO_Q("\n\rRetype the new password: ", d);
        echo_off(d->descriptor);
        STATE(d) = CON_ACCTFORGOTCNF;
        return;

    case CON_ACCTFORGOTCNF: /* confirm the new password and apply the reset */
        echo_on(d->descriptor);

        for (; isspace(*arg); arg++)
            continue;

        if (strcmp(arg, d->account_password)) {
            SEND_TO_Q("\n\rPasswords don't match... start over.\n\rNew account password: ", d);
            *d->account_password = '\0';
            echo_off(d->descriptor);
            STATE(d) = CON_ACCTFORGOTNEW;
            return;
        }

        {
            const std::string reset_code = d->account_character_name;
            const std::string new_password = d->account_password;
            *d->account_password = '\0';
            *d->account_character_name = '\0';
            d->state_deadline = 0;

            account::AccountData account_data;
            std::string error_message;
            if (!account::complete_password_reset(kAccountStorageRoot, d->account_email, reset_code,
                    new_password, time(0), &account_data, &error_message)) {
                mudlog_account_event(d, "Account password reset failed");
                SEND_TO_Q("\n\rThat reset code is not valid.\n\rPlease reconnect and try again.\n\r", d);
                STATE(d) = CON_CLOSE;
                return;
            }

            const bool had_active_session
                = !active_account_character_sessions(d, account_data).empty();
            mudlog_account_event(d,
                had_active_session ? "Account password reset completed (session active)"
                                   : "Account password reset completed",
                account_data.normalized_email.c_str());

            SEND_TO_Q("\n\rYour password has been updated.\n\r"
                      "Please log in again with your new password.\n\r",
                d);
            STATE(d) = CON_CLOSE;
            return;
        }
```

Active sessions are deliberately **not** disconnected — a forced link-drop can get a character killed and their gear lost. The log line is the whole response.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.ForgotPassword*'`
Expected: 4 tests PASS.

- [ ] **Step 5: Run both full suites**

Run: `scripts/rots-docker.sh test --gtest_filter='InterpreAccountMenu.*'`
Expected: all pass.

Run: `scripts/rots-docker.sh test --gtest_filter='AccountManagement.*'`
Expected: all pass except the known-failing `FormatsOutOfRangeSummaryTimestampsAsInvalid`.

- [ ] **Step 6: Build the server**

Run: `scripts/rots-docker.sh compile`
Expected: links to `bin/ageland` with no errors.

- [ ] **Step 7: Manual smoke test**

Local `sendmail` does not exist, and only the code's hash is stored, so the code cannot be read out of the account file. Capture the outgoing mail instead:

```bash
cat > /tmp/rots-capture-mail.sh <<'SH'
#!/bin/sh
cat >> /tmp/rots-mail.txt
SH
chmod +x /tmp/rots-capture-mail.sh
ROTS_SENDMAIL_COMMAND=/tmp/rots-capture-mail.sh ./bin/ageland 4444
```

Then, from another terminal, telnet to port 4444 and:
1. Enter `clauded3bugbot@example.com`, then five wrong passwords. Confirm the menu appears instead of a disconnect.
2. Wait 90 seconds without typing. Confirm the disconnect. Reconnect.
3. Reach the menu again, press `1`, read the code from `/tmp/rots-mail.txt`.
4. Enter a wrong code four times, then the right one. Confirm it is accepted (the cap is five).
5. Set `BrandNew1`, confirm it, and check the disconnect message.
6. Reconnect and log in with `BrandNew1`. Confirm no failed-login banner appears.
7. Reset the test account's password back to `TestPass123!` afterwards.

- [ ] **Step 8: Commit**

```bash
cd src && clang-format -i -style=WebKit interpre.cpp tests/interpre_account_menu_tests.cpp && cd ..
git add src/interpre.cpp src/tests/interpre_account_menu_tests.cpp
git commit -m "feat(account): apply the forgot-password reset and disconnect

The code and the new password are verified in one call, so a wrong code costs
one attempt rather than one per password the player then tries. Active sessions
on the account are logged, not disconnected."
```

---

## Notes for the reviewer

- **The enumeration property is the thing most likely to be broken by a well-meaning change.** Any branch in Tasks 5–7 that behaves observably differently for "no such account" — different message, different next state, different deadline, an early return — defeats the feature's whole design. `start_password_reset` returning `true` for unknown addresses is deliberate, not a swallowed error.
- **`d->bad_pws` is cleared when the reset is requested** (Task 5, Step 6) so a player who abandons the reset and reconnects is not immediately re-exhausted. It is *not* cleared on the menu redraw.
- **Attempt caps are five everywhere on purpose.** If a reviewer asks why the reset cap is not smaller, the answer is uniformity, decided deliberately.
