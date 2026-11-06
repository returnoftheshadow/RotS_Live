from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

# tests/integration, where pytest finds conftest.py, pytest.ini and the scenarios.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# A shard far past the boot group's few tests, so its share is empty while the selection is not.
EMPTY_SHARD = "999/999"


def run_boot_scenarios(shard: str, extra_arguments: list[str]) -> subprocess.CompletedProcess:
    """Runs the boot scenarios in a separate pytest under the shard; nothing starts a server when the share is empty."""
    environment = {name: value for name, value in os.environ.items() if not name.startswith(("PYTEST_", "ROTS_IT_"))}
    environment["ROTS_IT_LAUNCHER"] = "local"
    environment["ROTS_IT_SHARD"] = shard
    command = [sys.executable, "-m", "pytest", "scenarios", "-q", "-p", "no:cacheprovider", "-m", "scenario and boot", *extra_arguments]
    return subprocess.run(
        command, cwd=INTEGRATION_ROOT, env=environment, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120, check=False,
    )


@pytest.mark.parametrize("extra_arguments", [[], ["-n", "2"]], ids=["one process", "xdist"])
def test_a_shard_whose_share_is_empty_passes(extra_arguments: list[str]) -> None:
    completed = run_boot_scenarios(EMPTY_SHARD, extra_arguments)
    assert completed.returncode == 0, completed.stdout + completed.stderr


def test_an_empty_selection_still_fails_under_xdist() -> None:
    # -k matches no boot test, so the selection itself is empty, not just this shard's share.
    completed = run_boot_scenarios(EMPTY_SHARD, ["-n", "2", "-k", "no_such_scenario_name"])
    assert completed.returncode == pytest.ExitCode.NO_TESTS_COLLECTED, completed.stdout + completed.stderr
