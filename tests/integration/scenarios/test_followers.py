"""Followers and groups: the ACK lines a leader and its follower read when the follower cannot
follow, followers whose leader dies where it arrives, a rider whose mount takes the step, a follower
who dies itself outside a fight, and the group, ungroup and report commands."""

from __future__ import annotations

import re

import pytest

from rots_harness import world
from rots_harness.characters import IMPLEMENTOR, Character
from rots_harness.game_text import DEATH_CRY_FORMAT, NEW_BODY_MESSAGE, NOW_FOLLOWING_FORMAT
from rots_harness.immortal_commands import give_new_object, goto, load_mob, set_holylight, wizset
from rots_harness.replies import reply_to
from rots_harness.session import GameSession
from rots_harness.stat_reading import read_stat
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.followers]


def human_warrior(name: str, sex: str, load_room: int) -> Character:
    """A level-10 human warrior: a mortal on the implementor's side, so names match across them."""
    return Character(name=name, race="human", sex=sex, level=10, load_room=load_room, class_name="warrior")


# The leader: the implementor, male, so the follower's line names him as "him".
LEADER = IMPLEMENTOR
# A follower in the arena centre. Female, so the leader's line ("you lost her!") and the
# follower's ("follow him!") each show whose pronoun act() used.
FOLLOWER = human_warrior("Testtrailer", "female", world.ROOM_ARENA_CENTRE)
# A level-10 human mage in the arena centre who knows cure self fully: 100 practice sessions put
# its knowledge at the cap of 100 (recalc_skills, spec_pro.cpp). Female for the same reason.
CASTING_FOLLOWER = Character(
    name="Testchanter",
    race="human",
    sex="female",
    level=10,
    load_room=world.ROOM_ARENA_CENTRE,
    class_name="mage",
    skills=(("cure_self", 100),),
)
# Three mortals on the ledge before the Gallows Pit, who follow the bystander orc into it.
PIT_FOLLOWERS = (
    human_warrior("Testpilgrim", "female", world.ROOM_GALLOWS_APPROACH),
    human_warrior("Testtraveler", "female", world.ROOM_GALLOWS_APPROACH),
    human_warrior("Testwanderer", "female", world.ROOM_GALLOWS_APPROACH),
)
# The same three mortals in the arena centre, where the bystander orc's step east is safe.
ARENA_FOLLOWERS = tuple(
    human_warrior(follower.name, follower.sex, world.ROOM_ARENA_CENTRE) for follower in PIT_FOLLOWERS
)
# A mortal leader in the arena centre: male, so a line about him reads "him".
CAPTAIN = human_warrior("Testcaptain", "male", world.ROOM_ARENA_CENTRE)
# A mortal the captain groups.
MEMBER = human_warrior("Testmember", "female", world.ROOM_ARENA_CENTRE)
# A mortal who follows the leader riding the harness pony.
RIDER = human_warrior("Testrider", "female", world.ROOM_ARENA_CENTRE)
# Room 2301, the Stable Yard in zone 23, west of the paddock.
ROOM_STABLE_YARD = 2301
# Room 2302, the Paddock in zone 23, between the stable yard and the sinkhole.
ROOM_PADDOCK = 2302
# Room 2303, the Sinkhole in zone 23: a DEATH room east of the paddock, with no exits.
ROOM_SINKHOLE = 2303
# A mortal in the stable yard who rides the stable cob at the head of a follower.
HORSEMAN = human_warrior("Testhorseman", "male", ROOM_STABLE_YARD)
# A mortal in the stable yard who follows the horseman on foot.
YARD_FOLLOWER = human_warrior("Testgroom", "female", ROOM_STABLE_YARD)
# The horseman in the paddock, beside the sinkhole.
SINKHOLE_HORSEMAN = human_warrior("Testhorseman", "male", ROOM_PADDOCK)

# The pet orc (mob 1134), whose MOB_PET flag without MOB_ORC_FRIEND makes do_report refuse it.
PET_VNUM = 1134
# The stamina-less mob (mob 1160): the bystander orc (world.MOB_BYSTANDER_ORC) with no maximum
# moves either (`0 0 0 1`).
STAMINALESS_VNUM = 1160
# The harness pony (mob 1161): a MOB_MOUNT with no moves, so it can never take a step.
PONY_VNUM = 1161
# The ring of invisibility (object 1160): worn, it sets AFF_INVISIBLE on the wearer.
RING_VNUM = 1160
# The stable cob (mob 2301, mob/23.mob): the harness pony with 100 moves, so it can take steps.
COB_VNUM = 2301
# The cob's moves at boot, its file's maximum.
COB_MOVES = 100

