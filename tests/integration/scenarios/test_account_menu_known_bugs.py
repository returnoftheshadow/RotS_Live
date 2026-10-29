"""Known account-menu and account-tool bugs, each pinned as a strict xfail of the correct behaviour,
so the pin turns into a failure (XPASS) the day the bug is fixed."""

from __future__ import annotations

import contextlib
from typing import Iterator

import pytest

from rots_harness import world
from rots_harness.characters import HARNESS_EMAIL, HARNESS_PASSWORD, IMPLEMENTOR, Character
from rots_harness.launcher import LOGIN_EMAIL_PROMPT
from rots_harness.session import ACCOUNT_MENU_PROMPT, LOGIN_PASSWORD_PROMPT, GameSession

pytestmark = [pytest.mark.scenario, pytest.mark.account_menu]

# The account menu's create-a-new-character choice (show_account_menu, interpre.cpp).
ACCOUNT_MENU_NEW_CHARACTER = "4"
# The prompt for a new character's name (CON_ACCTMENU and CON_ACCTNEWCHAR in nanny, interpre.cpp).
NEW_CHARACTER_NAME_PROMPT = "New character name:"
# The end of the question a free name gets; format the full question with the name (CON_ACCTNEWCHAR).
ROLEPLAY_QUESTION_END = "a suitable name for roleplay in Middle-earth (Y/N)?"
ROLEPLAY_QUESTION_FORMAT = "Is {} a suitable name for roleplay in Middle-earth (Y/N)?"
# A word fill_word refuses as a name (fill[], interpre.cpp); it passes every check before the
# confirmation.
FILL_WORD_NAME = "The"
# What CON_NMECNF prints for a fill word or an invalid name (nanny, interpre.cpp).
ILLEGAL_NAME_REPLY = "Illegal name, please try another:"
# What CON_NMECNF prints when it accepts the name (nanny, interpre.cpp).
NEW_CHARACTER_ACCEPTED = "New character."
# A free name for a new character: letters only, unused in every test world.
FREE_NEW_NAME = "Thorin"

# A level-20 human warrior whose name is also a keyword of mob 1132, the bystander orc; it plays
# in Arena West, away from the implementor.
BYSTANDER = Character(name="Bystander", race="human", sex="male", level=20, load_room=world.ROOM_ARENA_WEST, class_name="warrior")
# The bystander orc (tests/integration/world/mob/11.mob), keywords `bystander orc`.
BYSTANDER_ORC_VNUM = 1132
# What `load mob 1132` prints (do_load, act_wiz.cpp), and the orc's line in a room.
BYSTANDER_ORC_CREATED = "You create a bystander orc."
BYSTANDER_ORC_IN_ROOM = "A bystander orc loiters here, involved in nothing."
# The password do_delete requires after the character's name (act_wiz.cpp).
DELETE_PASSWORD = "rots"
# Seconds the server is given to close the deleted player's connection.
CLOSE_SECONDS = 5.0


@contextlib.contextmanager
def account_connection(server, label: str) -> Iterator[GameSession]:
    """A connection at the account email prompt, closed when the block ends.

    It plays no character, so the server fixture does not quit it; label names its transcript.
    """
    connection = GameSession(server.handle, label, HARNESS_EMAIL, HARNESS_PASSWORD, server.run_dir)
    try:
        connection.expect([LOGIN_EMAIL_PROMPT])
        yield connection
    finally:
        connection.close()


@contextlib.contextmanager
def playing_connection(server, character: Character) -> Iterator[GameSession]:
    """A connection playing character, closed when the block ends; the server fixture does not quit it."""
    connection = GameSession(server.handle, character.name, HARNESS_EMAIL, HARNESS_PASSWORD, server.run_dir)
    try:
        connection.login()
        yield connection
    finally:
        connection.close()


