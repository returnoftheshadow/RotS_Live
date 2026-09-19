from __future__ import annotations

import pytest

from rots_harness import fixtures, records

pytestmark = pytest.mark.scenario


def test_server_boots_on_the_test_world_and_the_imp_can_tick(server, imp, harness) -> None:
    look = imp.command("look")
    assert look.contains("Immortal Start"), look.text

    tick = harness.tick()
    assert tick.contains("Harness: hourly tick complete.")


def test_wizset_and_stat_agree_on_a_victims_hit_points(server, imp, victim) -> None:
    imp.command(f"goto {fixtures.ROOM_ARENA_CENTRE}")
    imp.command("wizset harnessvictim hit 5")

    stat = imp.command("stat harnessvictim")
    assert stat.hit_points() is not None, stat.text
    current, maximum = stat.hit_points()
    assert current == 5
    assert maximum >= 5

    imp.command("restore harnessvictim")
    assert imp.command("stat harnessvictim").hit_points()[0] == maximum


def test_roster_records_start_empty(server) -> None:
    for spec in server.roster:
        assert records.read_exploits(server.lib_dir, spec.name) == []
