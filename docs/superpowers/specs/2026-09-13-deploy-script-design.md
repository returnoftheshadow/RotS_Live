# Deploy Script — Design

Date: 2026-09-13
Status: **Implemented on feat/deploy-script; manual verification on zzz-forge-test pending.**
Branch: `feat/deploy-script` (worktree `~/u/games/RotS_Live_deploy-script`), based on
`release-frodo` (a482ac7).

## What this changes

Deploying to a RotS port today is a hand-run sequence of `git pull`, `ssh`, `sftp`, and `make`
commands. This adds `scripts/deploy.py`, which runs that same sequence in order, stops at the first
failure, and tags the deployed commit. It also uploads the help files from `lib/text`, which
deploys sometimes need to carry and which today are copied by hand if at all.

## Scope

**In:** `deploy <env>` for the four ports plus test targets, `--dry-run`, source upload and build,
and help-file upload.

**Out, and why:**

- *Restarting a port, except test.* `deploy test --restart` runs `sudo systemctl restart
  rotsbuilding` at the end (step 9); every other env refuses `--restart`, and without the flag
  nothing restarts. There is no separate `restart <env>` subcommand. (If live/coders get it later:
  `systemctl restart rotslive|rotscoding` per `docs/Running the Game.md`; 4k has no service. Help
  tables alone can also be picked up without a restart via in-game `reload xhelp`.)
- *Deploying or reverting live, 4k, or coders.* For now only `test`, `zzz-forge-test` and
  `zzz-forge-test-4k` are approved; `deploy` and `revert` on any other env stop with a usage error
  before anything runs, dry runs included. Their table entries stay so they can be approved later.
- *Other `lib/text` files.* `motd`, `news`, `wizlist`, `immlist`, `bugs`, `typos`, `ideas`, etc. are
  owned by the server (the repo copies date from 2018–2020, and `../bin/autowiz` rewrites the
  wizlists). They are never uploaded.
- *Deleting remote files.* A help file removed from the repo is not removed from the server.
- *ssh keys.* There are none. The one password prompt per run is intentional and serves as the
  confirmation step — no separate "are you sure" prompt.
- *Third-party packages.* Standard library only (Python 3.10), driving the system `ssh`/`sftp`.

## Environments

Every env is on the one server named by the required `<user>@<host>` and `<ssh-port>` arguments
(see **Usage**). Remote port dir is `/rots/<dir>`; source is `<dir>/src`,
help files are `<dir>/lib/text`.

| env                 | dir                | banner color | backup | tag prefix | source edits          | approved | `--restart`    |
|---------------------|--------------------|--------------|--------|------------|-----------------------|----------|----------------|
| `live`              | `live-default3791` | bold red     | yes    | `live-`    | —                     | **no**   | —              |
| `4k`                | `live-pkarena4000` | bold magenta | yes    | `4k-`      | `USE_BIG_BROTHER` 1→0 | **no**   | —              |
| `test`              | `dev-building4802` | yellow       | yes    | `test-`    | —                     | yes      | `rotsbuilding` |
| `coders`            | `dev-coding4810`   | green        | **no** | `coders-`  | —                     | **no**   | —              |
| `zzz-forge-test`    | `zzz-forge-test`   | cyan         | yes    | none       | —                     | yes      | —              |
| `zzz-forge-test-4k` | `zzz-forge-test`   | cyan         | yes    | none       | `USE_BIG_BROTHER` 1→0 | yes      | —              |

Coders keeps no backups (`docs/Running the Game.md`). `zzz-forge-test-4k` exists so the 4k source
edit can be exercised without touching the 4k port.

This table is a single dictionary at the top of the script. Colors are ANSI and are disabled when
stdout is not a terminal.

## Help files

**Uploaded on every deploy:** every git-tracked file in `lib/text` whose name ends in `_tbl`, plus
`lib/text/help` (the plain `HELP` page). Today that is `help`, `help_tbl`, `spel_tbl`, `pray_tbl`,
`skil_tbl`, `spec_tbl`, `wizh_tbl`, `shap_tbl`, `scr_tbl`, `msdp_tbl`, `mudl_tbl`, `mdl_tbl`. The set
is computed from `git ls-files` at run time, so tables added by the help overhaul are included
without changing the script. Uploading all of them every time means every env matches the repo from
its first scripted deploy on, with no need to know what the server had before.

