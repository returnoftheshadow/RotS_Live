"""Known bugs in following, grouping, reporting, riding and teleporting, each pinned as a strict
xfail of the correct behaviour: the pin fails the run once the bug is fixed, so it can become a
plain test."""

from __future__ import annotations

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.session import GameSession
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario]

# Room 1137, a DEATH room in zone 11 with no exits: whoever walks in dies (do_move, act_move.cpp).
ROOM_GALLOWS_PIT = 1137


def human_warrior(name: str, sex: str, load_room: int) -> Character:
    """A level-10 human warrior: a mortal on the implementor's side, so names match across them."""
    return Character(name=name, race="human", sex=sex, level=10, load_room=load_room, class_name="warrior")


# The leader of the follower pins: the implementor, male, so a follower's lines name him "him".
LEADER = IMPLEMENTOR
# A follower in the arena centre; female, so the leader's line reads "her".
FOLLOWER = human_warrior("Testtrailer", "female", world.ROOM_ARENA_CENTRE)
# A mortal on the ledge who walks into the Gallows Pit carrying a bag with a cap in it.
CARRIER = human_warrior("Testcarrier", "female", world.ROOM_GALLOWS_APPROACH)
# A mortal leader in the arena centre.
CAPTAIN = human_warrior("Testcaptain", "male", world.ROOM_ARENA_CENTRE)
# A mortal the captain groups.
MEMBER = human_warrior("Testmember", "female", world.ROOM_ARENA_CENTRE)
# A mortal in the captain's room who is in no group.
OUTSIDER = human_warrior("Testoutsider", "female", world.ROOM_ARENA_CENTRE)
# A female mortal who follows the leader riding the male harness pony.
RIDER = human_warrior("Testrider", "female", world.ROOM_ARENA_CENTRE)
# A level-93 immortal (LEVEL_GOD, structs.h), the lowest level with teleport, in Immortal Start.
GOD = Character(
    name="Testgod",
    race="human",
    sex="male",
    level=93,
    load_room=world.ROOM_IMMORTAL_START,
    class_name="warrior",
)

# The bystander orc (mob 1132).
BYSTANDER_VNUM = 1132
# The harness pony (mob 1161): a male MOB_MOUNT with no moves.
PONY_VNUM = 1161
# The leather bag (object 1136), a container.
BAG_VNUM = 1136
# The leather cap (object 1137), head armour.
CAP_VNUM = 1137
# The ring of invisibility (object 1160): worn, it sets AFF_INVISIBLE on the wearer.
RING_VNUM = 1160

# The room the leader walks to, east of the arena centre.
DESTINATION_ROOM_NAME = "Arena East"
# The name of the DEATH room east of the Gallows Approach.
PIT_ROOM_NAME = "Gallows Pit"
# The end of what the follower reads as the leader walks east (do_move, act_move.cpp).
LEADER_LEAVES_MARKER = "leaves east."
# The start of every ACK line, the leader's and the follower's (do_move).
ANY_ACK_MARKER = "ACK!"
# The start of "You follow $N." (do_move), whoever the leader is.
ANY_FOLLOWED_MARKER = "You follow "
# What a riding follower reads when its mount cannot take the step (do_move's riding branch).
FOLLOWER_RIDING_ACK_MARKER = "ACK! You could not follow him riding!"
# What a follower without the moves for the step reads (do_move).
EXHAUSTED_MARKER = "You are too exhausted to move."
# What the follower reads when it starts following (add_follower, handler.cpp); format with the leader.
NOW_FOLLOWING_MARKER_FORMAT = "You now follow {}."
# What a player reads after dying, before it looks at its start room (extract_char, handler.cpp).
NEW_BODY_MARKER = "Your spirit found a new body to wear."
# What `look in corpse` prints before the corpse's contents (do_look, act_info.cpp).
CORPSE_CONTENTS_HEADER = "corpse (here) :"
# What `look in bag` prints before a carried bag's contents.
BAG_CONTENTS_HEADER = "bag (carried) :"
# The bystander orc's report as do_report's format string ("I am %s, my stamina is %s, and I am
# %s.") writes it, at full health with no stamina and full moves (act_othe.cpp).
BYSTANDER_REPORT_AS_WRITTEN = "A bystander orc says 'I am healthy, my stamina is empty, and I am energetic.'"
# What the leader reads when a character joins its group (add_character_to_group, act_othe.cpp).
JOINS_YOUR_GROUP_FORMAT = "{} joins your group."
# What the leader reads when a member leaves (remove_character_from_group); format with the name.
LEAVES_YOUR_GROUP_FORMAT = "{} leaves your group."
# What do_ungroup tells a leader who names someone outside its group (act_othe.cpp).
NOT_IN_YOUR_GROUP_MARKER = "That person is not in your group!"
# The word every group line has in common, for asserting a character read none of them.
GROUP_WORD = "group"
# What a rider reads as it mounts the male pony, with the pony's pronoun (do_ride, ranger.cpp).
MOUNTED_MARKER = "You mount a harness pony and start riding him."
# The start of what a rider reads as it mounts the pony, whatever the pronoun.
MOUNTED_MARKER_START = "You mount a harness pony and start riding "
# What the room reads as the rider mounts the male pony; format with the rider's name.
ROOM_MOUNTED_MARKER_FORMAT = "{} mounts a harness pony and starts riding him."
# The end of what the teleported character reads (do_teleport, act_wiz.cpp).
ANY_TELEPORTED_MARKER = "has teleported you!"
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0


