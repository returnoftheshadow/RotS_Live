from __future__ import annotations

import ast
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Optional, Sequence

import pytest

from rots_harness import asan_selection, ci_groups, groups, run_plan

# The registered groups the tests choose from, in registration order.
GROUPS = ["bank", "boot", "poison", "shaping", "shaping_commands"]
# The scenario files each test repository holds.
SCENARIO_FILES = ("test_bank.py", "test_poison.py")
# The real checkout, whose scenarios, groups and workflow the checks against it read.
REAL_REPO_ROOT = Path(__file__).resolve().parents[3]
# The ASan workflow the contract tests read.
WORKFLOW_PATH = REAL_REPO_ROOT / ".github/workflows/asan.yml"


@pytest.fixture
def repo_root(tmp_path: Path) -> Path:
    """A repository with a group file per name in GROUPS (shaping_commands as a subgroup) and the SCENARIO_FILES."""
    root = tmp_path / "repo"
    groups_dir = root / ci_groups.GROUPS_DIRECTORY
    (groups_dir / "shaping").mkdir(parents=True)
    for name in ("bank", "boot", "poison", "shaping"):
        (groups_dir / f"{name}.txt").write_text(f"the {name} group\n", encoding="utf-8")
    (groups_dir / "shaping" / "commands.txt").write_text("the shaping commands\n", encoding="utf-8")
    scenarios = root / "tests/integration/scenarios"
    scenarios.mkdir(parents=True)
    for name in SCENARIO_FILES:
        (scenarios / name).write_text("", encoding="utf-8")
    return root


def select(body: str, repo_root: Path) -> asan_selection.AsanSelection:
    return asan_selection.select_tests(body, GROUPS, repo_root)


def texts(body: str) -> list[str]:
    return [token.text for token in asan_selection.description_tokens(body)]


# Reading the tokens.


def test_each_token_form_is_read_with_its_name_and_value() -> None:
    body = "@asan-test-all @asan-test-poison @asan-test-unit=Bank.* @asan-test-scenario=scenarios/test_bank.py::test_a"
    assert asan_selection.description_tokens(body) == [
        asan_selection.Token("@asan-test-all", "all", None),
        asan_selection.Token("@asan-test-poison", "poison", None),
        asan_selection.Token("@asan-test-unit=Bank.*", "unit", "Bank.*"),
        asan_selection.Token("@asan-test-scenario=scenarios/test_bank.py::test_a", "scenario", "scenarios/test_bank.py::test_a"),
    ]


def test_tokens_inside_html_comments_are_ignored() -> None:
    body = "<!-- Ask with @asan-test-all or\n@asan-test-unit=Foo.* -->\n@asan-test-poison"
    assert texts(body) == ["@asan-test-poison"]


def test_a_repeated_token_is_read_once() -> None:
    assert texts("@asan-test-unit=A.*\n@asan-test-unit=A.*\n@asan-test-unit=B.*") == ["@asan-test-unit=A.*", "@asan-test-unit=B.*"]


@pytest.mark.parametrize("text", ["foo@asan-test-poison", "x-@asan-test-poison", "@asan-test-<group>", "asan-test-poison", "@asan-test-", "@integration-test-poison"])
def test_text_that_is_not_a_token_is_ignored(text: str) -> None:
    assert texts(text) == []


def test_a_group_token_ends_at_punctuation() -> None:
    assert texts("Run (@asan-test-poison).") == ["@asan-test-poison"]


def test_a_value_runs_to_the_next_whitespace() -> None:
    assert texts("@asan-test-unit=Foo.*;rm\tnext") == ["@asan-test-unit=Foo.*;rm"]


def test_integration_tokens_are_not_asan_tokens() -> None:
    assert ci_groups.description_tokens("@asan-test-poison @asan-test-all") == []


# Reading the description from the event file.


def test_read_description_reads_the_pull_requests_body(tmp_path: Path) -> None:
    event = tmp_path / "event.json"
    event.write_text(json.dumps({"action": "edited", "pull_request": {"body": "@asan-test-poison\n\U0001F600" * 3}}), encoding="utf-8")
    assert asan_selection.read_description(event) == "@asan-test-poison\n\U0001F600" * 3


def test_read_description_reads_a_missing_body_as_empty(tmp_path: Path) -> None:
    event = tmp_path / "event.json"
    event.write_text(json.dumps({"pull_request": {"body": None}}), encoding="utf-8")
    assert asan_selection.read_description(event) == ""


def test_read_description_refuses_an_event_without_a_pull_request(tmp_path: Path) -> None:
    event = tmp_path / "event.json"
    event.write_text(json.dumps({"push": {}}), encoding="utf-8")
    with pytest.raises(ValueError, match="holds no pull_request object"):
        asan_selection.read_description(event)


# Choosing the tests.


def test_all_runs_every_unit_test_and_every_scenario_sharded(repo_root: Path) -> None:
    selection = select("@asan-test-all", repo_root)
    assert selection.unit_mode() == "all"
    assert selection.scenario_mode() == "all"
    assert selection.shard_numbers(8) == list(range(1, 9))


def test_all_wins_over_every_other_token_but_keeps_them_for_their_checks(repo_root: Path) -> None:
    selection = select("@asan-test-poison @asan-test-unit=Foo.* @asan-test-all @asan-test-scenario=scenarios/test_bank.py", repo_root)
    assert selection.unit_mode() == "all"
    assert selection.scenario_mode() == "all"
    assert (selection.unit_filters, selection.groups, selection.scenario_ids) == (["Foo.*"], ["poison"], ["scenarios/test_bank.py"])
    assert selection.reasons[-1] == "@asan-test-all covers every other token, which must still select tests"


