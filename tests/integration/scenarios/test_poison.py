"""Poison over game hours: tainted bread hurts a mortal once per granted game hour for its
duration, then wears off."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.poison]

# A level-20 human warrior in the arena: a mortal, since immortals are not poisoned by food.
MORTAL = Character(
    name="Testeater",
    race="human",
    sex="male",
    level=20,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)
# The tainted bread's virtual number in the test world (tests/integration/world/obj/11.obj).
TAINTED_BREAD_VNUM = 1139
# The bread's fill value is 2 and food poison lasts twice its fill, in game hours.
POISON_HOURS = 4
# Damage the first poison tick does; regeneration cannot raise full hit points before it.
FIRST_TICK_DAMAGE = 5
# A hunger low enough that eating is allowed (above 20 is too full) and high enough not to
# reach 0 in this test.
HUNGER_BEFORE_EATING = 10
# What the mortal reads after eating the bread.
EATEN_POISON_MARKER = "Oops, that tasted rather strange!"
# What the mortal reads on each poison tick.
POISON_TICK_MARKER = "You writhe in pain as poison courses through your veins."
# What the mortal reads when the poison wears off.
POISON_WEARS_OFF_MARKER = "You feel less sick."
# Seconds to keep reading after an acknowledgement, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# The score line with current and maximum hit points.
HIT_POINTS_PATTERN = re.compile(r"You have (-?\d+)/(\d+) hit,")


def hit_points(session: GameSession) -> tuple:
    """The character's current and maximum hit points, read from score."""
    score = session.command("score").text
    match = HIT_POINTS_PATTERN.search(score)
    assert match is not None, f"no hit-point line in score:\n{score}"
    return int(match.group(1)), int(match.group(2))


def text_after_an_hour(server, session: GameSession, expected_marker: str = "") -> str:
    """Advances one game hour and returns what the session read during it."""
    server.advance_hours(1)
    if expected_marker:
        return session.expect([expected_marker]) + session.drain(SETTLE_SECONDS)
    return session.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MORTAL)
def test_tainted_bread_poisons_a_mortal_once_per_game_hour_then_wears_off(server) -> None:
    implementor = server.login(IMPLEMENTOR.name)
    mortal = server.login(MORTAL.name)
    mortal_keyword = MORTAL.name.lower()

    current_hit, maximum_hit = hit_points(mortal)
    assert current_hit == maximum_hit, (
        f"expected full hit points before eating, got {current_hit}/{maximum_hit}"
    )

    set_hunger = implementor.command(f"wizset {mortal_keyword} hunger {HUNGER_BEFORE_EATING}")
    assert set_hunger.contains(f"hunger set to {HUNGER_BEFORE_EATING}."), set_hunger.text
    implementor.command(f"goto {mortal_keyword}")
    assert implementor.command(f"load obj {TAINTED_BREAD_VNUM}").contains("You create")
    assert implementor.command(f"give bread {mortal_keyword}").contains("You give")
    assert mortal.command("eat bread").contains(EATEN_POISON_MARKER)

    first_hour = text_after_an_hour(server, mortal, POISON_TICK_MARKER)
    assert first_hour.count(POISON_TICK_MARKER) == 1, first_hour
    assert hit_points(mortal) == (maximum_hit - FIRST_TICK_DAMAGE, maximum_hit)

    for hour in range(2, POISON_HOURS + 1):
        during_hour = text_after_an_hour(server, mortal, POISON_TICK_MARKER)
        assert during_hour.count(POISON_TICK_MARKER) == 1, f"hour {hour}:\n{during_hour}"

    wearing_off = text_after_an_hour(server, mortal, POISON_WEARS_OFF_MARKER)
    assert POISON_TICK_MARKER not in wearing_off, wearing_off

    after_poison = text_after_an_hour(server, mortal)
    assert POISON_TICK_MARKER not in after_poison, after_poison
    assert POISON_WEARS_OFF_MARKER not in after_poison, after_poison