def walk_east(leader: GameSession, follower: GameSession) -> tuple:
    """Walks the leader east; returns what the follower and then the leader read in that pass."""
    leader.send_line("east")
    follower_text = follower.expect([LEADER_LEAVES_MARKER]) + follower.drain(SETTLE_SECONDS)
    leader_text = leader.expect([DESTINATION_ROOM_NAME]) + leader.drain(SETTLE_SECONDS)
    return follower_text, leader_text


def gather_in_arena_centre(leader: GameSession, follower: GameSession) -> None:
    """Brings the leader to the arena centre and has the follower follow it."""
    arrived = leader.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert arrived.contains("Arena Centre"), arrived.text
    following = follower.command(f"follow {LEADER.name.lower()}")
    assert following.contains(NOW_FOLLOWING_MARKER_FORMAT.format(LEADER.name)), following.text
    leader.drain(SETTLE_SECONDS)


def load_object(implementor: GameSession, vnum: int, short_description: str) -> None:
    """Loads an object into the implementor's inventory and asserts the reply names it."""
    created = implementor.command(f"load obj {vnum}")
    assert created.contains(f"You create {short_description}."), created.text


def contents_lines(text: str, header: str) -> list:
    """The lines `look in` lists after header, up to the prompt."""
    after_header = text.split(header, 1)[1]
    return [line.strip() for line in after_header.splitlines()[1:] if line.strip() and not line.rstrip().endswith(">")]


def group_the_member(captain: GameSession, member: GameSession) -> None:
    """Has the captain group the member, and asserts what each reads."""
    grouped = captain.command(f"group {MEMBER.name.lower()}")
    assert grouped.contains(JOINS_YOUR_GROUP_FORMAT.format(MEMBER.name)), grouped.text
    joined = member.expect([f"You join the group of {CAPTAIN.name}."])
    assert f"You join the group of {CAPTAIN.name}." in joined, joined
    member.drain(SETTLE_SECONDS)


