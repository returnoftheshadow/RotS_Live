"""Script triggers as players set them off.

Each kind of trigger runs when its command is typed, binds the characters and objects its script
names, and can still refuse what it may refuse.

These are behaviour pins for the trigger dispatch: walking in (ON_ENTER on a mob and on a floor
object), a guard turning back a walker, a follower and a rider (ON_BEFORE_ENTER), wearing
(ON_WEAR), pulling (ON_PULL), examining (ON_EXAMINE_OBJECT), eating (ON_EAT), drinking
(ON_DRINK), a spell's blow (ON_DAMAGE), a death (ON_DIE) and speech heard by a yell script
(ON_HEAR_YELL).

Three tests fail for certain only on an AddressSanitizer build: a food whose ON_EAT script
removes it, a floor object whose ON_ENTER script removes it, and a listener whose ON_HEAR_SAY
script removes it. The last test checks that a mob that waits after hearing speech repeats the
words it heard.

No draw decides an outcome. The affect-hours schedule keeps combat rounds, mob turns and zone
resets from running; script waits and casting delays end on the game loop's waiting list, which
runs every pass on every server. Magic missile's damage and a drink's size are drawn, but the stoic
refuses any blow, any missile kills a one-hit-point mob (missile_always_kills) and the flask
holds one drink. The casting roll only decides whether the cast happens, with its own message, so
it is repeated on that message (cast_magic_missile_until_it_lands).
"""

from __future__ import annotations

import re
from collections.abc import Sequence
from dataclasses import dataclass

import pytest

from rots_harness.casting import LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY
from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.game_text import (
    DEATH_CRY_FORMAT,
    DRINK_EMPTY_REPLY,
    FOLLOWED_FORMAT,
    FOLLOWER_ACK,
    GET_FORMAT,
    NOW_FOLLOWING_FORMAT,
    PURGED_REPLY,
    WEAR_HEAD_FORMAT,
)
from rots_harness.immortal_commands import give_new_object, goto
from rots_harness.replies import SETTLE_SECONDS, reply_to, settled_expect
from rots_harness.session import GameSession
from rots_harness.stat_reading import (
    CONSTITUTION_PATTERN,
    HIT_POINTS_BEFORE_GAIN_PATTERN,
    LEVEL_PATTERN,
    SAVING_THROW_PATTERN,
    read_stat,
)
from rots_harness.text_lines import capitalised, first_number
from rots_harness.world_resets import MobReset, ObjectReset

# Every test here is a scenario of the script_triggers group; each also marks one subgroup.
pytestmark = [pytest.mark.scenario, pytest.mark.script_triggers]

# Zone 25's hall (tests/integration/world/wld/25.wld): north to the chamber, east through a
# closed hatch to the closet. Every mortal here starts in it.
ROOM_TRIGGER_HALL = 2501
# The chamber north of the hall, reached through an open arch.
ROOM_TRIGGER_CHAMBER = 2502
# The name look shows for the hall.
ROOM_TRIGGER_HALL_NAME = "Trigger Hall"
# The name look shows for the chamber.
ROOM_TRIGGER_CHAMBER_NAME = "Trigger Chamber"
# The name look shows for the closet behind the hatch.
ROOM_LEVER_CLOSET_NAME = "Lever Closet"


@dataclass(frozen=True)
class TriggerMob:
    """A zone 25 mob a scenario places: its vnum, keyword and short description.

    Each but the mule runs the script of its own number (world/mob/25.mob, scr/25.scr).
    """

    # The mob's virtual number, and its script's.
    vnum: int
    # The keyword that finds this mob and no other.
    keyword: str
    # The mob's short description.
    short_description: str


# Script 2500, ON_ENTER: says "Welcome, %s." with ch2.name, the one walking in.
GREETER = TriggerMob(vnum=2500, keyword="greeter", short_description="a greeter")
# Script 2501, ON_BEFORE_ENTER: sends ch2 "The warden bars your way, %s." and returns false
# when ch2.level is below 50; anyone else comes in.
WARDEN = TriggerMob(vnum=2501, keyword="warden", short_description="a warden")
# Script 2502, ON_HEAR_YELL only: says "I hear you, %s." with ch2.name, the speaker.
ECHOER = TriggerMob(vnum=2502, keyword="echoer", short_description="an echoer")
# Script 2503, ON_DAMAGE: says "Your blow cannot touch me, %s." with ch2.name, the attacker,
# and returns false. 200 hit points.
STOIC = TriggerMob(vnum=2503, keyword="stoic", short_description="a stoic")
# Script 2504, ON_DIE: says "My watch is ended." and lets the death go ahead. One hit point,
# level 5, 10000 experience.
MOURNER = TriggerMob(vnum=2504, keyword="mourner", short_description="a mourner")
# Script 2505, ON_DIE: says "Not yet!", sets ch1.hit to 30 and returns false. One hit point.
STUBBORN = TriggerMob(vnum=2505, keyword="stubborn", short_description="a stubborn old man")
# Script 2506, ON_HEAR_SAY: sends the room "The vanisher fades away." and removes himself.
VANISHER = TriggerMob(vnum=2506, keyword="vanisher", short_description="a vanisher")
# Script 2507, ON_HEAR_SAY: says "I heard that."
ANSWERER = TriggerMob(vnum=2507, keyword="answerer", short_description="an answerer")
# Script 2508, ON_HEAR_SAY: says "Let me think about that.", waits 20 passes (five seconds),
# then says "You said: %s" with str1, the words it heard.
PARROT = TriggerMob(vnum=2508, keyword="parrot", short_description="a parrot")
# Mob 2509, no script: a MOB_MOUNT with moves to spend.
MULE = TriggerMob(vnum=2509, keyword="mule", short_description="a harness mule")