# The room the leader walks to, east of the arena centre.
DESTINATION_ROOM_NAME = "Arena East"
# The end of what the follower reads as the leader walks east (do_move, act_move.cpp), sent
# before any of the follower's outcomes.
LEADER_LEAVES_MARKER = "leaves east."
# What the leader reads when a follower is left behind (do_move, act_move.cpp); format with the
# follower as the leader sees it. Its pronoun is the follower's: the act() actor is the follower.
LEADER_ACK_MARKER_FORMAT = "ACK! {} could not follow, you lost {}!"
# What the follower reads when it is left behind (do_move): the pronoun is the leader's.
FOLLOWER_ACK_MARKER = "ACK! You could not follow him!"
# The start of every ACK line, the leader's and the follower's.
ANY_ACK_MARKER = "ACK!"
# What a follower reads when it follows (do_move); format with the leader as the follower sees it.
FOLLOWED_MARKER_FORMAT = "You follow {}."
# The start of FOLLOWED_MARKER_FORMAT, whoever the leader is.
ANY_FOLLOWED_MARKER = "You follow "
# The start of what a rider reads as it mounts the pony (do_ride, ranger.cpp). The pronoun after
# it is the rider's today, not the pony's (a known bug).
MOUNTED_MARKER_START = "You mount a harness pony and start riding "
# What the leader reads when a riding follower's mount cannot take the step (do_move's riding
# branch); format with the follower's name.
LEADER_RIDING_ACK_MARKER_FORMAT = "ACK! {} could not follow riding, you lost her!"
# What a riding follower reads when its mount cannot take the step.
FOLLOWER_RIDING_ACK_MARKER = "ACK! You could not follow him riding!"
# What a riding follower reads after FOLLOWER_RIDING_ACK_MARKER when its mount has too few moves.
MOUNT_EXHAUSTED_MARKER = "Your mount is too exhausted to move."
# The start of what a rider reads as it mounts the cob (do_ride, ranger.cpp).
MOUNTED_COB_MARKER_START = "You mount a stable cob and start riding "
# What those in the room a mount leaves read (perform_move_mount and show_mount_to_char,
# act_move.cpp and act_info.cpp); format with the rider as they see it.
RIDES_EAST_FORMAT = "{} is leaving east, riding on a stable cob."
# The name look shows for the paddock, where the cob carries its rider.
PADDOCK_ROOM_NAME = "Paddock"
# The name look shows for the sinkhole, the death room east of the paddock.
SINKHOLE_ROOM_NAME = "Sinkhole"
# Every refusal do_move's riding branch can give the rider (act_move.cpp).
RIDING_REFUSAL_MARKERS = [
    "You do not control your mount.",
    "Your mount cannot swim.",
    "You are too exhausted to ride.",
    MOUNT_EXHAUSTED_MARKER,
    "Your mount would not go there.",
    "Can not go there mounted.",
    "You cannot go that way.",
]
# What a rider reads as its death unseats it in a room its mount has not reached yet (stop_riding
# from raw_kill, fight.cpp): a mortal cannot see a mob in another room (CAN_SEE, utility.cpp).
UNSEATED_BY_DEATH_MARKER = "You stop riding someone."
# What the sinkhole shows of the rider's and the cob's corpses (get_corpse_desc, fight.cpp): a
# player's corpse is named by its race.
RIDER_CORPSE_LINE = "The silent corpse of *a Human* is lying here."
COB_CORPSE_LINE = "The silent corpse of a stable cob is lying here."
# What command_interpreter (interpre.cpp) tells a character it is asked to run a command for
# while a delay is running.
BUSY_MARKER = "You are busy with something else."
# What a follower without the moves for the step reads (do_move, res_flag 3 from
# check_simple_move).
EXHAUSTED_MARKER = "You are too exhausted to move."
# What a mortal follower reads when its mob leader dies in another room (stop_follower,
# handler.cpp): CAN_SEE (utility.cpp) hides a mob in another room from a mortal.
STOP_FOLLOWING_UNSEEN_MARKER = "You stop following someone."
# The name of the room a follower sees as it walks into the pit (do_look on arrival).
PIT_ROOM_NAME = "Gallows Pit"
# What the caster reads when the casting delay starts (do_cast, spell_pa.cpp).
CONCENTRATE_MARKER = "You start to concentrate."
# What the caster reads when its mana does not cover the cast (can_cast_spell, spell_pa.cpp).
NO_ENERGY_MARKER = "You can't summon enough energy to cast the spell."
# The outcomes of cure self when the delay ends: the concentration roll fails (do_cast), or the
# spell runs on a caster at full hit points or below them (spell_cure_self, mage.cpp).
CAST_OUTCOME_MARKERS = ["You lost your concentration!", "You are already healthy.", "You feel better."]
# Seconds to wait for the cast's outcome: cure self's 36 beats at mage level 10 are
# 36 * 30 / (30 + 10) = 27 passes, under seven seconds (CASTING_TIME, spells.h); 20 leaves room
# for a loaded host.
CAST_TIMEOUT = 20.0
# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0

