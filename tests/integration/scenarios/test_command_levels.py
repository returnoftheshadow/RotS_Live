"""Immortal command levels: teleport needs level 93 and shutdown needs level 96.

Teleport also refuses malformed requests before it moves anyone.
"""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character, immortal
from rots_harness.game_text import (
    NO_ONE_AROUND_REPLY,
    TELEPORTED_END,
    TELEPORTED_FORMAT,
    UNRECOGNIZED_COMMAND_REPLY,
)
from rots_harness.immortal_commands import goto
from rots_harness.replies import SETTLE_SECONDS, settled_expect

pytestmark = [pytest.mark.scenario, pytest.mark.wizlevels]


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

# What the room the character leaves reads (do_teleport); format with the character's name.
DISAPPEARS_FORMAT = "{} disappears in a puff of smoke."
# What the room the character arrives in reads (do_teleport); format with the character's name.
ARRIVES_FORMAT = "{} arrives from a puff of smoke."
# do_teleport's first refusal, to no argument; it then refuses no character by the name
# (NO_ONE_AROUND_REPLY), the speaker itself and no destination, in that order.
NO_ARGUMENT_REPLY = "Who do you wish to teleport?"
# do_teleport's refusal when the speaker names itself.
SELF_REPLY = "Use 'goto' to teleport yourself."
# do_teleport's refusal when no destination follows the name.
NO_DESTINATION_REPLY = "Where do you wish to send this person?"
# A name no character in the test has.
UNKNOWN_NAME = "nobodyhere"
# What do_shutdown (act_wiz.cpp) answers an argument that is none of its options: it shuts
# nothing down.
UNKNOWN_OPTION_REPLY = "Unknown shutdown option."
# An argument do_shutdown does not know. Sent instead of a bare `shutdown`, which shuts the
# server down if the level gate ever lets it through.
HARMLESS_SHUTDOWN_ARGUMENT = "foo"


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOD, LESSER_GOD, MORTAL)
def test_teleport_needs_level_93(server) -> None:
    """Pins teleport's level: a level-92 immortal does not have it, and a level-93 one teleports.

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
    goto(implementor, world.ROOM_ARENA_CENTRE, world.ROOM_ARENA_CENTRE_NAME)
    mortal.drain(SETTLE_SECONDS)
    teleport_command = f"teleport {MORTAL.name.lower()} {world.ROOM_ARENA_EAST}"

    refused = lesser_god.command(teleport_command)
    assert refused.contains(UNRECOGNIZED_COMMAND_REPLY), refused.text
    mortal_read = mortal.drain(SETTLE_SECONDS)
    assert TELEPORTED_END not in mortal_read, mortal_read
    implementor_read = implementor.drain(SETTLE_SECONDS)
    assert DISAPPEARS_FORMAT.format(MORTAL.name) not in implementor_read, implementor_read
    looked = mortal.command("look")
    assert looked.room_name() == world.ROOM_ARENA_CENTRE_NAME, looked.text

    granted = god.command(teleport_command)
    assert not granted.contains(UNRECOGNIZED_COMMAND_REPLY), granted.text
    teleported_marker = TELEPORTED_FORMAT.format(GOD.name)
    teleported = settled_expect(mortal, [teleported_marker])
    assert world.ROOM_ARENA_EAST_NAME in teleported, teleported
    vanished = implementor.drain(SETTLE_SECONDS)
    assert vanished.count(DISAPPEARS_FORMAT.format(MORTAL.name)) == 1, vanished

    god.command(f"teleport {MORTAL.name.lower()} {world.ROOM_ARENA_CENTRE}")
    returned = settled_expect(mortal, [teleported_marker])
    assert world.ROOM_ARENA_CENTRE_NAME in returned, returned
    arrived = implementor.drain(SETTLE_SECONDS)
    assert arrived.count(ARRIVES_FORMAT.format(MORTAL.name)) == 1, arrived


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOD, MORTAL)
def test_teleport_refuses_a_missing_target_itself_and_a_missing_destination(server) -> None:
    """Pins do_teleport's refusals: no argument, an unknown name, the speaker, no destination.

    do_teleport (act_wiz.cpp) checks them in that order before it moves anyone, so the mortal
    named in the last request stays where it was. No draw is on any path.
    """
    server.login(IMPLEMENTOR.name)
    god = server.login(GOD.name)
    mortal = server.login(MORTAL.name)
    mortal.drain(SETTLE_SECONDS)

    requests = [
        ("teleport", NO_ARGUMENT_REPLY),
        (f"teleport {UNKNOWN_NAME} {world.ROOM_ARENA_EAST}", NO_ONE_AROUND_REPLY),
        (f"teleport {GOD.name.lower()} {world.ROOM_ARENA_EAST}", SELF_REPLY),
        (f"teleport {MORTAL.name.lower()}", NO_DESTINATION_REPLY),
    ]
    for request, refusal in requests:
        reply = god.command(request)
        assert reply.contains(refusal), f"{request}:\n{reply.text}"

    god_looked = god.command("look")
    assert god_looked.room_name() == world.ROOM_IMMORTAL_START_NAME, god_looked.text
    mortal_read = mortal.drain(SETTLE_SECONDS)
    assert TELEPORTED_END not in mortal_read, mortal_read
    mortal_looked = mortal.command("look")
    assert mortal_looked.room_name() == world.ROOM_ARENA_CENTRE_NAME, mortal_looked.text


@pytest.mark.characters(IMPLEMENTOR, AREA_GOD, SHUTDOWN_GOD)
def test_shutdown_is_refused_at_level_95_and_reached_at_96(server) -> None:
    """Pins shutdown's level: a level-95 immortal does not have it; level 96 and the implementor do.

    Shutdown's row is LEVEL_GRGOD - 1 (96), so the level-95 immortal reads "Unrecognized command."
    and the server keeps answering. The level-96 immortal's and the implementor's `shutdown foo`
    reach do_shutdown, which answers an unknown option without shutting anything down. None of the
    commands is a real shutdown: the server exiting would fail the test.
    """
    implementor = server.login(IMPLEMENTOR.name)
    area_god = server.login(AREA_GOD.name)
    shutdown_god = server.login(SHUTDOWN_GOD.name)
    shutdown_command = f"shutdown {HARMLESS_SHUTDOWN_ARGUMENT}"

    refused = area_god.command(shutdown_command)
    assert refused.contains(UNRECOGNIZED_COMMAND_REPLY), refused.text
    assert UNKNOWN_OPTION_REPLY not in refused.text, refused.text
    looked = area_god.command("look")
    assert looked.room_name() == world.ROOM_IMMORTAL_START_NAME, looked.text

    for session in (shutdown_god, implementor):
        answered = session.command(shutdown_command)
        assert answered.contains(UNKNOWN_OPTION_REPLY), answered.text
        assert not answered.contains(UNRECOGNIZED_COMMAND_REPLY), answered.text
        looked = session.command("look")
        assert looked.room_name() == world.ROOM_IMMORTAL_START_NAME, looked.text