# Zone 25's objects (world/obj/25.obj); each one's script is its vnum plus 50, and ch1 in it is
# whoever set it off.
# A small bell, ON_ENTER: sends ch1 BELL_LINE_FORMAT with ch1.name.
BELL_VNUM = 2500
# A paper lantern, ON_ENTER: sends ch1 LANTERN_LINE, then removes itself.
LANTERN_VNUM = 2501
# A thorned diadem, ON_WEAR: sends ch1 DIADEM_LINE and returns false.
DIADEM_VNUM = 2503
# A silver coronet, ON_WEAR: sends ch1 CORONET_LINE.
CORONET_VNUM = 2504
# An iron lever for the hall's east hatch, ON_PULL: sends ch1 LEVER_LINE.
LEVER_VNUM = 2505
# A ledger, ON_EXAMINE_OBJECT: sends ch1 LEDGER_LINE.
LEDGER_VNUM = 2506
# A honey cake, ON_EAT: sends ch1 CAKE_LINE and returns false.
CAKE_VNUM = 2507
# A flask holding one drink of water, ON_DRINK: sends ch1 FLASK_LINE and returns false.
FLASK_VNUM = 2508
# A dry biscuit, ON_EAT: sends ch1 BISCUIT_LINE, then removes itself.
BISCUIT_VNUM = 2509
# A black ribbon, with no script: something for the mourner to carry.
RIBBON_VNUM = 2510
# A rusted crank for the same hatch, ON_PULL: sends ch1 CRANK_LINE and returns false.
CRANK_VNUM = 2511
# The bell's short description.
BELL_SHORT_DESCRIPTION = "a small bell"
# The lantern's short description.
LANTERN_SHORT_DESCRIPTION = "a paper lantern"
# The diadem's short description.
DIADEM_SHORT_DESCRIPTION = "a thorned diadem"
# The coronet's short description.
CORONET_SHORT_DESCRIPTION = "a silver coronet"
# The lever's short description.
LEVER_SHORT_DESCRIPTION = "an iron lever"
# The crank's short description.
CRANK_SHORT_DESCRIPTION = "a rusted crank"
# The cake's short description.
CAKE_SHORT_DESCRIPTION = "a honey cake"
# The flask's short description.
FLASK_SHORT_DESCRIPTION = "a flask"
# The biscuit's short description.
BISCUIT_SHORT_DESCRIPTION = "a dry biscuit"
# The ribbon's short description.
RIBBON_SHORT_DESCRIPTION = "a black ribbon"

# What the bell sends whoever walks in; format with that one's name.
BELL_LINE_FORMAT = "A small bell chimes for {}."
# What the lantern sends as it removes itself.
LANTERN_LINE = "The paper lantern crumbles to dust."
# What the diadem sends as it refuses to be worn.
DIADEM_LINE = "The thorned diadem will not sit on your head."
# What the coronet sends as it is worn.
CORONET_LINE = "The silver coronet settles on your brow."
# What the lever sends as it is pulled.
LEVER_LINE = "The iron lever creaks."
# What the crank sends as it refuses to be pulled.
CRANK_LINE = "The rusted crank will not move."
# What the ledger sends as it is examined.
LEDGER_LINE = "The pages of the ledger rustle."
# What the cake sends as it is eaten.
CAKE_LINE = "The honey cake is sweet."
# What the flask sends as it is drunk from.
FLASK_LINE = "The water in the flask is cold."
# What the biscuit sends as it removes itself.
BISCUIT_LINE = "The dry biscuit crumbles away."
# What the warden sends whoever it turns back; format with that one's name.
WARDEN_LINE_FORMAT = "The warden bars your way, {}."
# The part of WARDEN_LINE_FORMAT every refusal shares.
WARDEN_BARS_PART = "bars your way"
# What the greeter says to whoever walks in; format with that one's name.
GREETING_FORMAT = "Welcome, {}."
# What the echoer says to a speaker; format with the speaker's name.
ECHO_FORMAT = "I hear you, {}."
# The part of ECHO_FORMAT every echo shares.
ECHO_PART = "I hear you"
# What the stoic says to its attacker; format with the attacker's name.
STOIC_REFUSAL_FORMAT = "Your blow cannot touch me, {}."
# What the mourner says as it dies.
MOURNER_LAST_WORDS = "My watch is ended."
# What the stubborn old man says as it refuses to die.
STUBBORN_REFUSAL = "Not yet!"
# What the answerer says when it hears speech.
ANSWER_LINE = "I heard that."
# What the parrot says as it starts its pause.
PARROT_PAUSE_LINE = "Let me think about that."
# The start of what the parrot says after its pause, before the words it heard.
PARROT_REPEAT_LINE_START = "You said: "
# What the vanisher sends its room as it removes itself.
VANISHER_LINE = "The vanisher fades away."
# The mourner's line in a room's look, his long description.
MOURNER_ROOM_LINE = "A frail mourner stands here."
# The stubborn old man's line in a room's look.
STUBBORN_ROOM_LINE = "A stubborn old man stands here."
# The ledger's action description, which examine shows once its script has run.
LEDGER_DESCRIPTION = "Columns of small figures fill the ledger."

