"""A herald announces each player who comes into his hall once, by name and title, from the text
his builder wrote; a % in that text other than %s and %% shows as written."""

from __future__ import annotations

import pytest

from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.herald]

# The rooms (tests/integration/world/wld/24.wld): the antechamber the guest starts in, the proud
# herald's hall north of it and the odd herald's hall south of it.
ROOM_ANTECHAMBER = 2405
ROOM_HERALDS_HALL = 2404
ROOM_ODD_HERALDS_HALL = 2406
HERALDS_HALL_NAME = "Herald's Hall"
ANTECHAMBER_NAME = "Antechamber"
# The heralds (mob/24.mob): MOB_SPEC and program 28, the herald special (spec_pro.cpp), each with
# his announcement as his second death cry. The proud herald's is "Make way for %s, %s!"; the odd
# herald's is "Hail, %s %s! Tolls are 5%% today and 100%d tomorrow.".
PROUD_HERALD_VNUM = 2402
ODD_HERALD_VNUM = 2403

# A level-20 human warrior who sets a title and walks in. Level 20 is the least that may set a
# title (do_title, act_othe.cpp).
GUEST = Character(
    name="Testguest", race="human", sex="male", level=20, load_room=ROOM_ANTECHAMBER, class_name="warrior"
)
# The guest's title and what setting it prints (do_title).
TITLE = "the Wanderer"
TITLE_SET = f"OK, you're now {GUEST.name} {TITLE}."

# What the guest reads from each herald (do_say and say_to_char, act_comm.cpp): the first %s is the
# name and the second the title, "%%" is one '%', and "%d" stays as written.
PROUD_ANNOUNCEMENT = f"A proud herald says 'Make way for {GUEST.name}, {TITLE}!'"
ODD_ANNOUNCEMENT = f"An odd herald says 'Hail, {GUEST.name} {TITLE}! Tolls are 5% today and 100%d tomorrow.'"
# The start every announcement of each herald shares, whatever the rest reads.
PROUD_SAYS = "A proud herald says '"
ODD_SAYS = "An odd herald says '"

# Seconds to wait for an announcement, and to keep reading after one.
ANNOUNCE_TIMEOUT = 8.0
SETTLE_SECONDS = 1.0


def stripped_lines(text: str) -> list:
    """The text's lines, stripped, blank ones dropped."""
    return [line.strip() for line in text.splitlines() if line.strip()]


def set_title(guest: GameSession) -> None:
    """Gives the guest the title the heralds announce."""
    titled = guest.command(f"set title {TITLE}").text
    assert TITLE_SET in stripped_lines(titled), titled


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(PROUD_HERALD_VNUM, ROOM_HERALDS_HALL))
@pytest.mark.characters(IMPLEMENTOR, GUEST)
def test_a_herald_announces_a_newcomer_by_name_and_title_once(server) -> None:
    """The guest walks into the herald's hall and reads the herald announcing him by name and title.
    A command in the hall, and leaving and coming back, bring no second announcement: the herald
    remembers whom he has announced. Nothing draws."""
    guest = server.login(GUEST.name)
    set_title(guest)

    guest.send_line("north")
    arrived = guest.expect([PROUD_SAYS], timeout=ANNOUNCE_TIMEOUT) + guest.drain(SETTLE_SECONDS)
    assert HERALDS_HALL_NAME in arrived, arrived
    assert PROUD_ANNOUNCEMENT in stripped_lines(arrived), arrived

    looked = guest.command("look").text
    left = guest.command("south").text
    came_back = guest.command("north").text
    later = looked + left + came_back + guest.drain(SETTLE_SECONDS)
    assert PROUD_SAYS not in later, later
    assert ANTECHAMBER_NAME in left, left
    assert HERALDS_HALL_NAME in came_back, came_back


@pytest.mark.schedule("affect-hours")
@pytest.mark.world_resets(MobReset(ODD_HERALD_VNUM, ROOM_ODD_HERALDS_HALL))
@pytest.mark.characters(IMPLEMENTOR, GUEST)
def test_a_herald_shows_a_percent_d_in_his_text_as_written(server) -> None:
    """Fix pin (#549): a herald whose text holds "%d" announces it as written, beside the name, the
    title and a "%%" read as one '%'. #549 made "%d" stay as written; before it, the text was a
    printf format and "%d" printed a number read past the name and title. Nothing draws."""
    guest = server.login(GUEST.name)
    set_title(guest)

    guest.send_line("south")
    arrived = guest.expect([ODD_SAYS], timeout=ANNOUNCE_TIMEOUT) + guest.drain(SETTLE_SECONDS)
    assert ODD_ANNOUNCEMENT in stripped_lines(arrived), arrived