def ride_after_the_leader(leader: GameSession, rider: GameSession) -> None:
    """Brings the leader to the arena centre, where the world places the harness pony, and has the
    rider follow the leader and mount the pony."""
    arrived = leader.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert arrived.contains("Arena Centre"), arrived.text
    following = rider.command(f"follow {LEADER.name.lower()}")
    assert following.contains(NOW_FOLLOWING_MARKER_FORMAT.format(LEADER.name)), following.text
    mounted = rider.command("ride pony")
    assert mounted.contains(MOUNTED_MARKER_START), mounted.text
    leader.drain(SETTLE_SECONDS)


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="`ungroup <non-member>` should tell the leader alone 'That person is not in your group!'",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CAPTAIN, MEMBER, OUTSIDER)
def test_ungroup_refuses_a_character_outside_the_group(server) -> None:
    """Bug: do_ungroup (act_othe.cpp:602) tests `group->contains(ch)`, the leader, not the named character.

    The leader is always in its own group, so `ungroup <outsider>` tells the outsider "You have
    been kicked out of Testcaptain's group!" and "You leave the group of Testcaptain.", tells the
    leader "Testoutsider leaves your group.", and shows the member the outsider leaving, though
    the outsider was never in the group. Correct: the refusal, to the leader alone. Nothing draws.
    """
    server.login(IMPLEMENTOR.name)
    captain = server.login(CAPTAIN.name)
    member = server.login(MEMBER.name)
    outsider = server.login(OUTSIDER.name)
    group_the_member(captain, member)
    outsider.drain(SETTLE_SECONDS)

    refused = captain.command(f"ungroup {OUTSIDER.name.lower()}")
    assert refused.contains(NOT_IN_YOUR_GROUP_MARKER), refused.text
    assert LEAVES_YOUR_GROUP_FORMAT.format(OUTSIDER.name) not in refused.text, refused.text
    assert GROUP_WORD not in outsider.drain(SETTLE_SECONDS)
    assert GROUP_WORD not in member.drain(SETTLE_SECONDS)


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="report should read 'and I am', as do_report's format string writes it",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR)
@pytest.mark.world_resets(MobReset(BYSTANDER_VNUM, world.ROOM_ARENA_CENTRE))
def test_report_keeps_the_capital_i_its_format_string_writes(server) -> None:
    """Bug: do_report (act_othe.cpp:646-647) lower-cases every character after the first.

    do_report builds "I am %s, my stamina is %s, and I am %s." and then lower-cases the rest of
    the sentence to lower the capitalised prompt words it filled in, which also lowers the format
    string's own second "I": the room reads "and i am energetic.". Correct: "and I am". The
    bystander orc reports at full health. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    assert implementor.command(f"goto {world.ROOM_ARENA_CENTRE}").room_name() == "Arena Centre"
    report = implementor.command("force bystander report")
    assert report.contains("Ok."), report.text
    assert BYSTANDER_REPORT_AS_WRITTEN in report.text, report.text


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a death-room death should leave a cap inside the bag it was in, as any death but a player kill or poison does",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CARRIER)
def test_a_death_room_death_leaves_a_bagged_cap_in_its_bag(server) -> None:
    """Bug: make_physical_corpse (fight.cpp:753) tests `!IS_NPC(killer)`, which is true for a NULL killer.

    do_move (act_move.cpp:935) kills a character who walks into a DEATH room with raw_kill(ch,
    NULL, 0). raw_kill (fight.cpp:924) treats a NULL killer as no player kill, but
    make_physical_corpse reads it as one and runs move_wearables_to_corpse (fight.cpp:675-682),
    lifting the wearable cap out of the bag to the corpse's top level, as it should only for a
    player kill or poison. Correct: the corpse lists the bag, and the cap is still in it. The
    implementor reads the corpse. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    carrier = server.login(CARRIER.name)
    assert implementor.command(f"goto {world.ROOM_GALLOWS_APPROACH}").room_name() == "Gallows Approach"
    load_object(implementor, BAG_VNUM, "a leather bag")
    load_object(implementor, CAP_VNUM, "a leather cap")
    assert implementor.command("put cap bag").contains("You put a leather cap in a leather bag."), implementor.everything
    given = implementor.command(f"give bag {CARRIER.name.lower()}")
    assert given.contains(f"You give a leather bag to {CARRIER.name}."), given.text

    carrier.send_line("east")
    died = carrier.expect([NEW_BODY_MARKER]) + carrier.drain(SETTLE_SECONDS)
    assert PIT_ROOM_NAME in died, died
    assert implementor.command(f"goto {ROOM_GALLOWS_PIT}").room_name() == PIT_ROOM_NAME

    corpse = implementor.command("look in corpse").text
    corpse_contents = contents_lines(corpse, CORPSE_CONTENTS_HEADER)
    assert "a leather bag" in corpse_contents, corpse
    assert "a leather cap" not in corpse_contents, corpse
    taken = implementor.command("get bag corpse")
    assert taken.contains("You get a leather bag from"), taken.text
    bag = implementor.command("look in bag").text
    assert contents_lines(bag, BAG_CONTENTS_HEADER) == ["a leather cap"], bag


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a leader who cannot see a follower should read no ACK line when the follower's own step fails, as when it never tries",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, FOLLOWER)
def test_a_follower_without_moves_the_leader_cannot_see_leaves_the_leader_no_ack_line(server) -> None:
    """Bug: do_move (act_move.cpp:803) sends the leader's ACK line for a failed step with hide_invisible FALSE.

    When the follower tries the step and check_simple_move fails, the leader who cannot see it
    reads "ACK! someone could not follow, you lost her!". The loop for a follower that never
    tries the step (act_move.cpp:1062) and the riding branch (act_move.cpp:950) both hide it.
    Correct: no ACK line for the leader. The leader is the implementor with holylight off; the
    ring's AFF_INVISIBLE hides the follower. The moves are set before the ring goes on: wizset
    cannot find a player the implementor cannot see. Nothing on the way draws an outcome.
    """
    leader = server.login(LEADER.name)
    follower = server.login(FOLLOWER.name)
    gather_in_arena_centre(leader, follower)
    holylight = leader.command("set holylight off")
    assert holylight.contains("Holylight mode is now off."), holylight.text
    exhausted = leader.command(f"wizset player {FOLLOWER.name.lower()} move 0")
    assert exhausted.contains(f"{FOLLOWER.name}'s move set to 0."), exhausted.text
    load_object(leader, RING_VNUM, "a ring of invisibility")
    given = leader.command(f"give ring {FOLLOWER.name.lower()}")
    assert given.contains(f"You give a ring of invisibility to {FOLLOWER.name}."), given.text
    assert follower.command("wear ring").contains("You slide a ring of invisibility on to your"), follower.everything

    left_behind, leader_text = walk_east(leader, follower)
    assert EXHAUSTED_MARKER in left_behind, left_behind
    assert ANY_ACK_MARKER not in leader_text, leader_text


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a rider whose mount cannot take the step should not be told 'You follow'",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, RIDER)
@pytest.mark.world_resets(MobReset(PONY_VNUM, world.ROOM_ARENA_CENTRE))
def test_a_rider_whose_mount_cannot_move_is_not_told_it_follows(server) -> None:
    """Bug: do_move's riding branch (act_move.cpp:953) sends "You follow $N." before the mount's check.

    The rider's own check_simple_move passes, so it reads "You follow Testimm."; only then does
    the mount's check (act_move.cpp:983-990) fail and send "ACK! You could not follow him
    riding!", and the rider stays. Correct: only the ACK line. Nothing draws.
    """
    leader = server.login(LEADER.name)
    rider = server.login(RIDER.name)
    ride_after_the_leader(leader, rider)

    left_behind, _ = walk_east(leader, rider)
    assert FOLLOWER_RIDING_ACK_MARKER in left_behind, left_behind
    assert ANY_FOLLOWED_MARKER not in left_behind, left_behind