# What the puller reads as a lever moves (do_pull, act_move.cpp); format with the lever.
PULL_FORMAT = "You pull at {}."
# What do_pull sends the hatch's room as it opens the closed hatch.
HATCH_OPENS = "The hatch opens slowly."
# What a walker reads at the closed hatch (do_move, act_move.cpp).
HATCH_CLOSED_REPLY = "The hatch seems to be closed."
# What the eater reads as it eats (do_eat, act_obj2.cpp); format with the food.
EAT_FORMAT = "You eat {}."
# What do_eat says when the eater has no such food.
NOTHING_TO_EAT_REPLY = "You don't seem to have any."
# What the drinker reads as it drinks water (do_drink, act_obj2.cpp).
DRINK_WATER_REPLY = "You drink the water."
# The start of what a rider reads as it mounts the mule (do_ride, ranger.cpp).
MOUNTED_START = "You mount a harness mule and start riding "
# What do_ride says to someone already riding.
ALREADY_RIDING_REPLY = "You are riding already."
# The part of a corpse's line in a room's look.
CORPSE_OF_PART = "corpse of"
# What the leader reads then; format with the follower, whose pronoun ends it.
LEADER_ACK_FORMAT = "ACK! {} could not follow, you lost her!"
# What a killer reads for its share of a kill (group_gain, fight.cpp).
EXPERIENCE_SHARE_PATTERN = re.compile(r"You receive your share of experience -- (\d+) points\.")

# The caster's share of the mourner's death, which group_gain (fight.cpp) reports, for the caster
# (level 10, human, alignment 0, the only killer) and the mourner (level 5, 10000 experience,
# placed by a reset row of difficulty 100, in zone 25, whose map x is 21). No term draws:
#   group_gain: level_total = GET_LEVELB(caster) + attacked_level (the caster's GET_LEVELB, set
#                             by damage()) = 10 + 10 = 20
#               share = GET_EXP(mourner) / 10 = 1000; * (killers + 1) / killers = 2000;
#                       / level_total = 100
#               group_bonus = min(share * 10 / 2, (20 - 10 - 10) * share / 4) = 0
#               base = share * 10 + group_bonus = 1000
#   exp_with_modifiers: base_exp = 1000 / max(10 + 1, 5 - 2) = 90; the mourner is within 6
#                       levels, at most level 5 (no age term), neither aggressive, fast,
#                       switching, remembering, resting nor special, and the caster is not
#                       good (alignment 0): exp = 90
#                       difficulty 100: exp = 90 * 100 / 100 = 90
#                       east bonus (a human, zone x 21 > 8): exp += 90 * min(21 - 8, 5) * 3 / 100
#                       = 13, so 103
#                       exp += 2 * 103 / max(1, 10 - 1) = 22, so 125
EXPECTED_SHARE = 125
# The stat line with experience, padded to seven places (do_stat_character, act_wiz.cpp).
EXPERIENCE_PATTERN = re.compile(r"XP: \[\s*(-?\d+)\]")
# The same stat line's alignment, padded to four places.
ALIGNMENT_PATTERN = re.compile(r"Align: \[\s*(-?\d+)\]")
# Seconds to wait for a cast or a script's wait to end: magic missile's casting time and the
# parrot's 20 passes are each under six seconds; the rest leaves room for a loaded host.
WAIT_TIMEOUT_SECONDS = 15.0
# Game hours that outlast the two hours of anger an attack on a mob gives (on_attacked_character).
ANGER_HOURS = 3


def human_warrior(name: str, sex: str = "male") -> Character:
    """Return a level-20 human warrior in the trigger hall, below the warden's level 50."""
    return Character(name=name, race="human", sex=sex, level=20, load_room=ROOM_TRIGGER_HALL, class_name="warrior")


# The mortal who walks north into the chamber.
WALKER = human_warrior("Testwalker")
# The mortal who follows the implementor north; female, so the leader's ACK line reads "her".
FOLLOWER = human_warrior("Testfollower", sex="female")
# The mortal who rides the mule north.
RIDER = human_warrior("Testrider", sex="female")
# The mortal who wears the diadem or the coronet.
WEARER = human_warrior("Testwearer")
# The mortal who pulls the lever or the crank.
PULLER = human_warrior("Testpuller")
# The mortal who examines the ledger.
READER = human_warrior("Testreader")
# The mortal who speaks and yells to the listening mobs.
SPEAKER = human_warrior("Testspeaker")
# A mortal who can eat and drink: hunger and thirst at 10, well below the limits do_eat (20) and
# do_drink (23) refuse at, and sober.
DINER = Character(
    name="Testdiner",
    race="human",
    sex="male",
    level=20,
    load_room=ROOM_TRIGGER_HALL,
    class_name="warrior",
    stored=StoredValues(hunger=10, thirst=10, drunk=0),
)
# A level-10 human mage who knows magic missile fully (100 practice sessions put its knowledge
# at the cap, recalc_skills in spec_pro.cpp), with alignment 0 so no alignment rule changes the
# experience of a kill (exp_with_modifiers, fight.cpp).
CASTER = Character(
    name="Testcaster",
    race="human",
    sex="female",
    level=10,
    load_room=ROOM_TRIGGER_HALL,
    class_name="mage",
    skills=(("magic_missile", 100),),
    stored=StoredValues(alignment=0),
)


def said(mob: TriggerMob, line: str) -> str:
    """Return what a player in the room reads when the mob says a line (say_to_char, act_comm.cpp).

    act() capitalises the first letter of the line it sends.
    """
    short = mob.short_description
    return f"{capitalised(short)} says '{line}'"


