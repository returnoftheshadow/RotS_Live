# Account forgot-password reset — design

**Date:** 2026-08-28
**Status:** approved, not yet implemented
**Depends on:** `feat/account-failed-login-count` (`b4bf3d2`), which added the failed-login fields
this design clears on a completed reset.

## Why

A player who forgets their account password currently has no way back in. The account menu's
"5) Reset account password" requires the *current* password, so it only helps someone already
logged in. Everyone else exhausts five guesses, gets disconnected, and has to find an immortal to
run `admin_reset_password` by hand.

This adds a self-service path at the one moment we know the player needs it: the point where they
have just run out of password attempts.

## Flow

After the fifth rejected password, instead of `Invalid account credentials... disconnecting.`, the
connection lands on a menu:

```
1) Reset your account password
0) Disconnect

This menu will close in 90 seconds.
Choice:
```

Pressing `1` mails a numeric code to the account's address and prompts for it. Five wrong codes
invalidates the code and disconnects. The correct code prompts for a new password, then a
confirmation, then disconnects with a message to log in again with the new password.

Pressing `0`, or letting the deadline pass, disconnects.

## Connection states

`CON_ACCTVERIFY` is currently the highest at 39; these continue from 40.

| State | Prompt | Transitions |
|---|---|---|
| `CON_ACCTPWDFAIL` | the menu above | `1` → send code, → `CON_ACCTFORGOTCODE` · `0` → disconnect · other → re-show menu (deadline is **not** extended) · deadline → disconnect |
| `CON_ACCTFORGOTCODE` | `Reset code: ` | correct → `CON_ACCTFORGOTNEW` · wrong → re-prompt · 5th wrong → invalidate code, disconnect · empty → disconnect · code expiry reached → say so, disconnect |

Every failure at `CON_ACCTFORGOTCODE` prints the same line and the fifth disconnects, whatever the
cause and whether or not the address has an account. The count that drives that is kept on the
descriptor: an address with no account has no file to count on, so a cap read out of stored state
would silently never fire for exactly the case the flow exists to hide. The account layer keeps its
own persistent cap in parallel — that is what kills the code across a reconnect — but it never
decides what the player sees.
| `CON_ACCTFORGOTNEW` | `New account password: ` | passes `is_valid_password` → `CON_ACCTFORGOTCNF` · fails → re-prompt with the policy error |
| `CON_ACCTFORGOTCNF` | `Retype the new password: ` | match → persist, disconnect · mismatch → back to `CON_ACCTFORGOTNEW` |

The existing five-strike counter (`d->bad_pws`) is what triggers the menu; its threshold does not
change.

## Approach: separate reset-code fields

Considered and rejected:

- **Reuse the email-verification code fields.** No new schema, but a pending signup verification and
  a pending reset would clobber each other, and a code mailed for one purpose would be usable for
  the other. Codes should be purpose-bound.
- **Generalize into one code with a `purpose` tag.** Least duplication, but a reset would still
  clobber a pending verification, and it means refactoring live production auth code rather than
  adding beside it.

**Chosen: separate fields and functions, built on the verification path's existing primitives.**
Purpose-bound, additive, and it leaves the working verification path untouched.

### Data model

Four fields on `AccountData` (`account_management_types.h`), mirroring the verification set:

```cpp
std::string password_reset_code_hash;
long password_reset_code_sent_at = 0;
long password_reset_code_expires_at = 0;
int password_reset_attempt_count = 0;
```

Serialized in `account_management_storage.cpp`, parsed in `account_management.cpp`'s
`parse_account_property`. As with the failed-login fields, absent keys keep the struct defaults, so
account files written before this change parse unchanged — no schema bump, no migration.

### Constants

```cpp
static constexpr long PASSWORD_RESET_WINDOW_SECONDS = EMAIL_VERIFICATION_WINDOW_SECONDS;          // 15 min
static constexpr long PASSWORD_RESET_RESEND_COOLDOWN_SECONDS = EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS; // 60s
static constexpr int MAX_PASSWORD_RESET_ATTEMPTS = MAX_EMAIL_VERIFICATION_ATTEMPTS;               // 5
```

Defined in terms of the verification constants rather than repeating the literals, so the
uniformity is deliberate and visible: five password attempts, five verification attempts, five
reset-code attempts.

### New functions (`account_management_identity.{h,cpp}`)

