# Deploy Script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `scripts/deploy.py`, which uploads `src/` and the help files to one RotS port over one
password-prompted ssh connection, builds there, and tags the deployed commit locally.

**Architecture:** One standard-library Python file with pure pieces (env table, argument parsing,
help-file checks, tag naming, remote shell-command builders) kept apart from the pieces that run
things (`Checkout` for local git, `SshRunner` for ssh/sftp over an OpenSSH master connection). A
`deploy()` function runs the numbered steps in order against injected `checkout` and `runner`
objects, so tests drive it with fakes. Remote commands are POSIX `sh` strings, tested by running
them locally against a temporary directory standing in for `/rots/<dir>`.

**Tech Stack:** Python 3.10 standard library (`argparse`, `dataclasses`, `subprocess`, `shlex`,
`unittest`), system `git`, `ssh`, `sftp`; remote GNU `find`/`cp`/`stat`/`make`.

**Spec:** `docs/superpowers/specs/2026-09-13-deploy-script-design.md`

## Global Constraints

- Work in the worktree `~/u/games/RotS_Live_deploy-script` on branch `feat/deploy-script`. Never
  commit in `~/u/games/RotS_Live_DEPLOY` (the deploy dir must stay clean on `release-frodo`).
- Standard library only; must run on Python 3.10. No `pytest` — tests use `unittest` and run with
  `python3 scripts/deploy_tests.py` (a class name may be appended, e.g.
  `python3 scripts/deploy_tests.py EnvTableTest -v`).
- Usage is exactly `scripts/deploy.py deploy <env> <user>@<host> <ssh-port> [--dry-run]`. The login
  and ssh port are required positionals with **no defaults**. The real account name, host, and ssh
  port must never appear in the repo — not in code, tests, docs, or commit messages. Tests use
  `someone@example.org` and port `2222`.
- Envs and dirs: `live`→`live-default3791`, `4k`→`live-pkarena4000`, `test`→`dev-building4802`,
  `coders`→`dev-coding4810`, `zzz-forge-test` and `zzz-forge-test-4k`→`zzz-forge-test`.
- Remote commands must be POSIX `sh` (the server's login shell may not be bash) and every
  interpolated value goes through `shlex.quote`.
- **During development, nothing may be run against the server except `/rots/zzz-forge-test`, and
  only in Task 7 with Andrew present.** Tasks 1–6 never open a network connection.
- Deploy tags stay local; never `git push --tags`.
- Commit messages end with the trailer line:
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```

## Where this plan refines the spec

These are deliberate and small; they do not change any agreed behavior.

1. The help format check also rejects a `#~` line that is not the last line (the game stops
   indexing at the first `#~`, hiding every later entry).
2. "Uncommitted changes" means changes to tracked files. Untracked files outside `src/` are ignored
   because they are never uploaded; untracked or ignored files inside `src/` still stop the deploy.
3. When a test target is deployed from a branch other than `release-frodo`, the pull is skipped
   (a feature branch may have no upstream) and the warning says so.
4. The backup step copies with `find -exec cp -rp -t backup.new {} +` rather than the spec's
   `find -exec cp -rp {} backup.new/ \;`, so a single failed copy makes the whole `find` fail and
   stop the backup; the `\;` form runs `cp` once per file and would hide a failure in the middle.
5. The revert command restores help files with `find backup/lib-text -mindepth 1 -maxdepth 1 -exec
   cp -p -t <port>/lib/text {} +` (a bare `cp backup/lib-text/*` fails when the backup has no help
   files at all), restores everything else from `backup` while skipping `lib-text`, touches the
   restored `.o` files so `make` always relinks even when `../bin/ageland` is newer than the
   restored objects, and clears the `src/DEPLOY_IN_PROGRESS` marker as its last step.
6. The source edit step checks line counts with `grep -cxF` (fixed-string, whole-line) rather than
   a regex match, so a line that happens to contain sed/regex metacharacters cannot change what
   "exactly one line" means.
7. Envs with a backup write a `src/DEPLOY_IN_PROGRESS` marker before uploading and clear it after a
   successful build; the next deploy to that env refuses to run (at the pre-check step) while the
   marker is present, so a failed or interrupted deploy cannot overwrite the only good backup.
8. Every remote command runs as `sh -c '<command>'` rather than as the raw trailing ssh argument, so
   it does not depend on the server's login shell (which may not be bash) to parse it.
9. `<user>` and `<host>` are restricted to `[A-Za-z0-9._-]`, with no leading `-`, so the login
   argument cannot be misread as an ssh option (e.g. `-oProxyCommand=...`).

## File Structure

- Create `scripts/deploy.py` — the whole tool, in sections added task by task: environments and
  arguments; help files; the local checkout; remote commands; running commands; the deploy.
- Create `scripts/deploy_tests.py` — `unittest` suite loading `deploy.py` by path, in the same
  section order.

No existing files change.

## Prerequisite for Task 7 only

`lib/text/help_tbl:92` is `#RRGGBB.`, which the help check (correctly) rejects, so the script stops
at step 1 on the current tree. Before Task 7, that line must be fixed on `release-frodo` in its own
PR (indent it or join it to line 91), and `feat/deploy-script` rebased onto it. Tasks 1–6 do not
depend on this; their tests use their own fixtures.

---

### Task 1: Environments and arguments

**Files:**
- Create: `scripts/deploy.py`
- Create: `scripts/deploy_tests.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `REPO_ROOT: Path`, `DEPLOY_BRANCH = "release-frodo"`, `HELP_DIR = "lib/text"`,
    `SOCKET_PARENT = "/tmp"`, color codes `BOLD_RED`, `BOLD_MAGENTA`, `YELLOW`, `GREEN`, `CYAN`
  - `class DeployError(Exception)`
  - `@dataclass(frozen=True) SourceEdit(path: str, old_line: str, new_line: str)`; `BIG_BROTHER_OFF`
  - `@dataclass(frozen=True) Env(name, dir_name, color, backup: bool, tag_prefix: Optional[str], source_edits: Tuple[SourceEdit, ...] = (), require_branch: bool = True)`
  - `ENVS: Dict[str, Env]`
  - `port_dir(env: Env) -> str` (raises `DeployError` unless `/rots/[a-z0-9-]+`)
  - `@dataclass(frozen=True) Server(user: str, host: str, port: int)` with property `login -> "user@host"`
  - `parse_login(value: str) -> Tuple[str, str]`, `parse_port(value: str) -> int`
  - `build_parser() -> argparse.ArgumentParser` (args: `command`, `env`, `login` as `(user, host)`, `port`, `dry_run`)

- [ ] **Step 1: Write the failing tests**

Create `scripts/deploy_tests.py`:

```python
#!/usr/bin/env python3

import contextlib
import datetime
import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parent / "deploy.py"
SPEC = importlib.util.spec_from_file_location("deploy", MODULE_PATH)
deploy = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules["deploy"] = deploy
SPEC.loader.exec_module(deploy)

TEST_ENV = deploy.ENVS["zzz-forge-test"]
SERVER = deploy.Server("someone", "example.org", 2222)


# ---------------------------------------------------------------------------------------------
# Environments and arguments
# ---------------------------------------------------------------------------------------------


class EnvTableTest(unittest.TestCase):
    def test_every_env_dir_passes_the_remote_path_guard(self) -> None:
        for env in deploy.ENVS.values():
            self.assertEqual(deploy.port_dir(env), f"/rots/{env.dir_name}")

    def test_only_the_4k_envs_turn_big_brother_off(self) -> None:
        with_edits = sorted(name for name, env in deploy.ENVS.items() if env.source_edits)

        self.assertEqual(with_edits, ["4k", "zzz-forge-test-4k"])
        self.assertEqual(deploy.ENVS["4k"].source_edits, (deploy.BIG_BROTHER_OFF,))
        self.assertEqual(deploy.BIG_BROTHER_OFF.old_line, "#define USE_BIG_BROTHER 1")
        self.assertEqual(deploy.BIG_BROTHER_OFF.new_line, "#define USE_BIG_BROTHER 0")

    def test_coders_is_the_only_env_without_a_backup(self) -> None:
        self.assertEqual([name for name, env in deploy.ENVS.items() if not env.backup], ["coders"])

    def test_real_ports_are_tagged_and_need_the_deploy_branch(self) -> None:
        for name in ("live", "4k", "test", "coders"):
            self.assertEqual(deploy.ENVS[name].tag_prefix, name + "-")
            self.assertTrue(deploy.ENVS[name].require_branch)

    def test_test_targets_are_untagged_and_allow_any_branch(self) -> None:
        for name in ("zzz-forge-test", "zzz-forge-test-4k"):
            self.assertEqual(deploy.ENVS[name].dir_name, "zzz-forge-test")
            self.assertIsNone(deploy.ENVS[name].tag_prefix)
            self.assertFalse(deploy.ENVS[name].require_branch)

    def test_port_dir_guard_rejects_anything_but_a_plain_name(self) -> None:
        for bad in ("../etc", "a/b", "", "Live", "a b", "x;rm"):
            env = deploy.Env("bad", bad, deploy.CYAN, backup=True, tag_prefix=None)
            with self.assertRaises(deploy.DeployError):
                deploy.port_dir(env)


class ArgumentsTest(unittest.TestCase):
    def parse(self, *argv: str):
        with contextlib.redirect_stderr(io.StringIO()):
            return deploy.build_parser().parse_args(argv)

    def assert_usage_error(self, *argv: str) -> None:
        with self.assertRaises(SystemExit) as caught:
            self.parse(*argv)
        self.assertEqual(caught.exception.code, 2)

    def test_parses_env_login_and_port_in_order(self) -> None:
        args = self.parse("deploy", "live", "someone@example.org", "2222")

        self.assertEqual((args.env, args.login, args.port, args.dry_run),
                         ("live", ("someone", "example.org"), 2222, False))

    def test_dry_run_flag(self) -> None:
        self.assertTrue(self.parse("deploy", "4k", "someone@example.org", "2222", "--dry-run").dry_run)

    def test_every_argument_is_required(self) -> None:
        self.assert_usage_error()
        self.assert_usage_error("deploy")
        self.assert_usage_error("deploy", "live")
        self.assert_usage_error("deploy", "live", "someone@example.org")

    def test_unknown_env_is_rejected(self) -> None:
        self.assert_usage_error("deploy", "prod", "someone@example.org", "2222")

    def test_login_needs_exactly_one_at_with_both_sides(self) -> None:
        for bad in ("example.org", "a@b@c", "@example.org", "someone@"):
            self.assert_usage_error("deploy", "live", bad, "2222")

    def test_port_must_be_a_valid_number(self) -> None:
        for bad in ("ssh", "0", "70000", "-1", "22.0"):
            self.assert_usage_error("deploy", "live", "someone@example.org", bad)


