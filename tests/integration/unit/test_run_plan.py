from __future__ import annotations

import itertools
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Optional

import pytest

from rots_harness import run_plan, sharding
from rots_harness.collect_report import CollectedTest
from rots_harness.run_plan import AnswerOutcome, Gate
from rots_harness.sharding import Shard

# The registered groups, in registration order; shaping marks no selected test.
GROUPS = ["boot", "poison", "shaping", "summon"]
# Four selected tests; the second is in two groups.
TESTS = [
    CollectedTest("scenarios/test_boot.py::test_one", frozenset({"scenario", "boot"})),
    CollectedTest("scenarios/test_boot.py::test_two", frozenset({"scenario", "boot", "poison", "characters"})),
    CollectedTest("scenarios/test_poison.py::test_three", frozenset({"scenario", "poison"})),
    CollectedTest("scenarios/test_summon.py::test_four", frozenset({"scenario", "summon"})),
]
# Every selected test's node id, in collection order.
ALL_IDS = [test.node_id for test in TESTS]
# Recorded seconds for the first two tests; the others count the default.
DURATIONS = {ALL_IDS[0]: 30.0, ALL_IDS[1]: 90.0, "scenarios/test_gone.py::test_old": 600.0}
# The pytest command make integration ran before the plan, for GROUP=boot and JOBS=8.
TODAYS_BOOT_COMMAND = [
    "py", "-m", "pytest", "tests/integration/scenarios", "-q", "-m", "scenario and (boot)", "-n", "8",
    "--junitxml=build/integration/results.xml", "--durations=25",
]


def plan(skipped: Optional[list[str]] = None, jobs: int = 1, shard: Optional[Shard] = None) -> run_plan.RunPlan:
    return run_plan.build_plan(TESTS, GROUPS, skipped or [], DURATIONS, jobs, shard)


def flat_rows(ticked: tuple[bool, ...]) -> list[run_plan.PlanRow]:
    """One numbered group checkbox per tick, as a plan without subgroups numbers them."""
    return [run_plan.PlanRow(f"group_{position + 1}", run_plan.RowKind.GROUP, (position,)) for position in range(len(ticked))]


def answer(text: str, ticked: tuple[bool, ...]) -> run_plan.AnswerResult:
    """apply_answer on a plan of len(ticked) groups without subgroups."""
    return run_plan.apply_answer(text, ticked, flat_rows(ticked))


# Plan building


def test_each_group_counts_every_test_it_marks_and_empty_groups_are_dropped() -> None:
    built = plan()
    assert [(name, len(built.group_test_ids(name, []))) for name in built.offered_groups] == [("boot", 2), ("poison", 2), ("summon", 1)]
    assert built.skipped_groups == []


def test_the_total_counts_a_test_in_two_groups_once() -> None:
    assert plan().kept_test_ids([]) == ALL_IDS


def test_skipped_groups_are_listed_as_skipped_and_not_offered() -> None:
    built = plan(skipped=["summon", "shaping"])
    assert built.offered_groups == ["boot", "poison"]
    assert built.skipped_groups == ["summon"]
    assert built.kept_test_ids([]) == ALL_IDS[:3]


def test_unticking_a_group_drops_every_test_it_marks_even_one_another_kept_group_marks() -> None:
    assert plan().kept_test_ids(["poison"]) == [ALL_IDS[0], ALL_IDS[3]]


def test_a_test_in_no_group_still_counts_in_the_total() -> None:
    ungrouped = CollectedTest("scenarios/test_x.py::test_loose", frozenset({"scenario"}))
    built = run_plan.build_plan([*TESTS, ungrouped], GROUPS, [], {}, 1)
    assert built.kept_test_ids(["boot", "poison", "summon"]) == [ungrouped.node_id]
    assert built.ungrouped_count([]) == 1


def test_a_group_counts_only_the_tests_that_run_with_the_other_groups_left_out() -> None:
    built = plan(skipped=["poison"])
    # test_two is in boot and poison; poison is skipped, so boot runs test_one alone.
    assert built.group_test_ids("boot", []) == [ALL_IDS[0]]
    # A skipped or unticked group counts what it would run if it were kept.
    assert built.group_test_ids("poison", []) == ALL_IDS[1:3]
    assert built.group_test_ids("summon", ["summon"]) == [ALL_IDS[3]]


# The groups a test leaves out: every combination of the registered ones.
EXCLUSION_CHOICES = [list(combination) for count in range(len(GROUPS) + 1) for combination in itertools.combinations(GROUPS, count)]


@pytest.mark.parametrize("shard", [Shard(number, count) for count in (2, 3) for number in range(1, count + 1)], ids=str)
@pytest.mark.parametrize("excluded", EXCLUSION_CHOICES, ids=lambda excluded: "-".join(excluded) or "none")
def test_a_sharded_plan_keeps_exactly_the_tests_conftest_keeps(shard: Shard, excluded: list[str]) -> None:
    # pytest's -m leaves out every test an excluded group marks; conftest then shards what is left.
    selected = [test.node_id for test in TESTS if not test.markers & set(excluded)]
    conftest_kept = sharding.in_shard(selected, shard)
    skipped, unticked = excluded[:1], excluded[1:]
    built = plan(skipped=skipped, shard=shard)
    assert set(built.kept_test_ids(unticked)) == conftest_kept


def test_a_sharded_plan_balances_the_shards_by_the_committed_times_as_conftest_does() -> None:
    # test_one alone takes as long as the rest, so it has a shard to itself.
    shard_seconds = {ALL_IDS[0]: 100.0, ALL_IDS[1]: 1.0, ALL_IDS[2]: 1.0, ALL_IDS[3]: 1.0}
    for number in (1, 2):
        built = run_plan.build_plan(TESTS, GROUPS, [], DURATIONS, 1, Shard(number, 2), shard_seconds=shard_seconds)
        assert set(built.kept_test_ids([])) == sharding.in_shard(ALL_IDS, Shard(number, 2), shard_seconds)
    assert run_plan.build_plan(TESTS, GROUPS, [], DURATIONS, 1, Shard(1, 2), shard_seconds=shard_seconds).kept_test_ids([]) == [ALL_IDS[0]]


