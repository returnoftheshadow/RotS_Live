from __future__ import annotations

import json
import sys
from pathlib import Path
import pytest

from rots_harness import asan_selection, ci_groups, run_plan

# The registered groups the tests choose from, in registration order.
GROUPS = ["bank", "boot", "poison", "shaping", "shaping_commands"]
# The scenario files each test repository holds.
SCENARIO_FILES = ("test_bank.py", "test_poison.py")
# The real checkout, whose scenarios the collection tests read.
REAL_REPO_ROOT = Path(__file__).resolve().parents[3]


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
    return asan_selection.select_tests(asan_selection.description_tokens(body), GROUPS, repo_root)


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


# Choosing the tests.


def test_all_runs_every_unit_test_and_every_scenario_sharded(repo_root: Path) -> None:
    selection = select("@asan-test-all", repo_root)
    assert selection.unit_mode() == "all"
    assert selection.scenario_mode() == "all"
    assert selection.shard_numbers(8) == list(range(1, 9))


def test_all_wins_over_every_other_token(repo_root: Path) -> None:
    selection = select("@asan-test-poison @asan-test-unit=Foo.* @asan-test-all @asan-test-scenario=scenarios/test_bank.py", repo_root)
    assert selection.unit_mode() == "all"
    assert selection.scenario_mode() == "all"
    assert selection.reasons[-1] == "@asan-test-all covers every other token"


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
    ("@asan-test-unit=`id`", "may hold only letters, digits and _ . * ? :, not '`'"),
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


def test_a_scenario_token_without_a_value_is_refused(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match=r"^@asan-test-scenario: needs =<scenario id>$"):
        select("@asan-test-scenario", repo_root)


def test_an_unknown_group_stops_the_run_and_lists_the_registered_ones(repo_root: Path) -> None:
    expected = (
        r"unknown integration groups: posion, shaping_comands; registered groups: bank, boot, poison, shaping, shaping_commands "
        r"\(or all, unit=<filter>, scenario=<id>\)"
    )
    with pytest.raises(asan_selection.InvalidTokens, match=expected):
        select("@asan-test-posion @asan-test-shaping_comands", repo_root)


@pytest.mark.parametrize("token", ["@asan-test-all=yes", "@asan-test-poison=1"])
def test_only_unit_and_scenario_take_a_value(repo_root: Path, token: str) -> None:
    with pytest.raises(asan_selection.InvalidTokens, match="takes no '='"):
        select(token, repo_root)


def test_every_unusable_token_is_listed_even_beside_all(repo_root: Path) -> None:
    with pytest.raises(asan_selection.InvalidTokens) as invalid:
        select("@asan-test-all @asan-test-unit=-Foo @asan-test-scenario=scenarios/test_gone.py @asan-test-posion", repo_root)
    lines = str(invalid.value).splitlines()
    assert [line.split(":")[0] for line in lines] == ["@asan-test-unit=-Foo", "@asan-test-scenario=scenarios/test_gone.py", "the description names unknown integration groups"]


# Deciding whether an event runs the tests.


@pytest.mark.parametrize("action", ["opened", "synchronize", "reopened"])
def test_an_event_other_than_an_edit_runs_while_tokens_are_present(action: str) -> None:
    run, why = asan_selection.restart_decision(action, "@asan-test-poison", False, "")
    assert run
    assert why == f"the pull request event is {action}, and the description has @asan-test tokens"


@pytest.mark.parametrize("action", ["opened", "synchronize", "edited"])
def test_a_description_without_tokens_runs_nothing(action: str) -> None:
    body = "<!-- @asan-test-all --> Plain description."
    assert asan_selection.restart_decision(action, body, True, "@asan-test-all") == (False, "the description has no @asan-test token")


def test_an_edit_that_leaves_the_description_alone_does_not_rerun() -> None:
    assert asan_selection.restart_decision("edited", "@asan-test-poison", False, "") == (False, "the edit did not change the description")