def test_a_group_token_runs_that_group_and_no_unit_tests(repo_root: Path) -> None:
    selection = select("@asan-test-shaping_commands", repo_root)
    assert selection.groups == ["shaping_commands"]
    assert selection.unit_mode() == "none"
    assert selection.scenario_mode() == "selected"
    assert selection.shard_numbers(8) == [1]


def test_unit_tokens_run_only_unit_tests(repo_root: Path) -> None:
    selection = select("@asan-test-unit=Bank.* @asan-test-unit=Vault.Opens:Vault.Closes", repo_root)
    assert selection.unit_filters == ["Bank.*", "Vault.Opens:Vault.Closes"]
    assert selection.unit_mode() == "filters"
    assert selection.scenario_mode() == "none"


def test_scenario_tokens_run_only_scenarios(repo_root: Path) -> None:
    selection = select("@asan-test-scenario=scenarios/test_bank.py @asan-test-scenario=scenarios/test_poison.py::test_ticks", repo_root)
    assert selection.scenario_ids == ["scenarios/test_bank.py", "scenarios/test_poison.py::test_ticks"]
    assert selection.unit_mode() == "none"
    assert selection.scenario_mode() == "selected"


def test_tokens_combine_and_each_gives_a_reason(repo_root: Path) -> None:
    selection = select("@asan-test-unit=Bank.* @asan-test-poison @asan-test-scenario=scenarios/test_bank.py::test_a", repo_root)
    assert (selection.unit_filters, selection.groups, selection.scenario_ids) == (["Bank.*"], ["poison"], ["scenarios/test_bank.py::test_a"])
    assert selection.reasons == [
        "`@asan-test-unit=Bank.*`: the unit tests matching `Bank.*`",
        "`@asan-test-poison`: the integration group `poison`",
        "`@asan-test-scenario=scenarios/test_bank.py::test_a`: the scenarios of `scenarios/test_bank.py::test_a`",
    ]


@pytest.mark.parametrize("scenario_id", [
    "scenarios/test_bank.py::test_a_banker_keeps_coins",
    "scenarios/test_bank.py::test_crowded_floor[exactly-100]",
    "scenarios/test_bank.py::test_shaping[mob-/new]",
    "scenarios/test_bank.py::test_percent[%F]",
    "scenarios/test_bank.py::test_tilde[~f]",
])
def test_a_scenario_id_may_name_a_test_and_its_parameter_id(repo_root: Path, scenario_id: str) -> None:
    assert select(f"@asan-test-scenario={scenario_id}", repo_root).scenario_ids == [scenario_id]


@pytest.mark.parametrize("unit_filter", ["*", "Bank.*", "Bank?Test.Opens", "A.B:C.*", "Suite_1.Test_2"])
def test_a_unit_filter_of_positive_patterns_is_accepted(repo_root: Path, unit_filter: str) -> None:
    assert select(f"@asan-test-unit={unit_filter}", repo_root).unit_filters == [unit_filter]


# Refusing tokens.