**Format check, before connecting:** applies to the help chapters the game indexes — the
`"text/<name>"` entries of `help_content[]` in `src/consts.cpp`, parsed at run time. The game's
parser (`build_help_index`, `modify.cpp:723`) and `do_help` (`act_info.cpp:2094`) treat **any** line
starting with `#` as the end of an entry, and the index loop never terminates without `#~`. Each
chapter file must:

- exist and be tracked;
- end with a `#~` line;
- have no line starting with `#` other than `#`, `# ` (trailing whitespace), or `#~`.

A failure names the file and line and stops the deploy. `mdl_tbl` and `mudl_tbl` are not help
chapters (whole-file reads, or unreferenced), so they are uploaded but not checked.

Known at time of writing: `lib/text/help_tbl:92` is `#RRGGBB.` (from f26d314), which truncates
`HELP COLOR` in-game and orphans the rest of that entry. This check fails on it, so it must be fixed
before the first scripted deploy.

## Usage

```
scripts/deploy.py deploy <env> <user>@<host> <ssh-port>
scripts/deploy.py deploy <env> <user>@<host> <ssh-port> --dry-run
scripts/deploy.py deploy test <user>@<host> <ssh-port> --restart
scripts/deploy.py revert <env> <user>@<host> <ssh-port>
```

`<env>` must be an approved env (see **Environments**); `--restart` is only accepted for `test`.

`revert` puts an env with a backup back to its `src/backup`: it asks for the ssh password once,
restores `src/` and the help files, forces a relink, clears `src/DEPLOY_IN_PROGRESS`, and checks
that `bin/ageland` was rebuilt. It refuses coders, which keeps no backup.

`<user>@<host>` and `<ssh-port>` are required positional arguments with no defaults, so the repo
never records the login account or the server's ssh port. A missing or malformed value (a login
without exactly one `@`, a non-numeric port) stops with a usage message before anything else runs.
Deployers keep their full command in their own notes.