# The stat line of a mob's points: current, maximum and gain of hit points, stamina and moves
# (do_stat_character, act_wiz.cpp). Stamina is the game's word for mana.
POINTS_PATTERN = re.compile(r"HP :\[(\d+)/(\d+)\+\d+\(\d+\)\]\s+Stamina :\[(\d+)/(\d+)\+\d+\(\d+\)\]\s+Move :\[(\d+)/(\d+)\+")
# What a room reads when the bystander orc reports outside a group (do_report through do_say,
# act_othe.cpp). do_report lower-cases every letter after the first, so the second "I" reads "i"
# today (a known bug); either case passes here.
BYSTANDER_REPORT_PATTERN = re.compile(r"A bystander orc says 'I am healthy, my stamina is empty, and [Ii] am energetic\.'")
# The start of any line do_say sends a room for the bystander or the pet orc.
ANY_ORC_SAYS_PATTERN = re.compile(r"orc says '")
# What a group member reads when the stamina-less mob reports (do_report through do_gsay).
STAMINALESS_REPORT_PATTERN = re.compile(r"A stamina-less mob group-says 'I am healthy, my stamina is empty, and [Ii] am exhausted\.'")
# The leader line of `group` for the stamina-less mob (print_group_leader, act_othe.cpp).
STAMINALESS_LEADER_LINE = "HP:  Healthy,    S:Empty, MV:Exhausted -- a stamina-less mob (Head of group)"
# The end of the leader line of `group`, after which the drain collects the member lines.
HEAD_OF_GROUP_MARKER = "(Head of group)"
# What the leader reads when a character joins its group (add_character_to_group,
# act_othe.cpp); format with the member's name.
JOINS_YOUR_GROUP_FORMAT = "{} joins your group."
# What the leader reads when a member leaves (remove_character_from_group); format with the name.
LEAVES_YOUR_GROUP_FORMAT = "{} leaves your group."
# What the leader reads when its last member leaves.
DISBANDED_MARKER = "You have disbanded the group."
# What a member reads when the leader ungroups it (do_ungroup); format with the leader's name.
KICKED_OUT_FORMAT = "You have been kicked out of {}'s group!"
# What a member reads as it leaves a group (remove_character_from_group); format with the leader.
YOU_LEAVE_GROUP_FORMAT = "You leave the group of {}."


def walk_east(leader: GameSession, follower: GameSession) -> tuple:
    """Walks the leader east; returns what the follower and then the leader read.

    Every outcome for either side arrives in the pass that runs the step: the follower reads the
    leader leave first, the leader reads its new room first, and the drain collects the rest.
    """
    leader.send_line("east")
    follower_text = follower.expect([LEADER_LEAVES_MARKER]) + follower.drain(SETTLE_SECONDS)
    leader_text = leader.expect([DESTINATION_ROOM_NAME]) + leader.drain(SETTLE_SECONDS)
    return follower_text, leader_text


def walk_leader_east(leader: GameSession) -> str:
    """Walks the leader east and returns what it read: its new room first, then the rest of the pass."""
    return reply_to(leader, "east", [DESTINATION_ROOM_NAME])


def gather_in_arena_centre(leader: GameSession, follower: GameSession) -> None:
    """Brings the leader to the arena centre and has the follower follow it."""
    goto(leader, world.ROOM_ARENA_CENTRE, "Arena Centre", exact=False)
    following = follower.command(f"follow {LEADER.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(LEADER.name)), following.text
    leader.drain(SETTLE_SECONDS)


def force(implementor: GameSession, keyword: str, command: str) -> str:
    """Forces a mob to run a command; returns the implementor's reply.

    do_force (act_wiz.cpp) answers "Ok." and then runs the command at once, so what the mob's
    command shows the implementor's room arrives in the same reply.
    """
    reply = implementor.command(f"force {keyword} {command}")
    assert reply.contains("Ok."), reply.text
    return reply.text


def read_points(implementor: GameSession, keyword: str) -> tuple:
    """A mob's current and maximum hit points, stamina and moves, as `stat mob` shows them."""
    text = read_stat(implementor, f"mob {keyword}")
    points = POINTS_PATTERN.search(text)
    assert points is not None, f"no points line for {keyword}:\n{text}"
    return tuple(int(value) for value in points.groups())


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, FOLLOWER)
def test_a_sitting_follower_left_behind_gets_ack_lines_and_a_standing_one_follows(server) -> None:
    """Pins #389: a follower that is not standing is told it was left behind, and so is its leader.

    do_move (act_move.cpp) never asks a follower below POSITION_STANDING to move; since #389 it
    sends both ACK lines instead of leaving them unsaid. The control has the same follower stand
    and walk after the same leader. No draw decides either outcome: the affect-hours schedule
    keeps mobs still, and the leader's move cost draws but cannot stop an implementor.
    """
    leader = server.login(LEADER.name)
    follower = server.login(FOLLOWER.name)
    gather_in_arena_centre(leader, follower)

    assert follower.command("sit").contains("You sit down."), follower.everything
    left_behind, leader_text = walk_east(leader, follower)
    assert left_behind.count(FOLLOWER_ACK_MARKER) == 1, left_behind
    leader_ack = LEADER_ACK_MARKER_FORMAT.format(FOLLOWER.name, "her")
    assert leader_text.count(leader_ack) == 1, leader_text
    assert follower.command("look").room_name() == "Arena Centre"

    # Control: the leader comes back, the follower stands, and the same step east is taken.
    assert leader.command("west").room_name() == "Arena Centre"
    assert follower.command("stand").contains("You stand up."), follower.everything
    followed, leader_text = walk_east(leader, follower)
    assert FOLLOWED_MARKER_FORMAT.format(LEADER.name) in followed, followed
    assert FOLLOWER_ACK_MARKER not in followed, followed
    assert DESTINATION_ROOM_NAME in followed, followed
    assert leader_ack not in leader_text, leader_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, CASTING_FOLLOWER)