def death_cry(mob: TriggerMob) -> str:
    """Return what a character in the room reads from raw_kill's death_cry (fight.cpp)."""
    return DEATH_CRY_FORMAT.format(mob.short_description)


def join_the_mortal(implementor: GameSession) -> None:
    """Bring the implementor to the trigger hall; goto sets off no trigger."""
    goto(implementor, ROOM_TRIGGER_HALL, ROOM_TRIGGER_HALL_NAME)


def missile_always_kills(implementor: GameSession, mob: TriggerMob) -> str:
    """Check from the mob's stat that any magic missile that lands kills it, and return the stat.

    Whatever the missile rolls, it kills.

    Margin check, term by term:
      spell_magic_missile (mage.cpp): dam = 12 + number(1, mag_power / 6) >= 13; a saved
        missile does dam >> 1 >= 6.
      apply_spell_damage (mage.cpp): saving_throw = the mob's saving throw (stat, 0) minus the
        caster's spell penetration (get_spell_pen_value: mage level / 5.0 >= 0) <= 0, so
        damage_multiplier is 1.0 at 0, or 2.0 - 20.0 / (20.0 - saving_throw) > 1.0 below it:
        dam >= int(6 * 1.0) = 6.
      damage() (fight.cpp): the mob has no resistance or shield, so GET_HIT = 1 (stat) - dam
        <= -5 < 0; a player's blow then sets it to min(GET_HIT, -20) <= -20, and update_pos
        makes that POSITION_DEAD, since -20 <= -GET_CON / 2 = -10 / 2 = -5 (stat, Con 10).
    So die() runs on every missile that lands.
    """
    stat = read_stat(implementor, f"mob {mob.keyword}")
    hit_points = HIT_POINTS_BEFORE_GAIN_PATTERN.search(stat)
    assert hit_points is not None, stat
    current_hit = int(hit_points.group(1))
    maximum_hit = int(hit_points.group(2))
    assert (current_hit, maximum_hit) == (1, 1), stat
    assert first_number(SAVING_THROW_PATTERN, stat) == 0, stat
    assert first_number(CONSTITUTION_PATTERN, stat) == 10, stat
    return stat


def let_the_anger_pass(server) -> None:
    """Run the game hours that end the caster's anger, so that the caster can quit after the test.

    Attacking a mob gives the attacker two hours of anger (on_attacked_character,
    char_utils_combat.cpp), and do_quit (act_othe.cpp) refuses a mortal who has it. Under
    affect-hours only advanced hours wear it off (affect_update, limits.cpp).
    """
    server.advance_hours(ANGER_HOURS)


def cast_magic_missile_until_it_lands(caster: GameSession, target: TriggerMob, landed_outcomes: Sequence[str]) -> str:
    """Cast magic missile at the mob until it gets past the casting roll; return that cast's text.

    The roll (do_cast, spell_pa.cpp) fails when number(0, 100) >= 100, once in 101, with its own
    message. Each attempt costs mana, which nothing restores under affect-hours, so a run of
    failures ends in can_cast_spell's refusal, which fails the test.
    """
    outcomes = [LOST_CONCENTRATION_REPLY, NO_ENERGY_REPLY, *landed_outcomes]
    while True:
        text = reply_to(caster, f"cast 'magic missile' {target.keyword}", outcomes, timeout=WAIT_TIMEOUT_SECONDS)
        if any(outcome in text for outcome in landed_outcomes):
            return text
        assert NO_ENERGY_REPLY not in text, text
        assert LOST_CONCENTRATION_REPLY in text, text


@pytest.mark.script_triggers_movement
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WALKER)
@pytest.mark.world_resets(
    MobReset(GREETER.vnum, ROOM_TRIGGER_CHAMBER),
    ObjectReset(BELL_VNUM, ROOM_TRIGGER_CHAMBER),
)
def test_a_mob_and_a_floor_object_greet_a_walker_by_name_mob_first(server) -> None:
    """Pins a walk-in setting off the mobs' ON_ENTER scripts, then the floor objects', by name.

    Each script names the walker. trigger_room_event (script.cpp) runs the people's scripts with
    the walker as ch2, then the contents' with the walker as ch1, after do_move (act_move.cpp)
    has shown the walker the room.
    """
    walker = server.login(WALKER.name)

    moved = walker.command("north").text
    greeting = said(GREETER, GREETING_FORMAT.format(WALKER.name))
    chime = BELL_LINE_FORMAT.format(WALKER.name)
    assert ROOM_TRIGGER_CHAMBER_NAME in moved and greeting in moved and chime in moved, moved
    assert moved.index(ROOM_TRIGGER_CHAMBER_NAME) < moved.index(greeting) < moved.index(chime), moved
    assert moved.count(greeting) == 1 and moved.count(chime) == 1, moved


@pytest.mark.script_triggers_movement
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, WALKER)
@pytest.mark.world_resets(MobReset(WARDEN.vnum, ROOM_TRIGGER_CHAMBER))
def test_a_guard_turns_back_a_walker_below_its_level_and_lets_an_immortal_in(server) -> None:
    """Pins a guard's ON_BEFORE_ENTER script turning back a walker and letting an immortal in.

    A script that returns false keeps the walker out; one that does not lets the walker in.
    check_simple_move (act_move.cpp) runs the destination's ON_BEFORE_ENTER scripts before the
    move and gives up on a false return; do_move then sends nothing more. The warden refuses a
    level-20 walker and lets the level-100 implementor in.
    """
    implementor = server.login(IMPLEMENTOR.name)
    walker = server.login(WALKER.name)

    refused = walker.command("north").text
    assert WARDEN_LINE_FORMAT.format(WALKER.name) in refused, refused
    assert ROOM_TRIGGER_CHAMBER_NAME not in refused, refused
    looked = walker.command("look")
    assert looked.room_name() == ROOM_TRIGGER_HALL_NAME, looked.text

    join_the_mortal(implementor)
    admitted = implementor.command("north")
    assert admitted.room_name() == ROOM_TRIGGER_CHAMBER_NAME, admitted.text
    assert WARDEN_BARS_PART not in admitted.text, admitted.text