def test_skip_groups_are_split_on_spaces_and_commas_without_repeats() -> None:
    assert run_plan.parse_skip_groups(" poison, summon  poison,", GROUPS) == ["poison", "summon"]
    assert run_plan.parse_skip_groups("", GROUPS) == []


def test_an_unregistered_skip_group_is_rejected_with_the_valid_names() -> None:
    with pytest.raises(ValueError, match="bot.*boot, poison, shaping, summon"):
        run_plan.parse_skip_groups("poison bot", GROUPS)


# Estimates


def test_the_estimate_uses_recorded_times_and_the_default_for_the_rest() -> None:
    # 30 + 90 + 15 + 15 seconds.
    assert plan().estimated_minutes(ALL_IDS) == pytest.approx(2.5)
    assert plan().untimed_count(ALL_IDS) == 2


def test_the_estimate_is_divided_by_the_workers() -> None:
    assert plan(jobs=2).estimated_minutes(ALL_IDS) == pytest.approx(1.25)
    assert plan(jobs=0).estimated_minutes(ALL_IDS) == pytest.approx(2.5)


def test_a_group_estimate_covers_its_own_tests() -> None:
    poison = plan().group_test_ids("poison", [])
    assert plan().estimated_minutes(poison) == pytest.approx(105 / 60)


def test_the_estimate_divides_by_no_more_workers_than_tests() -> None:
    # 150 seconds on 4 of the 8 workers.
    assert plan(jobs=8).estimated_minutes(ALL_IDS) == pytest.approx(150 / 4 / 60)
    assert plan(jobs=8).estimated_minutes(ALL_IDS[:1]) == pytest.approx(30 / 60)
    assert plan(jobs=8).estimated_minutes([]) == 0


@pytest.mark.parametrize("minutes, text", [(0.0, "0.0 min"), (0.44, "0.4 min"), (9.94, "9.9 min"), (12.6, "13 min")])
def test_minutes_show_one_decimal_below_ten_and_whole_minutes_above(minutes: float, text: str) -> None:
    assert run_plan.format_minutes(minutes) == text


@pytest.mark.parametrize("value, workers", [("8", 8), ("1", 1), ("", 1), ("0", 1), ("many", 1)])
def test_jobs_give_the_workers_the_estimate_divides_by(value: str, workers: int) -> None:
    assert run_plan.parse_jobs(value) == workers


def test_the_plan_text_shows_checkboxes_skipped_groups_totals_and_the_footer() -> None:
    built = plan(skipped=["summon"])
    lines = run_plan.format_plan(built, (True, False), run_plan.RunContext(launcher="local", jobs_text="1", shard="2/8"))
    text = "\n".join(lines)
    assert "launcher local" in text and "JOBS 1" in text and "shard 2/8" in text
    assert "[x] 1. boot" in text
    assert "[ ] 2. poison" in text
    assert "skipped: summon (1 test)" in text
    # Unticking poison leaves only test_one: 30 seconds.
    assert "Total: 1 test, about 0.5 min (estimate from recorded times)" in text
    assert "even one another ticked group also marks" in text


def test_the_sharded_plan_text_counts_this_shards_tests_and_says_how_unticking_moves_them() -> None:
    built = plan(shard=Shard(2, 2))
    lines = run_plan.format_plan(built, (True, True, True), run_plan.RunContext(launcher="local", jobs_text="1", shard="2/2"))
    text = "\n".join(lines)
    # The sorted selection is one, two, three, four; shard 2/2 keeps two and four.
    assert "Total: 2 tests" in text
    assert "summon     1 test" in text
    assert "ROTS_IT_SHARD" in text


def test_the_plan_text_counts_the_kept_tests_without_a_recorded_time() -> None:
    lines = run_plan.format_plan(plan(), (True, True, True), run_plan.RunContext(launcher="docker", jobs_text="1", shard=None))
    text = "\n".join(lines)
    assert "shard" not in text
    assert "2 tests have no recorded time and count 15 s each" in text


# Groups with subgroups

# The registered groups in registration order: shaping has three subgroups, of which
# shaping_saving marks no selected test.
TREE_GROUPS = ["boot", "shaping", "shaping_commands", "shaping_editor", "shaping_saving", "summon"]
# shaping's subgroups.
TREE_SUBGROUPS = {"shaping": ["shaping_commands", "shaping_editor", "shaping_saving"]}
# Five selected tests; the shaping_editor test is in summon too.
TREE_TESTS = [
    CollectedTest("scenarios/test_boot.py::test_one", frozenset({"scenario", "boot"})),
    CollectedTest("scenarios/test_shaping.py::test_a", frozenset({"scenario", "shaping", "shaping_commands"})),
    CollectedTest("scenarios/test_shaping.py::test_b", frozenset({"scenario", "shaping", "shaping_commands"})),
    CollectedTest("scenarios/test_shaping.py::test_c", frozenset({"scenario", "shaping", "shaping_editor", "summon"})),
    CollectedTest("scenarios/test_summon.py::test_d", frozenset({"scenario", "summon"})),
]
# Every tree test's node id, in collection order.
TREE_IDS = [test.node_id for test in TREE_TESTS]
# Recorded seconds: shaping_commands takes 1.0 min (45 + 15 s), shaping_editor 0.5 min.
TREE_DURATIONS = {TREE_IDS[1]: 45.0, TREE_IDS[3]: 30.0}
# The context the tree plans are formatted with.
TREE_CONTEXT = run_plan.RunContext(launcher="local", jobs_text="1", shard=None)


def tree_plan(skipped: Optional[list[str]] = None) -> run_plan.RunPlan:
    return run_plan.build_plan(TREE_TESTS, TREE_GROUPS, skipped or [], TREE_DURATIONS, 1, subgroups=TREE_SUBGROUPS)


def tree_rows(skipped: Optional[list[str]] = None) -> list[run_plan.PlanRow]:
    return run_plan.plan_rows(tree_plan(skipped))


def test_subgroups_are_offered_and_their_group_is_not() -> None:
    built = tree_plan()
    assert built.offered_groups == ["boot", "shaping_commands", "shaping_editor", "summon"]
    assert built.skipped_groups == []


