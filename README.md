# Return of the Shadow

This is the current live code for the MUD Return of the Shadow. The majority of
the code base is C++, but there is still some C that is used to generate the
random maze files and the PK Fame.

## Getting Started

These instructions will get you a copy of the project up and running on your
local machine for development and testing purposes. See deployment for notes on
how to deploy the project on the live system.

### Prerequisites

On your Unix based system you'll need to install the following packages.

1. gcc (This is needed to create the C files)
2. g++ (This is needed for the main game compiler)
3. clang-format (We use  this to format all the code base)
4. make (This is just something you should have in general)
5. cmake (Used by the root Makefile and the direct CMake workflow)
6. GoogleTest development files (Needed to configure and build `ageland_tests`)
7. 32-bit C/C++ development support (The game build uses `-m32`)
8. 32-bit libcrypt development files (Needed when linking the game)
9. Rust and Cargo (Needed for the proxy and `make smoke-account`)
10. python3 (Needed for the account smoke harness)

On Debian or Ubuntu, the missing build prerequisites usually look like this:

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install \
  build-essential \
  clang-format \
  cmake \
  g++-multilib \
  libc6-dev-i386 \
  libgtest-dev \
  libcrypt-dev:i386 \
  python3
```

Some distributions package the 32-bit crypt development files as
`libxcrypt-dev:i386` instead of `libcrypt-dev:i386`.

Install Rust and Cargo with rustup or your system package manager.

### Installing

Below is a step by step series that help you setup your development environment

#### Step 1: Fork the Project

First you'll need to fork this repository, and create a local clone of that
fork. [please follow these instructions on how to fork a
project](https://help.github.com/articles/fork-a-repo/)

#### Step 2: Setup the Player Files

After you have successfully forked this repository, you'll need to setup the
games files. In your terminal navigate to the local repository and run the
following commands.

```bash
make setup
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target setup
```

This will create all user folder structure that the game needs to run. This
will not important any characters to the game, so the first character created
will be promoted to a level 100 Implementor.

#### Step 3: Setting up the World Files

We keep the world files in a separate git repository to keep from having merge
conflicts with the main game code.

You'll need to fork the following repository
[https://github.com/Noobinabox/RotS-WorldFiles](https://github.com/Noobinabox/RotS-WorldFiles)

Once you have successfully forked the project, copy the files into the main
code root directory.

#### Step 4: Compiling the Game

Once all the game files are setup from Step 2 you'll need to compile the game.
In your terminal navigate to the local repository and run the following
commands.

```bash
make build
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ageland
```

> You'll see tons of notifications of deprecated functions, but the game should
> compile none the less.

This will compile all the code and create an executable called ageland in the
./bin folder.

#### Step 4a: Running the Unit Tests

For the C++ unit tests you can use either workflow.

```bash
make test
```

Or directly with CMake from the repository root:

```bash
cmake -S src -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ageland_tests
ctest --test-dir build --output-on-failure
```

#### Step 4b: Running the Account Smoke Test

The account/login smoke flow is kept separate from `make test` so unit tests stay
fast and stable. Run it manually when validating account, login, authentication,
or character-selection changes.

```bash
make smoke-account
```

The smoke harness creates and removes temporary ignored runtime data under
`lib/accounts`, `lib/players`, `lib/plrobjs`, and `lib/exploits`. Failed runs
preserve their `/tmp/rots-account-smoke-*` logs for debugging, and
`--keep-artifacts` also preserves the temporary account files.

### Production Account Verification Email

The account system sends verification codes through a local sendmail-compatible
command. By default the game executes:

```bash
/usr/sbin/sendmail -t -oi
```

You can override that command with `ROTS_SENDMAIL_COMMAND`, but the usual Ubuntu
VPS setup is to install `msmtp` as the local sendmail bridge and have it relay
through the no-reply Gmail or Google Workspace account.

#### Gmail Account Setup

1. Enable 2-Step Verification on the no-reply Google account.
2. Create an app password for the VPS mail sender.
3. Use the full no-reply email address as the SMTP username.
4. Store only the app password on the VPS; do not commit it to this repository.

Google currently requires an app password for this type of username/password SMTP
setup when 2-Step Verification is enabled. App passwords can be unavailable for
some accounts, including organization accounts with policy restrictions,
Advanced Protection, or security-key-only 2-Step Verification. Google Workspace
documents `smtp.gmail.com` with TLS port `587`, SSL port `465`, and app-password
authentication for app/device SMTP sending:

* https://support.google.com/accounts/answer/185833
* https://knowledge.workspace.google.com/admin/gmail/send-email-from-a-printer-scanner-or-app

#### Ubuntu VPS Setup With msmtp

Install the sendmail-compatible bridge:

```bash
sudo apt update
sudo apt install msmtp msmtp-mta ca-certificates
```

Create the config where the user that runs `/usr/sbin/sendmail` can read it.
For a system-wide config, create `/etc/msmtprc`:

```bash
sudo install -m 600 -o root -g root /dev/null /etc/msmtprc
sudo nano /etc/msmtprc
```

Example config:

```ini
defaults
auth           on
tls            on
tls_trust_file /etc/ssl/certs/ca-certificates.crt
logfile        /var/log/msmtp.log