if __name__ == "__main__":
    unittest.main()
```

(The imports cover every later task too, so the header never needs editing again.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py`
Expected: FAIL — `FileNotFoundError` for `scripts/deploy.py`.

- [ ] **Step 3: Write the implementation**

Create `scripts/deploy.py`:

```python
#!/usr/bin/env python3
"""Deploy RotS source and help files to one port on the game server.

    scripts/deploy.py deploy <env> <user>@<host> <ssh-port> [--dry-run]

The login and ssh port are arguments with no defaults, so this file never records them.
Design: docs/superpowers/specs/2026-09-13-deploy-script-design.md
"""

import argparse
import datetime
import os
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, List, Optional, Sequence, Tuple


REPO_ROOT = Path(__file__).resolve().parent.parent
DEPLOY_BRANCH = "release-frodo"
HELP_DIR = "lib/text"
PORT_DIR_PATTERN = re.compile(r"^/rots/[a-z0-9-]+$")
# The ssh control socket has to fit in a Unix socket path (about 108 bytes), so it lives under
# /tmp rather than a possibly long $TMPDIR.
SOCKET_PARENT = "/tmp"

BOLD_RED = "1;31"
BOLD_MAGENTA = "1;35"
YELLOW = "33"
GREEN = "32"
CYAN = "36"


class DeployError(Exception):
    """A deploy step failed; the message says why."""


# ---------------------------------------------------------------------------------------------
# Environments and arguments
# ---------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class SourceEdit:
    path: str  # relative to src/
    old_line: str
    new_line: str


BIG_BROTHER_OFF = SourceEdit("big_brother.h", "#define USE_BIG_BROTHER 1", "#define USE_BIG_BROTHER 0")


@dataclass(frozen=True)
class Env:
    name: str
    dir_name: str
    color: str
    backup: bool
    tag_prefix: Optional[str]
    source_edits: Tuple[SourceEdit, ...] = ()
    require_branch: bool = True


ENVS = {
    env.name: env
    for env in (
        Env("live", "live-default3791", BOLD_RED, backup=True, tag_prefix="live-"),
        Env("4k", "live-pkarena4000", BOLD_MAGENTA, backup=True, tag_prefix="4k-",
            source_edits=(BIG_BROTHER_OFF,)),
        Env("test", "dev-building4802", YELLOW, backup=True, tag_prefix="test-"),
        # The coding port keeps no backups (docs/Running the Game.md).
        Env("coders", "dev-coding4810", GREEN, backup=False, tag_prefix="coders-"),
        # Test targets: never tagged, and deployable from a feature branch.
        Env("zzz-forge-test", "zzz-forge-test", CYAN, backup=True, tag_prefix=None,
            require_branch=False),
        Env("zzz-forge-test-4k", "zzz-forge-test", CYAN, backup=True, tag_prefix=None,
            source_edits=(BIG_BROTHER_OFF,), require_branch=False),
    )
}


def port_dir(env: Env) -> str:
    """The env's directory on the server, refusing anything that is not a plain /rots/<name>."""
    path = f"/rots/{env.dir_name}"
    if not PORT_DIR_PATTERN.match(path):
        raise DeployError(f"refusing to run remote commands against {path!r}")
    return path


@dataclass(frozen=True)
class Server:
    user: str
    host: str
    port: int

    @property
    def login(self) -> str:
        return f"{self.user}@{self.host}"


def parse_login(value: str) -> Tuple[str, str]:
    user, _, host = value.partition("@")
    if value.count("@") != 1 or not user or not host:
        raise argparse.ArgumentTypeError(f"expected <user>@<host>, got {value!r}")
    return user, host


def parse_port(value: str) -> int:
    if not value.isdigit() or not 1 <= int(value) <= 65535:
        raise argparse.ArgumentTypeError(f"expected an ssh port number, got {value!r}")
    return int(value)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="deploy.py", description="Deploy RotS to a port on the game server.")
    commands = parser.add_subparsers(dest="command", required=True)
    deploy_parser = commands.add_parser("deploy", help="upload, build, and tag one env")
    deploy_parser.add_argument("env", choices=list(ENVS))
    deploy_parser.add_argument("login", type=parse_login, metavar="user@host")
    deploy_parser.add_argument("port", type=parse_port, metavar="ssh-port")
    deploy_parser.add_argument("--dry-run", action="store_true",
        help="run the local checks and print every step; change nothing")
    return parser
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 12 tests, `OK`.

- [ ] **Step 5: Commit**

```bash
chmod +x scripts/deploy.py scripts/deploy_tests.py
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): env table and command-line arguments for scripts/deploy.py" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Help files — which to upload, and the format check

**Files:**
- Modify: `scripts/deploy.py` (append a section)
- Modify: `scripts/deploy_tests.py` (insert classes above `if __name__ == "__main__":`)

**Interfaces:**
- Consumes: `DeployError`, `HELP_DIR`, `REPO_ROOT` (Task 1).
- Produces:
  - `help_files(tracked: Iterable[str]) -> List[str]` — sorted names (not paths) in `lib/text` that are `help` or end in `_tbl`
  - `help_chapters(consts_text: str) -> List[str]` — names from `"text/<name>"` entries of `help_content[]`, in file order
  - `check_help_format(name: str, text: str) -> List[str]` — problem strings, empty if fine
  - `check_help_tables(repo: Path, tracked: Iterable[str]) -> List[str]`

Background for the implementer: the game's help parser (`build_help_index`, `src/modify.cpp:723`) and
the `help` command (`do_help`, `src/act_info.cpp:2094`) read a help table line by line. The first
line of an entry holds its keywords; **any** line starting with `#` ends the entry; a line `#~` ends
the file. So a text line like `#RRGGBB.` silently cuts an entry short.

- [ ] **Step 1: Write the failing tests**

Insert above `if __name__ == "__main__":` in `scripts/deploy_tests.py`:

```python
# ---------------------------------------------------------------------------------------------
# Help files
# ---------------------------------------------------------------------------------------------

CONSTS_SNIPPET = """int help_summary_length = 2;

struct help_index_summary help_content[] = {
    { "general", "General information", "text/help_tbl", 0, 0, 0, 0 },
    { "specializations", "Becoming an expert in a field of your choice", "text/spec_tbl", 0, 0, 0,
        0 },
};

const char* unrelated = "text/not_a_chapter";
"""


class HelpFilesTest(unittest.TestCase):
    def test_selects_tracked_help_tables_and_the_help_page(self) -> None:
        tracked = ["lib/text/help", "lib/text/help_tbl", "lib/text/help_tbl.old", "lib/text/motd",
                   "lib/text/new_tbl", "lib/text/sub/deep_tbl", "src/other_tbl"]

        self.assertEqual(deploy.help_files(tracked), ["help", "help_tbl", "new_tbl"])


class HelpChaptersTest(unittest.TestCase):
    def test_reads_the_text_entries_of_help_content(self) -> None:
        self.assertEqual(deploy.help_chapters(CONSTS_SNIPPET), ["help_tbl", "spec_tbl"])

    def test_missing_table_is_an_error(self) -> None:
        with self.assertRaises(deploy.DeployError):
            deploy.help_chapters("int nothing_here;")

    def test_the_real_consts_cpp_lists_nine_chapters(self) -> None:
        text = (deploy.REPO_ROOT / "src" / "consts.cpp").read_text(errors="replace")

        self.assertEqual(deploy.help_chapters(text), ["help_tbl", "spel_tbl", "pray_tbl", "skil_tbl", "spec_tbl",
                                                      "wizh_tbl", "shap_tbl", "scr_tbl", "msdp_tbl"])


class HelpFormatTest(unittest.TestCase):
    def test_well_formed_file_passes(self) -> None:
        self.assertEqual(deploy.check_help_format("help_tbl", "KILL HIT\n\nStart a fight.\n#\nFLEE\nRun.\n#~\n"), [])

    def test_separator_with_trailing_space_passes(self) -> None:
        self.assertEqual(deploy.check_help_format("help_tbl", "A\ntext\n# \nB\ntext\n#~"), [])

    def test_line_starting_with_hash_fails_and_names_the_line(self) -> None:
        problems = deploy.check_help_format("help_tbl", "COLOR\nHex values must look like\n#RRGGBB.\n#~\n")

        self.assertEqual(len(problems), 1)
        self.assertIn("lib/text/help_tbl:3:", problems[0])
        self.assertIn("#RRGGBB.", problems[0])

    def test_missing_end_marker_fails(self) -> None:
        problems = deploy.check_help_format("spel_tbl", "A\ntext\n#\n")

        self.assertEqual(problems, ["lib/text/spel_tbl: does not end with a '#~' line"])

    def test_end_marker_before_the_end_fails(self) -> None:
        problems = deploy.check_help_format("help_tbl", "A\n#~\nB\n#~\n")

        self.assertEqual(len(problems), 1)
        self.assertIn("lib/text/help_tbl:2:", problems[0])


class CheckHelpTablesTest(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name)
        (self.repo / "src").mkdir()
        (self.repo / "lib" / "text").mkdir(parents=True)
        (self.repo / "src" / "consts.cpp").write_text(CONSTS_SNIPPET)

    def test_reports_bad_chapters_and_untracked_chapters(self) -> None:
        (self.repo / "lib" / "text" / "help_tbl").write_text("A\n#oops\n#~\n")

        problems = deploy.check_help_tables(self.repo, ["lib/text/help_tbl"])

        self.assertEqual(len(problems), 2)
        self.assertIn("lib/text/help_tbl:2:", problems[0])
        self.assertIn("lib/text/spec_tbl: listed in src/consts.cpp", problems[1])

    def test_clean_chapters_pass(self) -> None:
        for name in ("help_tbl", "spec_tbl"):
            (self.repo / "lib" / "text" / name).write_text("A\ntext\n#~\n")

        self.assertEqual(deploy.check_help_tables(self.repo, ["lib/text/help_tbl", "lib/text/spec_tbl"]), [])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py HelpFilesTest HelpChaptersTest HelpFormatTest CheckHelpTablesTest`