def test_the_rows_number_a_header_before_its_subgroups() -> None:
    assert tree_rows() == [
        run_plan.PlanRow("boot", run_plan.RowKind.GROUP, (0,)),
        run_plan.PlanRow("shaping", run_plan.RowKind.HEADER, (1, 2)),
        run_plan.PlanRow("shaping_commands", run_plan.RowKind.SUBGROUP, (1,)),
        run_plan.PlanRow("shaping_editor", run_plan.RowKind.SUBGROUP, (2,)),
        run_plan.PlanRow("summon", run_plan.RowKind.GROUP, (3,)),
    ]


def test_a_group_none_of_whose_subgroups_has_a_selected_test_is_left_out() -> None:
    commands_only = [test for test in TREE_TESTS if "shaping_editor" not in test.markers and "summon" not in test.markers]
    built = run_plan.build_plan(commands_only, TREE_GROUPS, ["shaping_commands"], {}, 1, subgroups=TREE_SUBGROUPS)
    assert built.offered_groups == ["boot"]
    assert [row.name for row in run_plan.plan_rows(built)] == ["boot"]


def test_the_nested_plan_shows_the_header_totals_and_indents_the_subgroups() -> None:
    lines = run_plan.format_plan(tree_plan(), (True, True, True, True), TREE_CONTEXT)
    assert lines[1:6] == [
        f"  [x] 1. {'boot':<20}  {'1 test':>9}  {'0.2 min':>8}",
        f"  [x] 2. {'shaping':<20}  {'3 tests':>9}  {'1.5 min':>8}",
        f"      [x] 3. {'shaping_commands':<16}  {'2 tests':>9}  {'1.0 min':>8}",
        f"      [x] 4. {'shaping_editor':<16}  {'1 test':>9}  {'0.5 min':>8}",
        f"  [x] 5. {'summon':<20}  {'2 tests':>9}  {'0.8 min':>8}",
    ]


def test_a_header_box_shows_some_ticked_and_counts_what_its_ticked_subgroups_run() -> None:
    text = "\n".join(run_plan.format_plan(tree_plan(), (True, False, True, True), TREE_CONTEXT))
    assert f"  [-] 2. {'shaping':<20}  {'1 test':>9}" in text
    assert f"      [ ] 3. {'shaping_commands':<16}  {'2 tests':>9}" in text


def test_a_header_with_nothing_ticked_counts_what_ticking_its_subgroups_adds() -> None:
    text = "\n".join(run_plan.format_plan(tree_plan(), (True, False, False, True), TREE_CONTEXT))
    assert f"  [ ] 2. {'shaping':<20}  {'3 tests':>9}" in text
    # test_c is in summon too, so summon still runs it while shaping is unticked.
    assert "Total: 2 tests," in text


@pytest.mark.parametrize("ticked, toggled", [
    ((True, True, True, True), (True, False, False, True)),
    ((True, False, True, True), (True, False, False, True)),
    ((True, False, False, True), (True, True, True, True)),
])
def test_a_header_number_unticks_its_subgroups_when_any_is_ticked_and_ticks_them_all_otherwise(
    ticked: tuple[bool, ...], toggled: tuple[bool, ...],
) -> None:
    result = run_plan.apply_answer("2", ticked, tree_rows())
    assert result.outcome is AnswerOutcome.CONTINUE
    assert result.ticked == toggled


@pytest.mark.parametrize("typed, ticked", [
    ("3", (True, False, True, True)),
    ("1 4", (False, True, False, True)),
    ("1-3", (False, False, False, True)),
    ("2-5", (True, False, False, False)),
    ("4-5", (True, True, False, False)),
    ("a", (True, True, True, True)),
    ("n", (False, False, False, False)),
])
def test_numbers_and_ranges_count_the_headers_and_a_range_toggles_each_subgroup_once(typed: str, ticked: tuple[bool, ...]) -> None:
    assert run_plan.apply_answer(typed, (True, True, True, True), tree_rows()).ticked == ticked


def test_a_number_past_the_last_row_is_refused_with_the_row_count() -> None:
    result = run_plan.apply_answer("6", (True, True, True, True), tree_rows())
    assert result.ticked == (True, True, True, True)
    assert result.message is not None and "from 1 to 5" in result.message


def test_starting_with_every_subgroup_and_group_unticked_is_refused() -> None:
    result = run_plan.apply_answer("", (False, False, False, False), tree_rows())
    assert result.outcome is AnswerOutcome.CONTINUE
    assert result.message is not None and "nothing is ticked" in result.message


@pytest.mark.parametrize("ticked, unticked", [
    ((True, True, True, True), []),
    ((True, False, True, True), ["shaping_commands"]),
    ((True, False, False, True), ["shaping"]),
    ((False, False, False, False), ["boot", "shaping", "summon"]),
])
def test_the_unticked_groups_name_a_group_whose_offered_subgroups_are_all_unticked(ticked: tuple[bool, ...], unticked: list[str]) -> None:
    assert run_plan.unticked_groups(tree_plan(), ticked) == unticked


def test_skipping_a_group_skips_its_subgroups_and_lists_the_group_alone() -> None:
    built = tree_plan(skipped=["shaping"])
    assert built.offered_groups == ["boot", "summon"]
    assert built.skipped_groups == ["shaping"]
    assert built.kept_test_ids([]) == [TREE_IDS[0], TREE_IDS[4]]
    text = "\n".join(run_plan.format_plan(built, (True, True), TREE_CONTEXT))
    assert "skipped: shaping (3 tests)" in text
    assert "shaping_commands" not in text


def test_skipping_a_subgroup_lists_it_as_skipped_and_offers_the_rest() -> None:
    built = tree_plan(skipped=["shaping_editor"])
    assert built.offered_groups == ["boot", "shaping_commands", "summon"]
    assert built.skipped_groups == ["shaping_editor"]
    assert built.kept_test_ids([]) == [TREE_IDS[0], TREE_IDS[1], TREE_IDS[2], TREE_IDS[4]]
    assert [row.name for row in run_plan.plan_rows(built)] == ["boot", "shaping", "shaping_commands", "summon"]
    text = "\n".join(run_plan.format_plan(built, (True, True, True), TREE_CONTEXT))
    assert "skipped: shaping_editor (1 test)" in text