def test_a_follower_in_a_casting_delay_gets_the_ack_line_instead_of_busy(server) -> None:
    """Pins #389: a standing follower still in a delay is told it was left behind, not that it is busy.

    Before #389 do_move asked a delayed follower to move, and command_interpreter
    (interpre.cpp) answered "You are busy with something else."; now do_move sends both ACK
    lines when the follower's delay is running. Cure self's casting delay is 27 passes (about
    seven seconds), so it is still running when the leader's step, sent after the follower's
    "You start to concentrate.", arrives. Nothing on the way draws before the leader moves; the
    concentration roll at the end of the cast decides only which outcome line follows.
    """
    leader = server.login(LEADER.name)
    follower = server.login(CASTING_FOLLOWER.name)
    gather_in_arena_centre(leader, follower)

    follower.send_line("cast 'cure self'")
    started = follower.expect([CONCENTRATE_MARKER, NO_ENERGY_MARKER])
    assert CONCENTRATE_MARKER in started, started
    left_behind, leader_text = walk_east(leader, follower)
    assert BUSY_MARKER not in left_behind, left_behind
    assert left_behind.count(FOLLOWER_ACK_MARKER) == 1, left_behind
    assert leader_text.count(LEADER_ACK_MARKER_FORMAT.format(CASTING_FOLLOWER.name, "her")) == 1, leader_text

    follower.expect(CAST_OUTCOME_MARKERS, timeout=CAST_TIMEOUT)
    follower.drain(SETTLE_SECONDS)
    assert follower.command("look").room_name() == "Arena Centre"


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, FOLLOWER)
def test_a_follower_without_moves_left_behind_gets_ack_lines(server) -> None:
    """Pins the ACK lines for a standing follower whose own step fails, which #389 kept.

    The follower stands and has no delay, so do_move (act_move.cpp) asks it to move; its own
    do_move call then fails in check_simple_move, which returns 3 because no room costs less than
    one move, and sends the ACK lines before "You are too exhausted to move.". #389 left these
    lines as they were and only stopped them naming a leader that is gone. Under affect-hours
    no hour runs, so the follower's moves stay at 0.
    """
    leader = server.login(LEADER.name)
    follower = server.login(FOLLOWER.name)
    gather_in_arena_centre(leader, follower)

    wizset(leader, f"player {FOLLOWER.name.lower()}", "move", 0, shown_name=FOLLOWER.name)
    left_behind, leader_text = walk_east(leader, follower)
    assert EXHAUSTED_MARKER in left_behind, left_behind
    assert left_behind.count(FOLLOWER_ACK_MARKER) == 1, left_behind
    assert leader_text.count(LEADER_ACK_MARKER_FORMAT.format(FOLLOWER.name, "her")) == 1, leader_text
    assert follower.command("look").room_name() == "Arena Centre"


def follow_the_bystander(implementor: GameSession, followers: list, room: int) -> None:
    """Brings the implementor to room, where the world places the bystander orc, and has every
    follower follow the orc.

    Each follower reads the orc by its short description: they share its room.
    """
    arrived = implementor.command(f"goto {room}")
    assert arrived.room_name() is not None, arrived.text
    for follower in followers:
        following = follower.command("follow bystander")
        assert following.contains(NOW_FOLLOWING_FORMAT.format("a bystander orc")), following.text
    implementor.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, *PIT_FOLLOWERS)
