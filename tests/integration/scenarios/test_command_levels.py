"""Immortal command levels: teleport needs level 93 and refuses malformed requests, and shutdown
needs level 96."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character

pytestmark = [pytest.mark.scenario, pytest.mark.wizlevels]


def immortal(name: str, level: int) -> Character:
    """A human warrior immortal of the given level; immortals log in to Immortal Start."""
    return Character(
        name=name,
        race="human",
        sex="male",
        level=level,
        load_room=world.ROOM_IMMORTAL_START,
        class_name="warrior",
    )


# A level-93 immortal (LEVEL_GOD, structs.h): the lowest level teleport's row in the command
# table allows (COMMANDO 122, interpre.cpp).
GOD = immortal("Testgod", 93)
# A level-92 immortal: one level below teleport's.
LESSER_GOD = immortal("Testdemigod", 92)
# A level-95 immortal (LEVEL_AREAGOD): one level below shutdown's row (COMMANDO 61,
# LEVEL_GRGOD - 1 = 96).
AREA_GOD = immortal("Testareagod", 95)
# A level-96 immortal: the lowest level shutdown's row allows.
SHUTDOWN_GOD = immortal("Testgreatgod", 96)
# A level-12 human warrior in the arena centre: the mortal the immortals try to teleport.
MORTAL = Character(
    name="Testwalker",
    race="human",
    sex="female",
    level=12,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="warrior",
)

# What command_interpreter (interpre.cpp) answers when the speaker's level is below the
# command's row, and no social has the word.
UNRECOGNIZED_MARKER = "Unrecognized command."
# What the teleported character reads (do_teleport, act_wiz.cpp); format with the immortal's name.
TELEPORTED_MARKER_FORMAT = "{} has teleported you!"
# The end of TELEPORTED_MARKER_FORMAT, whoever teleported.
ANY_TELEPORTED_MARKER = "has teleported you!"
# What the room the character leaves reads (do_teleport); format with the character's name.
DISAPPEARS_MARKER_FORMAT = "{} disappears in a puff of smoke."
# What the room the character arrives in reads (do_teleport); format with the character's name.
ARRIVES_MARKER_FORMAT = "{} arrives from a puff of smoke."
# do_teleport's refusals, in the order it checks: no argument, no character by the name, the
# speaker itself, and no destination.
NO_ARGUMENT_MARKER = "Who do you wish to teleport?"
NO_SUCH_CHARACTER_MARKER = "No-one by that name around."
SELF_MARKER = "Use 'goto' to teleport yourself."
NO_DESTINATION_MARKER = "Where do you wish to send this person?"
# A name no character in the test has.
UNKNOWN_NAME = "nobodyhere"
# What do_shutdown (act_wiz.cpp) answers an argument that is none of its options: it shuts
# nothing down.
UNKNOWN_OPTION_MARKER = "Unknown shutdown option."
# An argument do_shutdown does not know. Sent instead of a bare `shutdown`, which shuts the
# server down if the level gate ever lets it through.
HARMLESS_SHUTDOWN_ARGUMENT = "foo"
# Seconds to keep reading after a reply, for text that might still arrive.
SETTLE_SECONDS = 1.0


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOD, LESSER_GOD, MORTAL)
def test_teleport_needs_level_93(server) -> None:
    """Pins #268: a level-92 immortal does not have teleport, and a level-93 one teleports a mortal.

    command_interpreter (interpre.cpp) drops a command whose table row is above the speaker's
    level, so the level-92 immortal reads "Unrecognized command." and the mortal stays put; the
    level-93 immortal sends the same command, the mortal arrives in Arena East, and the
    implementor in the arena centre reads it vanish. The level-93 immortal then teleports it back,
    and the implementor reads it arrive. The implementor's lines are sent in the pass that sends
    the mortal its own, so they are read after it. No draw is on any path, and the affect-hours
    schedule keeps Arena East's target orc still.
    """
    implementor = server.login(IMPLEMENTOR.name)
    lesser_god = server.login(LESSER_GOD.name)
    god = server.login(GOD.name)
    mortal = server.login(MORTAL.name)
    assert implementor.command(f"goto {world.ROOM_ARENA_CENTRE}").room_name() == "Arena Centre"
    mortal.drain(SETTLE_SECONDS)
    teleport_command = f"teleport {MORTAL.name.lower()} {world.ROOM_ARENA_EAST}"

    refused = lesser_god.command(teleport_command)
    assert refused.contains(UNRECOGNIZED_MARKER), refused.text
    assert ANY_TELEPORTED_MARKER not in mortal.drain(SETTLE_SECONDS)
    assert DISAPPEARS_MARKER_FORMAT.format(MORTAL.name) not in implementor.drain(SETTLE_SECONDS)
    assert mortal.command("look").room_name() == "Arena Centre"

    granted = god.command(teleport_command)
    assert not granted.contains(UNRECOGNIZED_MARKER), granted.text
    teleported_marker = TELEPORTED_MARKER_FORMAT.format(GOD.name)
    teleported = mortal.expect([teleported_marker]) + mortal.drain(SETTLE_SECONDS)
    assert "Arena East" in teleported, teleported
    vanished = implementor.drain(SETTLE_SECONDS)
    assert vanished.count(DISAPPEARS_MARKER_FORMAT.format(MORTAL.name)) == 1, vanished

    god.command(f"teleport {MORTAL.name.lower()} {world.ROOM_ARENA_CENTRE}")
    returned = mortal.expect([teleported_marker]) + mortal.drain(SETTLE_SECONDS)
    assert "Arena Centre" in returned, returned
    arrived = implementor.drain(SETTLE_SECONDS)
    assert arrived.count(ARRIVES_MARKER_FORMAT.format(MORTAL.name)) == 1, arrived


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOD, MORTAL)
def test_teleport_refuses_a_missing_target_itself_and_a_missing_destination(server) -> None:
    """Pins do_teleport's refusals: no argument, an unknown name, the speaker itself, no destination.

    do_teleport (act_wiz.cpp) checks them in that order before it moves anyone, so the mortal
    named in the last request stays where it was. No draw is on any path.
    """
    server.login(IMPLEMENTOR.name)
    god = server.login(GOD.name)
    mortal = server.login(MORTAL.name)
    mortal.drain(SETTLE_SECONDS)

    requests = [
        ("teleport", NO_ARGUMENT_MARKER),
        (f"teleport {UNKNOWN_NAME} {world.ROOM_ARENA_EAST}", NO_SUCH_CHARACTER_MARKER),
        (f"teleport {GOD.name.lower()} {world.ROOM_ARENA_EAST}", SELF_MARKER),
        (f"teleport {MORTAL.name.lower()}", NO_DESTINATION_MARKER),
    ]
    for request, refusal in requests:
        reply = god.command(request)
        assert reply.contains(refusal), f"{request}:\n{reply.text}"
    assert god.command("look").room_name() == "Immortal Start"
    assert ANY_TELEPORTED_MARKER not in mortal.drain(SETTLE_SECONDS)
    assert mortal.command("look").room_name() == "Arena Centre"


@pytest.mark.characters(IMPLEMENTOR, AREA_GOD, SHUTDOWN_GOD)
def test_shutdown_is_refused_at_level_95_and_reached_at_96(server) -> None:
    """Pins #358 and #359: a level-95 immortal does not have shutdown; level 96 and the implementor do.

    Shutdown's row is LEVEL_GRGOD - 1 (96) in the repository (#359 left the level-93 row to a
    test-port deploy edit), so the level-95 immortal reads "Unrecognized command." and the
    server keeps answering. The level-96 immortal's and the implementor's `shutdown foo` reach
    do_shutdown, which answers an unknown option without shutting anything down. None of the
    commands is a real shutdown: the server exiting would fail the test.
    """
    implementor = server.login(IMPLEMENTOR.name)
    area_god = server.login(AREA_GOD.name)
    shutdown_god = server.login(SHUTDOWN_GOD.name)
    shutdown_command = f"shutdown {HARMLESS_SHUTDOWN_ARGUMENT}"

    refused = area_god.command(shutdown_command)
    assert refused.contains(UNRECOGNIZED_MARKER), refused.text
    assert UNKNOWN_OPTION_MARKER not in refused.text, refused.text
    assert area_god.command("look").room_name() == "Immortal Start"

    for session in (shutdown_god, implementor):
        answered = session.command(shutdown_command)
        assert answered.contains(UNKNOWN_OPTION_MARKER), answered.text
        assert not answered.contains(UNRECOGNIZED_MARKER), answered.text
        assert session.command("look").room_name() == "Immortal Start"