Expected: ERROR — `AttributeError: module 'deploy' has no attribute 'help_files'` (and similar).

- [ ] **Step 3: Write the implementation**

Append to `scripts/deploy.py`:

```python


# ---------------------------------------------------------------------------------------------
# Help files
# ---------------------------------------------------------------------------------------------

HELP_CHAPTER_PATTERN = re.compile(r'"text/([^"/]+)"')


def help_files(tracked: Iterable[str]) -> List[str]:
    """The lib/text files every deploy uploads: the help tables (*_tbl) and the plain HELP page."""
    names = []
    for path in tracked:
        directory, _, name = path.rpartition("/")
        if directory == HELP_DIR and (name == "help" or name.endswith("_tbl")):
            names.append(name)
    return sorted(names)


def help_chapters(consts_text: str) -> List[str]:
    """The help files the game indexes: the "text/<name>" entries of help_content[] in consts.cpp."""
    start = consts_text.find("help_content[]")
    if start < 0:
        raise DeployError("could not find help_content[] in src/consts.cpp")
    end = consts_text.find("};", start)
    return HELP_CHAPTER_PATTERN.findall(consts_text[start:end])


def check_help_format(name: str, text: str) -> List[str]:
    """Problems that would break build_help_index (modify.cpp) or do_help (act_info.cpp).

    Both treat any line starting with '#' as the end of an entry, and '#~' as the end of the file.
    """
    lines = text.split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    problems = []
    for number, line in enumerate(lines, start=1):
        marker = line.rstrip()
        if not line.startswith("#") or marker == "#" or (marker == "#~" and number == len(lines)):
            continue
        if marker == "#~":
            problems.append(f"{HELP_DIR}/{name}:{number}: '#~' before the end of the file hides every entry after it")
        else:
            problems.append(f"{HELP_DIR}/{name}:{number}: starts with '#', which ends the entry here: {line!r}")
    if not lines or lines[-1].rstrip() != "#~":
        problems.append(f"{HELP_DIR}/{name}: does not end with a '#~' line")
    return problems


def check_help_tables(repo: Path, tracked: Iterable[str]) -> List[str]:
    tracked = set(tracked)
    problems = []
    for chapter in help_chapters((repo / "src" / "consts.cpp").read_text(errors="replace")):
        path = f"{HELP_DIR}/{chapter}"
        if path not in tracked:
            problems.append(f"{path}: listed in src/consts.cpp help_content[] but not tracked in git")
            continue
        problems.extend(check_help_format(chapter, (repo / path).read_text(errors="replace")))
    return problems
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 23 tests, `OK`.

- [ ] **Step 5: Commit**

```bash
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): pick help files to upload and check their format" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: The local checkout — pre-flight, pull, and tags

**Files:**
- Modify: `scripts/deploy.py` (append a section)
- Modify: `scripts/deploy_tests.py` (insert classes above `if __name__ == "__main__":`)

**Interfaces:**
- Consumes: `DeployError`, `DEPLOY_BRANCH`, `HELP_DIR`, `Env`, `ENVS`, `port_dir` (Task 1);
  `help_files`, `check_help_tables` (Task 2).
- Produces:
  - `git(repo: Path, *args: str) -> str` (raises `DeployError` with git's stderr)
  - `current_branch(repo: Path) -> str`
  - `check_checkout(repo: Path, env: Env) -> List[str]` — raises on a stop, returns warnings
  - `next_tag_name(prefix: str, day: datetime.date, existing: Iterable[str]) -> str`
  - `previous_tag(prefix: str, existing: Iterable[str]) -> Optional[str]`
  - `help_changes_line(previous: Optional[str], changed: Sequence[str]) -> str`
  - `tag_message(env: Env, sha: str, help_line: str) -> str`
  - `class Checkout(repo: Path)` with attribute `repo` and methods
    `prepare(env: Env, dry_run: bool) -> List[str]`, `head() -> Tuple[str, str]` (sha, subject),
    `help_files() -> List[str]`, `help_problems() -> List[str]`,
    `create_tag(env: Env, sha: str, help_names: Sequence[str], day: datetime.date) -> str`

Background: `put -r *` in sftp uploads everything in local `src/` whose name does not start with
`.`, so a stray untracked or ignored file there (e.g. `src/game.o`) would be uploaded; dot-entries
such as `src/.remember/` are skipped by the glob and are fine.

- [ ] **Step 1: Write the failing tests**

Insert above `if __name__ == "__main__":` in `scripts/deploy_tests.py`:

```python
# ---------------------------------------------------------------------------------------------
# The local checkout
# ---------------------------------------------------------------------------------------------


class GitRepoTestCase(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name)
        environment = mock.patch.dict(os.environ, {
            "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.org",
            "GIT_COMMITTER_NAME": "Test", "GIT_COMMITTER_EMAIL": "test@example.org",
        })
        environment.start()
        self.addCleanup(environment.stop)
        self.run_git("init", "-q", "-b", "release-frodo")
        self.write("src/game.cpp", "int main() {}\n")
        self.write("src/.gitignore", "*.o\n.remember/\n")
        self.write("lib/text/help_tbl", "A\n#~\n")
        self.write("lib/text/motd", "Welcome.\n")
        self.commit("initial")
        self.checkout = deploy.Checkout(self.repo)

    def run_git(self, *args: str) -> str:
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True, text=True).stdout

    def write(self, relative: str, text: str) -> None:
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def commit(self, message: str) -> str:
        self.run_git("add", "-A")
        self.run_git("commit", "-q", "-m", message)
        return self.run_git("rev-parse", "HEAD").strip()


class CheckCheckoutTest(GitRepoTestCase):
    def test_clean_checkout_on_the_deploy_branch_passes(self) -> None:
        self.assertEqual(deploy.check_checkout(self.repo, deploy.ENVS["live"]), [])

    def test_uncommitted_change_stops(self) -> None:
        self.write("src/game.cpp", "int main() { return 1; }\n")

        with self.assertRaisesRegex(deploy.DeployError, "uncommitted"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_ignored_object_file_in_src_stops(self) -> None:
        self.write("src/game.o", "binary")

        with self.assertRaisesRegex(deploy.DeployError, "src/game.o"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_untracked_source_file_in_src_stops(self) -> None:
        self.write("src/new_feature.cpp", "// not committed\n")

        with self.assertRaisesRegex(deploy.DeployError, "src/new_feature.cpp"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_dot_directories_in_src_and_files_outside_src_pass(self) -> None:
        self.write("src/.remember/now.md", "notes\n")
        self.write("notes.txt", "scratch\n")

        self.assertEqual(deploy.check_checkout(self.repo, deploy.ENVS["live"]), [])

    def test_other_branch_stops_a_real_port(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        with self.assertRaisesRegex(deploy.DeployError, "feat/x"):
            deploy.check_checkout(self.repo, deploy.ENVS["live"])

    def test_other_branch_only_warns_for_a_test_target(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        warnings = deploy.check_checkout(self.repo, TEST_ENV)

        self.assertEqual(len(warnings), 1)
        self.assertIn("feat/x", warnings[0])


class CheckoutTest(GitRepoTestCase):
    def test_head_returns_sha_and_subject(self) -> None:
        sha = self.run_git("rev-parse", "HEAD").strip()

        self.assertEqual(self.checkout.head(), (sha, "initial"))

    def test_help_files_come_from_git(self) -> None:
        self.write("lib/text/untracked_tbl", "A\n#~\n")

        self.assertEqual(self.checkout.help_files(), ["help_tbl"])

    def test_dry_run_does_not_pull(self) -> None:
        # There is no remote, so a pull would fail.
        self.assertEqual(self.checkout.prepare(deploy.ENVS["live"], dry_run=True), [])

    def test_real_run_pulls_on_the_deploy_branch(self) -> None:
        with self.assertRaisesRegex(deploy.DeployError, "git pull --ff-only failed"):
            self.checkout.prepare(deploy.ENVS["live"], dry_run=False)

    def test_test_target_on_another_branch_does_not_pull(self) -> None:
        self.run_git("checkout", "-q", "-b", "feat/x")

        self.assertEqual(len(self.checkout.prepare(TEST_ENV, dry_run=False)), 1)


class TagNamingTest(unittest.TestCase):
    DAY = datetime.date(2026, 9, 13)

    def test_first_tag_of_the_day(self) -> None:
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-12"]), "test-2026-09-13")

    def test_second_and_third_tags_of_the_day(self) -> None:
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-13"]), "test-2026-09-13-2")
        self.assertEqual(deploy.next_tag_name("test-", self.DAY, ["test-2026-09-13", "test-2026-09-13-2"]),
                         "test-2026-09-13-3")

    def test_previous_tag_is_the_latest_date_and_suffix_for_the_prefix(self) -> None:
        tags = ["live-2026-09-11", "live-2026-09-13", "live-2026-09-13-2", "live-2026-09-13-10",
                "test-2026-09-20", "live-hotfix", "4k-2026-10-01"]

        self.assertEqual(deploy.previous_tag("live-", tags), "live-2026-09-13-10")
        self.assertIsNone(deploy.previous_tag("coders-", tags))

    def test_help_changes_line(self) -> None:
        self.assertEqual(deploy.help_changes_line(None, []), "Help changes: first tagged deploy")
        self.assertEqual(deploy.help_changes_line("test-2026-09-13", []), "Help changes since test-2026-09-13: none")
        self.assertEqual(deploy.help_changes_line("test-2026-09-13", ["help_tbl", "shap_tbl"]),
                         "Help changes since test-2026-09-13: help_tbl, shap_tbl")


class CreateTagTest(GitRepoTestCase):
    def tag_contents(self, name: str) -> str:
        return self.run_git("tag", "-l", "--format=%(contents)", name)

    def test_first_tag_records_env_dir_sha_and_first_deploy(self) -> None:
        sha = self.run_git("rev-parse", "HEAD").strip()

        name = self.checkout.create_tag(deploy.ENVS["test"], sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.assertEqual(name, "test-2026-09-13")
        contents = self.tag_contents(name)
        self.assertIn(f"Deployed {sha} to test (/rots/dev-building4802).", contents)
        self.assertIn("Help changes: first tagged deploy", contents)
        self.assertNotIn("someone", contents)

    def test_later_tag_lists_only_help_files_changed_since_the_previous_tag(self) -> None:
        env = deploy.ENVS["test"]
        first = self.run_git("rev-parse", "HEAD").strip()
        self.checkout.create_tag(env, first, ["help_tbl"], datetime.date(2026, 9, 13))
        self.write("lib/text/help_tbl", "A\nnew text\n#~\n")
        self.write("lib/text/motd", "Changed on purpose.\n")
        second = self.commit("help update")

        name = self.checkout.create_tag(env, second, ["help_tbl"], datetime.date(2026, 9, 14))

        self.assertEqual(name, "test-2026-09-14")
        self.assertIn("Help changes since test-2026-09-13: help_tbl", self.tag_contents(name))

    def test_same_day_redeploy_gets_a_suffix_and_no_help_changes(self) -> None:
        env = deploy.ENVS["test"]
        sha = self.run_git("rev-parse", "HEAD").strip()
        self.checkout.create_tag(env, sha, ["help_tbl"], datetime.date(2026, 9, 13))

        name = self.checkout.create_tag(env, sha, ["help_tbl"], datetime.date(2026, 9, 13))

        self.assertEqual(name, "test-2026-09-13-2")
        self.assertIn("Help changes since test-2026-09-13: none", self.tag_contents(name))
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py CheckCheckoutTest CheckoutTest TagNamingTest CreateTagTest`
Expected: ERROR — `AttributeError: module 'deploy' has no attribute 'Checkout'` / `check_checkout` / `next_tag_name`.

- [ ] **Step 3: Write the implementation**

Append to `scripts/deploy.py`:

```python


# ---------------------------------------------------------------------------------------------
# The local checkout
# ---------------------------------------------------------------------------------------------

TAG_DATE_PATTERN = re.compile(r"^(\d{4}-\d{2}-\d{2})(?:-(\d+))?$")


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise DeployError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def current_branch(repo: Path) -> str:
    return git(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()


def check_checkout(repo: Path, env: Env) -> List[str]:
    """Stop on anything that would deploy the wrong files; return warnings worth showing."""
    if git(repo, "status", "--porcelain", "--untracked-files=no").strip():
        raise DeployError("the checkout has uncommitted changes; commit or stash them first")
    extras = []
    status = git(repo, "status", "--porcelain", "--ignored", "--untracked-files=all", "--", "src")
    for line in status.splitlines():
        path = line[3:].strip('"')
        # `put -r *` skips names starting with '.', such as src/.remember/.
        if not path.split("/")[1].startswith("."):
            extras.append(path)
    if extras:
        raise DeployError("src/ has untracked or ignored files that `put -r *` would upload: " + ", ".join(extras))
    branch = current_branch(repo)
    if branch == DEPLOY_BRANCH:
        return []
    message = f"the checkout is on {branch!r}, not {DEPLOY_BRANCH!r}"
    if env.require_branch:
        raise DeployError(message)
    return [message + "; deploying it without pulling because this is a test target"]


def next_tag_name(prefix: str, day: datetime.date, existing: Iterable[str]) -> str:
    existing = set(existing)
    base = f"{prefix}{day.isoformat()}"
    name, count = base, 1
    while name in existing:
        count += 1
        name = f"{base}-{count}"
    return name


def previous_tag(prefix: str, existing: Iterable[str]) -> Optional[str]:
    """The latest <prefix>YYYY-MM-DD[-N] tag, or None."""
    dated = []
    for tag in existing:
        match = TAG_DATE_PATTERN.match(tag[len(prefix):]) if tag.startswith(prefix) else None
        if match:
            dated.append((match.group(1), int(match.group(2) or 1), tag))
    return max(dated)[2] if dated else None


def help_changes_line(previous: Optional[str], changed: Sequence[str]) -> str:
    if previous is None:
        return "Help changes: first tagged deploy"
    return f"Help changes since {previous}: {', '.join(changed) if changed else 'none'}"


def tag_message(env: Env, sha: str, help_line: str) -> str:
    return f"Deployed {sha} to {env.name} ({port_dir(env)}).\n\n{help_line}\n"


class Checkout:
    """The local git checkout being deployed."""

    def __init__(self, repo: Path):
        self.repo = repo

    def prepare(self, env: Env, dry_run: bool) -> List[str]:
        warnings = check_checkout(self.repo, env)
        if not dry_run and current_branch(self.repo) == DEPLOY_BRANCH:
            git(self.repo, "pull", "--ff-only")
        return warnings

    def head(self) -> Tuple[str, str]:
        sha, _, subject = git(self.repo, "log", "-1", "--format=%H%x00%s").strip().partition("\0")
        return sha, subject

    def _tracked_help_dir(self) -> List[str]:
        return git(self.repo, "ls-files", "--", HELP_DIR).splitlines()

    def help_files(self) -> List[str]:
        return help_files(self._tracked_help_dir())

    def help_problems(self) -> List[str]:
        return check_help_tables(self.repo, self._tracked_help_dir())

    def create_tag(self, env: Env, sha: str, help_names: Sequence[str], day: datetime.date) -> str:
        existing = git(self.repo, "tag", "--list").split()
        previous = previous_tag(env.tag_prefix, existing)
        changed: List[str] = []
        if previous is not None:
            paths = [f"{HELP_DIR}/{name}" for name in help_names]
            diff = git(self.repo, "diff", "--name-only", previous, sha, "--", *paths)
            changed = [path.rpartition("/")[2] for path in diff.split()]
        name = next_tag_name(env.tag_prefix, day, existing)
        git(self.repo, "tag", "-a", name, sha, "-m", tag_message(env, sha, help_changes_line(previous, changed)))
        return name
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 42 tests, `OK`.

- [ ] **Step 5: Commit**

```bash
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): local checkout pre-flight, pull, and deploy tags" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Remote shell commands