@pytest.mark.parametrize("token, reason", [
    ("@asan-test-unit=Foo.*;rm", "may hold only letters, digits and _ . * ? :, not ';'"),
    ("@asan-test-unit=$(id)", "may hold only letters, digits and _ . * ? :, not '$'"),
    ("@asan-test-unit=Foo|Bar", "not '|'"),
    ("@asan-test-unit=Foo&&Bar", "not '&'"),
    ("@asan-test-unit=../Foo", "not '/'"),
    ("@asan-test-unit=-Foo.*", "may not hold '-'"),
    ("@asan-test-unit=Foo.*-Foo.Slow", "may not hold '-'"),
    ("@asan-test-unit=", "needs a GoogleTest filter after '='"),
    ("@asan-test-unit=Foo::Bar", "has an empty pattern beside a ':'"),
    ("@asan-test-unit=:Foo", "has an empty pattern beside a ':'"),
    ("@asan-test-unit", "needs =<GoogleTest filter>"),
])
def test_an_unusable_unit_filter_stops_the_run_naming_the_token(repo_root: Path, token: str, reason: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select(f"Fixes it.\n{token}\n", repo_root)
    assert str(invalid.value).startswith(f"{token}: ")
    assert reason in str(invalid.value)


@pytest.mark.parametrize("value, reason", [
    ("scenarios/../conftest.py", "may not hold '..'"),
    ("scenarios/test_bank.py::test_a[..]", "may not hold '..'"),
    ("/etc/passwd", "must be relative to tests/integration, not an absolute path"),
    ("tests/integration/scenarios/test_bank.py", "leave out the leading tests/integration/"),
    ("scenarios/test_gone.py", "names tests/integration/scenarios/test_gone.py, which does not exist"),
    ("scenarios/conftest.py", "must be scenarios/test_<name>.py"),
    ("unit/test_groups.py", "must be scenarios/test_<name>.py"),
    ("scenarios/test_bank.py;rm", "must be scenarios/test_<name>.py"),
    ("scenarios/test_bank.py::test_a$(id)", "must be scenarios/test_<name>.py"),
    ("scenarios/test_bank.py::test_a[x]extra", "must be scenarios/test_<name>.py"),
    ("scenarios/test_bank.py::1test", "must be scenarios/test_<name>.py"),
    ("scenarios/sub/test_bank.py", "must be scenarios/test_<name>.py"),
    ("", "needs a scenario id after '='"),
])
def test_an_unusable_scenario_id_stops_the_run_naming_the_token(repo_root: Path, value: str, reason: str) -> None:
    token = f"@asan-test-scenario={value}"
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select(token, repo_root)
    assert str(invalid.value).startswith(f"{token}: ")
    assert reason in str(invalid.value)


@pytest.mark.parametrize("token", [
    "`@asan-test-unit=Bank.*`",
    "@asan-test-unit=Bank.*,",
    "@asan-test-scenario=scenarios/test_bank.py.",
    "(@asan-test-scenario=scenarios/test_bank.py)",
    "@asan-test-scenario=scenarios/test_bank.py`",
])
def test_a_value_carrying_prose_punctuation_or_backticks_gets_the_plain_text_hint(repo_root: Path, token: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match=re.escape(asan_selection.PLAIN_TEXT_HINT)):
        select(f"Run {token} please", repo_root)


def test_a_value_refused_for_another_reason_gets_no_plain_text_hint(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select("@asan-test-unit=Foo;rm", repo_root)
    assert asan_selection.PLAIN_TEXT_HINT not in str(invalid.value)


@pytest.mark.parametrize("token, expected", [
    ("@ASAN-TEST-all", "@ASAN-TEST-all: tokens are lower case; write @asan-test-"),
    ("@Asan-Test-unit=Foo.*", "@Asan-Test-unit=Foo.*: tokens are lower case; write @asan-test-"),
    ("@asan-test-ALL", "@asan-test-ALL: tokens are lower case; write @asan-test-all"),
    ("@asan-test-Unit=Foo.*", "@asan-test-Unit=Foo.*: tokens are lower case; write @asan-test-unit"),
])
def test_a_token_in_capitals_is_refused_with_the_lower_case_hint(repo_root: Path, token: str, expected: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select(token, repo_root)
    assert expected in str(invalid.value).splitlines()


def test_a_scenario_token_without_a_value_is_refused(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match=r"^@asan-test-scenario: needs =<scenario id>$"):
        select("@asan-test-scenario", repo_root)


def test_an_unknown_group_stops_the_run_and_lists_the_registered_ones(repo_root: Path) -> None:
    expected = (
        r"^the description names unknown integration groups: posion, shaping_comands; registered groups: bank, boot, poison, "
        r"shaping, shaping_commands \(or all, unit=<filter>, scenario=<id>\)$"
    )
    with pytest.raises(asan_selection.InvalidTokens, match=expected):
        select("@asan-test-posion @asan-test-shaping_comands", repo_root)


def test_an_unknown_group_in_capitals_adds_the_lower_case_hint(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match=r"unknown integration groups: Poison; .*; tokens are lower case$"):
        select("@asan-test-Poison", repo_root)


@pytest.mark.parametrize("token", ["@asan-test-all=yes", "@asan-test-poison=1"])
def test_only_unit_and_scenario_take_a_value(repo_root: Path, token: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match="takes no '='"):
        select(token, repo_root)


def test_every_unusable_token_is_listed_even_beside_all(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select("@asan-test-all @asan-test-unit=-Foo @asan-test-scenario=scenarios/test_gone.py @asan-test-posion", repo_root)
    lines = str(invalid.value).splitlines()
    assert [line.split(":")[0] for line in lines] == ["@asan-test-unit=-Foo", "@asan-test-scenario=scenarios/test_gone.py", "the description names unknown integration groups"]


# Echoing description text safely.


@pytest.mark.parametrize("value, echoed", [
    ("##[error]x", "??[error]x"),
    ("Foo‮Bar", "Foo?Bar"),
    ("Foo​Bar", "Foo?Bar"),
    ("Foo⁦Bar", "Foo?Bar"),
    ("Foo\x1b[31mBar", "Foo?[31mBar"),
    ("Foo\x00Bar", "Foo?Bar"),
    ("café", "caf?"),
])
def test_echo_safe_replaces_every_character_outside_its_set(value: str, echoed: str) -> None:
    assert asan_selection.echo_safe(value) == echoed


@pytest.mark.parametrize("value", ["##[error]Injected", "Foo‮Bar", "Foo​Bar", "Foo\x1b[2JBar", "::error::Injected"])
def test_an_invalid_token_is_echoed_without_controls_or_a_workflow_command(repo_root: Path, value: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select(f"@asan-test-unit={value}", repo_root)
    message = str(invalid.value)
    assert all(asan_selection.ECHO_SAFE_CHARACTER.fullmatch(character) for character in message.replace("\n", ""))
    assert "##[" not in message
    assert not any(line.startswith(("::", "##")) for line in message.splitlines())


def test_select_prints_an_invalid_token_safely(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    status, _, summary = run_select(repo_root, tmp_path, "@asan-test-unit=##[error]x‮")
    assert status == 2
    error = capsys.readouterr().err
    assert "@asan-test-unit=??[error]x?:" in error
    assert "‮" not in error + summary
    assert "##[" not in error + summary


# Deciding whether an event runs the tests.


@pytest.mark.parametrize("action", ["opened", "synchronize", "reopened", "edited"])
def test_every_event_runs_while_tokens_are_present(action: str) -> None:
    run, why = asan_selection.run_decision(action, asan_selection.description_tokens("@asan-test-poison"))
    assert run
    assert why == f"the pull request event is {action}, and the description has @asan-test tokens"


@pytest.mark.parametrize("action", ["opened", "synchronize", "edited"])
def test_a_description_without_tokens_runs_nothing(action: str) -> None:
    tokens = asan_selection.description_tokens("<!-- @asan-test-all --> Plain description.")
    assert asan_selection.run_decision(action, tokens) == (False, "the description has no @asan-test token")


# The select command.


def run_select(repo_root: Path, tmp_path: Path, description: Optional[str], action: str = "opened", event: Optional[Path] = None) -> tuple[int, str, str]:
    """Runs select over repo_root with description (or the event file); returns its status, the output file's text and the summary's."""
    output = tmp_path / "output"
    summary = tmp_path / "summary.md"
    argv = ["--repo-root", str(repo_root), "--summary", str(summary), "select", "--action", action, "--output", str(output), "--full-suite-shards", "8"]
    if event is not None:
        argv += ["--event", str(event)]
    status = asan_selection.main(argv, environ={}, description=description)
    read = lambda path: path.read_text(encoding="utf-8") if path.exists() else ""
    return status, read(output), read(summary)


def test_select_writes_every_output_on_one_line_each(repo_root: Path, tmp_path: Path) -> None:
    body = "$(rm -rf /) @asan-test-unit=Bank.* @asan-test-poison @asan-test-scenario=scenarios/test_bank.py::test_a"
    status, output, summary = run_select(repo_root, tmp_path, body)
    assert status == 0
    assert output == (
        "run=true\nunit=filters\nunit_filters=[\"Bank.*\"]\nscenarios=selected\ngroups=[\"poison\"]\n"
        "scenario_ids=[\"scenarios/test_bank.py::test_a\"]\nshards=[1]\n"
    )
    assert summary.startswith("### ASan run: chosen unit tests; chosen scenarios\n\n- Why: the pull request event is opened")


def test_select_reads_the_description_from_the_event_file(repo_root: Path, tmp_path: Path) -> None:
    event = tmp_path / "event.json"
    event.write_text(json.dumps({"pull_request": {"body": "@asan-test-poison"}}), encoding="utf-8")
    status, output, _ = run_select(repo_root, tmp_path, None, event=event)
    assert status == 0
    assert "groups=[\"poison\"]\n" in output


def test_select_reads_the_event_file_github_event_path_names(repo_root: Path, tmp_path: Path) -> None:
    event = tmp_path / "event.json"
    event.write_text(json.dumps({"pull_request": {"body": "@asan-test-bank"}}), encoding="utf-8")
    output = tmp_path / "output"
    status = asan_selection.main(
        ["--repo-root", str(repo_root), "select", "--action", "edited", "--output", str(output)], environ={"GITHUB_EVENT_PATH": str(event)},
    )
    assert status == 0
    assert "groups=[\"bank\"]\n" in output.read_text(encoding="utf-8")


def test_select_fails_with_status_2_without_an_event_file(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    status, output, _ = run_select(repo_root, tmp_path, None)
    assert status == 2
    assert output == ""
    assert "no event file" in capsys.readouterr().err


def test_select_for_all_writes_every_shard_and_the_tokens_beside_it(repo_root: Path, tmp_path: Path) -> None:
    status, output, summary = run_select(repo_root, tmp_path, "@asan-test-all @asan-test-unit=Bank.* @asan-test-poison")
    assert status == 0
    assert output == (
        "run=true\nunit=all\nunit_filters=[\"Bank.*\"]\nscenarios=all\ngroups=[\"poison\"]\n"
        "scenario_ids=[]\nshards=[1, 2, 3, 4, 5, 6, 7, 8]\n"
    )
    assert summary.startswith("### ASan run: every unit test; every scenario, split across 8 shards\n")


def test_select_writes_no_run_without_tokens(repo_root: Path, tmp_path: Path) -> None:
    status, output, summary = run_select(repo_root, tmp_path, "Plain words. <!-- @asan-test-all -->", action="edited")
    assert status == 0
    assert output == "run=false\nunit=none\nunit_filters=[]\nscenarios=none\ngroups=[]\nscenario_ids=[]\nshards=[1]\n"
    assert summary == "### ASan run: not started\n\n- the description has no @asan-test token\n"


def test_select_fails_with_status_2_on_an_unusable_token_and_writes_no_output(
    repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str],
) -> None:
    status, output, summary = run_select(repo_root, tmp_path, "@asan-test-unit=Foo;rm @asan-test-posion")
    assert status == 2
    assert output == ""
    error = capsys.readouterr().err
    assert "@asan-test-unit=Foo;rm: may hold only" in error
    assert "unknown integration groups: posion" in error
    # Indented, so the description's text renders as code in the summary.
    assert "\n    @asan-test-unit=Foo;rm: may hold only" in summary


def test_select_fails_with_status_2_on_an_unreadable_group_file(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    (repo_root / ci_groups.GROUPS_DIRECTORY / "bad-name.txt").write_text("a group\n", encoding="utf-8")
    status, output, _ = run_select(repo_root, tmp_path, "@asan-test-poison")
    assert status == 2
    assert output == ""
    assert "cannot read the scenario groups" in capsys.readouterr().err


# The unit tests.

# What --gtest_list_tests prints for two suites.
GTEST_LISTING = "BankTest.\n  KeepsCoins\n  KeepsItems\nVault/Typed.  # TypeParam = int\n  Opens  # GetParam() = 1\n"


def test_listed_tests_reads_the_full_names() -> None:
    assert asan_selection.listed_tests(GTEST_LISTING) == ["BankTest.KeepsCoins", "BankTest.KeepsItems", "Vault/Typed.Opens"]


def test_listed_tests_reads_an_empty_listing_as_no_tests() -> None:
    assert asan_selection.listed_tests("") == []


class FakeUnitBinary:
    """Lists tests per filter from a table and records every command it runs."""

    def __init__(self, listings: dict[str, str], run_status: int = 0, list_status: int = 0) -> None:
        # The --gtest_list_tests output for each filter; an unknown filter lists nothing.
        self.listings = listings
        # The status the run returns.
        self.run_status = run_status
        # The status every listing returns.
        self.list_status = list_status
        # Every listing command, in order.
        self.listed: list[list[str]] = []
        # Every run command, in order.
        self.ran: list[list[str]] = []

    def list_tests(self, command: list[str]) -> tuple[int, str]:
        self.listed.append(command)
        unit_filter = command[-1].removeprefix("--gtest_filter=")
        return self.list_status, self.listings.get(unit_filter, "")

    def run(self, command: list[str], keep_stdin: bool) -> int:
        self.ran.append(command)
        return self.run_status


def run_unit(binary: FakeUnitBinary, environ: dict[str, str], tmp_path: Path) -> int:
    return asan_selection.main(
        ["--summary", str(tmp_path / "summary.md"), "run-unit", "--binary", "bin/tests"],
        environ=environ, list_tests=binary.list_tests, run_command=binary.run,
    )


def test_run_unit_checks_each_filter_then_runs_them_joined_once(tmp_path: Path) -> None:
    binary = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n", "Vault.Opens": "Vault.\n  Opens\n"})
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*", "Vault.Opens"])}
    assert run_unit(binary, environ, tmp_path) == 0
    assert binary.listed == [
        ["bin/tests", "--gtest_list_tests", "--gtest_filter=Bank*"],
        ["bin/tests", "--gtest_list_tests", "--gtest_filter=Vault.Opens"],
    ]
    assert binary.ran == [["bin/tests", "--gtest_filter=Bank*:Vault.Opens"]]
    summary = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "- `@asan-test-unit=Bank*`: 1 test\n  - `BankTest.KeepsCoins`\n" in summary
    assert summary.endswith("\nThe unit tests passed.\n")


def test_run_unit_fails_without_running_when_a_filter_matches_nothing(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    # GoogleTest itself would pass a run of zero tests.
    binary = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n"})
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*", "Bnak*"])}
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert binary.ran == []
    assert "@asan-test-unit=Bnak*: matches no unit test\n" in capsys.readouterr().err + "\n"


def test_a_filter_ending_in_a_full_stop_that_matches_nothing_gets_the_plain_text_hint(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    binary = FakeUnitBinary({})
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank.Opens."])}
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert asan_selection.PLAIN_TEXT_HINT in capsys.readouterr().err


def test_run_unit_fails_when_the_binary_cannot_list_its_tests(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    binary = FakeUnitBinary({"Bank*": "ERROR: AddressSanitizer: heap-use-after-free\n"}, list_status=23)
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*"])}
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert binary.ran == []
    assert "listing the unit tests failed (exit status 23)" in capsys.readouterr().err


def test_run_unit_returns_the_tests_status(tmp_path: Path) -> None:
    binary = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n"}, run_status=23)
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*"])}
    assert run_unit(binary, environ, tmp_path) == 23
    assert (tmp_path / "summary.md").read_text(encoding="utf-8").endswith("\nThe unit tests failed with exit status 23.\n")


def test_run_unit_for_all_runs_every_test_named_as_it_runs(tmp_path: Path) -> None:
    binary = FakeUnitBinary({})
    assert run_unit(binary, {"ASAN_UNIT": "all", "ASAN_UNIT_FILTERS": "[]"}, tmp_path) == 0
    assert binary.listed == []
    assert binary.ran == [["bin/tests"]]


def test_run_unit_for_all_still_checks_the_filters_beside_it(tmp_path: Path) -> None:
    binary = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n"})
    environ = {"ASAN_UNIT": "all", "ASAN_UNIT_FILTERS": json.dumps(["Bank*", "Bnak*"])}
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert binary.ran == []
    passing = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n"})
    assert run_unit(passing, {"ASAN_UNIT": "all", "ASAN_UNIT_FILTERS": json.dumps(["Bank*"])}, tmp_path) == 0
    assert passing.ran == [["bin/tests"]]


def test_run_unit_for_none_runs_nothing(tmp_path: Path) -> None:
    binary = FakeUnitBinary({})
    assert run_unit(binary, {"ASAN_UNIT": "none"}, tmp_path) == 0
    assert binary.ran == []


@pytest.mark.parametrize("environ", [
    {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Foo;rm"])},
    {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["-Foo"])},
    {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": "[]"},
    {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": "Foo.*"},
    {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps([1])},
    {"ASAN_UNIT": "some"},
    {},
])
def test_run_unit_checks_its_inputs_again(environ: dict[str, str], tmp_path: Path) -> None:
    binary = FakeUnitBinary({"Foo;rm": "Foo.\n  Bar\n", "-Foo": "Foo.\n  Bar\n"})
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_USAGE
    assert binary.listed == []
    assert binary.ran == []


# The scenarios.


class FakeCollector:
    """Collects node ids per group or targets from a table and records every command it runs."""

    def __init__(self, collected: dict[tuple[str, ...], list[str]], run_status: int = 0) -> None:
        # The node ids each group (as a one-item tuple) or target tuple collects; one missing from the table collects none.
        self.collected = collected
        # The status a run returns.
        self.run_status = run_status
        # Every run command, in order.
        self.ran: list[list[str]] = []

    def collect(self, group: str, targets: Optional[Sequence[str]]) -> list[str]:
        if targets == ["tests/integration/scenarios/test_bank.py::test_typo"]:
            raise asan_selection.NothingSelected("pytest could not collect it (exit status 4); its output is above")
        key = (group,) if targets is None else tuple(targets)
        return self.collected.get(key, [])

    def run(self, command: list[str], keep_stdin: bool) -> int:
        self.ran.append(command)
        return self.run_status


# What each group and target list collects in the scenario tests.
COLLECTED = {
    ("poison",): ["scenarios/test_poison.py::test_ticks", "scenarios/test_poison.py::test_wears_off"],
    ("tests/integration/scenarios/test_bank.py",): ["scenarios/test_bank.py::test_a", "scenarios/test_bank.py::test_b"],
    ("tests/integration/scenarios/test_poison.py::test_ticks",): ["scenarios/test_poison.py::test_ticks"],
}


def run_scenarios(collector: FakeCollector, environ: dict[str, str], repo_root: Path, tmp_path: Path) -> int:
    return asan_selection.main(
        ["--repo-root", str(repo_root), "--summary", str(tmp_path / "summary.md"), "run-scenarios", "--python", "py", "--jobs", "4"],
        environ=environ, collect=collector.collect, run_command=collector.run,
    )


def test_run_scenarios_runs_every_selected_test_once(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {
        "ASAN_SCENARIOS": "selected",
        "ASAN_GROUPS": json.dumps(["poison"]),
        "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py", "scenarios/test_poison.py::test_ticks"]),
    }
    assert run_scenarios(collector, environ, repo_root, tmp_path) == 0
    assert collector.ran == [[
        "py", "-m", "pytest",
        "tests/integration/scenarios/test_poison.py::test_ticks", "tests/integration/scenarios/test_poison.py::test_wears_off",
        "tests/integration/scenarios/test_bank.py::test_a", "tests/integration/scenarios/test_bank.py::test_b",
        "-q", "-n", "4", *run_plan.REPORT_ARGUMENTS,
    ]]
    summary = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "- `@asan-test-poison`: 2 tests\n" in summary
    assert "- `@asan-test-scenario=scenarios/test_poison.py::test_ticks`: 1 test\n  - `scenarios/test_poison.py::test_ticks`\n" in summary
    assert summary.endswith("\nThe scenarios passed.\n")


def write_run(repo_root: Path, name: str, test_id: str, log: str) -> None:
    """A kept run directory under repo_root holding test_id's test-id.txt and game.log."""
    run_dir = repo_root / asan_selection.RUNS_RELATIVE / name
    run_dir.mkdir(parents=True)
    (run_dir / "test-id.txt").write_text(test_id + "\n", encoding="utf-8")
    (run_dir / "game.log").write_text(log, encoding="utf-8")


def test_run_scenarios_reports_a_failure_with_its_artifact_and_sanitizer_lines(repo_root: Path, tmp_path: Path) -> None:
    def failing_run(command: list[str], keep_stdin: bool) -> int:
        write_run(repo_root, "abc", "scenarios/test_bank.py::test_a", "Boot db -- BEGIN.\n==36==ERROR: AddressSanitizer: stack-buffer-underflow on address 0x407fc56e\n")
        write_run(repo_root, "def", "scenarios/test_bank.py::test_b", "Entering game loop.\n")
        return 1

    environ = {"ASAN_SCENARIOS": "all", "ROTS_IT_SHARD": "3/8"}
    status = asan_selection.main(
        ["--repo-root", str(repo_root), "--summary", str(tmp_path / "summary.md"), "run-scenarios", "--python", "py", "--jobs", "4"],
        environ=environ, run_command=failing_run,
    )
    assert status == 1
    summary = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "The scenarios failed with pytest exit status 1. Their kept run directories are in the `asan-runs-shard-3` artifact." in summary
    assert "\n    scenarios/test_bank.py::test_a: ==36==ERROR: AddressSanitizer: stack-buffer-underflow on address 0x407fc56e\n" in summary
    assert "test_bank.py::test_b" not in summary


def test_sanitizer_report_lines_skip_logs_older_than_the_run(repo_root: Path) -> None:
    write_run(repo_root, "old", "scenarios/test_bank.py::test_a", "ERROR: AddressSanitizer: old report\n")
    old_log = repo_root / asan_selection.RUNS_RELATIVE / "old" / "game.log"
    os.utime(old_log, (1000.0, 1000.0))
    assert asan_selection.sanitizer_report_lines(repo_root / asan_selection.RUNS_RELATIVE, 2000.0) == []


def test_a_failure_without_reports_says_so() -> None:
    assert asan_selection.scenario_outcome_summary(1, "", []).endswith("`asan-runs-shard-1` artifact.\nNo game.log holds an AddressSanitizer report.\n")


def test_run_scenarios_returns_pytests_status(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector(COLLECTED, run_status=1)
    environ = {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])}
    assert run_scenarios(collector, environ, repo_root, tmp_path) == 1


@pytest.mark.parametrize("scenario_id, reason", [
    ("scenarios/test_poison.py", "@asan-test-scenario=scenarios/test_poison.py: selects no test"),
    ("scenarios/test_bank.py::test_typo", "@asan-test-scenario=scenarios/test_bank.py::test_typo: pytest could not collect it (exit status 4)"),
])
def test_run_scenarios_fails_without_running_when_a_token_selects_nothing(
    repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str], scenario_id: str, reason: str,
) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py", scenario_id])}
    assert run_scenarios(collector, environ, repo_root, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert collector.ran == []
    assert reason in capsys.readouterr().err


def test_run_scenarios_fails_when_a_group_selects_nothing(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIOS": "selected", "ASAN_GROUPS": json.dumps(["bank"])}
    assert run_scenarios(collector, environ, repo_root, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert "@asan-test-bank: selects no test" in capsys.readouterr().err


def test_run_scenarios_for_all_runs_make_integrations_ci_command(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector({})
    assert run_scenarios(collector, {"ASAN_SCENARIOS": "all", "ROTS_IT_SHARD": "3/8"}, repo_root, tmp_path) == 0
    assert collector.ran == [run_plan.pytest_run_command("py", "scenario", "4")]
    assert (tmp_path / "summary.md").read_text(encoding="utf-8") == "### ASan scenarios: every scenario, shard 3/8\n\nThe scenarios passed.\n"


def test_run_scenarios_with_one_job_runs_without_workers(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])}
    asan_selection.main(
        ["--repo-root", str(repo_root), "run-scenarios", "--python", "py", "--jobs", "1"],
        environ=environ, collect=collector.collect, run_command=collector.run,
    )
    assert "-n" not in collector.ran[0]


@pytest.mark.parametrize("environ", [
    {"ASAN_SCENARIOS": "selected", "ASAN_GROUPS": json.dumps(["posion"])},
    {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["scenarios/../conftest.py"])},
    {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["--collect-only"])},
    {"ASAN_SCENARIOS": "selected", "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_gone.py"])},
    {"ASAN_SCENARIOS": "selected"},
    {"ASAN_SCENARIOS": "every"},
    {"ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])},
])
def test_run_scenarios_checks_its_inputs_again(repo_root: Path, tmp_path: Path, environ: dict[str, str]) -> None:
    collector = FakeCollector(COLLECTED)
    assert run_scenarios(collector, environ, repo_root, tmp_path) == asan_selection.EXIT_USAGE
    assert collector.ran == []


def check_scenarios(collector: FakeCollector, environ: dict[str, str], repo_root: Path, tmp_path: Path) -> int:
    return asan_selection.main(
        ["--repo-root", str(repo_root), "--summary", str(tmp_path / "summary.md"), "check-scenarios", "--python", "py"],
        environ=environ, collect=collector.collect,
    )


@pytest.mark.parametrize("mode", ["selected", "all"])
def test_check_scenarios_lists_each_tokens_tests(repo_root: Path, tmp_path: Path, mode: str) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIOS": mode, "ASAN_GROUPS": json.dumps(["poison"]), "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])}
    assert check_scenarios(collector, environ, repo_root, tmp_path) == 0
    assert collector.ran == []
    summary = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert summary.startswith("### ASan scenarios each token selects\n\n- `@asan-test-poison`: 2 tests\n")


@pytest.mark.parametrize("mode", ["selected", "all"])
def test_check_scenarios_fails_when_a_token_selects_nothing(repo_root: Path, tmp_path: Path, mode: str) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIOS": mode, "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_poison.py"])}
    assert check_scenarios(collector, environ, repo_root, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED


def test_check_scenarios_needs_the_scenario_mode(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    collector = FakeCollector(COLLECTED)
    assert check_scenarios(collector, {"ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])}, repo_root, tmp_path) == asan_selection.EXIT_USAGE
    assert "ASAN_SCENARIOS must be none, all, selected" in capsys.readouterr().err


def test_a_long_list_of_tests_is_cut_short_in_the_summary() -> None:
    names = [f"Suite.Test{number}" for number in range(asan_selection.SUMMARY_NAME_LIMIT + 3)]
    lines = asan_selection.names_markdown(names)
    assert len(lines) == asan_selection.SUMMARY_NAME_LIMIT + 1
    assert lines[-1] == "  - and 3 more"


def test_a_collection_that_takes_too_long_names_the_token(monkeypatch: pytest.MonkeyPatch) -> None:
    def slow_collection(*arguments, **keywords):
        raise subprocess.TimeoutExpired("pytest", asan_selection.COLLECT_TIMEOUT_SECONDS)

    monkeypatch.setattr(run_plan, "collect_selection", slow_collection)
    collect = lambda group, targets: asan_selection.collect_node_ids("py", group, targets)
    with pytest.raises(asan_selection.NothingSelected, match=r"^@asan-test-scenario=scenarios/test_bank.py: collecting it took longer than 300 seconds$"):
        asan_selection.scenario_token_tests([], ["scenarios/test_bank.py"], collect)


# Collecting from the real checkout boots no server, so these run pytest itself.


def test_collect_node_ids_lists_a_real_scenario_files_tests() -> None:
    node_ids = asan_selection.collect_node_ids(sys.executable, "", ["tests/integration/scenarios/test_bank.py"])
    assert "scenarios/test_bank.py::test_a_banker_keeps_an_item_lists_it_and_hands_it_back" in node_ids
    assert all(node_id.startswith("scenarios/test_bank.py::") for node_id in node_ids)


def test_collect_node_ids_reports_a_test_that_does_not_exist() -> None:
    with pytest.raises(asan_selection.NothingSelected, match=r"exit status 4"):
        asan_selection.collect_node_ids(sys.executable, "", ["tests/integration/scenarios/test_bank.py::test_no_such_test"])


def test_collect_node_ids_selects_a_group_by_its_marker() -> None:
    node_ids = asan_selection.collect_node_ids(sys.executable, "bank", None)
    assert node_ids
    assert all(node_id.startswith("scenarios/test_bank") for node_id in node_ids)


def test_the_real_scenario_ids_in_the_shard_times_pass_the_id_check() -> None:
    # Every committed test id without a space must be writable as a token.
    recorded = json.loads((REAL_REPO_ROOT / "tests/integration/shard_durations.json").read_text(encoding="utf-8"))
    refused = [node_id for node_id in recorded if " " not in node_id and asan_selection.scenario_id_problem(node_id, REAL_REPO_ROOT)]
    assert refused == []


def test_no_registered_group_is_named_like_a_token_form() -> None:
    names = {group.name for group in groups.read_groups(REAL_REPO_ROOT / ci_groups.GROUPS_DIRECTORY)}
    assert names.isdisjoint(asan_selection.RESERVED_NAMES)


def test_no_scenario_harness_module_or_conftest_imports_the_asan_selector() -> None:
    # ci_groups exempts asan_selection.py from the every-group rule because nothing a scenario runs reaches it.
    integration = REAL_REPO_ROOT / "tests/integration"
    sources = [*integration.glob("rots_harness/*.py"), *integration.glob("**/conftest.py"), *integration.glob("scenarios/*.py")]
    importers = [path.name for path in sources if path.name != "asan_selection.py" and "asan_selection" in imported_names(path)]
    assert importers == []
    assert ci_groups.ASAN_ONLY_HARNESS_FILES == frozenset({"tests/integration/rots_harness/asan_selection.py"})


def imported_names(path: Path) -> set[str]:
    """Every part of every module name, and every name, the Python source at path imports."""
    names: set[str] = set()
    for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
        if isinstance(node, ast.Import):
            names.update(part for alias in node.names for part in alias.name.split("."))
        elif isinstance(node, ast.ImportFrom):
            names.update((node.module or "").split("."))
            names.update(alias.name for alias in node.names)
    return names


# The workflow and the selector agree on every name that passes between them.


def workflow_text() -> str:
    return WORKFLOW_PATH.read_text(encoding="utf-8")


def job_text(name: str) -> str:
    """The lines of one job in the workflow, from its key to the next job's."""
    match = re.search(rf"^  {name}:\n(.*?)(?=^  [a-z_]+:\n|\Z)", workflow_text(), re.M | re.S)
    assert match is not None, f"the workflow has no job {name}"
    return match.group(1)


def steps_running(command: str) -> list[str]:
    """The text of every workflow step whose command runs the selector's command."""
    steps = re.split(r"^      - name: ", workflow_text(), flags=re.M)
    return [step for step in steps if re.search(rf"rots_harness\.asan_selection\b.*?\s{re.escape(command)}\s", step, re.S)]


def output_names() -> list[str]:
    return [line.split("=", 1)[0] for line in asan_selection.output_lines(asan_selection.AsanSelection(), True, 1)]


def test_the_select_job_exposes_every_output_select_writes() -> None:
    outputs = re.search(r"^    outputs:\n((?:^      .*\n)+)", job_text("select"), re.M)
    assert outputs is not None
    declared = dict(re.findall(r"^      (\w+): \$\{\{ steps\.select\.outputs\.(\w+) \}\}$", outputs.group(1), re.M))
    assert sorted(declared) == sorted(output_names())
    assert all(name == source for name, source in declared.items())


def test_every_output_the_workflow_reads_is_one_select_writes() -> None:
    read = set(re.findall(r"\b(?:steps|needs)\.select\.outputs\.(\w+)", workflow_text()))
    assert read <= set(output_names())


@pytest.mark.parametrize("command", sorted(asan_selection.COMMAND_VARIABLES))
def test_every_step_running_a_command_passes_the_variables_it_reads(command: str) -> None:
    steps = steps_running(command)
    assert steps, f"no workflow step runs {command}"
    for step in steps:
        for variable in asan_selection.COMMAND_VARIABLES[command]:
            output = asan_selection.VARIABLE_OUTPUTS[variable]
            assert re.search(rf"^ +{variable}: \$\{{\{{ (?:steps|needs)\.select\.outputs\.{output} \}}\}}$", step, re.M), (command, variable)
            if "docker run" in step:
                assert re.search(rf"--env {variable}(?=\s)", step), (command, variable)


def test_the_select_step_reads_the_event_file_not_the_environment() -> None:
    assert "github.event.pull_request.body }}" not in workflow_text()
    assert "PR_BODY" not in workflow_text()


def test_the_workflow_uploads_kept_run_directories_under_the_name_the_summary_gives() -> None:
    assert f"name: {asan_selection.RUNS_ARTIFACT_FORMAT.format(number='${{ matrix.shard }}')}\n" in workflow_text()


def test_the_workflow_has_one_concurrency_group_per_pull_request() -> None:
    assert workflow_text().count("concurrency:") == 1
    assert "\nconcurrency:\n  # " in workflow_text()
    assert "  group: asan-${{ github.event.pull_request.number }}\n  cancel-in-progress: true\n" in workflow_text()