@pytest.mark.parametrize("previous", [
    "@asan-test-poison @asan-test-unit=A.*",
    "Old words.\n@asan-test-unit=A.* @asan-test-poison @asan-test-poison",
    "@asan-test-poison\n<!-- @asan-test-all -->\n@asan-test-unit=A.*",
])
def test_an_edit_that_keeps_the_same_tokens_does_not_rerun(previous: str) -> None:
    run, why = asan_selection.restart_decision("edited", "New words. @asan-test-poison @asan-test-unit=A.*", True, previous)
    assert not run
    assert why == "the edit left the @asan-test tokens as they were, so the earlier run stands"


@pytest.mark.parametrize("previous", [
    "No tokens yet.",
    "@asan-test-poison",
    "@asan-test-poison @asan-test-unit=A.* @asan-test-all",
    "@asan-test-poison @asan-test-unit=B.*",
    "@asan-test-poison <!-- @asan-test-unit=A.* -->",
])
def test_an_edit_that_changes_the_tokens_reruns(previous: str) -> None:
    run, why = asan_selection.restart_decision("edited", "@asan-test-poison @asan-test-unit=A.*", True, previous)
    assert run
    assert why == "the edit changed the @asan-test tokens"


# The select command.


def run_select(repo_root: Path, tmp_path: Path, environ: dict[str, str], action: str = "opened") -> tuple[int, str, str]:
    """Runs select over repo_root; returns its status, the output file's text and the summary's."""
    output = tmp_path / "output"
    summary = tmp_path / "summary.md"
    status = asan_selection.main(
        ["--repo-root", str(repo_root), "--summary", str(summary), "select", "--action", action, "--output", str(output), "--full-suite-shards", "8"],
        environ=environ,
    )
    read = lambda path: path.read_text(encoding="utf-8") if path.exists() else ""
    return status, read(output), read(summary)


def test_select_writes_every_output_on_one_line_each(repo_root: Path, tmp_path: Path) -> None:
    body = "$(rm -rf /) @asan-test-unit=Bank.* @asan-test-poison @asan-test-scenario=scenarios/test_bank.py::test_a"
    status, output, summary = run_select(repo_root, tmp_path, {"PR_BODY": body})
    assert status == 0
    assert output == (
        "run=true\nunit=filters\nunit_filters=[\"Bank.*\"]\nscenarios=selected\ngroups=[\"poison\"]\n"
        "scenario_ids=[\"scenarios/test_bank.py::test_a\"]\nshards=[1]\n"
    )
    assert summary.startswith("### ASan run: chosen unit tests; chosen scenarios\n\n- Why: the pull request event is opened")


def test_select_for_all_writes_every_shard(repo_root: Path, tmp_path: Path) -> None:
    status, output, summary = run_select(repo_root, tmp_path, {"PR_BODY": "@asan-test-all"})
    assert status == 0
    assert "unit=all\n" in output
    assert "scenarios=all\n" in output
    assert "shards=[1, 2, 3, 4, 5, 6, 7, 8]\n" in output
    assert summary.startswith("### ASan run: every unit test; every scenario, split across 8 shards\n")


def test_select_writes_no_run_for_an_edit_that_keeps_the_tokens(repo_root: Path, tmp_path: Path) -> None:
    environ = {"PR_BODY": "Better words. @asan-test-poison", "PR_PREVIOUS_BODY": "@asan-test-poison", "PR_BODY_CHANGED": "true"}
    status, output, summary = run_select(repo_root, tmp_path, environ, action="edited")
    assert status == 0
    assert output == "run=false\nunit=none\nunit_filters=[]\nscenarios=none\ngroups=[]\nscenario_ids=[]\nshards=[1]\n"
    assert summary == "### ASan run: not started\n\n- the edit left the @asan-test tokens as they were, so the earlier run stands\n"