```cpp
bool start_password_reset(root, email, sent_at, code_expires_at, error_message = nullptr);
bool verify_password_reset_code(root, email, code, attempted_at, error_message = nullptr);
bool complete_password_reset(root, email, code, new_password, reset_at, account, error_message = nullptr);
```

Both key off the **email**, not the account name, because the caller is unauthenticated and only
ever has the address the player typed.

`start_password_reset`:
1. Invalid email, or no account for it → return `true`, write nothing, mail nothing.
2. A reset code sent less than `PASSWORD_RESET_RESEND_COOLDOWN_SECONDS` ago → return `true`, mail
   nothing.
3. Otherwise generate a code with `generate_numeric_verification_code()`, hash it with
   `generate_hash_for_secret()`, stamp the three timestamp/expiry fields, zero the attempt count,
   write the account file, and mail it with a new `send_password_reset_email()` alongside the
   existing `send_verification_email()`.

`verify_password_reset_code` checks a code without consuming it, so the player is told immediately
that a code is wrong rather than after typing a new password twice:
1. No account, no pending code, or an expired code → generic failure.
2. Mismatch → increment `password_reset_attempt_count`, persist; at `MAX_PASSWORD_RESET_ATTEMPTS`,
   clear the hash and expiry so the code is dead even across a reconnect.
3. Match → success, and **nothing is written** — the code stays pending for the completing call.

`complete_password_reset` runs the same checks and then applies the change:
1. Same failure cases as above, including the attempt increment — so it is safe to call directly.
2. Match → apply `reset_account_password(&account, new_password, "forgot-password", reset_at)`,
   clear the reset-code fields, set `email_verified` (see below), clear the failed-login fields,
   and write once.

Because a successful check never touches the attempt counter, verifying at the prompt and again at
completion costs the player nothing. The only thing that can change between the two is expiry, which
the deadline handles.

The existing `prepare_email_verification_code` / `confirm_email_verification_code` are the shape to
follow but are **not** reused directly — `confirm_email_verification_code` short-circuits on
`email_verified` and calls `verify_email()`, neither of which is right here. The genuinely shared
primitives (`generate_numeric_verification_code`, `generate_hash_for_secret`, `verify_password`)
are called directly.

## Not confirming whether an account exists

The menu appears after five wrong passwords whether or not the address has an account, and
pressing `1` always prints the same line:

```
If an account exists for that address, a reset code has been sent to it.
```

Pressing `1` **always** advances to the code prompt — whether a code was mailed, suppressed by the
cooldown, or skipped because no account exists. The transition never depends on the outcome, since
that is exactly what would leak the account's existence. When the cooldown suppressed the send, the
code from the earlier send is still pending and still works, so a player who pressed `1` twice in
quick succession can complete the reset with the first code they received.

With no account behind the address, nothing is mailed and the code prompt cannot be satisfied; the
player burns five attempts and is disconnected, indistinguishable from wrong codes. This preserves
the property the login path already has, and that the failed-login recording in `b4bf3d2` was
written to protect.

Two costs, accepted deliberately:

- A player who typo'd their own address is walked through a reset that could never succeed, and the
  message cannot tell them why.
- If `sendmail` fails for a real account, that player hits the same dead end.

Both are logged for the immortals (below), so they are diagnosable from the log even though the
player cannot be told.

## The menu deadline

`descriptor_data` gains one field:

```cpp
time_t state_deadline; /* absolute; 0 = none */
```

Set when `CON_ACCTPWDFAIL` is entered, cleared on every transition out of it. It must be an
absolute deadline rather than a comparison against `last_input_time`, or typing junk at the menu
would reset the clock indefinitely.

**90 seconds**, stated in the menu text. Provisional — kept because it is already far more generous
than the instant disconnect the fifth wrong password causes today, but revisit if it proves too
quick in practice. Nothing depends on the value; raising it is a one-constant change.

`check_pre_login_idle()` (`comm.cpp:691`) already reaps characterless descriptors after 15 minutes,
but it is swept once a minute (`comm.cpp:1112`), so a 90-second deadline checked there would
actually fire between 90 and 150 seconds. The deadline check therefore goes on the existing
one-second pulse block instead — a short walk of a small list, negligible cost.

### The code prompt is not deadlined — it lives exactly as long as the code