def test_a_group_offered_with_one_subgroup_is_a_header_over_it_that_toggles_with_it() -> None:
    # GROUP=shaping_commands selects only that subgroup's tests.
    commands_tests = [test for test in TREE_TESTS if "shaping_commands" in test.markers]
    built = run_plan.build_plan(commands_tests, TREE_GROUPS, [], TREE_DURATIONS, 1, subgroups=TREE_SUBGROUPS)
    rows = run_plan.plan_rows(built)
    assert rows == [
        run_plan.PlanRow("shaping", run_plan.RowKind.HEADER, (0,)),
        run_plan.PlanRow("shaping_commands", run_plan.RowKind.SUBGROUP, (0,)),
    ]
    assert run_plan.format_plan(built, (True,), TREE_CONTEXT)[1:3] == [
        f"  [x] 1. {'shaping':<20}  {'2 tests':>9}  {'1.0 min':>8}",
        f"      [x] 2. {'shaping_commands':<16}  {'2 tests':>9}  {'1.0 min':>8}",
    ]
    for typed in ("1", "2"):
        unticked = run_plan.apply_answer(typed, (True,), rows).ticked
        assert unticked == (False,)
        assert run_plan.format_plan(built, unticked, TREE_CONTEXT)[1].startswith(f"  [ ] 1. {'shaping':<20}  {'2 tests':>9}")
        assert run_plan.unticked_groups(built, unticked) == ["shaping"]
        assert run_plan.apply_answer(typed, unticked, rows).ticked == (True,)
    assert run_plan.apply_answer("", (False,), rows).message is not None


def test_a_skipped_subgroup_and_the_rest_unticked_leave_out_the_whole_group_and_nothing_more() -> None:
    built = tree_plan(skipped=["shaping_editor"])
    ticked = (True, False, True)
    unticked = run_plan.unticked_groups(built, ticked)
    assert unticked == ["shaping"]
    # What pytest's -m keeps for "not (shaping_editor or shaping)", and what the plan counts.
    excluded = {"shaping_editor", *unticked}
    assert [test.node_id for test in TREE_TESTS if not test.markers & excluded] == [TREE_IDS[0], TREE_IDS[4]]
    assert built.kept_test_ids(["shaping_commands"]) == [TREE_IDS[0], TREE_IDS[4]]
    text = "\n".join(run_plan.format_plan(built, ticked, TREE_CONTEXT))
    assert f"  [ ] 2. {'shaping':<20}  {'2 tests':>9}" in text
    assert "Total: 2 tests," in text


def test_a_sharded_plan_with_subgroups_counts_this_shards_tests_under_each_header() -> None:
    built = run_plan.build_plan(TREE_TESTS, TREE_GROUPS, [], TREE_DURATIONS, 1, Shard(1, 2), subgroups=TREE_SUBGROUPS)
    lines = run_plan.format_plan(built, (True, True, True, True), run_plan.RunContext(launcher="local", jobs_text="1", shard="1/2"))
    # The sorted selection is one, a, b, c, d; shard 1/2 keeps one, b and d.
    assert lines[1:6] == [
        f"  [x] 1. {'boot':<20}  {'1 test':>9}  {'0.2 min':>8}",
        f"  [x] 2. {'shaping':<20}  {'1 test':>9}  {'0.2 min':>8}",
        f"      [x] 3. {'shaping_commands':<16}  {'1 test':>9}  {'0.2 min':>8}",
        f"      [x] 4. {'shaping_editor':<16}  {'0 tests':>9}  {'0.0 min':>8}",
        f"  [x] 5. {'summon':<20}  {'1 test':>9}  {'0.2 min':>8}",
    ]
    assert "Total: 3 tests," in lines[6]
    assert lines[-1] == run_plan.SHARD_FOOTER


def test_skip_groups_accepts_a_subgroup() -> None:
    assert run_plan.parse_skip_groups("shaping_editor shaping", TREE_GROUPS) == ["shaping_editor", "shaping"]


def write_tree_groups(groups_dir: Path) -> None:
    """Group files registering TREE_GROUPS."""
    groups_dir.mkdir()
    (groups_dir / "shaping").mkdir()
    for name in TREE_GROUPS:
        if name.startswith("shaping_"):
            path = groups_dir / "shaping" / f"{name[len('shaping_'):]}.txt"
        else:
            path = groups_dir / f"{name}.txt"
        path.write_text(f"the {name} group\n", encoding="utf-8")