def test_select_reruns_an_edit_that_adds_a_token(repo_root: Path, tmp_path: Path) -> None:
    environ = {"PR_BODY": "@asan-test-poison @asan-test-bank", "PR_PREVIOUS_BODY": "@asan-test-poison", "PR_BODY_CHANGED": "true"}
    status, output, _ = run_select(repo_root, tmp_path, environ, action="edited")
    assert status == 0
    assert output.startswith("run=true\n")
    assert "groups=[\"poison\", \"bank\"]\n" in output


def test_select_fails_with_status_2_on_an_unusable_token_and_writes_no_output(
    repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str],
) -> None:
    status, output, summary = run_select(repo_root, tmp_path, {"PR_BODY": "@asan-test-unit=Foo;rm @asan-test-posion"})
    assert status == 2
    assert output == ""
    error = capsys.readouterr().err
    assert "@asan-test-unit=Foo;rm: may hold only" in error
    assert "unknown integration groups: posion" in error
    # Indented, so the description's text renders as code in the summary.
    assert "\n    @asan-test-unit=Foo;rm: may hold only" in summary


def test_select_checks_the_tokens_even_on_an_edit_that_keeps_them(repo_root: Path, tmp_path: Path) -> None:
    environ = {"PR_BODY": "@asan-test-posion", "PR_PREVIOUS_BODY": "@asan-test-posion", "PR_BODY_CHANGED": "true"}
    assert run_select(repo_root, tmp_path, environ, action="edited")[0] == 2


