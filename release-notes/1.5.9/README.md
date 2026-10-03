# Release Notes 1.5.9

The following has been added and or changed:

## Account Login

The login flow now starts with your account email address instead of a character name.

New and still-unverified accounts are sent a verification code through the configured local mailer
before entering the account menu.

Verified accounts can list linked characters, play a linked character, create a new character,
add an existing character, reset the account password, and log out from the account menu.

Existing legacy characters can be migrated into an account after confirming the old character
password.

## Account Characters

Account-backed characters can return from the character menu to the account menu, and
account-backed deletion now uses the account password.

Account reconnect handling is safer. An active linked character level 91 or below blocks alternate
character selection and account-menu new-character creation, while accounts with an active linked
character level 92 or higher can still switch characters.

Account-backed saves and migration now preserve more character state, including color settings,
combat speed and stance settings, equipment, object saves, and exploit history.

## Colors

The `color` command now supports foreground and background ANSI, RGB, and hex colors, and both
`color` and `colour` are accepted.

New `magic` and `weather` color categories were added, and spellcasting announcements now use the
configured magic color.

## Telnet

Direct telnet connections now receive the greeting and account prompt immediately after connecting.

## Account Administration

Account administration tools were updated for eligible staff, including account status management,
password resets, character linking, legacy character migration, and account-session review.
