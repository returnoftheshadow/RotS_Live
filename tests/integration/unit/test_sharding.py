from __future__ import annotations

import random
from pathlib import Path

import pytest

from rots_harness.durations import DEFAULT_TEST_SECONDS, read_durations
from rots_harness.sharding import MINIMUM_SPLIT_SECONDS, SHARD_DURATIONS_RELATIVE, Shard, in_shard, parse_shard

# tests/integration, which holds the committed shard durations file.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]

# Node ids shaped like the scenarios', in no particular order.
NODE_IDS = [f"scenarios/test_{module}.py::test_case_{case}" for module in ("summon", "boot", "poison") for case in range(7)]


def test_a_shard_value_names_its_number_and_count() -> None:
    assert parse_shard("2/4") == Shard(number=2, count=4)


def test_a_single_shard_is_allowed() -> None:
    assert parse_shard("1/1") == Shard(number=1, count=1)


@pytest.mark.parametrize("value", ["", "2", "2/", "/4", "a/4", "2/b", "2/4/1", " 2/4", "2/4 ", "+2/4", "-1/4", "2.0/4", "0/4", "5/4", "1/0", "0/0"])
def test_a_malformed_shard_value_is_refused(value: str) -> None:
    with pytest.raises(ValueError):
        parse_shard(value)


@pytest.mark.parametrize("count", [1, 2, 3, 4, 5, 21, 40])
def test_every_test_lands_in_exactly_one_shard(count: int) -> None:
    shares = [in_shard(NODE_IDS, Shard(number, count)) for number in range(1, count + 1)]
    assert sorted(node_id for share in shares for node_id in share) == sorted(NODE_IDS)


def test_the_shares_differ_in_size_by_at_most_one() -> None:
    sizes = [len(in_shard(NODE_IDS, Shard(number, 4))) for number in range(1, 5)]
    assert max(sizes) - min(sizes) <= 1


def test_the_split_does_not_depend_on_collection_order() -> None:
    shuffled = list(NODE_IDS)
    random.Random(7).shuffle(shuffled)
    for number in range(1, 5):
        assert in_shard(shuffled, Shard(number, 4)) == in_shard(NODE_IDS, Shard(number, 4))


def test_the_shards_take_turns_in_sorted_order() -> None:
    ids = ["d", "b", "a", "c", "e"]
    assert in_shard(ids, Shard(1, 2)) == {"a", "c", "e"}
    assert in_shard(ids, Shard(2, 2)) == {"b", "d"}


def test_more_shards_than_tests_leaves_the_later_shards_empty() -> None:
    assert in_shard(["a", "b"], Shard(3, 4)) == set()
    assert in_shard(["a", "b"], Shard(4, 4)) == set()


def test_no_tests_gives_an_empty_share() -> None:
    assert in_shard([], Shard(1, 4)) == set()


# Seconds for most of NODE_IDS, every third one left out, as a durations file missing new tests would be.
RECORDED_SECONDS = {node_id: float((index * 37) % 90) for index, node_id in enumerate(NODE_IDS) if index % 3}


@pytest.mark.parametrize("count", [1, 2, 3, 4, 5, 6, 7, 8, 13, 21, 40])
@pytest.mark.parametrize("seed", range(5))
def test_with_recorded_times_every_test_lands_in_exactly_one_shard(count: int, seed: int) -> None:
    generator = random.Random(seed)
    recorded = {node_id: generator.uniform(0.0, 90.0) for node_id in NODE_IDS if generator.random() < 0.7}
    shares = [in_shard(NODE_IDS, Shard(number, count), recorded) for number in range(1, count + 1)]
    kept = [node_id for share in shares for node_id in share]
    assert sorted(kept) == sorted(NODE_IDS)


def test_a_test_with_no_recorded_time_lands_in_exactly_one_shard() -> None:
    missing = [node_id for node_id in NODE_IDS if node_id not in RECORDED_SECONDS]
    assert missing
    for node_id in missing:
        owners = [number for number in range(1, 5) if node_id in in_shard(NODE_IDS, Shard(number, 4), RECORDED_SECONDS)]
        assert len(owners) == 1