def test_select_fails_with_status_2_on_an_unreadable_group_file(repo_root: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    (repo_root / ci_groups.GROUPS_DIRECTORY / "bad-name.txt").write_text("a group\n", encoding="utf-8")
    status, output, _ = run_select(repo_root, tmp_path, {"PR_BODY": "@asan-test-poison"})
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
    assert "@asan-test-unit=Bnak*: matches no unit test" in capsys.readouterr().err


def test_run_unit_fails_when_the_binary_cannot_list_its_tests(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    binary = FakeUnitBinary({"Bank*": "ERROR: AddressSanitizer: heap-use-after-free\n"}, list_status=1)
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*"])}
    assert run_unit(binary, environ, tmp_path) == asan_selection.EXIT_NOTHING_SELECTED
    assert binary.ran == []
    assert "listing the unit tests failed (exit status 1)" in capsys.readouterr().err


def test_run_unit_returns_the_tests_status(tmp_path: Path) -> None:
    binary = FakeUnitBinary({"Bank*": "BankTest.\n  KeepsCoins\n"}, run_status=1)
    environ = {"ASAN_UNIT": "filters", "ASAN_UNIT_FILTERS": json.dumps(["Bank*"])}
    assert run_unit(binary, environ, tmp_path) == 1
    assert (tmp_path / "summary.md").read_text(encoding="utf-8").endswith("\nThe unit tests failed with exit status 1.\n")


def test_run_unit_for_all_runs_every_test_without_listing(tmp_path: Path) -> None:
    binary = FakeUnitBinary({})
    assert run_unit(binary, {"ASAN_UNIT": "all", "ASAN_UNIT_FILTERS": "[]"}, tmp_path) == 0
    assert binary.listed == []
    assert binary.ran == [["bin/tests", "--gtest_brief=1"]]


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
    """Collects node ids per pytest arguments from a table and records every command it runs."""

    def __init__(self, collected: dict[tuple[str, ...], list[str]], run_status: int = 0) -> None:
        # The node ids each argument list collects; one missing from the table collects none.
        self.collected = collected
        # The status a run returns.
        self.run_status = run_status
        # Every run command, in order.
        self.ran: list[list[str]] = []

    def collect(self, arguments: list[str]) -> list[str]:
        if tuple(arguments) == ("tests/integration/scenarios/test_bank.py::test_typo",):
            raise asan_selection.NothingSelected("pytest could not collect it (exit status 4):\nERROR: not found")
        return self.collected.get(tuple(arguments), [])

    def run(self, command: list[str], keep_stdin: bool) -> int:
        self.ran.append(command)
        return self.run_status


# What each group and scenario argument list collects in the scenario tests.
COLLECTED = {
    ("tests/integration/scenarios", "-m", "scenario and (poison)"): ["scenarios/test_poison.py::test_ticks", "scenarios/test_poison.py::test_wears_off"],
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
    assert (tmp_path / "summary.md").read_text(encoding="utf-8") == "### ASan scenarios: every scenario, shard 3/8\n"


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
])
def test_run_scenarios_checks_its_inputs_again(repo_root: Path, tmp_path: Path, environ: dict[str, str]) -> None:
    collector = FakeCollector(COLLECTED)
    assert run_scenarios(collector, environ, repo_root, tmp_path) == asan_selection.EXIT_USAGE
    assert collector.ran == []


def test_check_scenarios_lists_each_tokens_tests(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_GROUPS": json.dumps(["poison"]), "ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_bank.py"])}
    status = asan_selection.main(
        ["--repo-root", str(repo_root), "--summary", str(tmp_path / "summary.md"), "check-scenarios", "--python", "py"],
        environ=environ, collect=collector.collect,
    )
    assert status == 0
    assert collector.ran == []
    summary = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert summary.startswith("### ASan scenarios each token selects\n\n- `@asan-test-poison`: 2 tests\n")


def test_check_scenarios_fails_when_a_token_selects_nothing(repo_root: Path, tmp_path: Path) -> None:
    collector = FakeCollector(COLLECTED)
    environ = {"ASAN_SCENARIO_IDS": json.dumps(["scenarios/test_poison.py"])}
    status = asan_selection.main(["--repo-root", str(repo_root), "check-scenarios", "--python", "py"], environ=environ, collect=collector.collect)
    assert status == asan_selection.EXIT_NOTHING_SELECTED


def test_a_long_list_of_tests_is_cut_short_in_the_summary() -> None:
    names = [f"Suite.Test{number}" for number in range(asan_selection.SUMMARY_NAME_LIMIT + 3)]
    lines = asan_selection.names_markdown(names)
    assert len(lines) == asan_selection.SUMMARY_NAME_LIMIT + 1
    assert lines[-1] == "  - and 3 more"


# Collecting from the real checkout boots no server, so these run pytest itself.


def test_collect_node_ids_lists_a_real_scenario_files_tests() -> None:
    node_ids = asan_selection.collect_node_ids(sys.executable, ["tests/integration/scenarios/test_bank.py"], REAL_REPO_ROOT)
    assert "scenarios/test_bank.py::test_a_banker_keeps_an_item_lists_it_and_hands_it_back" in node_ids
    assert all(node_id.startswith("scenarios/test_bank.py::") for node_id in node_ids)


def test_collect_node_ids_reports_a_test_that_does_not_exist() -> None:
    with pytest.raises(asan_selection.NothingSelected, match=r"exit status 4"):
        asan_selection.collect_node_ids(sys.executable, ["tests/integration/scenarios/test_bank.py::test_no_such_test"], REAL_REPO_ROOT)


def test_collect_node_ids_selects_a_group_by_its_marker() -> None:
    node_ids = asan_selection.collect_node_ids(sys.executable, ["tests/integration/scenarios", "-m", "scenario and (bank)"], REAL_REPO_ROOT)
    assert node_ids
    assert all(node_id.startswith("scenarios/test_bank") for node_id in node_ids)


def test_the_real_scenario_ids_in_the_shard_times_pass_the_id_check() -> None:
    # Every committed test id without a space must be writable as a token.
    recorded = json.loads((REAL_REPO_ROOT / "tests/integration/shard_durations.json").read_text(encoding="utf-8"))
    refused = [node_id for node_id in recorded if " " not in node_id and asan_selection.scenario_id_problem(node_id, REAL_REPO_ROOT)]
    assert refused == []