@pytest.mark.world_resets(MobReset(world.MOB_BYSTANDER_ORC, world.ROOM_GALLOWS_APPROACH))
def test_every_follower_walks_in_after_a_leader_that_dies_where_it_arrives(server) -> None:
    """Pins #389: when the leader is removed where it arrives, every follower still takes the step.

    The bystander orc walks east into the Gallows Pit, a DEATH room, and raw_kill (fight.cpp)
    extracts it there; extract_char's die_follower (handler.cpp) stops each follower following
    it. Since #389 do_move (act_move.cpp) walks a vector of the followers taken before the move,
    not the dead leader's freed list, so all three still step in, and its ch->master guards
    name no leader: no "You follow" and no ACK line. Each dies in the pit in turn and wakes in
    Human Start. Every message arrives in the pass that runs the orc's step, after the
    unseen-leader line each follower reads first. Nothing on the way draws an outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    followers = [server.login(follower.name) for follower in PIT_FOLLOWERS]
    follow_the_bystander(implementor, followers, world.ROOM_GALLOWS_APPROACH)

    force(implementor, "bystander", "east")
    for follower, character in zip(followers, PIT_FOLLOWERS):
        text = follower.expect([STOP_FOLLOWING_UNSEEN_MARKER]) + follower.drain(SETTLE_SECONDS)
        assert text.count(STOP_FOLLOWING_UNSEEN_MARKER) == 1, f"{character.name}:\n{text}"
        assert PIT_ROOM_NAME in text, f"{character.name}:\n{text}"
        assert text.count(NEW_BODY_MESSAGE) == 1, f"{character.name}:\n{text}"
        assert ANY_FOLLOWED_MARKER not in text, f"{character.name}:\n{text}"
        assert ANY_ACK_MARKER not in text, f"{character.name}:\n{text}"
    for follower in followers:
        assert follower.command("look").room_name() == world.ROOM_HUMAN_START_NAME


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, *ARENA_FOLLOWERS)
@pytest.mark.world_resets(MobReset(world.MOB_BYSTANDER_ORC, world.ROOM_ARENA_CENTRE))
def test_every_follower_follows_a_mob_leader_that_survives_the_step(server) -> None:
    """Control for the Gallows Pit pin: the same three followers follow the same orc east and live.

    With no DEATH room the orc stays their leader through the step, so do_move (act_move.cpp)
    tells each "You follow someone.": the orc is in Arena East when the line is sent, and CAN_SEE
    (utility.cpp) hides a mob in another room from a mortal. Nothing on the way draws an outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    followers = [server.login(follower.name) for follower in ARENA_FOLLOWERS]
    follow_the_bystander(implementor, followers, world.ROOM_ARENA_CENTRE)

    force(implementor, "bystander", "east")
    for follower, character in zip(followers, ARENA_FOLLOWERS):
        text = follower.expect([DESTINATION_ROOM_NAME]) + follower.drain(SETTLE_SECONDS)
        assert text.count(FOLLOWED_MARKER_FORMAT.format("someone")) == 1, f"{character.name}:\n{text}"
        assert NEW_BODY_MESSAGE not in text, f"{character.name}:\n{text}"
        assert ANY_ACK_MARKER not in text, f"{character.name}:\n{text}"
    for follower in followers:
        assert follower.command("look").room_name() == DESTINATION_ROOM_NAME


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, MEMBER)
def test_report_and_group_show_a_mob_with_no_stamina_or_moves(server) -> None:
    """Pins #389: report and group read a mob whose maximum stamina or moves is 0 without dividing by it.

    do_report and print_group_leader (act_othe.cpp) divide by the maximum stamina and moves; since
    #389 they divide by 1 when it is 0, so the mob reads as empty or exhausted instead of the
    server dying of a division by zero. The bystander orc has no stamina and 50 moves; the
    stamina-less mob has neither, leads the member's group, and reports to it with group-say.
    The control is the pet orc, which do_report refuses before any division (its refusal goes to
    the pet alone, so the room reads nothing). Nothing on the way draws.
    """
    implementor = server.login(IMPLEMENTOR.name)
    member = server.login(MEMBER.name)
    goto(implementor, world.ROOM_ARENA_CENTRE, "Arena Centre")

    load_mob(implementor, world.MOB_BYSTANDER_ORC, "a bystander orc")
    assert read_points(implementor, "bystander") == (60, 60, 0, 0, 50, 50)
    report = force(implementor, "bystander", "report")
    assert BYSTANDER_REPORT_PATTERN.search(report), report

    load_mob(implementor, PET_VNUM, "a pet orc")
    refused = force(implementor, "pet", "report")
    assert not ANY_ORC_SAYS_PATTERN.search(refused), refused

    load_mob(implementor, STAMINALESS_VNUM, "a stamina-less mob")
    assert read_points(implementor, "zerostam") == (60, 60, 0, 0, 0, 0)
    force(implementor, "zerostam", f"group {MEMBER.name.lower()}")
    joined = member.expect(["You join the group of a stamina-less mob."])
    assert "You join the group of a stamina-less mob." in joined, joined
    member.drain(SETTLE_SECONDS)
    group = reply_to(member, "group", [HEAD_OF_GROUP_MARKER])
    assert group.count(STAMINALESS_LEADER_LINE) == 1, group
    force(implementor, "zerostam", "report")
    grouped_report = member.expect(["group-says"]) + member.drain(SETTLE_SECONDS)
    assert STAMINALESS_REPORT_PATTERN.search(grouped_report), grouped_report


def group_the_member(captain: GameSession, member: GameSession) -> None:
    """Has the captain group the member, and asserts what each reads."""
    grouped = captain.command(f"group {MEMBER.name.lower()}")
    assert grouped.contains(JOINS_YOUR_GROUP_FORMAT.format(MEMBER.name)), grouped.text
    joined = member.expect([f"You join the group of {CAPTAIN.name}."])
    assert f"You join the group of {CAPTAIN.name}." in joined, joined
    member.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CAPTAIN, MEMBER)
def test_ungroup_names_a_member_out_of_the_group(server) -> None:
    """Pins `ungroup <member>`: the member is told it was kicked out, and the group disbands.

    do_ungroup (act_othe.cpp) tells the member it was kicked out, and remove_character_from_group
    tells the member it leaves and the leader that it left; the last member gone, the leader
    reads that the group is disbanded. Nothing draws.
    """
    server.login(IMPLEMENTOR.name)
    captain = server.login(CAPTAIN.name)
    member = server.login(MEMBER.name)
    group_the_member(captain, member)

    ungrouped = captain.command(f"ungroup {MEMBER.name.lower()}")
    assert ungrouped.text.count(LEAVES_YOUR_GROUP_FORMAT.format(MEMBER.name)) == 1, ungrouped.text
    assert ungrouped.text.count(DISBANDED_MARKER) == 1, ungrouped.text
    kicked = member.expect([YOU_LEAVE_GROUP_FORMAT.format(CAPTAIN.name)]) + member.drain(SETTLE_SECONDS)
    assert kicked.count(KICKED_OUT_FORMAT.format(CAPTAIN.name)) == 1, kicked
    assert captain.command("group").contains("But you are not the member of a group!"), captain.everything


def put_on_the_ring(leader: GameSession, follower: GameSession) -> None:
    """Hands the follower the ring of invisibility and has it put the ring on."""
    give_new_object(leader, RING_VNUM, "ring", FOLLOWER.name.lower(), shown_recipient=FOLLOWER.name, short_description="a ring of invisibility")
    worn = follower.command("wear ring")
    assert worn.contains("You slide a ring of invisibility on to your"), worn.text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, FOLLOWER)