@pytest.mark.script_triggers_movement
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, FOLLOWER)
@pytest.mark.world_resets(MobReset(WARDEN.vnum, ROOM_TRIGGER_CHAMBER))
def test_a_guard_turns_back_a_follower_while_its_leader_goes_in(server) -> None:
    """Pins a guard refusing a follower as it refuses a walker, both sides reading the ACK lines.

    The leader passes the warden; each follower then makes the same move (do_move with
    SCMD_FOLLOW, act_move.cpp), whose check_simple_move runs the warden's script for it.
    """
    implementor = server.login(IMPLEMENTOR.name)
    follower = server.login(FOLLOWER.name)
    join_the_mortal(implementor)
    following = follower.command(f"follow {IMPLEMENTOR.name.lower()}")
    assert following.contains(NOW_FOLLOWING_FORMAT.format(IMPLEMENTOR.name)), following.text
    implementor.drain(SETTLE_SECONDS)

    implementor.send_line("north")
    followed = FOLLOWED_FORMAT.format(IMPLEMENTOR.name)
    left_behind = settled_expect(follower, [FOLLOWER_ACK, followed])
    refusal = WARDEN_LINE_FORMAT.format(FOLLOWER.name)
    assert refusal in left_behind and FOLLOWER_ACK in left_behind, left_behind
    assert left_behind.index(refusal) < left_behind.index(FOLLOWER_ACK), left_behind
    assert followed not in left_behind, left_behind
    leader_text = settled_expect(implementor, [ROOM_TRIGGER_CHAMBER_NAME])
    assert LEADER_ACK_FORMAT.format(FOLLOWER.name) in leader_text, leader_text
    looked = follower.command("look")
    assert looked.room_name() == ROOM_TRIGGER_HALL_NAME, looked.text


@pytest.mark.script_triggers_movement
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(RIDER)
@pytest.mark.world_resets(
    MobReset(MULE.vnum, ROOM_TRIGGER_HALL),
    MobReset(WARDEN.vnum, ROOM_TRIGGER_CHAMBER),
)
def test_a_guard_turns_back_a_rider(server) -> None:
    """Pins a guard refusing a rider as it refuses a walker: rider and mount stay where they are.

    do_move's riding branch (act_move.cpp) checks the rider's own step with check_simple_move,
    which runs the warden's script for the rider, before the mount's.
    """
    rider = server.login(RIDER.name)
    mounted = rider.command(f"ride {MULE.keyword}")
    assert mounted.contains(MOUNTED_START), mounted.text

    refused = rider.command("north").text
    assert WARDEN_LINE_FORMAT.format(RIDER.name) in refused, refused
    assert ROOM_TRIGGER_CHAMBER_NAME not in refused, refused
    looked = rider.command("look")
    assert looked.room_name() == ROOM_TRIGGER_HALL_NAME, looked.text
    mounted_again = rider.command(f"ride {MULE.keyword}")
    assert mounted_again.contains(ALREADY_RIDING_REPLY), mounted_again.text


@pytest.mark.script_triggers_movement
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WALKER)
@pytest.mark.world_resets(
    ObjectReset(BELL_VNUM, ROOM_TRIGGER_CHAMBER),
    ObjectReset(LANTERN_VNUM, ROOM_TRIGGER_CHAMBER),
)
def test_a_floor_object_that_removes_itself_as_someone_walks_in_leaves_the_next_one_to_react(server) -> None:
    """Fix pin (#543): a floor object whose ON_ENTER script removes it leaves the next one to react.

    The room's next floor object runs its own script. Certain only on an AddressSanitizer build.
    #543 made the next floor object run its script; before it, the loop over the room's contents
    (trigger_room_event, script.cpp) read the freed lantern for the next object. The bell is
    placed first and the lantern after it, and obj_to_room puts each new object at the head of
    the room's contents, so the lantern's script runs first.
    """
    walker = server.login(WALKER.name)

    moved = walker.command("north").text
    chime = BELL_LINE_FORMAT.format(WALKER.name)
    assert LANTERN_LINE in moved and chime in moved, moved
    assert moved.index(LANTERN_LINE) < moved.index(chime), moved
    looked = walker.command("look").text
    assert capitalised(LANTERN_SHORT_DESCRIPTION) not in looked, looked
    assert capitalised(BELL_SHORT_DESCRIPTION) in looked, looked


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WEARER)
@pytest.mark.world_resets(ObjectReset(DIADEM_VNUM, ROOM_TRIGGER_HALL))
def test_a_wear_script_that_returns_false_keeps_the_item_off(server) -> None:
    """Pins an ON_WEAR script that returns false leaving the item in the wearer's hands, not worn.

    perform_wear (act_obj2.cpp) runs the item's script after its own checks and gives up on a
    false return, before the wear message.
    """
    wearer = server.login(WEARER.name)
    picked_up = wearer.command("get diadem")
    assert picked_up.contains(GET_FORMAT.format(DIADEM_SHORT_DESCRIPTION)), picked_up.text

    refused = wearer.command("wear diadem").text
    assert DIADEM_LINE in refused, refused
    assert WEAR_HEAD_FORMAT.format(DIADEM_SHORT_DESCRIPTION) not in refused, refused
    equipment = wearer.command("equipment").text
    assert DIADEM_SHORT_DESCRIPTION not in equipment, equipment
    inventory = wearer.command("inventory")
    assert inventory.contains(DIADEM_SHORT_DESCRIPTION), inventory.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(WEARER)