The 90-second deadline is cleared on entry to `CON_ACCTFORGOTCODE` and no shorter one replaces it.
Disconnecting someone who is holding a valid code destroys the code's value: getting back to the
prompt costs five more failed passwords, and by then the cooldown may have lapsed and issued a
different code anyway. So the connection outlives the menu deadline by design.

The generic 15-minute reaper is *not* the right backstop for it either, because it measures from
last input: a wrong code typed at minute 10 keeps the connection alive to minute 25, stranding the
player at a prompt whose code expired at minute 15. Instead `CON_ACCTFORGOTCODE` reuses the same
one-second deadline check, seeded from the account's `password_reset_code_expires_at` rather than a
fixed offset. When it passes the player is told the code expired and disconnected — at which point
nothing is lost, because the code was already dead.

The same code-expiry deadline carries through `CON_ACCTFORGOTNEW` and `CON_ACCTFORGOTCNF`. The code
is verified at the prompt but not consumed there — it is re-checked when the new password is
applied — so it can still lapse while the player is choosing a password. Closing the connection at
expiry is better than letting them type a password twice only to have the reset rejected. The whole
flow is therefore bounded by exactly one thing: how long the code is good for.

### Reconnecting mid-flow does not strand the player

A player whose link drops after the code is mailed can reconnect, fail five passwords, press `1`
again, and — inside the 60-second cooldown — be prompted for the code they already received, since
the suppressed send leaves the original code pending. Past the cooldown they are simply mailed a
fresh one. Either way the flow is recoverable without immortal intervention.

## Active sessions are not disconnected

If a session is already playing on the account, a completed reset does **not** kick it. Production
allows one active login per player and enforces it socially, but a forced link-drop can get a
character killed and their gear lost — a worse outcome than the one being prevented. Immortals also
double-log deliberately when troubleshooting.

Instead, both mudlog events record whether a session was live on the account at the time, using the
existing `active_account_character_sessions()` helper. That is the visibility substituting for
eviction.

## Logging

Via the existing `mudlog_account_event`:

- `Account password reset requested` — on pressing `1`, with the address and host. Also logged when
  suppressed by the cooldown, when no account exists, and when the mail send fails, each
  distinguishable in the log but never to the player.
- `Account password reset completed` — on success, with whether a session was active.

## Error handling

- Empty input at the code prompt disconnects, matching `CON_ACCTPWD`.
- A failed account-file write during `complete_password_reset` leaves the old password in place and
  reports a generic failure; the code stays valid so the player can retry.
- The new password goes through `is_valid_password` and the existing `MAX_ACCOUNT_PASSWORD_LENGTH`
  cap, the same policy as account creation and the authenticated reset.

## Testing

Unit (`src/tests/account_management_tests.cpp`):

- Round-trip of the four new fields; an account file lacking them parses to defaults.
- `start_password_reset`: unknown email writes nothing; cooldown suppresses a second send; a fresh
  send stamps hash/expiry and zeroes the attempt count.
- `complete_password_reset`: wrong code increments and persists; the fifth wrong code clears the
  hash and expiry; an expired code fails; the correct code changes the password hash, clears the
  reset fields, sets `email_verified`, and clears the failed-login fields.
- `verify_password_reset_code`: a correct code succeeds without consuming the code or touching the
  attempt count, so the completing call still works; a wrong one increments exactly as the
  completing call would.
- A code mailed for verification cannot complete a reset, and vice versa.

Integration (`src/tests/interpre_account_menu_tests.cpp`, driving `nanny()` as the existing account
tests do):

- The fifth wrong password lands on `CON_ACCTPWDFAIL` rather than `CON_CLOSE`.
- `0` disconnects; invalid input re-shows the menu without extending the deadline.
- The full happy path across all four states ends at `CON_CLOSE` with the new password persisted.
- An address with no account reaches the code prompt and prints the identical message.

Manual: boot on a spare port against the debug account per the established smoke-test pattern.
Local `sendmail` is absent, so the send failure path is what gets exercised locally; the code can be
read out of the account file to drive the rest of the flow.

## Deliberately out of scope

No lockout, no rate limit on password guessing, and no throttle on reconnecting for another five
attempts. That question was parked on 2026-08-28 pending discussion among other stakeholders and is
unchanged by this work. The 60-second reset-email cooldown here is narrowly about not letting this
feature become an inbox-flooding tool, not a general answer to brute force.