`--dry-run` runs the local checks (step 1's tree checks and the help format check) and prints every
remaining step with its exact command. It runs nothing on the server and nothing that changes the
local checkout (no pull, no tag fetch, tag, or push).

The script deploys the checkout it lives in: `src/` and `lib/text/` next to the `scripts/` directory
holding it. In normal use that is `RotS_Live_DEPLOY`.

## Steps

Each step checks its result; any failure stops the run, skips to step 10, and names the failed step.

**Local**

1. **Pull and check.** Stop if `git status --porcelain` shows uncommitted changes, or if `src/`
   contains untracked or ignored files whose names do not start with `.` (these would be uploaded by
   `put -r *`; dotfiles such as `src/.remember/` are not matched by `*`). Stop if the branch is not
   `release-frodo` — for `test` and `zzz-forge-test*` this is only a warning, so those ports can be
   deployed from a feature branch (with no pull; `test` is still tagged). Then `git pull --ff-only`; record the commit SHA and subject. Then compute the
   help-file set and run the format check (see **Help files**).
2. **Banner, then connect.** Print the target (`<user>@<host>:/rots/<dir>`, dir in the env's
   color), the commit, any source edits, and the help files to upload. Then open an OpenSSH master
   connection (`ssh -M -S <socket> -fN -p <ssh-port> <user>@<host>`); this is the only password prompt.
   The socket lives in a fresh private (0700) temp directory. All later `ssh` and `sftp` calls pass
   `-S`/`-o ControlPath=<socket>` and do not prompt.

**Remote (ssh), commands run as `cd /rots/<dir>/src && ...`**

Before any remote command, the script asserts the port dir matches `^/rots/[a-z0-9-]+$`, and every
remote command string is built with `shlex.quote`.

3. **Pre-check.** Confirm `src`, `bin`, and `lib/text` exist under `/rots/<dir>`. Refuse the deploy
   if `src`, `bin`, `lib/text`, or any symlink under them resolves outside `/rots/<dir>` (the tool
   never touches a file outside the port's game dir; chown uses `-h`, so it never follows a link,
   and `revert` makes the same check). List paths `<user>` cannot write: everything in `src` and
   `bin`, `lib/text` itself, and each help file that already exists there. If one of the three
   folders itself is unwritable, stop and say to fix it by hand — the deploy never changes the
   folders themselves, and nothing asks for a sudo password. Otherwise, if anything is unwritable,
   run `sudo chown -h <user>` on what is inside `src` and `bin` and on the help files (never on the
   folders), over `ssh -t` (so a sudo password prompt reaches the terminal), re-run the check, and
   stop if anything is still unwritable. No `chmod` is ever run. Group
   membership alone is not enough when files lack group write, which is why this happens before
   anything changes — so an upload can never fail halfway on a permission error.
4. **Backup** (skipped when the env has `backup: no`).
   `rm -rf backup.new && mkdir backup.new && find . -mindepth 1 -maxdepth 1 ! -name backup ! -name backup.new -exec cp -rp {} backup.new/ \;`
   then `mkdir backup.new/lib-text` and `cp -p` each help file that already exists in
   `../lib/text` into it. Only if all of that succeeded: `rm -rf backup && mv backup.new backup`.
   Copying before `make clean` keeps the `.o` files, and `-p` keeps timestamps, so a revert only
   relinks. If any copy fails the previous `backup/` is untouched.

**Remote (sftp)**

5. **Upload.** Write a batch file to the temp directory and run
   `sftp -o ControlPath=<socket> -P <ssh-port> -b <batch> <user>@<host>`:
   ```
   lcd <checkout>/src
   cd /rots/<dir>/src
   put -r *
   lcd <checkout>/lib/text
   cd /rots/<dir>/lib/text
   put help
   put help_tbl
   ...one put per help file...
   ```
   `-b` makes sftp exit non-zero on the first failed command.

**Remote (ssh)**

6. **Source edits** (4k and `zzz-forge-test-4k` only). Require exactly one line matching
   `^#define USE_BIG_BROTHER 1$` in `big_brother.h`; replace it with `#define USE_BIG_BROTHER 0` via
   `sed -i`; then require exactly one `^#define USE_BIG_BROTHER 0$` line and no `... 1` line. The
   local checkout is never edited.
7. **Build.** Record the server time, then `make clean`, then `make all -j2`, streaming output. Then
   require `../bin/ageland` to exist with a modification time at or after the recorded time.

**Local**

8. **Tag** (skipped for envs without a tag prefix). First fetch the env's tags from the main repo
   (`git fetch --no-tags git@github.com:returnoftheshadow/RotS_Live.git 'refs/tags/<prefix>*:refs/tags/<prefix>*'`),
   so a name another deployer already pushed is not reused. Then create an annotated tag on the
   deployed SHA named `<prefix>YYYY-MM-DD`, or `-2`, `-3`, … if that name exists, and push it
   (`git push git@github.com:returnoftheshadow/RotS_Live.git refs/tags/<name>`) so every deployer
   and dev can see what went out. The main repo is named by URL, not by remote, because remote names
   differ between checkouts. A failed fetch or push only prints a warning (a failed push also prints
   the command to push by hand): the deploy is already built, and the restart still runs. The message names the env,
   the remote dir, the SHA, and the help changes: the help files that differ between the env's
   previous tag (latest existing `<prefix>*` tag) and this commit
   (`git diff --name-only <prev> <sha> -- <help files>`), or `none`, or `first tagged deploy` when
   there is no previous tag. `git show <tag>` therefore tells whether a deploy carried help updates.
   A help file deleted from the repo since the previous tag is not listed as a help change (and, per
   **Scope**, it is not deleted on the server either).

**Remote (ssh)**

9. **Restart** (only with `--restart`, which only `test` accepts). `sudo systemctl restart
   rotsbuilding` over `ssh -t`, so a sudo password prompt reaches the terminal. It changes no file's
   owner or mode. It runs after the tag, so a failed tag leaves the port unrestarted (the report
   says so and prints the command) and a failed restart leaves the deploy tagged (the report prints
   the command and no revert hint).

   Without a sudoers rule, sudo asks for a second password here: it remembers a password per
   terminal, and each `ssh -t` is a new one. To skip that prompt, root installs a drop-in naming the
   deploying logins and nothing but this command, checked by `visudo` before it goes in place:
   ```sh
   tmp=$(mktemp) && \
   printf '%s ALL=(root) NOPASSWD: /bin/systemctl restart rotsbuilding, /usr/bin/systemctl restart rotsbuilding\n' "<user>, <user>" > "$tmp" && \
   sudo visudo -cf "$tmp" && sudo install -m 0440 -o root -g root "$tmp" /etc/sudoers.d/rots-deploy; \
   rm -f "$tmp"
   ```
   Both paths are listed because `/bin` and `/usr/bin` can name the same binary and sudo matches the
   path it resolves. The file name must not contain a `.` (sudo skips those in `/etc/sudoers.d`).
   `sudo -k; sudo -n -l systemctl restart rotsbuilding` prints the command without a prompt when
   the rule matches; `sudo -l -U <user>` shows it for another login. Every other sudo use still
   asks for a password, and step 3's `sudo chown` is not covered. Undo:
   `sudo rm /etc/sudoers.d/rots-deploy`. The script works the same either way; it only changes
   whether step 9 prompts.
10. **Close and report.** Always close the master connection (`ssh -S <socket> -O exit`) and delete the
   temp directory, including after a failure or Ctrl-C. Print either success with the tag name, or
   the failed step plus a revert hint:
   - envs with a backup: `scripts/deploy.py revert <env> <user>@<host> <ssh-port>`, with the
     equivalent server-side command printed underneath as a fallback.
   - coders: redeploy the previous commit.

## Code shape

One file, `scripts/deploy.py`, split into small functions with the pure parts separated from the
parts that run commands:

- **Env table**, **tag naming**, **help-file set and format check** — pure (the file set takes the
  `git ls-files` output and `consts.cpp` text as inputs).
- **Command builders** — pure functions returning argument lists / remote command strings for each
  step. Dry-run prints these.
- **Runner** — a thin class that runs a command locally, over the ssh master, or over sftp, and
  raises a `StepFailed(step, detail)` on non-zero exit. Tests substitute a fake runner.
- **`deploy(env, runner, dry_run)`** — the ordered steps, with the `finally` that closes the
  connection.

## Testing

**Automated** — `scripts/deploy_tests.py`, `unittest`, no network:

- Env table: every env's dir matches the path guard; only `4k` and `zzz-forge-test-4k` carry the
  source edit; coders has no backup; zzz envs have no tag prefix; only `test` and the zzz envs are
  approved; only `test` has a restart service (`rotsbuilding`).
- Approval and restart: `deploy`/`revert` on live, 4k, or coders (dry run included) and `--restart`
  on a zzz env stop with a usage error without calling the deploy or revert; `--restart` runs the
  sudo restart over a tty after the tag, never after a failed tag, and reports its own failure.
- Arguments: `<env> <user>@<host> <ssh-port>` all required, in that order; a missing argument, a
  login without `@` or with two, and a non-numeric port stop with a usage message; the parsed user,
  host, and port reach every ssh/sftp command.
- Tag naming: first of the day, `-2` when taken, `-3` when both are taken.
- Tag message help changes: changed files listed; `none`; `first tagged deploy`.
- Help-file set: `*_tbl` plus `help`; `help_tbl.old` excluded; untracked `_tbl` excluded; a new
  tracked `foo_tbl` included.
- Chapter parsing from a `consts.cpp` snippet.
- Format check: a clean file passes; `# ` passes; `#RRGGBB.` fails naming the line; missing `#~`
  fails; a chapter listed in `consts.cpp` but missing fails.
- Local pre-flight against a throwaway git repo: dirty tree stops; untracked `src/foo.o` stops;
  untracked `src/.remember/` passes; wrong branch stops for `live`, warns for `zzz-forge-test`.
- Command builders: path guard rejects `/rots/../etc` and similar; backup command skips `backup` and
  `backup.new`; sftp batch contains the source `put -r *` and one `put` per help file.
- Step order and early stop with a fake runner: a failure at step 3 runs no upload; the connection
  close runs on every failure path; coders issues no backup command; tags are created only on
  success.
- `--dry-run` output lists all steps for each env and invokes no remote command.

**Manual, against `/rots/zzz-forge-test` only** — Andrew present for the password. No run touches the
four real port dirs until Andrew says the script is proven. Prerequisite: `zzz-forge-test` has
`src`, `bin`, and `lib/text`.

1. `--dry-run` for all six envs; read the commands.
2. `deploy zzz-forge-test`: one password prompt, `sftp -b` reuses the master (the main assumption to
   confirm), backup replaced cleanly on a second run including `backup/lib-text`, help files match
   the checkout, build verified.
3. `deploy zzz-forge-test-4k`: `big_brother.h` on the server reads `0`; local file still `1`.
4. Make a file in the test `src` unwritable to the ssh user and confirm step 3 catches it before any
   change.
5. With a deliberately broken help table on the feature branch, confirm the deploy stops before the
   password prompt.