account        gmail
host           smtp.gmail.com
port           587
from           no-reply@example.com
user           no-reply@example.com
password       YOUR_16_CHARACTER_APP_PASSWORD

account default : gmail
```

Keep the config and log file locked down, but readable by the runtime user. If
the game runs as root, mode `600` is enough:

```bash
sudo chmod 600 /etc/msmtprc
sudo touch /var/log/msmtp.log
sudo chmod 660 /var/log/msmtp.log
sudo chown root:adm /var/log/msmtp.log
```

If the game runs as a dedicated non-root user, the safest setup is usually a
per-user config owned by that game user:

```bash
sudo -u rots install -m 600 /dev/null /home/rots/.msmtprc
sudo -u rots nano /home/rots/.msmtprc
```

Use the same config body shown above. The file must include
`account default : gmail`. If the user running `/usr/sbin/sendmail` cannot read
any config file with that default account, `msmtp` reports:

```text
sendmail: account default not found: no configuration file available
```

Alternatively, keep `/etc/msmtprc` and grant a tightly scoped group read path to
that file for the game user.

For per-user configs, also make the `logfile` path writable by that same user.
For example:

```ini
logfile        /home/rots/.logs/msmtp.log
```

```bash
sudo -u rots mkdir -p /home/rots/.logs
sudo -u rots touch /home/rots/.logs/msmtp.log
sudo -u rots chmod 700 /home/rots/.logs
sudo -u rots chmod 600 /home/rots/.logs/msmtp.log
```

#### Validation

Send a direct test message from the VPS:

```bash
printf 'To: your-test-address@example.com\nFrom: no-reply@example.com\nSubject: RotS mail test\n\nTest from RotS VPS.\n' | /usr/sbin/sendmail -t -oi
```

Also run the same test as the actual game service user:

```bash
sudo -u rots sh -c "printf 'To: your-test-address@example.com\nFrom: no-reply@example.com\nSubject: RotS mail test\n\nTest from RotS game user.\n' | /usr/sbin/sendmail -t -oi"
```

Check delivery and the local msmtp log:

```bash
tail -n 50 /var/log/msmtp.log
```

Then run the normal account smoke flow:

```bash
make smoke-account
```

For a systemd service, the default command usually needs no environment
override. If you want the service file to be explicit, add:

```ini
Environment="ROTS_SENDMAIL_COMMAND=/usr/sbin/sendmail -t -oi"
```

#### Troubleshooting

If no verification email arrives:

1. Run the direct `/usr/sbin/sendmail -t -oi` test above from the same user that
   runs the game.
2. If you see `account default not found: no configuration file available`,
   create `~/.msmtprc` for the game user or make `/etc/msmtprc` readable by that
   user, and confirm the config includes `account default : gmail`.
3. If you see `cannot log to ... Permission denied`, change the `logfile` path
   to a file writable by the same user running `/usr/sbin/sendmail`, create its
   parent directory, or temporarily remove the `logfile` line while testing.
4. Check `/var/log/msmtp.log` or the configured per-user log file for
   authentication, TLS, or quota errors.
5. Confirm the Google account still has 2-Step Verification enabled and that the
   app password has not been revoked. Google revokes app passwords after the
   account password changes.
6. Confirm the VPS can make outbound TCP connections to `smtp.gmail.com:587`.
7. Check spam filtering on the receiving mailbox.

Gmail and Google Workspace apply sending limits and may reject suspicious
messages. Google Workspace currently documents a rolling 24-hour sending limit
for Gmail SMTP users and recommends SMTP relay for organization app/device
sending at higher scale:

* https://support.google.com/a/answer/166852
* https://knowledge.workspace.google.com/admin/gmail/send-email-from-a-printer-scanner-or-app

## GitHub Actions

This repository includes a GitHub Actions workflow that runs on pushes to
`master` and pull requests targeting `master`. It builds the game, runs the C++
unit tests, and then runs the proxy-backed account smoke flow.

If you want GitHub to block merges until those checks pass, enable branch
protection for `master` in the repository settings and mark the `Build, Unit
Tests, and Smoke Tests` job from the CI workflow as a required status check.
If you also want to block direct pushes to `master`, make sure your branch
protection or ruleset disables direct-push bypass as well.

#### Step 5: Running the Game

From the repository root you can run the following command.

```bash
make run
```

Or if you want you can still run the binary directly from the root directory

```bash
./bin/ageland -p 3791
```

Either command will start the game in the foreground and keep it attached to your terminal until you stop it.

If you want the game to expect the Rust proxy header, use the explicit proxy flag instead:

```bash
./bin/ageland -x 3791
```

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.MD) for details on our code of
conduct, and the process for submitting pull request to us.

## Releases and Design Documentation

All releases should be documented on what was changed and added. Please don't
documentation line for line what you changed but a summarization so that we can
present it to the end-users. You can find all the release notes here.

* [RotS Code Release Builds](release-notes/README.md)

Design documentation should be added to the following location.

* [RotS Design Documentation](game%20design%20docs/README.md)

## Authors

* **Seth Lyon** [Noobinabox](https://github.com/Noobinabox)
* **David Gurley** [drelidan7](https://github.com/drelidan7)
* **Andrew Humbert** [ahumbert](https://github.com/ahumbert)