def collect_tree_tests(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
    return TREE_TESTS, 0


@pytest.mark.parametrize("arguments, answers, expression, skip_groups", [
    ([], ["2\n", "y\n"], "scenario and not (shaping)", "SKIP_GROUPS=shaping"),
    ([], ["3\n", "y\n"], "scenario and not (shaping_commands)", "SKIP_GROUPS=shaping_commands"),
    (["--skip-groups", "shaping_editor"], ["3\n", "y\n"], "scenario and not (shaping_editor or shaping)", "SKIP_GROUPS='shaping_editor shaping'"),
])
def test_at_a_terminal_unticked_subgroups_reach_the_run_and_the_repeat_line(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture,
    arguments: list[str], answers: list[str], expression: str, skip_groups: str,
) -> None:
    write_tree_groups(tmp_path / "groups")
    monkeypatch.setattr(run_plan, "GROUPS_DIR", tmp_path / "groups")
    runner = FakeRunner()
    status = run_plan.main(
        ["--python", "py", *arguments], environ={}, run_command=runner, collect=collect_tree_tests, interactive=True,
        reader=ScriptedReader(answers), durations_path=tmp_path / "durations.json",
    )
    assert status == 0
    assert runner.commands == [(run_plan.pytest_run_command("py", expression, ""), True)]
    assert f"Repeat: make integration PYTHON=py {skip_groups} CONFIRM=yes" in capsys.readouterr().out


# Expressions and commands


@pytest.mark.parametrize("group, excluded, expression", [
    ("", [], "scenario"),
    ("boot or (poison and not summon)", [], "scenario and (boot or (poison and not summon))"),
    ("", ["poison", "summon"], "scenario and not (poison or summon)"),
    ("boot or poison", ["summon"], "scenario and (boot or poison) and not (summon)"),
])
def test_the_run_expression_keeps_the_group_and_leaves_out_the_excluded(group: str, excluded: list[str], expression: str) -> None:
    assert run_plan.run_expression(group, excluded) == expression


def test_the_run_command_is_todays_pytest_command() -> None:
    assert run_plan.pytest_run_command("py", "scenario and (boot)", "8") == TODAYS_BOOT_COMMAND


@pytest.mark.parametrize("jobs_text", ["", "0", "1"])
def test_one_worker_runs_without_xdist(jobs_text: str) -> None:
    assert "-n" not in run_plan.pytest_run_command("py", "scenario", jobs_text)


def test_the_repeat_line_reproduces_the_choice() -> None:
    line = run_plan.repeat_command(".venv/bin/python", "boot or poison", ["summon", "shaping"], "8")
    assert line == "make integration PYTHON=.venv/bin/python GROUP='boot or poison' JOBS=8 SKIP_GROUPS='summon shaping' CONFIRM=yes"


def test_the_repeat_line_leaves_out_defaults() -> None:
    assert run_plan.repeat_command("python3", "", ["summon"], "1") == "make integration SKIP_GROUPS=summon CONFIRM=yes"


# Answers


@pytest.mark.parametrize("typed, ticked", [
    ("2", (True, False, True)),
    ("1 3", (False, True, False)),
    ("1,3", (False, True, False)),
    ("2-3", (True, False, False)),
    ("a", (True, True, True)),
])
def test_answers_toggle_and_tick_groups(typed: str, ticked: tuple[bool, ...]) -> None:
    result = answer(typed, (True, True, True))
    assert result.outcome is AnswerOutcome.CONTINUE
    assert result.ticked == ticked


def test_n_unticks_every_group() -> None:
    assert answer("n", (True, False)).ticked == (False, False)


def test_toggling_twice_ticks_again() -> None:
    once = answer("2", (True, True))
    assert answer("2", once.ticked).ticked == (True, True)


@pytest.mark.parametrize("typed", ["", "y", "Y", "yes"])
def test_enter_or_y_starts(typed: str) -> None:
    result = answer(typed, (True, False))
    assert result.outcome is AnswerOutcome.START
    assert result.ticked == (True, False)


def test_q_cancels() -> None:
    assert answer("q", (True,)).outcome is AnswerOutcome.CANCEL


def test_starting_with_nothing_ticked_is_refused() -> None:
    result = answer("", (False, False))
    assert result.outcome is AnswerOutcome.CONTINUE
    assert result.message is not None and "nothing is ticked" in result.message


@pytest.mark.parametrize("typed", ["0", "4", "3-2", "x", "1-", "2 zebra"])
def test_bad_answers_change_nothing_and_say_why(typed: str) -> None:
    result = answer(typed, (True, True, True))
    assert result.outcome is AnswerOutcome.CONTINUE
    assert result.ticked == (True, True, True)
    assert result.message


# Gate decision


def test_ci_is_recognised_from_ci_true() -> None:
    assert run_plan.is_ci({"CI": "true"})
    assert run_plan.is_ci({"CI": "TRUE"})
    assert not run_plan.is_ci({})
    assert not run_plan.is_ci({"CI": "false"})


def test_confirm_yes_runs_without_asking_however_long() -> None:
    assert run_plan.decide_gate(confirm=True, interactive=True, estimate_minutes=500, threshold_minutes=10) is Gate.RUN


def test_a_terminal_asks_even_for_a_short_run() -> None:
    assert run_plan.decide_gate(confirm=False, interactive=True, estimate_minutes=1, threshold_minutes=10) is Gate.ASK


def test_without_a_terminal_a_short_run_starts() -> None:
    assert run_plan.decide_gate(confirm=False, interactive=False, estimate_minutes=9.9, threshold_minutes=10) is Gate.RUN


def test_without_a_terminal_a_long_run_needs_confirmation() -> None:
    assert run_plan.decide_gate(confirm=False, interactive=False, estimate_minutes=10, threshold_minutes=10) is Gate.NEEDS_CONFIRMATION


def test_the_confirm_threshold_defaults_to_ten_minutes() -> None:
    assert run_plan.parse_confirm_minutes(None) == 10.0
    assert run_plan.parse_confirm_minutes("2.5") == 2.5


@pytest.mark.parametrize("value", ["0", "-1", "abc", "", "nan", "inf"])
def test_a_threshold_that_is_not_a_positive_number_is_rejected(value: str) -> None:
    with pytest.raises(ValueError):
        run_plan.parse_confirm_minutes(value)


def test_confirm_accepts_yes_or_nothing() -> None:
    assert run_plan.parse_confirm("yes") is True
    assert run_plan.parse_confirm("YES") is True
    assert run_plan.parse_confirm("") is False
    with pytest.raises(ValueError):
        run_plan.parse_confirm("no")


# Timed prompt


class ScriptedReader:
    """Answers read(timeout) from a script: None stands for a timeout. Records each timeout asked for."""

    def __init__(self, answers: list[Optional[str]]) -> None:
        # The answers still to give, in order.
        self._answers = list(answers)
        # The timeout of every read, in order.
        self.timeouts: list[Optional[float]] = []
        # Whether a read is outstanding; the scripted reader never leaves one.
        self.reading = False

    def read(self, timeout: Optional[float]) -> Optional[str]:
        self.timeouts.append(timeout)
        return self._answers.pop(0)


def ask(reader, timeout_seconds: float = 300.0, step_seconds: float = 60.0) -> tuple[run_plan.PromptResult, list[str]]:
    written: list[str] = []
    result = run_plan.ask(
        3, lambda ticked: [f"plan {ticked}"], reader, written.append, timeout_seconds=timeout_seconds, countdown_step_seconds=step_seconds,
        rows=flat_rows((True, True, True)),
    )
    return result, written


def test_no_input_starts_with_every_group_ticked_after_the_timeout() -> None:
    blocked = threading.Event()
    reader = run_plan.TimedLineReader(lambda: (blocked.wait(), "")[1])
    result, written = ask(reader, timeout_seconds=0.05, step_seconds=0.02)
    assert result.outcome is AnswerOutcome.START
    assert result.ticked == (True, True, True)
    assert result.timed_out
    assert reader.reading
    assert any("starting" in line.lower() for line in written)
    blocked.set()


def test_the_countdown_prints_while_it_waits() -> None:
    reader = ScriptedReader([None, None, "q\n"])
    _, written = ask(reader, timeout_seconds=300.0, step_seconds=60.0)
    assert sum("starts by itself in" in line for line in written) == 2


def test_the_first_input_stops_the_countdown_for_good() -> None:
    reader = ScriptedReader(["2\n", "\n"])
    result, written = ask(reader)
    assert result.outcome is AnswerOutcome.START
    assert result.ticked == (True, False, True)
    assert not result.timed_out
    assert reader.timeouts[0] is not None
    assert reader.timeouts[1] is None
    assert "plan (True, False, True)" in written


def test_a_refused_start_keeps_asking() -> None:
    reader = ScriptedReader(["n\n", "\n", "q\n"])
    result, written = ask(reader)
    assert result.outcome is AnswerOutcome.CANCEL
    assert any("nothing is ticked" in line for line in written)


def test_closed_input_cancels() -> None:
    result, _ = ask(ScriptedReader([""]))
    assert result.outcome is AnswerOutcome.CANCEL


def test_the_timed_reader_returns_a_line_and_leaves_no_read_outstanding() -> None:
    reader = run_plan.TimedLineReader(iter(["first\n", "second\n"]).__next__)
    assert reader.read(1.0) == "first\n"
    assert not reader.reading
    assert reader.read(1.0) == "second\n"


def test_the_timed_reader_keeps_a_late_line_for_the_next_read() -> None:
    release = threading.Event()
    reader = run_plan.TimedLineReader(lambda: (release.wait(), "late\n")[1])
    assert reader.read(0.01) is None
    assert reader.reading
    release.set()
    assert reader.read(1.0) == "late\n"


def test_the_timed_reader_reports_a_failing_source_as_closed_input() -> None:
    def closed() -> str:
        raise ValueError("I/O operation on closed file")

    assert run_plan.TimedLineReader(closed).read(1.0) == ""


# main


class FakeRunner:
    """Records the commands main runs and answers each with a status."""

    def __init__(self, status: int = 0) -> None:
        # The status every command returns.
        self._status = status
        # Each command run, with the stdin it was given.
        self.commands: list[tuple[list[str], bool]] = []

    def __call__(self, command: list[str], keep_stdin: bool) -> int:
        self.commands.append((command, keep_stdin))
        return self._status


def fail_collection(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
    raise AssertionError("main collected tests")


def run_main(arguments: list[str], environ: dict[str, str], runner: FakeRunner, tmp_path: Path, collect=fail_collection, interactive: bool = False) -> int:
    return run_plan.main(
        arguments, environ=environ, run_command=runner, collect=collect, interactive=interactive, durations_path=tmp_path / "durations.json",
        shard_durations_path=tmp_path / "shard_durations.json",
    )


def test_under_ci_main_runs_todays_command_without_collecting(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner(status=3)
    status = run_main(["--python", "py", "--group", "boot", "--jobs", "8", "--skip-groups", ""], {"CI": "true"}, runner, tmp_path)
    assert status == 3
    assert runner.commands == [(TODAYS_BOOT_COMMAND, True)]
    assert capsys.readouterr().out == ""


def test_under_ci_skip_groups_leave_groups_out_without_a_plan(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    status = run_main(["--python", "py", "--group", "boot or poison", "--jobs", "8", "--skip-groups", "poison,summon"], {"CI": "true"}, runner, tmp_path)
    assert status == 0
    assert runner.commands == [(run_plan.pytest_run_command("py", "scenario and (boot or poison) and not (poison or summon)", "8"), True)]
    assert capsys.readouterr().out == ""


def test_under_ci_an_unknown_skip_group_is_a_usage_error(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py", "--skip-groups", "bot"], {"CI": "true"}, runner, tmp_path) == run_plan.EXIT_USAGE
    assert runner.commands == []
    assert "bot" in capsys.readouterr().err


def test_an_unknown_skip_group_is_a_usage_error(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py", "--skip-groups", "bot"], {}, runner, tmp_path) == run_plan.EXIT_USAGE
    assert runner.commands == []
    assert "bot" in capsys.readouterr().err


def test_a_bad_threshold_is_a_usage_error(tmp_path: Path) -> None:
    runner = FakeRunner()
    environ = {run_plan.CONFIRM_MINUTES_VARIABLE: "soon"}
    assert run_main(["--python", "py"], environ, runner, tmp_path) == run_plan.EXIT_USAGE
    assert runner.commands == []


def test_a_bad_confirm_is_a_usage_error(tmp_path: Path) -> None:
    assert run_main(["--python", "py", "--confirm", "no"], {}, FakeRunner(), tmp_path) == run_plan.EXIT_USAGE


def collect_boot_tests(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
    return [test for test in TESTS if "boot" in test.markers], 0


def collect_forty_tests(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
    # 40 untimed tests at 15 seconds: 10 minutes on one worker.
    return [CollectedTest(f"scenarios/test_many.py::test_{index}", frozenset({"scenario", "boot"})) for index in range(40)], 0


def test_without_a_terminal_a_short_run_starts_at_once(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py", "--group", "boot"], {}, runner, tmp_path, collect=collect_boot_tests) == 0
    assert [command for command, _ in runner.commands] == [run_plan.pytest_run_command("py", "scenario and (boot)", "")]
    assert "[x] 1. boot" in capsys.readouterr().out


def test_without_a_terminal_a_long_run_stops_with_its_own_status_and_line(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py"], {}, runner, tmp_path, collect=collect_forty_tests) == run_plan.EXIT_NEEDS_CONFIRMATION
    assert runner.commands == []
    lines = capsys.readouterr().out.splitlines()
    marked = [line for line in lines if line.startswith(run_plan.NEEDS_CONFIRMATION_PREFIX)]
    assert len(marked) == 1
    assert "CONFIRM=yes" in marked[0]


def test_a_run_that_starts_prints_no_confirmation_line(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    assert run_main(["--python", "py", "--group", "boot"], {}, FakeRunner(), tmp_path, collect=collect_boot_tests) == 0
    assert run_plan.NEEDS_CONFIRMATION_PREFIX not in capsys.readouterr().out


def test_a_sharded_run_collects_without_the_shard_and_plans_this_shards_tests(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    environments: list[dict] = []

    def collect_all(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        environments.append(dict(environ))
        return TESTS, 0

    environ = {"ROTS_IT_SHARD": "1/3", "OTHER": "kept"}
    status = run_main(["--python", "py", "--skip-groups", "poison", "--plan-only"], environ, FakeRunner(), tmp_path, collect=collect_all)
    assert status == 0
    assert environments == [{"OTHER": "kept"}]
    # Without poison the sorted selection is test_one and test_four; shard 1/3 keeps test_one.
    assert "Total: 1 test," in capsys.readouterr().out


def test_a_sharded_run_plans_the_shard_the_shard_durations_file_balances(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    # test_four alone takes as long as the other three, so shard 1/2 keeps only it.
    (tmp_path / "shard_durations.json").write_text(f'{{"{ALL_IDS[3]}": 100.0}}', encoding="utf-8")

    def collect_all(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        return TESTS, 0

    status = run_main(["--python", "py", "--plan-only"], {"ROTS_IT_SHARD": "1/2"}, FakeRunner(), tmp_path, collect=collect_all)
    assert status == 0
    assert "Total: 1 test," in capsys.readouterr().out


@pytest.mark.parametrize("value", ["", "4/3", "two"])
def test_a_bad_shard_is_a_usage_error(tmp_path: Path, value: str, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py"], {"ROTS_IT_SHARD": value}, runner, tmp_path, collect=collect_boot_tests) == run_plan.EXIT_USAGE
    assert runner.commands == []
    assert "ROTS_IT_SHARD" in capsys.readouterr().err


def test_ctrl_c_while_collecting_cancels(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    def interrupted(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        raise KeyboardInterrupt

    runner = FakeRunner()
    assert run_main(["--python", "py"], {}, runner, tmp_path, collect=interrupted) == run_plan.EXIT_CANCELLED
    assert runner.commands == []
    assert "Cancelled" in capsys.readouterr().out


# A stand-in for pytest, found through PYTHONPATH: it notes that it started, and notes that it
# survived unless it is stopped within SURVIVAL_SECONDS.
SLOW_COLLECTION = """
import pathlib
import sys
import time

folder = pathlib.Path(__file__).resolve().parents[1]
(folder / "started").write_text("", encoding="utf-8")
time.sleep(1.0)
(folder / "survived").write_text("", encoding="utf-8")
"""
# Longer than SLOW_COLLECTION sleeps.
SURVIVAL_SECONDS = 1.5


def test_ctrl_c_while_collecting_stops_the_collection(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    stand_in = tmp_path / "pytest"
    stand_in.mkdir()
    # A regular package, which, unlike a namespace package, comes before the installed pytest.
    (stand_in / "__init__.py").write_text("", encoding="utf-8")
    (stand_in / "__main__.py").write_text(SLOW_COLLECTION, encoding="utf-8")

    def interrupted_communicate(process: subprocess.Popen, *arguments, **keywords):
        deadline = time.monotonic() + 10
        while not (tmp_path / "started").exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        raise KeyboardInterrupt

    monkeypatch.setattr(subprocess.Popen, "communicate", interrupted_communicate)
    environ = {"PYTHONPATH": str(tmp_path), "PATH": os.environ.get("PATH", "")}
    status = run_plan.main(["--python", sys.executable], environ=environ, run_command=FakeRunner(), interactive=False, durations_path=tmp_path / "durations.json")
    assert status == run_plan.EXIT_CANCELLED
    assert (tmp_path / "started").exists()
    time.sleep(SURVIVAL_SECONDS)
    assert not (tmp_path / "survived").exists()


def test_a_higher_threshold_lets_the_long_run_start(tmp_path: Path) -> None:
    runner = FakeRunner()
    environ = {run_plan.CONFIRM_MINUTES_VARIABLE: "11"}
    assert run_main(["--python", "py"], environ, runner, tmp_path, collect=collect_forty_tests) == 0
    assert len(runner.commands) == 1


def test_confirm_yes_runs_the_long_run_without_the_skipped_groups_and_prints_the_repeat_line(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    status = run_main(["--python", "py", "--confirm", "yes", "--skip-groups", "poison"], {}, runner, tmp_path, collect=collect_forty_tests)
    assert status == 0
    assert [command for command, _ in runner.commands] == [run_plan.pytest_run_command("py", "scenario and not (poison)", "")]
    assert "Repeat: make integration PYTHON=py SKIP_GROUPS=poison CONFIRM=yes" in capsys.readouterr().out


def test_plan_only_prints_the_plan_and_runs_nothing(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()
    assert run_main(["--python", "py", "--plan-only"], {"CI": "true"}, runner, tmp_path, collect=collect_forty_tests) == 0
    assert runner.commands == []
    assert "Total: 40 tests, about 10 min" in capsys.readouterr().out


def test_a_failed_collection_returns_its_status(tmp_path: Path) -> None:
    runner = FakeRunner()

    def failed(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        return None, 4

    assert run_main(["--python", "py", "--group", "bot"], {}, runner, tmp_path, collect=failed) == 4
    assert runner.commands == []


def test_an_empty_selection_runs_pytest_at_once(tmp_path: Path) -> None:
    runner = FakeRunner(status=5)

    def nothing(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        return [], 5

    assert run_main(["--python", "py"], {}, runner, tmp_path, collect=nothing, interactive=True) == 5
    assert len(runner.commands) == 1


def test_at_a_terminal_an_unticked_group_is_left_out_and_the_repeat_line_printed(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    runner = FakeRunner()

    def collect_all(python: str, group: str, environ) -> tuple[Optional[list[CollectedTest]], int]:
        return TESTS, 0

    reader = ScriptedReader(["2\n", "y\n"])
    status = run_plan.main(
        ["--python", "py"], environ={}, run_command=runner, collect=collect_all, interactive=True, reader=reader,
        durations_path=tmp_path / "durations.json",
    )
    assert status == 0
    assert runner.commands == [(run_plan.pytest_run_command("py", "scenario and not (poison)", ""), True)]
    assert "Repeat: make integration PYTHON=py SKIP_GROUPS=poison CONFIRM=yes" in capsys.readouterr().out


def test_at_a_terminal_cancel_runs_nothing_and_returns_its_own_status(tmp_path: Path) -> None:
    runner = FakeRunner()
    status = run_plan.main(
        ["--python", "py"], environ={}, run_command=runner, collect=collect_boot_tests, interactive=True, reader=ScriptedReader(["q\n"]),
        durations_path=tmp_path / "durations.json",
    )
    assert status == run_plan.EXIT_CANCELLED
    assert runner.commands == []


def test_a_run_started_by_the_countdown_gets_no_terminal_input(tmp_path: Path) -> None:
    runner = FakeRunner()
    blocked = threading.Event()
    reader = run_plan.TimedLineReader(lambda: (blocked.wait(), "")[1])
    status = run_plan.main(
        ["--python", "py"], environ={}, run_command=runner, collect=collect_boot_tests, interactive=True, reader=reader,
        durations_path=tmp_path / "durations.json", prompt_timeout_seconds=0.05, countdown_step_seconds=0.02,
    )
    assert status == 0
    assert [keep_stdin for _, keep_stdin in runner.commands] == [False]
    blocked.set()


def test_main_estimates_from_the_durations_file(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    durations_path = tmp_path / "durations.json"
    durations_path.write_text('{"scenarios/test_many.py::test_0": 1200}', encoding="utf-8")
    status = run_plan.main(
        ["--python", "py", "--plan-only"], environ={}, run_command=FakeRunner(), collect=collect_forty_tests, interactive=False,
        durations_path=durations_path,
    )
    assert status == 0
    # 1200 + 39 * 15 seconds.
    assert "Total: 40 tests, about 30 min" in capsys.readouterr().out


def test_ctrl_c_at_the_prompt_cancels(tmp_path: Path) -> None:
    class InterruptedReader(ScriptedReader):
        def read(self, timeout: Optional[float]) -> Optional[str]:
            raise KeyboardInterrupt

    runner = FakeRunner()
    status = run_plan.main(
        ["--python", "py"], environ={}, run_command=runner, collect=collect_boot_tests, interactive=True, reader=InterruptedReader([]),
        durations_path=tmp_path / "durations.json",
    )
    assert status == run_plan.EXIT_CANCELLED
    assert runner.commands == []


# Signals while pytest runs


class FakeProcess:
    """A child process that records the signals sent to it."""

    def __init__(self) -> None:
        # Every signal sent, in order.
        self.signals: list[int] = []

    def send_signal(self, signal_number: int) -> None:
        self.signals.append(signal_number)


def test_a_sigterm_before_the_child_starts_reaches_it_once_it_has() -> None:
    process = FakeProcess()
    forwarder = run_plan.ChildSignalForwarder()
    forwarder.install()
    try:
        signal.raise_signal(signal.SIGTERM)
        assert process.signals == []
        forwarder.attach(process)
    finally:
        forwarder.restore()
    assert process.signals == [signal.SIGTERM]


def test_only_the_first_stopping_signal_is_forwarded_and_ctrl_c_is_ignored() -> None:
    process = FakeProcess()
    forwarder = run_plan.ChildSignalForwarder()
    forwarder.install()
    try:
        forwarder.attach(process)
        signal.raise_signal(signal.SIGINT)
        signal.raise_signal(signal.SIGTERM)
        signal.raise_signal(signal.SIGTERM)
    finally:
        forwarder.restore()
    assert process.signals == [signal.SIGTERM]


def test_restore_puts_back_the_previous_handlers() -> None:
    before = {signal_number: signal.getsignal(signal_number) for signal_number in (signal.SIGINT, signal.SIGTERM)}
    forwarder = run_plan.ChildSignalForwarder()
    forwarder.install()
    forwarder.restore()
    assert {signal_number: signal.getsignal(signal_number) for signal_number in before} == before


@pytest.mark.skipif(not hasattr(signal, "SIGHUP"), reason="this platform has no SIGHUP")
def test_an_ignored_signal_stays_ignored_so_the_child_inherits_that() -> None:
    previous = signal.signal(signal.SIGHUP, signal.SIG_IGN)
    try:
        forwarder = run_plan.ChildSignalForwarder()
        forwarder.install()
        try:
            assert signal.getsignal(signal.SIGHUP) == signal.SIG_IGN
        finally:
            forwarder.restore()
    finally:
        signal.signal(signal.SIGHUP, previous)


# A child that, like pytest, stops on SIGTERM or SIGHUP: it signals its parent (the process running
# run_in_foreground) with Ctrl-C, then SIGTERM, waits until that SIGTERM reaches it, signals SIGTERM
# and SIGHUP again, notes each signal it receives and exits 7.
SIGNALLING_CHILD = """
import os
import pathlib
import signal
import sys
import time

log = pathlib.Path(sys.argv[1])
received = []


def note(signal_number, frame):
    received.append(signal.Signals(signal_number).name)
    log.write_text(" ".join(received), encoding="utf-8")


signal.signal(signal.SIGTERM, note)
signal.signal(signal.SIGHUP, note)
parent = os.getppid()
os.kill(parent, signal.SIGINT)
os.kill(parent, signal.SIGTERM)
deadline = time.monotonic() + 5
while not received and time.monotonic() < deadline:
    time.sleep(0.01)
os.kill(parent, signal.SIGTERM)
os.kill(parent, signal.SIGHUP)
time.sleep(1.0)
sys.exit(7)
"""


@pytest.mark.skipif(not hasattr(signal, "SIGHUP"), reason="signals another process with os.kill, which is POSIX-only")
def test_run_in_foreground_forwards_the_first_sigterm_and_returns_the_childs_status(tmp_path: Path) -> None:
    script = tmp_path / "child.py"
    script.write_text(SIGNALLING_CHILD, encoding="utf-8")
    log = tmp_path / "signals.log"
    assert run_plan.run_in_foreground([sys.executable, str(script), str(log)], False) == 7
    assert log.read_text(encoding="utf-8") == "SIGTERM"


def test_a_child_killed_by_a_signal_returns_the_shells_status(tmp_path: Path) -> None:
    command = [sys.executable, "-c", "import os, signal; signal.raise_signal(signal.SIGTERM)"]
    status = run_plan.run_in_foreground(command, False)
    if os.name == "posix":
        assert status == 128 + signal.SIGTERM
    else:
        assert status != 0