**Files:**
- Modify: `scripts/deploy.py` (append a section)
- Modify: `scripts/deploy_tests.py` (insert classes above `if __name__ == "__main__":`)

**Interfaces:**
- Consumes: `DeployError`, `HELP_DIR`, `Env`, `SourceEdit`, `BIG_BROTHER_OFF`, `ENVS`, `port_dir` (Task 1).
- Produces (all return one POSIX `sh` command string, all call `port_dir` so an unsafe env raises):
  - `q(text: str) -> str` (alias for `shlex.quote`)
  - `missing_dirs_command(env) -> str` — exits 1 printing `missing directory: <path>`
  - `unwritable_command(env, help_names) -> str` — prints one unwritable path per line, nothing if all writable, always exits 0
  - `chown_command(env, user, help_names) -> str`
  - `backup_command(env, help_names) -> str`
  - `sftp_quote(text) -> str`, `sftp_batch(env, repo: Path, help_names) -> str` (sftp batch file text)
  - `source_edit_command(env, edit: SourceEdit) -> str`
  - `build_command(env) -> str`
  - `revert_command(env) -> str`

Testing approach: each command is run locally with `sh -c` after replacing `/rots/zzz-forge-test`
with a temporary directory that has `src/`, `bin/`, and `lib/text/`. That exercises the real shell
logic without a server. Two points the tests pin down, because they are easy to get wrong:
`find ... -exec cmd {} \;` exits 0 even when `cmd` fails, so the backup must use
`-exec cp -rp -t backup.new {} +`; and the old `backup/` may only be removed after the new copy
fully succeeded.

- [ ] **Step 1: Write the failing tests**

Insert above `if __name__ == "__main__":` in `scripts/deploy_tests.py`:

```python
# ---------------------------------------------------------------------------------------------
# Remote commands, run locally with sh against a fake port directory
# ---------------------------------------------------------------------------------------------


class RemoteCommandTestCase(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name) / "port"
        for sub in ("src", "bin", "lib/text"):
            (self.root / sub).mkdir(parents=True)
        (self.root / "src" / "game.cpp").write_text("old source\n")
        (self.root / "src" / "game.o").write_text("old object\n")
        (self.root / "lib" / "text" / "help_tbl").write_text("old help\n#~\n")

    def sh(self, command: str) -> subprocess.CompletedProcess:
        local = command.replace(deploy.port_dir(TEST_ENV), str(self.root))
        self.assertNotIn("/rots/", local)
        return subprocess.run(["sh", "-c", local], capture_output=True, text=True)


class MissingDirsCommandTest(RemoteCommandTestCase):
    def test_passes_when_src_bin_and_lib_text_exist(self) -> None:
        self.assertEqual(self.sh(deploy.missing_dirs_command(TEST_ENV)).returncode, 0)

    def test_names_a_missing_directory(self) -> None:
        (self.root / "bin").rmdir()

        result = self.sh(deploy.missing_dirs_command(TEST_ENV))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing directory:", result.stdout)
        self.assertIn("/bin", result.stdout)


@unittest.skipIf(os.geteuid() == 0, "root can write everything")
class UnwritableCommandTest(RemoteCommandTestCase):
    def test_prints_nothing_when_everything_is_writable(self) -> None:
        result = self.sh(deploy.unwritable_command(TEST_ENV, ["help_tbl", "not_there_tbl"]))

        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_lists_unwritable_source_and_help_files(self) -> None:
        (self.root / "src" / "game.cpp").chmod(0o444)
        (self.root / "lib" / "text" / "help_tbl").chmod(0o444)

        output = self.sh(deploy.unwritable_command(TEST_ENV, ["help_tbl"])).stdout

        self.assertIn(f"{self.root}/src/game.cpp", output)
        self.assertIn(f"{self.root}/lib/text/help_tbl", output)


class ChownCommandTest(unittest.TestCase):
    def test_is_valid_sh_and_targets_the_ssh_user(self) -> None:
        command = deploy.chown_command(TEST_ENV, "someone", ["help_tbl"])

        self.assertEqual(subprocess.run(["sh", "-n", "-c", command]).returncode, 0)
        self.assertIn("sudo chown -R someone /rots/zzz-forge-test/src /rots/zzz-forge-test/bin", command)
        self.assertIn("sudo chown someone /rots/zzz-forge-test/lib/text", command)


class BackupCommandTest(RemoteCommandTestCase):
    def test_copies_src_and_help_files_keeping_timestamps(self) -> None:
        stamp = time.time() - 3600
        os.utime(self.root / "src" / "game.o", (stamp, stamp))

        result = self.sh(deploy.backup_command(TEST_ENV, ["help_tbl", "not_there_tbl"]))

        self.assertEqual(result.returncode, 0, result.stderr)
        backup = self.root / "src" / "backup"
        self.assertEqual((backup / "game.cpp").read_text(), "old source\n")
        self.assertEqual(int((backup / "game.o").stat().st_mtime), int(stamp))
        self.assertEqual((backup / "lib-text" / "help_tbl").read_text(), "old help\n#~\n")
        self.assertFalse((backup / "lib-text" / "not_there_tbl").exists())
        self.assertFalse((self.root / "src" / "backup.new").exists())

    def test_second_run_replaces_the_backup_without_nesting(self) -> None:
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "backup" / "stale.txt").write_text("from an older backup\n")
        (self.root / "src" / "game.cpp").write_text("newer source\n")

        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)

        backup = self.root / "src" / "backup"
        self.assertEqual((backup / "game.cpp").read_text(), "newer source\n")
        self.assertFalse((backup / "stale.txt").exists())
        self.assertFalse((backup / "backup").exists())

    @unittest.skipIf(os.geteuid() == 0, "root can read everything")
    def test_failed_copy_keeps_the_previous_backup(self) -> None:
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "backup" / "marker.txt").write_text("previous backup\n")
        unreadable = self.root / "src" / "secret.cpp"
        unreadable.write_text("x\n")
        unreadable.chmod(0o000)
        self.addCleanup(unreadable.chmod, 0o644)

        result = self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"]))

        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / "src" / "backup" / "marker.txt").exists())

    def test_revert_restores_source_and_help_files(self) -> None:
        (self.root / "src" / "Makefile").write_text("all:\n\ttouch ../bin/ageland\n")
        self.assertEqual(self.sh(deploy.backup_command(TEST_ENV, ["help_tbl"])).returncode, 0)
        (self.root / "src" / "game.cpp").write_text("broken new source\n")
        (self.root / "lib" / "text" / "help_tbl").write_text("broken new help\n")

        result = self.sh(deploy.revert_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "src" / "game.cpp").read_text(), "old source\n")
        self.assertEqual((self.root / "lib" / "text" / "help_tbl").read_text(), "old help\n#~\n")
        self.assertFalse((self.root / "src" / "lib-text").exists())

    def test_rejects_an_unsafe_env(self) -> None:
        with self.assertRaises(deploy.DeployError):
            deploy.backup_command(deploy.Env("bad", "../etc", deploy.CYAN, backup=True, tag_prefix=None), [])


class SourceEditCommandTest(RemoteCommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.header = self.root / "src" / "big_brother.h"
        self.header.write_text("#ifndef USE_BIG_BROTHER\n#define USE_BIG_BROTHER 1\n#endif\n")
        self.command = deploy.source_edit_command(TEST_ENV, deploy.BIG_BROTHER_OFF)

    def test_turns_big_brother_off(self) -> None:
        result = self.sh(self.command)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.header.read_text(), "#ifndef USE_BIG_BROTHER\n#define USE_BIG_BROTHER 0\n#endif\n")

    def test_stops_when_the_line_is_already_changed(self) -> None:
        self.sh(self.command)

        result = self.sh(self.command)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected exactly one line: #define USE_BIG_BROTHER 1", result.stdout)

    def test_stops_without_editing_when_the_line_appears_twice(self) -> None:
        original = "#define USE_BIG_BROTHER 1\n#define USE_BIG_BROTHER 1\n"
        self.header.write_text(original)

        self.assertNotEqual(self.sh(self.command).returncode, 0)
        self.assertEqual(self.header.read_text(), original)


class BuildCommandTest(RemoteCommandTestCase):
    def test_passes_when_make_rebuilds_the_binary(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\trm -f *.o\nall:\n\ttouch ../bin/ageland\n")

        result = self.sh(deploy.build_command(TEST_ENV))

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / "src" / "game.o").exists())

    def test_fails_when_the_binary_is_stale(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\t@true\nall:\n\t@true\n")
        binary = self.root / "bin" / "ageland"
        binary.write_text("old")
        os.utime(binary, (time.time() - 3600, time.time() - 3600))

        result = self.sh(deploy.build_command(TEST_ENV))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not rebuilt", result.stdout)

    def test_fails_when_make_fails(self) -> None:
        (self.root / "src" / "Makefile").write_text("clean:\n\t@true\nall:\n\t@false\n")

        self.assertNotEqual(self.sh(deploy.build_command(TEST_ENV)).returncode, 0)


class SftpBatchTest(unittest.TestCase):
    def test_uploads_src_then_each_help_file(self) -> None:
        batch = deploy.sftp_batch(TEST_ENV, Path("/home/me/RotS"), ["help", "help_tbl"])

        self.assertEqual(batch, "\n".join([
            'lcd "/home/me/RotS/src"',
            'cd "/rots/zzz-forge-test/src"',
            "put -r *",
            'lcd "/home/me/RotS/lib/text"',
            'cd "/rots/zzz-forge-test/lib/text"',
            'put "help"',
            'put "help_tbl"',
        ]) + "\n")
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py MissingDirsCommandTest UnwritableCommandTest ChownCommandTest BackupCommandTest SourceEditCommandTest BuildCommandTest SftpBatchTest`
Expected: ERROR — `AttributeError: module 'deploy' has no attribute 'missing_dirs_command'` (and similar).

- [ ] **Step 3: Write the implementation**

Append to `scripts/deploy.py`:

```python


# ---------------------------------------------------------------------------------------------
# Remote commands (POSIX sh, run through ssh)
# ---------------------------------------------------------------------------------------------


def q(text: str) -> str:
    return shlex.quote(text)


def _help_paths(env: Env, help_names: Sequence[str]) -> str:
    return " ".join(q(f"{port_dir(env)}/{HELP_DIR}/{name}") for name in help_names)


def missing_dirs_command(env: Env) -> str:
    base = port_dir(env)
    dirs = " ".join(q(f"{base}/{sub}") for sub in ("src", "bin", HELP_DIR))
    return f'for d in {dirs}; do [ -d "$d" ] || {{ echo "missing directory: $d"; exit 1; }}; done'


def unwritable_command(env: Env, help_names: Sequence[str]) -> str:
    """Prints one line per path the ssh user cannot write, and nothing when everything is writable."""
    base = port_dir(env)
    text_dir = f"{base}/{HELP_DIR}"
    return (
        f"find {q(base + '/src')} {q(base + '/bin')} ! -writable -print 2>&1; "
        f"[ -w {q(text_dir)} ] || echo {q(text_dir)}; "
        f'for f in {_help_paths(env, help_names)}; do [ ! -e "$f" ] || [ -w "$f" ] || echo "$f"; done; true'
    )


def chown_command(env: Env, user: str, help_names: Sequence[str]) -> str:
    base = port_dir(env)
    return (
        f"sudo chown -R {q(user)} {q(base + '/src')} {q(base + '/bin')} && "
        f"sudo chown {q(user)} {q(base + '/' + HELP_DIR)} && "
        f'for f in {_help_paths(env, help_names)}; do [ ! -e "$f" ] || sudo chown {q(user)} "$f" || exit 1; done'
    )


def backup_command(env: Env, help_names: Sequence[str]) -> str:
    """Refresh src/backup with everything in src plus the server's current help files.

    Runs before make clean so the .o files come along and a revert only relinks. The old backup is
    replaced only once the new copy is complete.
    """
    base = port_dir(env)
    text_dir = q(f"{base}/{HELP_DIR}")
    names = " ".join(q(name) for name in help_names)
    return (
        f"cd {q(base + '/src')} && rm -rf backup.new && mkdir backup.new && "
        "find . -mindepth 1 -maxdepth 1 ! -name backup ! -name backup.new -exec cp -rp -t backup.new {} + && "
        "mkdir backup.new/lib-text && "
        f'for f in {names}; do [ ! -e {text_dir}/"$f" ] || cp -p {text_dir}/"$f" backup.new/lib-text/ || exit 1; done && '
        "rm -rf backup && mv backup.new backup"
    )


def sftp_quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def sftp_batch(env: Env, repo: Path, help_names: Sequence[str]) -> str:
    base = port_dir(env)
    lines = [
        f"lcd {sftp_quote(str(repo / 'src'))}",
        f"cd {sftp_quote(base + '/src')}",
        "put -r *",
        f"lcd {sftp_quote(str(repo / HELP_DIR))}",
        f"cd {sftp_quote(base + '/' + HELP_DIR)}",
    ]
    lines += [f"put {sftp_quote(name)}" for name in help_names]
    return "\n".join(lines) + "\n"


def _sed_pattern(text: str) -> str:
    return re.sub(r"([\\/.*\[\]^$])", r"\\\1", text)


def _sed_replacement(text: str) -> str:
    return re.sub(r"([\\/&])", r"\\\1", text)


def source_edit_command(env: Env, edit: SourceEdit) -> str:
    path = q(f"{port_dir(env)}/src/{edit.path}")
    old, new = q(edit.old_line), q(edit.new_line)
    script = q(f"s/^{_sed_pattern(edit.old_line)}$/{_sed_replacement(edit.new_line)}/")
    expected_old = q(f"{edit.path}: expected exactly one line: {edit.old_line}")
    expected_new = q(f"{edit.path}: the edit did not leave exactly one line: {edit.new_line}")
    return (
        f'[ "$(grep -cxF -- {old} {path})" = 1 ] || {{ echo {expected_old}; exit 1; }}; '
        f"sed -i {script} {path} && "
        f'[ "$(grep -cxF -- {new} {path})" = 1 ] && [ "$(grep -cxF -- {old} {path})" = 0 ] || '
        f"{{ echo {expected_new}; exit 1; }}"
    )


def build_command(env: Env) -> str:
    return (
        f"cd {q(port_dir(env) + '/src')} && start=$(date +%s) && make clean && make all -j6 && "
        '{ [ -f ../bin/ageland ] && [ "$(stat -c %Y ../bin/ageland)" -ge "$start" ] || '
        "{ echo 'make finished but ../bin/ageland was not rebuilt'; exit 1; }; }"
    )


def revert_command(env: Env) -> str:
    base = port_dir(env)
    return (
        f"cd {q(base + '/src')} && cp -p backup/lib-text/* {q(base + '/' + HELP_DIR)}/ && "
        "find backup -mindepth 1 -maxdepth 1 ! -name lib-text -exec cp -rp -t . {} + && make all -j6"
    )
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 59 tests, `OK`.

- [ ] **Step 5: Prove the backup test catches the `find -exec ... \;` mistake**

Temporarily change `-exec cp -rp -t backup.new {} +` to `-exec cp -rp {} backup.new/ \\;` in
`backup_command`, run `python3 scripts/deploy_tests.py BackupCommandTest`, and confirm
`test_failed_copy_keeps_the_previous_backup` FAILS. Revert the change (`git diff scripts/deploy.py`
must show only this task's additions against the last commit) and rerun to PASS.

- [ ] **Step 6: Commit**

```bash
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): remote pre-check, backup, source edit, build, and revert commands" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Running commands over one ssh master connection