def test_a_sitting_follower_the_leader_cannot_see_leaves_the_leader_no_ack_line(server) -> None:
    """Pins #389: a player follower the leader cannot see is left behind without the leader's ACK line.

    For a follower that never tries the step, do_move (act_move.cpp) sends the leader's ACK line
    with act()'s hide_invisible set for a player follower, so a leader who cannot see it reads
    nothing; the follower still reads its own line. The control is the same sitting follower
    while visible. The leader is the implementor with holylight off: AFF_INVISIBLE from the ring
    hides the follower from it (CAN_SEE, utility.cpp). Nothing on the way draws an outcome.
    """
    leader = server.login(LEADER.name)
    follower = server.login(FOLLOWER.name)
    gather_in_arena_centre(leader, follower)
    set_holylight(leader, "off")
    assert follower.command("sit").contains("You sit down."), follower.everything

    left_behind, leader_text = walk_east(leader, follower)
    assert left_behind.count(FOLLOWER_ACK_MARKER) == 1, left_behind
    assert leader_text.count(LEADER_ACK_MARKER_FORMAT.format(FOLLOWER.name, "her")) == 1, leader_text

    assert leader.command("west").room_name() == "Arena Centre"
    put_on_the_ring(leader, follower)
    left_behind, leader_text = walk_east(leader, follower)
    assert left_behind.count(FOLLOWER_ACK_MARKER) == 1, left_behind
    assert ANY_ACK_MARKER not in leader_text, leader_text


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CAPTAIN)
@pytest.mark.world_resets(MobReset(world.MOB_BYSTANDER_ORC, world.ROOM_ARENA_CENTRE))
def test_a_sitting_mob_follower_left_behind_gives_its_leader_an_ack_line(server) -> None:
    """Pins #389's ACK line for a mob follower that is not standing, read by a mortal and an immortal leader.

    do_move (act_move.cpp) sends the leader's ACK line for a mob follower without hiding it, after
    the leader has moved. The mortal captain then reads the orc as "someone": CAN_SEE
    (utility.cpp) hides a mob in another room from a mortal. The implementor, above that rule,
    reads its short description. Nothing on the way draws an outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    captain = server.login(CAPTAIN.name)
    goto(implementor, world.ROOM_ARENA_CENTRE, "Arena Centre")

    force(implementor, "bystander", f"follow {CAPTAIN.name.lower()}")
    assert "A bystander orc sits down." in force(implementor, "bystander", "sit")
    captain.drain(SETTLE_SECONDS)
    captain_text = walk_leader_east(captain)
    assert captain_text.count(LEADER_ACK_MARKER_FORMAT.format("someone", "him")) == 1, captain_text

    force(implementor, "bystander", "stand")
    force(implementor, "bystander", f"follow {IMPLEMENTOR.name.lower()}")
    force(implementor, "bystander", "sit")
    implementor_text = walk_leader_east(implementor)
    assert implementor_text.count(LEADER_ACK_MARKER_FORMAT.format("a bystander orc", "him")) == 1, implementor_text


def ride_after_the_leader(leader: GameSession, rider: GameSession) -> None:
    """Brings the leader to the arena centre, where the world places the harness pony, and has the
    rider follow the leader and mount the pony."""
    goto(leader, world.ROOM_ARENA_CENTRE, "Arena Centre", exact=False)
    following = rider.command(f"follow {LEADER.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(LEADER.name)), following.text
    mounted = rider.command("ride pony")
    assert mounted.contains(MOUNTED_MARKER_START), mounted.text
    leader.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, RIDER)
@pytest.mark.world_resets(MobReset(PONY_VNUM, world.ROOM_ARENA_CENTRE))
def test_a_rider_whose_mount_cannot_move_gets_the_riding_ack_lines(server) -> None:
    """Pins the riding ACK lines for a follower whose mount has no moves.

    do_move's riding branch (act_move.cpp) checks the rider's step, then the mount's with
    check_simple_move; the pony has no moves, so it returns 3 and both sides read the riding ACK
    lines, then the rider reads that its mount is too exhausted. Nothing on the way draws.
    """
    leader = server.login(LEADER.name)
    rider = server.login(RIDER.name)
    ride_after_the_leader(leader, rider)

    left_behind, leader_text = walk_east(leader, rider)
    assert left_behind.count(FOLLOWER_RIDING_ACK_MARKER) == 1, left_behind
    assert left_behind.count(MOUNT_EXHAUSTED_MARKER) == 1, left_behind
    assert leader_text.count(LEADER_RIDING_ACK_MARKER_FORMAT.format(RIDER.name)) == 1, leader_text
    assert rider.command("look").room_name() == "Arena Centre"


def mount_the_cob(implementor: GameSession, horseman: GameSession, room: int) -> None:
    """Brings the implementor to room, where the world places the stable cob, checks the cob's
    moves and has the horseman mount it."""
    arrived = implementor.command(f"goto {room}")
    assert arrived.room_name() is not None, arrived.text
    moves = read_points(implementor, "cob")[4:]
    assert moves == (COB_MOVES, COB_MOVES), moves
    mounted = horseman.command("ride cob")
    assert mounted.contains(MOUNTED_COB_MARKER_START), mounted.text
    implementor.drain(SETTLE_SECONDS)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, HORSEMAN, YARD_FOLLOWER)
@pytest.mark.world_resets(MobReset(COB_VNUM, ROOM_STABLE_YARD))
def test_a_rider_whose_mount_can_move_rides_east_and_its_follower_follows(server) -> None:
    """Legacy pin: a mounted leader rides one room east, and the follower on foot follows.

    do_move's riding branch (act_move.cpp) checks the rider's step and then the mount's
    (check_simple_move), and perform_move_mount carries the mount and its rider into the new
    room, which the rider sees. The room left reads the rider leave on its mount. The follower,
    standing in the room left, then follows (do_move, SCMD_FOLLOW) and reads the room it
    arrives in. The only draws on the way pick which track slot the step marks, which nobody
    reads here.
    """
    implementor = server.login(IMPLEMENTOR.name)
    horseman = server.login(HORSEMAN.name)
    follower = server.login(YARD_FOLLOWER.name)
    following = follower.command(f"follow {HORSEMAN.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(HORSEMAN.name)), following.text
    mount_the_cob(implementor, horseman, ROOM_STABLE_YARD)
    follower.drain(SETTLE_SECONDS)

    ridden = reply_to(horseman, "east", [PADDOCK_ROOM_NAME, *RIDING_REFUSAL_MARKERS])
    assert PADDOCK_ROOM_NAME in ridden, ridden
    # The follower reads the horseman leave, then follows in the same pass (do_move runs each
    # follower's step before it returns), so the drain collects whatever the follower did.
    followed = follower.expect([RIDES_EAST_FORMAT.format(HORSEMAN.name)]) + follower.drain(SETTLE_SECONDS)
    assert followed.count(RIDES_EAST_FORMAT.format(HORSEMAN.name)) == 1, followed
    assert followed.count(FOLLOWED_MARKER_FORMAT.format(HORSEMAN.name)) == 1, followed
    assert PADDOCK_ROOM_NAME in followed, followed
    assert ANY_ACK_MARKER not in followed, followed
    # The implementor reads the same line, with the "(holy protection)" an immortal sees beside
    # a low-level player's name (show_mount_to_char), and then the follower leave on foot.
    watched = implementor.drain(SETTLE_SECONDS).splitlines()
    riding_line_end = RIDES_EAST_FORMAT.format("")
    riding_lines = [line.strip() for line in watched if riding_line_end in line]
    assert len(riding_lines) == 1 and riding_lines[0].startswith(HORSEMAN.name), watched
    assert sum(YARD_FOLLOWER.name in line and "leaves east." in line for line in watched) == 1, watched

    assert horseman.command("look").room_name() == PADDOCK_ROOM_NAME
    assert follower.command("look").room_name() == PADDOCK_ROOM_NAME
    cob = implementor.command("where cob").text
    assert re.search(rf"a stable cob\s+- \[ *{ROOM_PADDOCK}\] {PADDOCK_ROOM_NAME}", cob), cob
    assert read_points(implementor, "cob")[4] < COB_MOVES


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, SINKHOLE_HORSEMAN)
@pytest.mark.world_resets(MobReset(COB_VNUM, ROOM_PADDOCK))
def test_a_rider_who_rides_into_a_death_room_dies_there_with_its_mount(server) -> None:
    """Legacy pin: riding into a DEATH room kills the rider and then its mount, each leaving a
    corpse there, and the rider wakes in its start room.

    perform_move_mount (act_move.cpp) moves each rider into the sinkhole and kills it there
    (raw_kill, fight.cpp) before it can look, then moves and kills the mount. The rider's death
    unseats it while the cob is still in the paddock, so it reads that it stops riding
    "someone". The implementor waits in the sinkhole and hears both death cries, rider first.
    The rider wakes in Human Start with 1 hit point (a death with no killer, raw_kill). The only
    draws on the way pick which track slot the step marks.
    """
    implementor = server.login(IMPLEMENTOR.name)
    horseman = server.login(SINKHOLE_HORSEMAN.name)
    mount_the_cob(implementor, horseman, ROOM_PADDOCK)
    goto(implementor, ROOM_SINKHOLE, SINKHOLE_ROOM_NAME)

    died = reply_to(horseman, "east", [NEW_BODY_MESSAGE, *RIDING_REFUSAL_MARKERS])
    assert died.count(UNSEATED_BY_DEATH_MARKER) == 1, died
    assert died.count(NEW_BODY_MESSAGE) == 1, died
    assert died.index(UNSEATED_BY_DEATH_MARKER) < died.index(NEW_BODY_MESSAGE), died
    assert world.ROOM_HUMAN_START_NAME in died, died
    assert SINKHOLE_ROOM_NAME not in died, died
    # Both deaths happen in the pass that runs the step, the rider's first, so the drain after
    # the rider's death cry collects the cob's.
    cob_cry = DEATH_CRY_FORMAT.format("a stable cob")
    rider_cry = DEATH_CRY_FORMAT.format(SINKHOLE_HORSEMAN.name)
    heard = implementor.expect([rider_cry]) + implementor.drain(SETTLE_SECONDS)
    assert heard.count(rider_cry) == 1 and heard.count(cob_cry) == 1, heard
    assert heard.index(rider_cry) < heard.index(cob_cry), heard

    assert horseman.command("look").room_name() == world.ROOM_HUMAN_START_NAME
    looked = implementor.command("look").text
    assert looked.count(RIDER_CORPSE_LINE) == 1, looked
    assert looked.count(COB_CORPSE_LINE) == 1, looked
    assert looked.count("corpse") == 2, looked
    cob = implementor.command("where cob").text
    assert "Couldn't find any such thing." in cob, cob
    stat_text = read_stat(implementor, f"player {SINKHOLE_HORSEMAN.name.lower()}")
    assert re.search(r"HP :\[1/", stat_text), stat_text


# A mortal on the ledge who follows the implementor and walks into the Gallows Pit alone.
STRAY = human_warrior("Teststray", "female", world.ROOM_GALLOWS_APPROACH)
# What a dying follower reads as it stops following its leader (stop_follower, handler.cpp);
# format with the leader.
STOP_FOLLOWING_FORMAT = "You stop following {}."
# What the leader reads as the follower stops following it; format with the follower.
STOPS_FOLLOWING_YOU_FORMAT = "{} stops following you."
# What the slain member reads from do_slay (act_offe.cpp), which calls raw_kill outside any fight.
SLAIN_MARKER = "chops you to pieces!"
# What every corpse's line in a room look holds (get_corpse_desc, fight.cpp). Its name for the
# dead is the race's star name.
CORPSE_IN_ROOM_MARKER = "corpse of "


def corpses_in_look(look_text: str) -> int:
    """How many of the room look's lines are a corpse.

    A corpse's line names the race, not the dead, so a test counts a death's corpse only in a room
    that holds no other corpse.
    """
    return sum(1 for line in look_text.splitlines() if CORPSE_IN_ROOM_MARKER in line)


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(LEADER, STRAY)
def test_a_follower_who_walks_into_a_death_room_reads_that_it_stops_following(server) -> None:
    """A follower dying outside a fight reads that it stops following its leader, before it wakes.

    raw_kill (fight.cpp) marks the dying dead only while the corpse takes their gear, then puts
    back the standing position they died in, so extract_char's die_follower (handler.cpp) sends
    the follower its own line: act() drops a TO_CHAR line to anyone not awake (comm.cpp). The
    leader, in the room before the pit, reads that the follower stops following. The follower
    leaves one corpse in the pit and wakes in Human Start. Nothing on the way draws an outcome.
    """
    leader = server.login(LEADER.name)
    stray = server.login(STRAY.name)
    goto(leader, world.ROOM_GALLOWS_APPROACH, "Gallows Approach")
    following = stray.command(f"follow {LEADER.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(LEADER.name)), following.text
    leader.drain(SETTLE_SECONDS)

    died = reply_to(stray, "east", [NEW_BODY_MESSAGE])
    stopped = STOP_FOLLOWING_FORMAT.format(LEADER.name)
    assert PIT_ROOM_NAME in died and died.count(stopped) == 1, died
    assert died.index(PIT_ROOM_NAME) < died.index(stopped) < died.index(NEW_BODY_MESSAGE), died
    stops_following = STOPS_FOLLOWING_YOU_FORMAT.format(STRAY.name)
    told = leader.expect([stops_following]) + leader.drain(SETTLE_SECONDS)
    assert told.count(stops_following) == 1, told

    assert stray.command("look").room_name() == world.ROOM_HUMAN_START_NAME
    goto(leader, world.ROOM_GALLOWS_PIT, PIT_ROOM_NAME)
    pit = leader.command("look").text
    assert corpses_in_look(pit) == 1, pit


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CAPTAIN, MEMBER)
def test_a_slain_group_member_reads_that_it_stops_following_and_leaves_the_group(server) -> None:
    """A grouped follower the implementor slays reads that it leaves its leader and the group.

    do_slay (act_offe.cpp) calls raw_kill outside any fight, so the member is standing again
    when extract_char's die_follower (handler.cpp) stops it following the captain and takes it
    out of his group: the member reads both lines before it wakes in Human Start, and the
    captain reads that it stops following him and leaves his group, which disbands. One corpse
    stays in the arena centre. Nothing on the way draws an outcome.
    """
    implementor = server.login(IMPLEMENTOR.name)
    captain = server.login(CAPTAIN.name)
    member = server.login(MEMBER.name)
    following = member.command(f"follow {CAPTAIN.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(CAPTAIN.name)), following.text
    group_the_member(captain, member)
    goto(implementor, world.ROOM_ARENA_CENTRE, "Arena Centre", exact=False)
    captain.drain(SETTLE_SECONDS)
    member.drain(SETTLE_SECONDS)

    slain = implementor.command(f"slay {MEMBER.name.lower()}")
    assert slain.contains("You chop her to pieces!"), slain.text
    died = member.expect([NEW_BODY_MESSAGE]) + member.drain(SETTLE_SECONDS)
    stopped = STOP_FOLLOWING_FORMAT.format(CAPTAIN.name)
    left = YOU_LEAVE_GROUP_FORMAT.format(CAPTAIN.name)
    assert SLAIN_MARKER in died and died.count(stopped) == 1 and died.count(left) == 1, died
    assert died.index(SLAIN_MARKER) < died.index(stopped) < died.index(left) < died.index(NEW_BODY_MESSAGE), died
    told = captain.expect([DISBANDED_MARKER]) + captain.drain(SETTLE_SECONDS)
    assert told.count(STOPS_FOLLOWING_YOU_FORMAT.format(MEMBER.name)) == 1, told
    assert told.count(LEAVES_YOUR_GROUP_FORMAT.format(MEMBER.name)) == 1, told

    assert member.command("look").room_name() == world.ROOM_HUMAN_START_NAME
    arena = implementor.command("look").text
    assert corpses_in_look(arena) == 1, arena
