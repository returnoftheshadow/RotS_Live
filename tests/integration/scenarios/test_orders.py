"""Orders to followers: what the leader and the room read as each charmed follower carries out
`order followers`, and orders that remove a follower, free one from following or remove the
leader itself."""

from __future__ import annotations

from dataclasses import dataclass

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import FORCED_REPLY
from rots_harness.immortal_commands import goto, load_mob
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import NO_SUCH_MOB_REPLY, read_stat
from rots_harness.text_lines import assert_in_order, capitalised

pytestmark = [pytest.mark.scenario, pytest.mark.orders]


@dataclass(frozen=True)
class HallMob:
    """A zone 22 mob (tests/integration/world/mob/22.mob)."""

    # The mob's virtual number, which `load mob` takes.
    vnum: int
    # The keyword that finds this mob and no other in the world.
    keyword: str
    # The mob's short description, as `load mob` and `stat` print it.
    short_description: str

    @property
    def shown(self) -> str:
        """The short description as act() starts a line with it, capitalised."""
        return capitalised(self.short_description)


# The three lackeys: charmed from the start (AFF_CHARM in their mob file), so whoever they follow
# can order them, and without scripts. The yew lackey alone is level 7, the level the warden
# removes; the others are level 5.
ASH = HallMob(vnum=2202, keyword="ash", short_description="an ash lackey")
ELM = HallMob(vnum=2203, keyword="elm", short_description="an elm lackey")
YEW = HallMob(vnum=2204, keyword="yew", short_description="a yew lackey")
# The order warden: his script (2200) removes from the game any level-7 speaker he hears, so of
# the lackeys only the yew lackey, and he says nothing himself.
WARDEN = HallMob(vnum=2200, keyword="warden", short_description="an order warden")
# The lackey captain: his script (2201) removes himself from the game when he hears speech
# containing "farewell".
CAPTAIN = HallMob(vnum=2201, keyword="captain", short_description="a lackey captain")

# The leader: a level-10 human warrior in the Order Hall, male, on the implementor's side so its
# name matches across them. Not level 7, so the warden never removes it.
LEADER = Character(name="Testleader", race="human", sex="male", level=10, load_room=world.ROOM_ORDER_HALL, class_name="warrior")

# How look names the Order Hall.
ORDER_HALL_NAME = "Order Hall"

# What the room reads as the leader orders its followers (do_order, act_offe.cpp); format with the
# leader as the room sees it.
ISSUES_AN_ORDER_FORMAT = "{} issues an order."
# What the leader reads once at least one follower was ordered (do_order).
ORDER_DONE_MARKER = "Ok."
# What the leader reads when no follower in the room is charmed (do_order).
NOBODY_WILLING_MARKER = "Nobody here is willing to carry out your orders!"
# What a listener reads as a mob speaks (say_to_char, act_comm.cpp); format with the speaker as
# the listener sees it and the words.
SAYS_FORMAT = "{} says '{}'"
# What the leader reads as a charmed follower stops following it (stop_follower, handler.cpp);
# format with the follower as the leader sees it.
HATES_YOUR_GUTS_FORMAT = "{} hates your guts!"
# What the rest of the room reads then; format with the follower and the leader.
REALIZES_A_JERK_FORMAT = "{} realizes that {} is a jerk!"
# What the room reads as a mob starts following someone (add_follower, handler.cpp); format with
# the follower and the leader.
NOW_FOLLOWS_FORMAT = "{} now follows {}."
# What the room reads as a mob sits down (do_sit, act_move.cpp); format with the mob.
SITS_DOWN_FORMAT = "{} sits down."
# The `stat mob` line naming a mob's master (do_stat_character, act_wiz.cpp); format with the
# master's name, or "<none>".
MASTER_IS_FORMAT = "Master is: {},"
# The words the followers are ordered to say. "hello" sets off no script but the warden's.
HELLO = "hello"
# The words that set off the lackey captain's script.
FAREWELL = "farewell"
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def force(implementor: GameSession, mob: HallMob, command: str, marker: str) -> str:
    """Forces mob to run command and returns what the implementor read up to marker and after.

    do_force (act_wiz.cpp) answers "Ok." and then runs the command at once, so what the mob's
    command shows the implementor's room arrives with it.
    """
    text = reply_to(implementor, f"force {mob.keyword} {command}", [marker])
    assert FORCED_REPLY in text, text
    return text


