"""Splits a run's collected tests into numbered shards, so parallel jobs each run one share of about equal time."""

from __future__ import annotations

import heapq
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Optional

from rots_harness.durations import DEFAULT_TEST_SECONDS

# A shard value: the shard's number, a slash and the shard count, digits only.
SHARD_PATTERN = re.compile(r"([0-9]+)/([0-9]+)")
# The committed per-test seconds the shards are balanced by, relative to tests/integration;
# make integration-shard-durations refreshes it from CI's results.
SHARD_DURATIONS_RELATIVE = Path("shard_durations.json")
# The least seconds the split counts for any test, so tests recorded at or near 0 s (a skip, say)
# still spread across the shards instead of all joining the shard with the least time.
MINIMUM_SPLIT_SECONDS = 1.0


@dataclass(frozen=True)
class Shard:
    """One share of a run split into count shards; number is 1-based."""

    # Which share this run keeps, from 1 to count.
    number: int
    # How many shares the run is split into.
    count: int


def parse_shard(value: str) -> Shard:
    """The shard a 'k/N' value names, such as '2/4'; raises ValueError unless 1 <= k <= N."""
    match = SHARD_PATTERN.fullmatch(value)
    if match is None:
        raise ValueError(f"expected k/N with whole numbers, such as 2/4, not {value!r}")
    number = int(match.group(1))
    count = int(match.group(2))
    if count < 1 or number < 1 or number > count:
        raise ValueError(f"expected k/N with 1 <= k <= N, not {value!r}")
    return Shard(number, count)


def in_shard(node_ids: Iterable[str], shard: Shard, recorded_seconds: Optional[Mapping[str, float]] = None) -> set[str]:
    """The node ids the shard keeps, from shares of about equal recorded seconds.

    Every id lands in exactly one of the count shards, and the shares depend only on the set of
    ids and recorded_seconds, never on their order, so every shard and every pytest-xdist worker
    of the same selection agrees on which shard owns each test. A test with no recorded time
    counts DEFAULT_TEST_SECONDS, and every test at least MINIMUM_SPLIT_SECONDS. With no recorded times at all, the shards take turns in sorted
    node id order, starting at shard 1.
    """
    seconds = recorded_seconds or {}
    unique_ids = set(node_ids)

    def test_seconds(node_id: str) -> float:
        return max(seconds.get(node_id, DEFAULT_TEST_SECONDS), MINIMUM_SPLIT_SECONDS)

    # Longest first, each to the shard with the least time so far (the lowest number on a tie):
    # the classic greedy split, whose slowest shard is never a third slower than the best
    # possible, and quick enough for every worker to repeat at collection. The node id breaks
    # ties between equal times, so the order is total.
    longest_first = sorted(unique_ids, key=lambda node_id: (-test_seconds(node_id), node_id))
    shard_loads = [(0.0, index) for index in range(shard.count)]
    kept = set()
    for node_id in longest_first:
        load, index = heapq.heappop(shard_loads)
        if index == shard.number - 1:
            kept.add(node_id)
        heapq.heappush(shard_loads, (load + test_seconds(node_id), index))
    return kept