@pytest.mark.followers
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="mounting should name the mount by its own pronoun: 'start riding him' for the male pony",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, RIDER)
@pytest.mark.world_resets(MobReset(PONY_VNUM, world.ROOM_ARENA_CENTRE))
def test_mounting_names_the_mount_by_its_own_pronoun(server) -> None:
    """Bug: do_ride (ranger.cpp:185-186) writes `$m`, the rider's pronoun, where the mount's `$M` is meant.

    "You mount $N and start riding $m." and "$n mounts $N and starts riding $m." take `$m` from
    the actor, so the female rider reads "You mount a harness pony and start riding her." and the
    room reads the same pronoun. Correct: "him", the male pony's. Nothing draws.
    """
    leader = server.login(LEADER.name)
    rider = server.login(RIDER.name)
    arrived = leader.command(f"goto {world.ROOM_ARENA_CENTRE}")
    assert arrived.contains("Arena Centre"), arrived.text
    rider.drain(SETTLE_SECONDS)

    mounted = rider.command("ride pony")
    assert mounted.contains(MOUNTED_MARKER), mounted.text
    seen = leader.expect([" mounts a harness pony"]) + leader.drain(SETTLE_SECONDS)
    assert ROOM_MOUNTED_MARKER_FORMAT.format(RIDER.name) in seen, seen


@pytest.mark.wizlevels
@pytest.mark.xfail(
    strict=True,
    raises=AssertionError,
    reason="a lower-level immortal should not teleport a higher-level one, as transfer, force, wizset and snoop refuse (intent unconfirmed)",
)
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, GOD)
def test_a_level_93_immortal_cannot_teleport_the_implementor(server) -> None:
    """Bug: do_teleport (act_wiz.cpp:379-404) checks no target level.

    do_trans (act_wiz.cpp:348-351) answers "Go transfer someone your own size.", and do_force
    (act_wiz.cpp:1933), do_wizset and do_snoop (act_wiz.cpp:1260) refuse a target at or above the
    speaker's level, but the level-93 immortal teleports the level-100 implementor to Arena East.
    The refusal's text is undecided, so the pin asserts only that the implementor reads no
    teleport and stays in Immortal Start. Nothing draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    god = server.login(GOD.name)
    implementor.drain(SETTLE_SECONDS)

    god.command(f"teleport {IMPLEMENTOR.name.lower()} {world.ROOM_ARENA_EAST}")
    assert ANY_TELEPORTED_MARKER not in implementor.drain(SETTLE_SECONDS)
    assert implementor.command("look").room_name() == "Immortal Start"