def follow(implementor: GameSession, follower: HallMob, leader_keyword: str, leader_shown: str) -> None:
    """Forces follower to follow the leader the keyword names, and asserts the room saw it."""
    marker = NOW_FOLLOWS_FORMAT.format(follower.shown, leader_shown)
    text = force(implementor, follower, f"follow {leader_keyword}", marker)
    assert text.count(marker) == 1, text


def gather_followers(implementor: GameSession, leader: GameSession, followers: list) -> None:
    """Brings the implementor to the Order Hall, loads followers and has each follow the leader.

    add_follower (handler.cpp) puts each new follower at the head of its leader's list and
    do_order walks that list from the head, so the followers are made to follow in reverse:
    the order then reaches them in the order given here.
    """
    goto(implementor, world.ROOM_ORDER_HALL, ORDER_HALL_NAME)
    for follower in followers:
        load_mob(implementor, follower.vnum, follower.short_description)

    for follower in reversed(followers):
        follow(implementor, follower, LEADER.name.lower(), LEADER.name)
    leader.drain(SETTLE_SECONDS)


def order_followers(leader: GameSession, command: str) -> str:
    """Has the leader order its followers to run command; returns what the leader read.

    do_order sends "Ok." or the refusal after every follower's command has run, so the whole
    order is read once either arrives.
    """
    return reply_to(leader, f"order followers {command}", [ORDER_DONE_MARKER, NOBODY_WILLING_MARKER])


def stat_mob(implementor: GameSession, mob: HallMob) -> str:
    """The implementor's whole `stat mob` reply for mob, or the line saying no mob matched."""
    return read_stat(implementor, f"mob {mob.keyword}", other_outcomes=[NO_SUCH_MOB_REPLY])


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEADER)
def test_ordered_followers_each_carry_out_the_order_in_list_order(server) -> None:
    """Pins `order followers` running the command for each charmed follower in turn.

    do_order (act_offe.cpp) tells the room the leader issues an order, then runs the command
    for each charmed follower in the leader's room, from the head of the leader's follower list,
    then tells the leader "Ok.". Each follower's say reaches the leader and the implementor in
    that order, and a second order to sit leaves each of them sitting. Nothing on the path
    draws: the lackeys speak language 0, and no script or special is in the room.
    """
    implementor = server.login(IMPLEMENTOR.name)
    leader = server.login(LEADER.name)
    followers = [ASH, YEW, ELM]
    gather_followers(implementor, leader, followers)

    said = order_followers(leader, f"say {HELLO}")
    spoken = [SAYS_FORMAT.format(follower.shown, HELLO) for follower in followers]
    assert_in_order(said, [*spoken, ORDER_DONE_MARKER])
    seen = implementor.expect([spoken[-1]]) + implementor.drain(SETTLE_SECONDS)
    assert_in_order(seen, [ISSUES_AN_ORDER_FORMAT.format(LEADER.name), *spoken])

    sat = order_followers(leader, "sit")
    sitting = [SITS_DOWN_FORMAT.format(follower.shown) for follower in followers]
    assert_in_order(sat, [*sitting, ORDER_DONE_MARKER])
    implementor.drain(SETTLE_SECONDS)
    for follower in followers:
        stat = stat_mob(implementor, follower)
        assert MASTER_IS_FORMAT.format(LEADER.name) in stat, stat
        assert "Pos: Sitting" in stat, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEADER)