@pytest.mark.world_resets(ObjectReset(CORONET_VNUM, ROOM_TRIGGER_HALL))
def test_a_wear_script_that_does_not_refuse_lets_the_item_be_worn(server) -> None:
    """Pins an ON_WEAR script that does not return false speaking, then the item worn as usual."""
    wearer = server.login(WEARER.name)
    picked_up = wearer.command("get coronet")
    assert picked_up.contains(GET_FORMAT.format(CORONET_SHORT_DESCRIPTION)), picked_up.text

    worn = wearer.command("wear coronet").text
    wear_message = WEAR_HEAD_FORMAT.format(CORONET_SHORT_DESCRIPTION)
    assert CORONET_LINE in worn and wear_message in worn, worn
    assert worn.index(CORONET_LINE) < worn.index(wear_message), worn
    equipment = wearer.command("equipment")
    assert equipment.contains(CORONET_SHORT_DESCRIPTION), equipment.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PULLER)
@pytest.mark.world_resets(ObjectReset(LEVER_VNUM, ROOM_TRIGGER_HALL))
def test_a_pull_script_speaks_and_the_lever_still_opens_its_hatch(server) -> None:
    """Pins a lever's ON_PULL script running before the pull, and an unrefused lever working.

    do_pull (act_move.cpp) runs the script first, then opens the lever's exit (value 0 room 2501,
    value 1 east) and tells the room.
    """
    puller = server.login(PULLER.name)

    pulled = puller.command("pull lever").text
    pull_message = PULL_FORMAT.format(LEVER_SHORT_DESCRIPTION)
    assert LEVER_LINE in pulled and pull_message in pulled and HATCH_OPENS in pulled, pulled
    assert pulled.index(LEVER_LINE) < pulled.index(pull_message) < pulled.index(HATCH_OPENS), pulled
    moved = puller.command("east")
    assert moved.room_name() == ROOM_LEVER_CLOSET_NAME, moved.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(PULLER)
@pytest.mark.world_resets(ObjectReset(CRANK_VNUM, ROOM_TRIGGER_HALL))
def test_a_pull_script_that_returns_false_leaves_the_hatch_shut(server) -> None:
    """Pins a lever whose ON_PULL script returns false not being pulled: its hatch stays shut."""
    puller = server.login(PULLER.name)

    refused = puller.command("pull crank").text
    assert CRANK_LINE in refused, refused
    assert PULL_FORMAT.format(CRANK_SHORT_DESCRIPTION) not in refused and HATCH_OPENS not in refused, refused
    blocked = puller.command("east")
    assert blocked.contains(HATCH_CLOSED_REPLY), blocked.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(READER)
@pytest.mark.world_resets(ObjectReset(LEDGER_VNUM, ROOM_TRIGGER_HALL))
def test_an_examine_script_speaks_before_the_description(server) -> None:
    """Pins an ON_EXAMINE_OBJECT script running as the object is examined, before its description.

    show_obj_to_char (act_info.cpp) runs the script, then shows the action description when the
    script did not refuse.
    """
    reader = server.login(READER.name)

    examined = reader.command("examine ledger").text
    assert LEDGER_LINE in examined and LEDGER_DESCRIPTION in examined, examined
    assert examined.index(LEDGER_LINE) < examined.index(LEDGER_DESCRIPTION), examined


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DINER)
@pytest.mark.world_resets(ObjectReset(CAKE_VNUM, ROOM_TRIGGER_HALL))
def test_eating_goes_ahead_when_the_food_script_returns_false(server) -> None:
    """Pins food whose ON_EAT script returns false being eaten all the same.

    do_eat (act_obj2.cpp) ignores the return.
    """
    diner = server.login(DINER.name)
    picked_up = diner.command("get cake")
    assert picked_up.contains(GET_FORMAT.format(CAKE_SHORT_DESCRIPTION)), picked_up.text

    eaten = diner.command("eat cake").text
    eat_message = EAT_FORMAT.format(CAKE_SHORT_DESCRIPTION)
    assert eat_message in eaten and CAKE_LINE in eaten, eaten
    assert eaten.index(eat_message) < eaten.index(CAKE_LINE), eaten
    eaten_again = diner.command("eat cake")
    assert eaten_again.contains(NOTHING_TO_EAT_REPLY), eaten_again.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DINER)
@pytest.mark.world_resets(ObjectReset(FLASK_VNUM, ROOM_TRIGGER_HALL))
def test_drinking_goes_ahead_when_the_drink_script_returns_false(server) -> None:
    """Pins a flask whose ON_DRINK script returns false being drunk from: its one drink is gone.

    do_drink (act_obj2.cpp) ignores the return and empties what was drunk after the script.
    """
    diner = server.login(DINER.name)
    picked_up = diner.command("get flask")
    assert picked_up.contains(GET_FORMAT.format(FLASK_SHORT_DESCRIPTION)), picked_up.text

    drunk = diner.command("drink flask").text
    assert DRINK_WATER_REPLY in drunk and FLASK_LINE in drunk, drunk
    assert drunk.index(DRINK_WATER_REPLY) < drunk.index(FLASK_LINE), drunk
    drunk_again = diner.command("drink flask")
    assert drunk_again.contains(DRINK_EMPTY_REPLY), drunk_again.text


