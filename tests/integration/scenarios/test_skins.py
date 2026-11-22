"""Object skins: boot reads a skin from its world file, and a restyle changes a copy in place.

These pin the object skins feature (docs/systems/object-skins.md). The test world's skin family
is obj 1140 (a plain steel sword, the base) with skins 1141 (owns its cost) and 1142; obj 1143
names a base that does not exist. Mob 1136, the steward, runs script 1101 when it hears
someone speak: it restyles the speaker's plain steel sword, wherever it is carried, into 1141,
then tries the cap (1137, no look of the sword), which is refused and logged.

The only rolls on these paths are the steward's hit points and age when `load mob` reads it
(read_mobile, db.cpp); no assertion reads them. vstat, load obj, give, wield, put, restyle and a
script line draw nothing. The schedule keeps mob activity and zone resets from running between
commands.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_log import wait_for_log_count
from rots_harness.game_text import PUT_FORMAT
from rots_harness.immortal_commands import GIVEN_START, give_new_object, goto, load_mob, load_object
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import OBJECT_STAT_LAST_LINE

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

# The skin family's base, obj 1140, the plain steel sword.
BASE = world.OBJECT_PLAIN_STEEL_SWORD
# Obj 1141, a skin of BASE whose file gives its own cost.
SKIN_OWNS_COST = 1141
# Obj 1142, a skin of BASE that takes every stat from it.
SKIN = 1142
# Obj 1143, which names base obj 1199, which does not exist.
ORPHAN = 1143
# Mob 1136, the steward, which runs script 1101 when it hears someone speak.
STEWARD = 1136
# Seconds to wait for a line to reach game.log.
LOG_TIMEOUT_SECONDS = 8.0
# Seconds to keep reading after a reply, for the rest of it.
REPLY_SETTLE_SECONDS = 0.5


def vstat(implementor: GameSession, vnum: int) -> str:
    """Return the implementor's vstat of an object prototype, read to its last line.

    The reply has lines ending in ']', so it is read with reply_to (do_stat_object, act_wiz.cpp).
    """
    return reply_to(implementor, f"vstat obj {vnum}", [OBJECT_STAT_LAST_LINE], settle=REPLY_SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_boot_reads_skins_from_the_world_file_and_demotes_one_without_a_base(server) -> None:
    """Pins boot's reading of skins: the base lists them, and a skin without a base is plain."""
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
    orphan_log_line = f"OBJ ERROR: object #{ORPHAN}: base obj 1199 not found - loaded as plain object"
    wait_for_log_count(server.handle.log_path, orphan_log_line, 1, timeout=LOG_TIMEOUT_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_restyle_changes_a_wielded_sword_in_place_and_refuses_another_family(server) -> None:
    """Pins restyle: it changes a wielded sword in place and refuses an object of another family."""
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    owner_keyword = OWNER.name.lower()
    goto(implementor, world.ROOM_ARENA_CENTRE)
    give_new_object(implementor, BASE, "sword", owner_keyword)
    wielded = owner.command("wield sword")
    assert wielded.contains("You wield a plain steel sword."), wielded.text

    owner.drain(REPLY_SETTLE_SECONDS)
    restyled = implementor.command(f"restyle {owner_keyword} sword {SKIN_OWNS_COST}")
    assert restyled.contains(
        f"Restyled obj {BASE} (a plain steel sword) to obj {SKIN_OWNS_COST} (an engraved steel sword)."
    ), restyled.text
    # The owner is told what changed, without the implementor's name (do_restyle, act_wiz.cpp).
    told = owner.expect(["A plain steel sword has been restyled into an engraved steel sword."])
    assert IMPLEMENTOR.name not in told, told
    equipment = owner.command("equipment")
    assert "<wielded>" in equipment.text and "an engraved steel sword" in equipment.text, equipment.text

    refused = implementor.command(f"restyle {owner_keyword} sword {world.OBJECT_LEATHER_CAP}")
    assert refused.contains(f"Obj {world.OBJECT_LEATHER_CAP} is not a look of obj {SKIN_OWNS_COST}."), refused.text
    still = owner.command("equipment")
    assert "an engraved steel sword" in still.text, still.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, OWNER)
def test_a_script_restyles_a_sword_in_a_carried_bag_and_logs_a_refused_line(server) -> None:
    """Pins a script's restyle: it reaches a sword in a carried bag and logs a refused line."""
    implementor = server.login(IMPLEMENTOR.name)
    owner = server.login(OWNER.name)
    goto(implementor, world.ROOM_ARENA_CENTRE)
    load_mob(implementor, STEWARD)
    load_object(implementor, world.OBJECT_LEATHER_BAG)
    load_object(implementor, BASE)
    put = implementor.command("put sword bag")
    put_reply = PUT_FORMAT.format("a plain steel sword", world.OBJECT_LEATHER_BAG_SHORT_DESCRIPTION)
    assert put.contains(put_reply), put.text
    given = implementor.command(f"give bag {OWNER.name.lower()}")
    assert given.contains(GIVEN_START), given.text

    heard = reply_to(owner, "say hello", ["Your sword is engraved."], settle=REPLY_SETTLE_SECONDS)
    assert "Your sword is engraved." in heard, heard
    in_bag = owner.command("look in bag")
    assert "an engraved steel sword" in in_bag.text, in_bag.text

    refusal_log_line = (
        f"SCRIPT ERROR: script #1101, line 4 (restyle obj): obj {SKIN_OWNS_COST} -> {world.OBJECT_LEATHER_CAP} "
        "not same family"
    )
    wait_for_log_count(server.handle.log_path, refusal_log_line, 1, timeout=LOG_TIMEOUT_SECONDS)
