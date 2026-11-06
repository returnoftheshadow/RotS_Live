"""Object skins: boot reads a skin from its world file, and a restyle changes a copy in place.

New-behaviour pins for the object skins feature (docs/systems/object-skins.md). The test world's
skin family is obj 1140 (a plain steel sword, the base) with skins 1141 (owns its cost) and 1142;
obj 1143 names a base that does not exist. Mob 1136, the steward, runs script 1101 when it hears
someone speak: it restyles the speaker's plain steel sword, wherever it is carried, into 1141,
then tries the cap (1137, no look of the sword), which is refused and logged.

The only rolls on these paths are the steward's hit points and age when `load mob` reads it
(read_mobile, db.cpp); no assertion reads them. vstat, load obj, give, wield, put, restyle and a
script line draw nothing. The schedule keeps mob activity and zone resets from running between
commands.
"""

from __future__ import annotations

import time

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.skins]

# A level-20 human warrior in the arena centre who holds the swords: a mortal on the
# implementor's side, so the implementor's commands find it by name.
OWNER = Character(
    name="Testowner",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)

BASE = 1140
SKIN_OWNS_COST = 1141
SKIN = 1142
ORPHAN = 1143
CAP = 1137
BAG = 1136
STEWARD = 1136


def vstat(implementor: GameSession, vnum: int) -> str:
    """The implementor's vstat of an object prototype; it has lines ending in ']', so read it to
    its last line (do_stat_object, act_wiz.cpp)."""
    implementor.send_line(f"vstat obj {vnum}")
    return implementor.expect(["Affections:"]) + implementor.drain(0.5)


def wait_for_log_line(server, line: str, timeout: float = 8.0) -> str:
    """Re-reads game.log until it holds line, or raises after timeout seconds. Under the Docker
    launcher the server's log reaches game.log apart from the game text, so a line logged
    before a reply can still be on its way when the reply arrives."""
    deadline = time.monotonic() + timeout
    while True:
        game_log = (server.run_dir / "game.log").read_text(errors="replace")
        if line in game_log:
            return game_log
        if time.monotonic() >= deadline:
            raise AssertionError(f"{line!r} not in game.log within {timeout}s:\n{game_log[-2000:]}")
        time.sleep(0.2)


def give_to_owner(implementor: GameSession, vnum: int, keyword: str) -> None:
    created = implementor.command(f"load obj {vnum}")
    assert created.contains("You create"), created.text
    given = implementor.command(f"give {keyword} testowner")
    assert given.contains("You give"), given.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_boot_reads_skins_from_the_world_file_and_demotes_one_without_a_base(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)

    base = vstat(implementor, BASE)
    assert f"Skins: {SKIN_OWNS_COST}, {SKIN}" in base, base

    # The skin's own file line says weight 999, cost 5000 and level 1; every stat but its own
    # cost comes from the base (apply_object_skins, object_skins.cpp).
    owns_cost = vstat(implementor, SKIN_OWNS_COST)
    assert f"Skin of: obj {BASE} (a plain steel sword)" in owns_cost, owns_cost
    assert "Weight: 350, Value: 5000," in owns_cost, owns_cost
    assert "Level 10," in owns_cost, owns_cost
    plain_skin = vstat(implementor, SKIN)
    assert "Weight: 350, Value: 1000," in plain_skin, plain_skin

    orphan = vstat(implementor, ORPHAN)
    assert "Skin of" not in orphan, orphan
    wait_for_log_line(server, f"OBJ ERROR: object #{ORPHAN}: base obj 1199 not found - loaded as plain object")


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_restyle_changes_a_wielded_sword_in_place_and_refuses_another_family(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    give_to_owner(implementor, BASE, "sword")
    wielded = owner.command("wield sword")
    assert wielded.contains("You wield a plain steel sword."), wielded.text

    owner.drain(0.5)
    restyled = implementor.command(f"restyle testowner sword {SKIN_OWNS_COST}")
    assert restyled.contains(
        f"Restyled obj {BASE} (a plain steel sword) to obj {SKIN_OWNS_COST} (an engraved steel sword)."
    ), restyled.text
    # The owner is told what changed, without the implementor's name (do_restyle, act_wiz.cpp).
    told = owner.expect(["A plain steel sword has been restyled into an engraved steel sword."])
    assert IMPLEMENTOR.name not in told, told
    equipment = owner.command("equipment")
    assert "<wielded>" in equipment.text and "an engraved steel sword" in equipment.text, equipment.text

    refused = implementor.command(f"restyle testowner sword {CAP}")
    assert refused.contains(f"Obj {CAP} is not a look of obj {SKIN_OWNS_COST}."), refused.text
    still = owner.command("equipment")
    assert "an engraved steel sword" in still.text, still.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_script_restyles_a_sword_in_a_carried_bag_and_logs_a_refused_line(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    implementor.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert implementor.command(f"load mob {STEWARD}").contains("You create"), "the steward"
    created = implementor.command(f"load obj {BAG}")
    assert created.contains("You create"), created.text
    created = implementor.command(f"load obj {BASE}")
    assert created.contains("You create"), created.text
    put = implementor.command("put sword bag")
    assert put.contains("You put a plain steel sword in a leather bag."), put.text
    given = implementor.command("give bag testowner")
    assert given.contains("You give"), given.text

    owner.send_line("say hello")
    heard = owner.expect(["Your sword is engraved."]) + owner.drain(0.5)
    assert "Your sword is engraved." in heard, heard
    in_bag = owner.command("look in bag")
    assert "an engraved steel sword" in in_bag.text, in_bag.text

    wait_for_log_line(server, f"SCRIPT ERROR: script #1101, line 4 (restyle obj): obj {SKIN_OWNS_COST} -> {CAP} not same family")