@pytest.mark.script_triggers_objects
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(DINER)
@pytest.mark.world_resets(ObjectReset(BISCUIT_VNUM, ROOM_TRIGGER_HALL))
def test_food_whose_eat_script_removes_it_is_eaten_once(server) -> None:
    """Fix pin (#543): food whose ON_EAT script removes it is eaten once, and the server goes on.

    Certain only on an AddressSanitizer build. #543 made the biscuit go once;
    before it, do_eat (act_obj2.cpp) extracted the biscuit again after its script had freed it.
    """
    diner = server.login(DINER.name)
    picked_up = diner.command("get biscuit")
    assert picked_up.contains(GET_FORMAT.format(BISCUIT_SHORT_DESCRIPTION)), picked_up.text

    eaten = diner.command("eat biscuit").text
    eat_message = EAT_FORMAT.format(BISCUIT_SHORT_DESCRIPTION)
    assert eat_message in eaten and BISCUIT_LINE in eaten, eaten
    assert eaten.index(eat_message) < eaten.index(BISCUIT_LINE), eaten
    eaten_again = diner.command("eat biscuit")
    assert eaten_again.contains(NOTHING_TO_EAT_REPLY), eaten_again.text


@pytest.mark.script_triggers_fights
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER)
@pytest.mark.world_resets(MobReset(STOIC.vnum, ROOM_TRIGGER_HALL))
def test_a_damage_script_that_returns_false_stops_a_spells_blow(server) -> None:
    """Pins a mob whose ON_DAMAGE script returns false taking nothing from a spell's blow.

    damage() (fight.cpp) runs the victim's script with the attacker as ch2 before it takes the
    hit points, and returns at once on a false return. Whatever magic missile rolled, the stoic
    keeps all 200 hit points.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    join_the_mortal(implementor)
    before = read_stat(implementor, f"mob {STOIC.keyword}")
    assert first_number(HIT_POINTS_BEFORE_GAIN_PATTERN, before) == 200, before

    refusal = said(STOIC, STOIC_REFUSAL_FORMAT.format(CASTER.name))
    cast_magic_missile_until_it_lands(caster, STOIC, [refusal])

    after = read_stat(implementor, f"mob {STOIC.keyword}")
    assert first_number(HIT_POINTS_BEFORE_GAIN_PATTERN, after) == 200, after
    # The blow left the caster fighting the stoic, and a fighter cannot quit.
    purged = implementor.command(f"purge {STOIC.keyword}")
    assert purged.contains(PURGED_REPLY), purged.text
    let_the_anger_pass(server)


@pytest.mark.script_triggers_fights
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER)
@pytest.mark.world_resets(MobReset(MOURNER.vnum, ROOM_TRIGGER_HALL))
def test_a_death_script_speaks_and_the_mob_still_dies_paying_experience(server) -> None:
    """Pins a mob whose ON_DIE script lets the death go ahead speaking, dying and paying its killer.

    It leaves one corpse with its belongings. die() (fight.cpp) runs the script, then group_gain
    pays the killer its share and raw_kill sends the death cry and makes the corpse. The share is
    pinned exactly (EXPECTED_SHARE).
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    join_the_mortal(implementor)
    give_new_object(implementor, RIBBON_VNUM, "ribbon", MOURNER.keyword)
    # The inputs EXPECTED_SHARE's working rests on.
    mourner_stat = missile_always_kills(implementor, MOURNER)
    mourner_level = first_number(LEVEL_PATTERN, mourner_stat)
    mourner_experience = first_number(EXPERIENCE_PATTERN, mourner_stat)
    assert (mourner_level, mourner_experience) == (5, 10000), mourner_stat
    caster_stat = read_stat(implementor, f"player {CASTER.name}")
    caster_level = first_number(LEVEL_PATTERN, caster_stat)
    caster_alignment = first_number(ALIGNMENT_PATTERN, caster_stat)
    assert (caster_level, caster_alignment) == (10, 0), caster_stat
    experience_before = first_number(EXPERIENCE_PATTERN, caster_stat)

    last_words = said(MOURNER, MOURNER_LAST_WORDS)
    mourner_cry = death_cry(MOURNER)
    killed = cast_magic_missile_until_it_lands(caster, MOURNER, [mourner_cry])
    assert last_words in killed, killed
    assert killed.index(last_words) < killed.index(mourner_cry), killed
    assert first_number(EXPERIENCE_SHARE_PATTERN, killed) == EXPECTED_SHARE, killed

    caster_stat_after = read_stat(implementor, f"player {CASTER.name}")
    experience_after = first_number(EXPERIENCE_PATTERN, caster_stat_after)
    assert experience_after - experience_before >= EXPECTED_SHARE, caster_stat_after
    looked = caster.command("look").text
    assert looked.count(CORPSE_OF_PART) == 1, looked
    assert MOURNER_ROOM_LINE not in looked, looked
    corpse = caster.command("look in corpse")
    assert corpse.contains(RIBBON_SHORT_DESCRIPTION), corpse.text
    let_the_anger_pass(server)