**Files:**
- Modify: `scripts/deploy.py` (append a section)
- Modify: `scripts/deploy_tests.py` (insert classes above `if __name__ == "__main__":`)

**Interfaces:**
- Consumes: `DeployError`, `Server` (Task 1).
- Produces:
  - `run_command(args: Sequence[str], what: str, capture: bool = False) -> str` — raises
    `DeployError("<what> exited with status N[: output]")`; returns stdout when `capture`, else `""`
  - `class SshRunner(server: Server, work_dir: Path)` with attributes `server`, `work_dir`,
    `socket` (`work_dir / "ssh-master"`), `connected: bool`, and methods
    `connect_args() -> List[str]`, `remote_args(command: str, tty: bool = False) -> List[str]`,
    `sftp_args(batch_path: Path) -> List[str]`, `close_args() -> List[str]`,
    `connect() -> None`, `remote(command: str, capture: bool = False, tty: bool = False) -> str`,
    `sftp(batch: str) -> None` (writes `work_dir / "upload.sftp"`), `close() -> None` (no-op unless connected)

Background: `ssh -M -S <socket> -fN` authenticates once (the password prompt), then backgrounds a
master process; every later `ssh -S <socket>` and `sftp -o ControlPath=<socket>` rides that
connection without prompting. `ssh -S <socket> -O exit` shuts the master down. Nothing in this task
opens a real connection — tests only check the argument lists and use `sys.executable` as a stand-in
process.

- [ ] **Step 1: Write the failing tests**

Insert above `if __name__ == "__main__":` in `scripts/deploy_tests.py`:

```python
# ---------------------------------------------------------------------------------------------
# Running commands
# ---------------------------------------------------------------------------------------------


class SshRunnerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = deploy.SshRunner(SERVER, Path("/tmp/rots-deploy-test"))

    def test_connect_opens_a_master_connection(self) -> None:
        self.assertEqual(self.runner.connect_args(), ["ssh", "-M", "-S", "/tmp/rots-deploy-test/ssh-master", "-fN",
                                                      "-p", "2222", "someone@example.org"])

    def test_remote_reuses_the_master_and_can_request_a_tty(self) -> None:
        self.assertEqual(self.runner.remote_args("make"), ["ssh", "-S", "/tmp/rots-deploy-test/ssh-master", "-p", "2222",
                                                           "someone@example.org", "make"])
        self.assertEqual(self.runner.remote_args("sudo true", tty=True)[3], "-t")

    def test_sftp_reuses_the_master(self) -> None:
        self.assertEqual(self.runner.sftp_args(Path("/tmp/b")), ["sftp", "-o",
                         "ControlPath=/tmp/rots-deploy-test/ssh-master", "-P", "2222", "-b", "/tmp/b",
                         "someone@example.org"])

    def test_sftp_writes_the_batch_file_and_runs_it(self) -> None:
        with tempfile.TemporaryDirectory() as work_dir:
            runner = deploy.SshRunner(SERVER, Path(work_dir))
            with mock.patch.object(deploy, "run_command") as run:
                runner.sftp("put -r *\n")

            batch_path = Path(work_dir) / "upload.sftp"
            run.assert_called_once_with(runner.sftp_args(batch_path), "sftp upload")
            self.assertEqual(batch_path.read_text(), "put -r *\n")

    def test_close_only_runs_after_connecting(self) -> None:
        with mock.patch.object(deploy.subprocess, "run") as run:
            self.runner.close()
            run.assert_not_called()
            self.runner.connected = True
            self.runner.close()
            run.assert_called_once()
            self.assertFalse(self.runner.connected)


class RunCommandTest(unittest.TestCase):
    def test_returns_captured_output(self) -> None:
        self.assertEqual(deploy.run_command([sys.executable, "-c", "print('hello')"], "hello", capture=True), "hello\n")

    def test_failure_raises_with_status_and_output(self) -> None:
        with self.assertRaisesRegex(deploy.DeployError, "thing exited with status 3: oops"):
            deploy.run_command([sys.executable, "-c", "import sys; print('oops'); sys.exit(3)"], "thing", capture=True)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py SshRunnerTest RunCommandTest`
Expected: ERROR — `AttributeError: module 'deploy' has no attribute 'SshRunner'` / `run_command`.

- [ ] **Step 3: Write the implementation**

Append to `scripts/deploy.py`:

```python


# ---------------------------------------------------------------------------------------------
# Running commands
# ---------------------------------------------------------------------------------------------


def run_command(args: Sequence[str], what: str, capture: bool = False) -> str:
    result = subprocess.run(list(args), text=True, capture_output=capture)
    if result.returncode != 0:
        detail = (result.stdout + result.stderr).strip() if capture else ""
        raise DeployError(f"{what} exited with status {result.returncode}" + (f": {detail}" if detail else ""))
    return result.stdout if capture else ""


class SshRunner:
    """Runs the remote steps over one OpenSSH master connection, so the password is asked once."""

    def __init__(self, server: Server, work_dir: Path):
        self.server = server
        self.work_dir = work_dir
        self.socket = work_dir / "ssh-master"
        self.connected = False

    def connect_args(self) -> List[str]:
        return ["ssh", "-M", "-S", str(self.socket), "-fN", "-p", str(self.server.port), self.server.login]

    def remote_args(self, command: str, tty: bool = False) -> List[str]:
        return ["ssh", "-S", str(self.socket), *(["-t"] if tty else []), "-p", str(self.server.port),
                self.server.login, command]

    def sftp_args(self, batch_path: Path) -> List[str]:
        return ["sftp", "-o", f"ControlPath={self.socket}", "-P", str(self.server.port), "-b", str(batch_path),
                self.server.login]

    def close_args(self) -> List[str]:
        return ["ssh", "-S", str(self.socket), "-O", "exit", "-p", str(self.server.port), self.server.login]

    def connect(self) -> None:
        run_command(self.connect_args(), "ssh connection")
        self.connected = True

    def remote(self, command: str, capture: bool = False, tty: bool = False) -> str:
        return run_command(self.remote_args(command, tty), "remote command", capture=capture)

    def sftp(self, batch: str) -> None:
        batch_path = self.work_dir / "upload.sftp"
        batch_path.write_text(batch)
        run_command(self.sftp_args(batch_path), "sftp upload")

    def close(self) -> None:
        if self.connected:
            subprocess.run(self.close_args(), capture_output=True)
            self.connected = False
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 66 tests, `OK`.

- [ ] **Step 5: Commit**

```bash
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): run remote steps and sftp over one ssh master connection" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: The deploy — ordered steps, banner, dry run, failure report, `main`

**Files:**
- Modify: `scripts/deploy.py` (append the final section)
- Modify: `scripts/deploy_tests.py` (insert classes above `if __name__ == "__main__":`)

**Interfaces:**
- Consumes: everything above — `ENVS`, `Server`, `port_dir`, `REPO_ROOT`, `SOCKET_PARENT`, colors,
  `DeployError`, `build_parser` (Task 1); `Checkout` (Task 3); all command builders (Task 4);
  `SshRunner` (Task 5). `deploy()` depends only on these duck-typed methods:
  - checkout: `repo`, `prepare(env, dry_run)`, `head()`, `help_files()`, `help_problems()`, `create_tag(env, sha, help_names, day)`
  - runner: `connect()`, `remote(command, capture=False, tty=False)`, `sftp(batch)`, `close()`
- Produces:
  - `STEP_TITLES: Dict[int, str]` for steps 1–8
  - `paint(text, color, enabled) -> str`
  - `banner(env, server, sha, subject, help_names, color) -> str`
  - `failure_report(env, step, detail, color) -> str`
  - `dry_run_plan(env, server, repo, help_names) -> str`
  - `deploy(env, server, checkout, runner, *, dry_run, color=False, out=print-with-flush, today=None) -> int` (0 ok, 1 failed, 130 interrupted)
  - `main(argv=None) -> int`; `if __name__ == "__main__": sys.exit(main())`

Step numbering follows the spec: 1 pull and check, 2 connect, 3 pre-check, 4 backup, 5 upload,
6 source edits, 7 build, 8 tag; closing the connection always happens in `finally`. The failure
report tells the deployer what state the server is in: steps 1–4 changed no deployed files; steps
5–7 print the revert command (or "keeps no backup" for coders); step 8 means only the local tag
failed.

- [ ] **Step 1: Write the failing tests**

Insert above `if __name__ == "__main__":` in `scripts/deploy_tests.py`:

```python
# ---------------------------------------------------------------------------------------------
# The deploy
# ---------------------------------------------------------------------------------------------


class FakeCheckout:
    repo = Path("/home/me/RotS")

    def __init__(self, problems=(), warnings=()) -> None:
        self.problems = list(problems)
        self.warnings = list(warnings)
        self.prepared = []
        self.tags = []

    def prepare(self, env, dry_run):
        self.prepared.append(dry_run)
        return self.warnings

    def head(self):
        return "abc1234def5678", "Merge pull request #300"

    def help_files(self):
        return ["help", "help_tbl"]

    def help_problems(self):
        return self.problems

    def create_tag(self, env, sha, help_names, day):
        self.tags.append((env.name, sha, list(help_names), day))
        return f"{env.tag_prefix}{day.isoformat()}"


class FakeRunner:
    def __init__(self, fail_when=lambda call: False, unwritable=("",)) -> None:
        self.calls = []
        self.fail_when = fail_when
        self.unwritable = list(unwritable)

    def _record(self, call):
        self.calls.append(call)
        if self.fail_when(call):
            raise deploy.DeployError("boom")

    def connect(self):
        self._record(("connect",))

    def remote(self, command, capture=False, tty=False):
        self._record(("remote", command, tty))
        if capture:
            return self.unwritable.pop(0) if len(self.unwritable) > 1 else self.unwritable[0]
        return ""

    def sftp(self, batch):
        self._record(("sftp", batch))

    def close(self):
        self.calls.append(("close",))

    def kinds(self):
        labels = []
        for call in self.calls:
            if call[0] != "remote":
                labels.append(call[0])
            elif "sudo chown" in call[1]:
                labels.append("chown")
            elif "backup.new" in call[1]:
                labels.append("backup")
            elif "! -writable" in call[1]:
                labels.append("unwritable")
            elif "missing directory" in call[1]:
                labels.append("dirs")
            elif "sed -i" in call[1]:
                labels.append("edit")
            elif "make all" in call[1]:
                labels.append("build")
            else:
                labels.append("remote?")
        return labels


class DeployTest(unittest.TestCase):
    DAY = datetime.date(2026, 9, 13)

    def run_deploy(self, env_name, checkout=None, runner=None, dry_run=False):
        self.checkout = checkout or FakeCheckout()
        self.runner = runner or FakeRunner()
        self.output = []
        status = deploy.deploy(deploy.ENVS[env_name], SERVER, self.checkout, self.runner, dry_run=dry_run,
                               out=self.output.append, today=self.DAY)
        return status

    def text(self):
        return "\n".join(self.output)

    def test_successful_deploy_runs_every_step_in_order_and_tags(self) -> None:
        status = self.run_deploy("test")

        self.assertEqual(status, 0)
        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "unwritable", "backup", "sftp", "build", "close"])
        self.assertEqual(self.checkout.tags, [("test", "abc1234def5678", ["help", "help_tbl"], self.DAY)])
        self.assertEqual(self.checkout.prepared, [False])
        self.assertIn("Tag: test-2026-09-13.", self.text())
        self.assertIn("someone@example.org:/rots/dev-building4802", self.text())

    def test_4k_edits_the_source_after_upload_and_before_build(self) -> None:
        self.assertEqual(self.run_deploy("4k"), 0)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "unwritable", "backup", "sftp", "edit", "build", "close"])
        self.assertIn("USE_BIG_BROTHER", self.text())

    def test_coders_skips_the_backup(self) -> None:
        self.assertEqual(self.run_deploy("coders"), 0)

        self.assertNotIn("backup", self.runner.kinds())

    def test_test_target_is_not_tagged(self) -> None:
        self.assertEqual(self.run_deploy("zzz-forge-test"), 0)

        self.assertEqual(self.checkout.tags, [])

    def test_help_problems_stop_before_connecting(self) -> None:
        status = self.run_deploy("live", checkout=FakeCheckout(problems=["lib/text/help_tbl:92: bad"]))

        self.assertEqual(status, 1)
        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("lib/text/help_tbl:92: bad", self.text())
        self.assertIn("FAILED at step 1", self.text())

    def test_failed_pre_check_uploads_nothing(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "missing directory" in call[1])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds(), ["connect", "dirs", "close"])
        self.assertIn("FAILED at step 3", self.text())
        self.assertIn("Nothing was uploaded", self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_unwritable_files_are_chowned_with_a_tty_then_rechecked(self) -> None:
        runner = FakeRunner(unwritable=["/rots/dev-building4802/src/game.cpp\n", ""])

        self.assertEqual(self.run_deploy("test", runner=runner), 0)

        self.assertEqual(self.runner.kinds()[:5], ["connect", "dirs", "unwritable", "chown", "unwritable"])
        chown = [call for call in self.runner.calls if call[0] == "remote" and "sudo chown" in call[1]][0]
        self.assertTrue(chown[2])
        self.assertIn("/rots/dev-building4802/src/game.cpp", self.text())

    def test_still_unwritable_after_chown_stops_before_backup(self) -> None:
        runner = FakeRunner(unwritable=["/rots/dev-building4802/src/game.cpp\n"])

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertNotIn("backup", self.runner.kinds())
        self.assertIn("still not writable", self.text())

    def test_failed_upload_prints_the_revert_command_and_closes(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "sftp")

        self.assertEqual(self.run_deploy("test", runner=runner), 1)

        self.assertEqual(self.runner.kinds()[-1], "close")
        self.assertIn("FAILED at step 5", self.text())
        self.assertIn(deploy.revert_command(deploy.ENVS["test"]), self.text())
        self.assertEqual(self.checkout.tags, [])

    def test_failed_build_on_coders_says_there_is_no_backup(self) -> None:
        runner = FakeRunner(fail_when=lambda call: call[0] == "remote" and "make all" in call[1])

        self.assertEqual(self.run_deploy("coders", runner=runner), 1)

        self.assertIn("keeps no backup", self.text())

    def test_interrupt_closes_the_connection(self) -> None:
        runner = FakeRunner()
        runner.connect = mock.Mock(side_effect=KeyboardInterrupt)

        self.assertEqual(self.run_deploy("test", runner=runner), 130)

        self.assertEqual(self.runner.kinds(), ["close"])
        self.assertIn("FAILED at step 2 (connect): interrupted", self.text())

    def test_dry_run_prints_every_step_and_runs_nothing_remote(self) -> None:
        for name in deploy.ENVS:
            with self.subTest(env=name):
                self.assertEqual(self.run_deploy(name, dry_run=True), 0)

                self.assertEqual(self.runner.kinds(), ["close"])
                self.assertEqual(self.checkout.prepared, [True])
                self.assertEqual(self.checkout.tags, [])
                for number in range(2, 9):
                    self.assertIn(f"== {number}. {deploy.STEP_TITLES[number]}", self.text())
                self.assertIn("ssh -M -S", self.text())
                self.assertIn('put "help_tbl"', self.text())

    def test_warnings_are_shown(self) -> None:
        self.run_deploy("zzz-forge-test", checkout=FakeCheckout(warnings=["on 'feat/x'"]))

        self.assertIn("warning: on 'feat/x'", self.text())


class MainTest(unittest.TestCase):
    def test_wires_arguments_into_deploy(self) -> None:
        with mock.patch.object(deploy, "deploy", return_value=0) as run:
            self.assertEqual(deploy.main(["deploy", "4k", "someone@example.org", "2222", "--dry-run"]), 0)

        env, server, checkout, runner = run.call_args.args
        self.assertEqual(env, deploy.ENVS["4k"])
        self.assertEqual(server, SERVER)
        self.assertEqual(checkout.repo, deploy.REPO_ROOT)
        self.assertTrue(str(runner.socket).startswith("/tmp/rots-deploy-"))
        self.assertTrue(run.call_args.kwargs["dry_run"])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 scripts/deploy_tests.py DeployTest MainTest`
Expected: ERROR — `AttributeError: module 'deploy' has no attribute 'deploy'` / `main`.

- [ ] **Step 3: Write the implementation**

Append to `scripts/deploy.py`:

```python


# ---------------------------------------------------------------------------------------------
# The deploy
# ---------------------------------------------------------------------------------------------

STEP_TITLES = {
    1: "pull and check",
    2: "connect",
    3: "pre-check",
    4: "backup",
    5: "upload",
    6: "source edits",
    7: "build",
    8: "tag",
}


def paint(text: str, color: str, enabled: bool) -> str:
    return f"\033[{color}m{text}\033[0m" if enabled else text


def banner(env: Env, server: Server, sha: str, subject: str, help_names: Sequence[str], color: bool) -> str:
    edits = "; ".join(f"{e.path}: {e.old_line!r} -> {e.new_line!r}" for e in env.source_edits) or "none"
    return "\n".join([
        f"Deploying to: {paint(env.name, env.color, color)}  ->  "
        f"{server.login}:/rots/{paint(env.dir_name, env.color, color)}",
        f"Commit:       {sha[:7]} {subject}",
        f"Source edits: {edits}",
        f"Help files:   {', '.join(help_names)}",
    ])


def failure_report(env: Env, step: int, detail: str, color: bool) -> str:
    lines = [paint(f"FAILED at step {step} ({STEP_TITLES[step]}): {detail}", BOLD_RED, color)]
    if step <= 4:
        lines.append("Nothing was uploaded; the source and help files on the server are unchanged.")
    elif step == 8:
        lines.append("The upload and build finished; only the local tag failed.")
    elif env.backup:
        lines.append(f"To revert: {revert_command(env)}")
    else:
        lines.append(f"{env.name} keeps no backup; to revert, deploy the previous commit.")
    return "\n".join(lines)


def dry_run_plan(env: Env, server: Server, repo: Path, help_names: Sequence[str]) -> str:
    shell = SshRunner(server, Path(SOCKET_PARENT) / "rots-deploy-XXXXXX")

    def ssh(command: str, tty: bool = False) -> str:
        return "  " + shlex.join(shell.remote_args(command, tty))

    lines = ["Dry run: nothing below is executed.", f"== 2. {STEP_TITLES[2]}", "  " + shlex.join(shell.connect_args())]
    lines += [f"== 3. {STEP_TITLES[3]}", ssh(missing_dirs_command(env)), ssh(unwritable_command(env, help_names)),
              "  only if something is unwritable:", ssh(chown_command(env, server.user, help_names), tty=True)]
    lines += [f"== 4. {STEP_TITLES[4]}",
              ssh(backup_command(env, help_names)) if env.backup else "  skipped: this env keeps no backup"]
    lines += [f"== 5. {STEP_TITLES[5]}", "  " + shlex.join(shell.sftp_args(shell.work_dir / "upload.sftp")),
              "  batch file:"]
    lines += ["    " + line for line in sftp_batch(env, repo, help_names).splitlines()]
    lines += [f"== 6. {STEP_TITLES[6]}"] + ([ssh(source_edit_command(env, e)) for e in env.source_edits] or ["  none"])
    lines += [f"== 7. {STEP_TITLES[7]}", ssh(build_command(env))]
    lines += [f"== 8. {STEP_TITLES[8]}",
              f"  git tag -a {env.tag_prefix}YYYY-MM-DD[-N] <sha>" if env.tag_prefix else "  none: test target"]
    lines += ["== close", "  " + shlex.join(shell.close_args())]
    return "\n".join(lines)


def _lines(output: str) -> List[str]:
    return [line for line in output.splitlines() if line.strip()]


def deploy(env: Env, server: Server, checkout, runner, *, dry_run: bool, color: bool = False,
           out: Callable[[str], None] = lambda line: print(line, flush=True),
           today: Optional[datetime.date] = None) -> int:
    """Run the deploy steps in order and return the process exit status."""
    step = 1
    tag = None

    def begin(number: int) -> None:
        nonlocal step
        step = number
        out(paint(f"== {number}. {STEP_TITLES[number]}", CYAN, color))

    try:
        begin(1)
        for warning in checkout.prepare(env, dry_run):
            out(paint(f"warning: {warning}", YELLOW, color))
        sha, subject = checkout.head()
        help_names = checkout.help_files()
        problems = checkout.help_problems()
        if problems:
            raise DeployError("these help files would break in-game help:\n  " + "\n  ".join(problems))
        out(banner(env, server, sha, subject, help_names, color))
        if dry_run:
            out(dry_run_plan(env, server, checkout.repo, help_names))
            return 0

        begin(2)
        runner.connect()

        begin(3)
        runner.remote(missing_dirs_command(env))
        unwritable = _lines(runner.remote(unwritable_command(env, help_names), capture=True))
        if unwritable:
            out(f"Not writable by {server.user}:\n  " + "\n  ".join(unwritable))
            out("Fixing ownership with sudo chown; sudo may ask for a password.")
            runner.remote(chown_command(env, server.user, help_names), tty=True)
            unwritable = _lines(runner.remote(unwritable_command(env, help_names), capture=True))
            if unwritable:
                raise DeployError("still not writable after chown:\n  " + "\n  ".join(unwritable))

        begin(4)
        if env.backup:
            runner.remote(backup_command(env, help_names))
        else:
            out(f"{env.name} keeps no backup; skipping.")

        begin(5)
        runner.sftp(sftp_batch(env, checkout.repo, help_names))

        begin(6)
        if not env.source_edits:
            out("none")
        for edit in env.source_edits:
            runner.remote(source_edit_command(env, edit))

        begin(7)
        runner.remote(build_command(env))

        begin(8)
        if env.tag_prefix:
            tag = checkout.create_tag(env, sha, help_names, today or datetime.date.today())
            out(f"Tagged {sha[:7]} as {tag} (local only).")
        else:
            out(f"{env.name} is a test target; no tag.")
    except DeployError as error:
        out(failure_report(env, step, str(error), color))
        return 1
    except KeyboardInterrupt:
        out(failure_report(env, step, "interrupted", color))
        return 130
    finally:
        runner.close()

    out(paint(f"Deployed {sha[:7]} to {env.name}.", GREEN, color)
        + (f" Tag: {tag}." if tag else "") + " Restart the port to run the new build.")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    user, host = args.login
    server = Server(user, host, args.port)
    color = sys.stdout.isatty() and "NO_COLOR" not in os.environ
    with tempfile.TemporaryDirectory(prefix="rots-deploy-", dir=SOCKET_PARENT) as work_dir:
        runner = SshRunner(server, Path(work_dir))
        return deploy(ENVS[args.env], server, Checkout(REPO_ROOT), runner, dry_run=args.dry_run, color=color)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 scripts/deploy_tests.py -v`
Expected: PASS — 80 tests, `OK`.

- [ ] **Step 5: Smoke-test the real entry point without the network**

Run: `scripts/deploy.py deploy zzz-forge-test someone@example.org 2222 --dry-run; echo "exit=$?"`
Expected, while `help_tbl:92` is still unfixed on this branch: a yellow warning that the checkout is
on `feat/deploy-script`, then `FAILED at step 1 (pull and check)` naming
`lib/text/help_tbl:92`, `Nothing was uploaded`, and `exit=1`. No ssh process starts. (After the
prerequisite fix is rebased in, the same command prints the banner and every step's command and
exits 0.)

Run: `scripts/deploy.py deploy live someone@example.org 2222 --dry-run; echo "exit=$?"`
Expected: `FAILED at step 1` because the checkout is not on `release-frodo`; `exit=1`.

Run: `scripts/deploy.py deploy live someone 2222; echo "exit=$?"`
Expected: usage error `expected <user>@<host>`; `exit=2`.

- [ ] **Step 6: Commit**

```bash
git add scripts/deploy.py scripts/deploy_tests.py
git commit -m "feat(deploy): run the deploy steps in order with dry run and failure report" -m "Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Manual verification against `/rots/zzz-forge-test` (Andrew present)

This task is run **with Andrew at the terminal**: he types the ssh password, and he supplies the real
`<user>@<host> <ssh-port>` (from his own notes — do not write them into any file). Every command
targets `zzz-forge-test` or `zzz-forge-test-4k` only. Stop and report at the first surprise.

The manual test count and expectations elsewhere in this plan predate the final-review fix wave and
may be stale; the automated suite size to expect is whatever `python3 scripts/deploy_tests.py`
reports after those fixes.

**Files:** none changed unless a bug is found (then fix it with a failing test first, as in Tasks 1–6).

**Prerequisites:**
- The `lib/text/help_tbl:92` fix is merged to `release-frodo` and `feat/deploy-script` is rebased onto it.
- `/rots/zzz-forge-test` exists on the server with `src`, `bin`, and `lib/text` (Andrew created `src`;
  `bin` and `lib/text` may need `mkdir`, or `make setup` from `src`).
- Before trusting step 3's `sudo chown` on any real port (not part of this task, but before it is ever
  run against one): on the server record the service users (`systemctl show -p User rotslive
  rotsbuilding rotscoding`) and the owner/group/mode of each port's `src`, `bin`, and `lib/text`
  (`stat -c '%U:%G %a %n' ...`). If a service runs as a different user and relies on owning those
  directories, stop and redesign the ownership fix (e.g. `chgrp`/`chmod g+w`) before deploying to a
  real port.

- [ ] **Step 1: Dry run every env**

Run (Andrew): `for e in live 4k test coders zzz-forge-test zzz-forge-test-4k; do scripts/deploy.py deploy $e <user>@<host> <ssh-port> --dry-run; done`
Expected: the four real ports stop at step 1 (feature branch); both zzz targets print the banner and
all commands and exit 0. Read the commands together.

- [ ] **Step 2: First real run**

Run (Andrew): `scripts/deploy.py deploy zzz-forge-test <user>@<host> <ssh-port>`
Expected: exactly one password prompt; no prompt for sftp (this confirms `sftp -o ControlPath` reuses
the master — the main assumption); steps 3–7 succeed; "no tag"; exit 0. Then on the server,
`ls /rots/zzz-forge-test/src/backup/lib-text` lists the help files that existed before, and
`cmp` of one uploaded help table against the local copy shows no difference.

- [ ] **Step 3: Second run replaces the backup cleanly**

Run the same command again.
Expected: exit 0; `/rots/zzz-forge-test/src/backup/backup` does not exist; no `backup.new` left over.

- [ ] **Step 4: The 4k source edit**

Run (Andrew): `scripts/deploy.py deploy zzz-forge-test-4k <user>@<host> <ssh-port>`
Expected: exit 0; on the server `grep USE_BIG_BROTHER /rots/zzz-forge-test/src/big_brother.h` shows
`#define USE_BIG_BROTHER 0`; locally `git diff` is empty and the file still says `1`.

- [ ] **Step 5: Ownership pre-check**

Two cases, then `deploy zzz-forge-test`:
- A copy of a source file in `/rots/zzz-forge-test/src` owned by another user (`sudo chown root`
  then `sudo chmod 644`). Expected: step 3 lists it, runs `sudo chown` on what is inside the folders
  with a terminal (one sudo prompt), re-checks, and continues — or, if sudo is refused, stops with
  "still not writable" before any backup or upload.
- One of the three folders itself unwritable (e.g. `lib/text` owned by another user, mode 750).
  Expected: step 3 stops at once, names the folder, asks for no sudo password, and changes nothing.

- [ ] **Step 6: A broken help table stops before connecting**

On a throwaway local commit, add a line `#oops` to the middle of `lib/text/scr_tbl`. Run
`deploy zzz-forge-test ... --dry-run`.
Expected: `FAILED at step 1` naming `lib/text/scr_tbl:<line>`, and no password prompt. Then drop the
throwaway commit (`git reset --hard HEAD~1` on the feature branch — confirm `git log` first).

- [ ] **Step 7: Revert drill**

Deploy `zzz-forge-test`. On a throwaway local commit, change a source file and a help file, then
deploy `zzz-forge-test` again. Run `scripts/deploy.py revert zzz-forge-test <user>@<host> <ssh-port>`.
Expected: `bin/ageland` was relinked (its mtime changed), the source and help files are back to the
previous deploy's contents, and `src/DEPLOY_IN_PROGRESS` is gone afterward. Then drop the throwaway
commit.

- [ ] **Step 8: Interrupted-deploy drill**

Run `deploy zzz-forge-test` and press Ctrl-C during step 5 or step 7.
Expected: the failure report is shown, including the `src/DEPLOY_IN_PROGRESS` warning (and, if
interrupted at step 7, the "remote make may still be running" line). Run `deploy zzz-forge-test`
again and confirm it stops at step 3 on the marker. Run `scripts/deploy.py revert zzz-forge-test ...`, then run
`deploy zzz-forge-test` once more and confirm it proceeds normally.

- [ ] **Step 9: Report**

Summarize for Andrew which steps passed, anything surprising, and whether he considers the script
proven for the real ports. Do not run it against any real port as part of this plan.