def test_an_order_goes_on_past_a_follower_it_removes(server) -> None:
    """New-behaviour pin: a follower the order removes is gone, and the next follower still obeys.

    The yew lackey, second of three, says hello; the warden's script removes it from the game,
    and extract_char (handler.cpp) stops it following, which frees its place on the leader's
    list. Since #562 do_order (act_offe.cpp) goes on from the next place still on the list, so
    the elm lackey says hello too; before it, the loop read the freed place's next link and
    stopped there. Nothing on the path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    leader = server.login(LEADER.name)
    followers = [ASH, YEW, ELM]
    gather_followers(implementor, leader, followers)
    load_mob(implementor, WARDEN.vnum, WARDEN.short_description)
    implementor.drain(SETTLE_SECONDS)

    said = order_followers(leader, f"say {HELLO}")
    elm_says_hello = SAYS_FORMAT.format(ELM.shown, HELLO)
    expected = [
        SAYS_FORMAT.format(ASH.shown, HELLO),
        SAYS_FORMAT.format(YEW.shown, HELLO),
        HATES_YOUR_GUTS_FORMAT.format(YEW.shown),
        elm_says_hello,
        ORDER_DONE_MARKER,
    ]
    assert_in_order(said, expected)
    seen = implementor.expect([elm_says_hello]) + implementor.drain(SETTLE_SECONDS)
    assert REALIZES_A_JERK_FORMAT.format(YEW.shown, LEADER.name) in seen, seen

    assert NO_SUCH_MOB_REPLY in stat_mob(implementor, YEW)
    for follower in (ASH, ELM):
        stat = stat_mob(implementor, follower)
        assert MASTER_IS_FORMAT.format(LEADER.name) in stat, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEADER)
def test_an_order_goes_on_past_a_follower_it_frees_from_following(server) -> None:
    """New-behaviour pin: an order to follow themselves frees every follower, not just the first.

    do_follow (act_move.cpp) lets a charmed follower without the pet or orc-friend flags follow
    itself, and stop_follower (handler.cpp) then frees its place on the leader's list and its
    charm. Since #562 do_order (act_offe.cpp) goes on from the next place still on the list, so
    all three stop following; before it, the loop read the freed place's next link and stopped
    after the first. Nothing on the path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    leader = server.login(LEADER.name)
    followers = [ASH, YEW, ELM]
    gather_followers(implementor, leader, followers)

    freed = order_followers(leader, "follow self")
    assert_in_order(freed, [*(HATES_YOUR_GUTS_FORMAT.format(follower.shown) for follower in followers), ORDER_DONE_MARKER])
    implementor.drain(SETTLE_SECONDS)
    for follower in followers:
        stat = stat_mob(implementor, follower)
        assert MASTER_IS_FORMAT.format("<none>") in stat, stat


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
def test_an_order_that_removes_its_leader_ends_there(server) -> None:
    """New-behaviour pin: an order that removes the leader ends with the follower that removed it.

    The lackey captain orders the ash and elm lackeys to say farewell; as the ash lackey says
    it, the captain's own script removes the captain from the game, and extract_char
    (handler.cpp) frees every place on its follower list, so neither lackey follows anyone. Since
    #562 do_order (act_offe.cpp) returns at once; before it, the loop went on and its "Ok." was
    sent to the freed captain, which an AddressSanitizer build reports as a heap-use-after-free.
    The elm lackey never says farewell either way. Nothing on the path draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    goto(implementor, world.ROOM_ORDER_HALL, ORDER_HALL_NAME)
    for mob in (CAPTAIN, ASH, ELM):
        load_mob(implementor, mob.vnum, mob.short_description)

    for follower in (ELM, ASH):
        follow(implementor, follower, CAPTAIN.keyword, CAPTAIN.short_description)

    ash_says = SAYS_FORMAT.format(ASH.shown, FAREWELL)
    ordered = force(implementor, CAPTAIN, f"order followers say {FAREWELL}", ash_says)
    expected = [
        ISSUES_AN_ORDER_FORMAT.format(CAPTAIN.shown),
        ash_says,
        REALIZES_A_JERK_FORMAT.format(ASH.shown, CAPTAIN.short_description),
        REALIZES_A_JERK_FORMAT.format(ELM.shown, CAPTAIN.short_description),
    ]
    assert_in_order(ordered, expected)
    assert SAYS_FORMAT.format(ELM.shown, FAREWELL) not in ordered, ordered

    assert NO_SUCH_MOB_REPLY in stat_mob(implementor, CAPTAIN)
    for follower in (ASH, ELM):
        stat = stat_mob(implementor, follower)
        assert MASTER_IS_FORMAT.format("<none>") in stat, stat