@pytest.mark.script_triggers_fights
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, CASTER)
@pytest.mark.world_resets(MobReset(STUBBORN.vnum, ROOM_TRIGGER_HALL))
def test_a_death_script_that_returns_false_keeps_the_mob_alive(server) -> None:
    """Pins a mob whose ON_DIE script returns false not dying: no death cry, corpse or share.

    die() (fight.cpp) gives up on a false return, ends the fight and keeps the hit points the
    script set.
    """
    implementor = server.login(IMPLEMENTOR.name)
    caster = server.login(CASTER.name)
    join_the_mortal(implementor)
    missile_always_kills(implementor, STUBBORN)

    refusal = said(STUBBORN, STUBBORN_REFUSAL)
    survived = cast_magic_missile_until_it_lands(caster, STUBBORN, [refusal])
    assert death_cry(STUBBORN) not in survived, survived
    assert EXPERIENCE_SHARE_PATTERN.search(survived) is None, survived

    after = read_stat(implementor, f"mob {STUBBORN.keyword}")
    assert first_number(HIT_POINTS_BEFORE_GAIN_PATTERN, after) == 30, after
    looked = caster.command("look").text
    assert CORPSE_OF_PART not in looked, looked
    assert STUBBORN_ROOM_LINE in looked, looked
    let_the_anger_pass(server)


@pytest.mark.script_triggers_speech
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPEAKER)
@pytest.mark.world_resets(MobReset(ECHOER.vnum, ROOM_TRIGGER_HALL))
def test_a_yell_script_answers_speech_said_in_its_room(server) -> None:
    """Pins a mob's ON_HEAR_YELL script running when someone in its room speaks, naming them.

    do_say (act_comm.cpp) sets off ON_HEAR_SAY for each mob in the room, and trigger_char_hear
    (script.cpp) runs a mob's yell script after its say script, for the same speech.
    """
    speaker = server.login(SPEAKER.name)

    spoken = speaker.command("say Is anyone there?").text
    assert said(ECHOER, ECHO_FORMAT.format(SPEAKER.name)) in spoken, spoken


@pytest.mark.script_triggers_speech
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPEAKER)
@pytest.mark.world_resets(MobReset(ECHOER.vnum, ROOM_TRIGGER_HALL))
def test_a_yell_sets_off_no_yell_script(server) -> None:
    """Pins a yell setting off no mob's script, yell script or not.

    do_gen_com (act_comm.cpp) calls no trigger.

    The yell's special procedures run for every room of the zone, and zone 25 has none. The say
    afterwards is the control: the same mob answers it.
    """
    speaker = server.login(SPEAKER.name)

    yelled = speaker.command("yell Is anyone there?").text
    assert ECHO_PART not in yelled, yelled
    spoken = speaker.command("say Is anyone there?").text
    assert said(ECHOER, ECHO_FORMAT.format(SPEAKER.name)) in spoken, spoken


@pytest.mark.script_triggers_speech
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPEAKER)
@pytest.mark.world_resets(
    MobReset(VANISHER.vnum, ROOM_TRIGGER_HALL),
    MobReset(ANSWERER.vnum, ROOM_TRIGGER_HALL),
)
def test_a_listener_that_removes_itself_leaves_the_next_listener_to_answer(server) -> None:
    """Fix pin (#543): a listener whose ON_HEAR_SAY script removes it leaves the next to answer.

    The room's next listener hears the same words. Certain only on an AddressSanitizer build.
    #543 made the next listener
    hear the words; before it, the loop over the room's people (do_say, act_comm.cpp) read the
    freed vanisher for the next listener. The vanisher is placed first and the answerer after it,
    and char_to_room (handler.cpp) adds each newcomer at the end of the room's people, so the
    vanisher hears first.
    """
    speaker = server.login(SPEAKER.name)

    spoken = speaker.command("say Hello?").text
    answer = said(ANSWERER, ANSWER_LINE)
    assert VANISHER_LINE in spoken and answer in spoken, spoken
    assert spoken.index(VANISHER_LINE) < spoken.index(answer), spoken
    looked = speaker.command("look").text
    assert capitalised(VANISHER.short_description) not in looked, looked
    assert capitalised(ANSWERER.short_description) in looked, looked


@pytest.mark.script_triggers_speech
@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(SPEAKER)
@pytest.mark.world_resets(MobReset(PARROT.vnum, ROOM_TRIGGER_HALL))
def test_a_mob_repeats_what_it_heard_after_a_pause_not_what_was_said_next(server) -> None:
    """Fix pin (#555): a mob that pauses after hearing speech repeats the words it heard.

    It does so though the speaker said something else meanwhile. #555 made set_script_text
    (script.cpp) copy the heard words into the parrot's str1; before it, str1 pointed at the text
    block the say was parsed into, which the second say's empty target overwrote, so the parrot
    said "You said: " with nothing after it.
    The parrot is waiting during the second say, so that say sets off no script.
    """
    speaker = server.login(SPEAKER.name)

    spoken = speaker.command("say marigold").text
    assert said(PARROT, PARROT_PAUSE_LINE) in spoken, spoken
    spoken_again = speaker.command("say tulip").text
    parrot_says = f"{capitalised(PARROT.short_description)} says"
    assert "You say 'tulip'" in spoken_again and parrot_says not in spoken_again, spoken_again

    # The repeat's start without said()'s closing quote, so it matches whatever words follow.
    repeat_start = said(PARROT, PARROT_REPEAT_LINE_START)[:-1]
    repeated = settled_expect(speaker, [repeat_start], timeout=WAIT_TIMEOUT_SECONDS)
    assert said(PARROT, f"{PARROT_REPEAT_LINE_START}marigold") in repeated, repeated