def new_name_answer(connection: GameSession, name: str) -> str:
    """Sends name at the new character name prompt and returns the reply up to the next prompt."""
    connection.send_line(name)
    return connection.expect([NEW_CHARACTER_NAME_PROMPT, ROLEPLAY_QUESTION_END, ACCOUNT_MENU_PROMPT, LOGIN_EMAIL_PROMPT])


def closes_within(connection: GameSession, seconds: float) -> bool:
    """Whether the server closes the connection within seconds; text read meanwhile is discarded."""
    try:
        connection.drain(seconds)
    except ConnectionError:
        return True
    return False


@pytest.mark.account_menu_characters
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="after `Illegal name, please try another:` at the new character confirmation, the next name should be offered for confirmation; today it is read as an account email address",
)
@pytest.mark.characters(IMPLEMENTOR)
def test_a_name_refused_at_the_new_character_confirmation_is_followed_by_another_name(server) -> None:
    """Bug: the confirmation of a new character from the account menu (CON_NMECNF) answers a fill
    word or an invalid name with `Illegal name, please try another: ` but sets CON_NME, the account
    email prompt (interpre.cpp:3150-3156), unlike its `n` answer, which returns an account player
    to the account menu (interpre.cpp:3174-3183). The next name is parsed as an email address and
    refused with the email error and `Account email:`. Correct: the name prompt takes it and offers
    it for confirmation like any free name (CON_ACCTNEWCHAR, interpre.cpp:4083-4085)."""
    with account_connection(server, "fillword") as connection:
        connection.send_line(HARNESS_EMAIL)
        connection.expect([LOGIN_PASSWORD_PROMPT])
        connection.send_line(HARNESS_PASSWORD)
        connection.expect([ACCOUNT_MENU_PROMPT])
        connection.send_line(ACCOUNT_MENU_NEW_CHARACTER)
        connection.expect([NEW_CHARACTER_NAME_PROMPT])
        question = new_name_answer(connection, FILL_WORD_NAME)
        assert ROLEPLAY_QUESTION_FORMAT.format(FILL_WORD_NAME) in question, question
        connection.send_line("y")
        refused = connection.expect([ILLEGAL_NAME_REPLY, NEW_CHARACTER_ACCEPTED])
        assert ILLEGAL_NAME_REPLY in refused and NEW_CHARACTER_ACCEPTED not in refused, refused

        retried = new_name_answer(connection, FREE_NEW_NAME)
        assert ROLEPLAY_QUESTION_FORMAT.format(FREE_NEW_NAME) in retried, retried


@pytest.mark.account_menu_store
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="`delete <player> rots` should delete and disconnect that player; it extracts a mob answering to the name in the implementor's room instead and leaves the player connected",
)
@pytest.mark.characters(IMPLEMENTOR, BYSTANDER)
def test_delete_disconnects_the_named_player_and_leaves_a_mob_of_the_same_keyword(server) -> None:
    """Bug: do_delete finds the character to disconnect with get_char_vis (act_wiz.cpp:3199-3208),
    which looks in the implementor's room first and matches mob keywords, where `wizset player`
    uses get_player_vis. With the bystander orc in the implementor's room, `delete bystander rots`
    extracts the orc, leaves the player Bystander connected, and still deletes Bystander's records;
    its next save prints `Error: you are not being saved.  Please contact an immortal.`
    (save_char, db.cpp:3487-3496). Correct: the player is disconnected and the orc stays."""
    implementor = server.login(IMPLEMENTOR.name)
    with playing_connection(server, BYSTANDER) as bystander:
        loaded = implementor.command(f"load mob {BYSTANDER_ORC_VNUM}")
        assert BYSTANDER_ORC_CREATED in loaded.text, loaded.text
        before = implementor.command("look")
        assert BYSTANDER_ORC_IN_ROOM in before.text, before.text

        implementor.command(f"delete {BYSTANDER.name.lower()} {DELETE_PASSWORD}")

        after = implementor.command("look")
        assert BYSTANDER_ORC_IN_ROOM in after.text, after.text
        assert closes_within(bystander, CLOSE_SECONDS), "the deleted player's connection stayed open"