def test_a_test_with_no_recorded_time_counts_the_default_seconds() -> None:
    # The new test counts as long as the recorded one, so each takes a shard and a short test joins it;
    # counted as nothing, the new test would have joined the short ones.
    recorded = {"long": DEFAULT_TEST_SECONDS, "short_a": 1.0, "short_b": 1.0}
    assert in_shard(["long", "new", "short_a", "short_b"], Shard(1, 2), recorded) == {"long", "short_a"}
    assert in_shard(["long", "new", "short_a", "short_b"], Shard(2, 2), recorded) == {"new", "short_b"}


def test_recorded_times_balance_the_shares_by_time_rather_than_by_count() -> None:
    # In turns, a and c would share a shard: 13 s against 5 s.
    recorded = {"a": 9.0, "b": 5.0, "c": 4.0}
    assert in_shard(["a", "b", "c"], Shard(1, 2), recorded) == {"a"}
    assert in_shard(["a", "b", "c"], Shard(2, 2), recorded) == {"b", "c"}


def test_the_balanced_split_does_not_depend_on_collection_order() -> None:
    shuffled = list(NODE_IDS)
    random.Random(11).shuffle(shuffled)
    for number in range(1, 6):
        assert in_shard(shuffled, Shard(number, 5), RECORDED_SECONDS) == in_shard(NODE_IDS, Shard(number, 5), RECORDED_SECONDS)


def test_equal_recorded_times_take_turns_in_sorted_order() -> None:
    recorded = {node_id: 3.0 for node_id in "abcde"}
    assert in_shard(list("dbace"), Shard(1, 2), recorded) == {"a", "c", "e"}
    assert in_shard(list("dbace"), Shard(2, 2), recorded) == {"b", "d"}


def test_a_repeated_node_id_is_kept_once() -> None:
    assert in_shard(["a", "a", "b"], Shard(1, 1), {"a": 1.0}) == {"a", "b"}


def test_the_committed_times_name_only_collected_style_node_ids() -> None:
    recorded = read_durations(INTEGRATION_ROOT / SHARD_DURATIONS_RELATIVE)
    assert recorded, "the committed shard durations file is missing or empty"
    assert all(node_id.startswith("scenarios/test_") and ".py::test_" in node_id for node_id in recorded)


@pytest.mark.parametrize("count", range(1, 21))
def test_the_committed_times_split_the_suite_and_a_new_test_into_every_shard_once(count: int) -> None:
    recorded = read_durations(INTEGRATION_ROOT / SHARD_DURATIONS_RELATIVE)
    node_ids = [*recorded, "scenarios/test_new.py::test_not_yet_timed"]
    shares = [in_shard(node_ids, Shard(number, count), recorded) for number in range(1, count + 1)]
    kept = [node_id for share in shares for node_id in share]
    assert sorted(kept) == sorted(node_ids)


@pytest.mark.parametrize("count", [2, 3, 4, 7])
def test_tests_recorded_at_no_time_spread_across_the_shards(count: int) -> None:
    recorded = {node_id: 0.0 for node_id in NODE_IDS}
    sizes = [len(in_shard(NODE_IDS, Shard(number, count), recorded)) for number in range(1, count + 1)]
    assert sum(sizes) == len(NODE_IDS)
    assert max(sizes) - min(sizes) <= 1


def test_a_long_shard_takes_no_more_near_zero_tests_than_the_floor_allows() -> None:
    # One 10 s test against twenty tests recorded at 0 s: counted at the floor, ten of them fill the
    # other shard before any joins the long one.
    short_ids = [f"short_{index:02}" for index in range(20)]
    recorded = {"long": 10 * MINIMUM_SPLIT_SECONDS, **{node_id: 0.0 for node_id in short_ids}}
    long_shard = in_shard(["long", *short_ids], Shard(1, 2), recorded)
    other_shard = in_shard(["long", *short_ids], Shard(2, 2), recorded)
    assert "long" in long_shard
    assert len(other_shard) == 15
    assert sorted(long_shard | other_shard) == sorted(["long", *short_ids])
