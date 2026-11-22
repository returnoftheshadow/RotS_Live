"""Follower rent cycles: an orc leader's geared, mounted recruits and led wargs come back as they
left through rent, login, a fight and a service stop."""

from __future__ import annotations

import re
from collections import Counter
from typing import NamedTuple

import pytest

from rots_harness.characters import IMPLEMENTOR, Character, StoredValues
from rots_harness.replies import reply_to
from rots_harness.session import CHARACTER_MENU_PROMPT, GameSession
from rots_harness.text_lines import capitalised, first_number
from rots_harness.world_resets import MobReset

pytestmark = [pytest.mark.scenario, pytest.mark.follower_rent]

# The inn, east to the yard, where the innkeeper stands and the leader rents.
ROOM_INN = 2700
# The yard, between the inn (west) and the track (east): the leader's load room.
ROOM_YARD = 2701
# The track, west to the yard, where the dummy is killed.
ROOM_TRACK = 2702
# The inn's name, which the leader reads first as it walks in.
INN_NAME = "Warband Inn"
# The yard's name, as above.
YARD_NAME = "Warband Yard"
# The track's name, as above.
TRACK_NAME = "Warband Track"
# The harness innkeeper (mob/11.mob), bound to the receptionist special by its vnum.
INNKEEPER_VNUM = 1105

# A level-48 orc warrior in the yard. Hunger and thirst are off so the advanced hours send no
# hunger lines.
LEADER = Character(
    name="Testwarlord",
    race="orc",
    sex="male",
    level=48,
    load_room=ROOM_YARD,
    class_name="warrior",
    stored=StoredValues(str=18, int=12, wil=12, dex=14, con=14, lea=14, hunger=-1, thirst=-1),
)
# The leader's name as a lookup keyword: the implementor's race is god, so names match.
LEADER_KEYWORD = LEADER.name.lower()


class Recruit(NamedTuple):
    """One level-16 orc recruit of zone 27, the weapon it is given and the wolf it rides."""

    # The vnum load mob takes (mob/27.mob).
    vnum: int
    # Its own keyword, which no other fixture answers to.
    keyword: str
    # Its short description, as every observer reads it.
    name: str
    # Its two-handed weapon's vnum (obj/27.obj).
    weapon_vnum: int
    # Its weapon's own keyword.
    weapon_keyword: str
    # Its weapon's short description.
    weapon_name: str
    # Its wolf's vnum (mob/27.mob).
    mount_vnum: int
    # Its wolf's own keyword.
    mount_keyword: str
    # Its wolf's short description.
    mount_name: str


class Warg(NamedTuple):
    """One of the two wargs the leader leads."""

    # The vnum load mob takes (mob/27.mob).
    vnum: int
    # Its own keyword.
    keyword: str
    # Its short description.
    name: str


# The three recruits, each with its own weapon and wolf.
RECRUITS = (
    Recruit(2700, "gashface", "a gash-faced recruit", 2715, "cleaver", "a jagged cleaver", 2703, "dun", "a dun wolf"),
    Recruit(2701, "squinteye", "a squint-eyed recruit", 2716, "waraxe", "a notched waraxe", 2704, "ashen", "an ashen wolf"),
    Recruit(2702, "broadback", "a broad-backed recruit", 2717, "greataxe", "a black greataxe", 2705, "sable", "a sable wolf"),
)
# The two wargs.
WARGS = (Warg(2706, "tawny", "a tawny warg"), Warg(2707, "brindled", "a brindled warg"))
# The armour each recruit is given (obj/27.obj 2700 to 2714), one item for each slot it fills.
ARMOUR_VNUMS = range(2700, 2715)
# The leader's armband (2718), so rent has something to store (zone-27.md).
ARMBAND_VNUM = 2718
# The practice dummy, the fight's target.
DUMMY_VNUM = 2708
# The dummy's short description.
DUMMY_NAME = "a practice dummy"
# The keyword every recruit, wolf and warg answers to, and nothing else.
WARBAND_KEYWORD = "warhost"
# The keyword every zone-27 object answers to, and nothing else.
GEAR_KEYWORD = "gearmark"
# A keyword nothing answers to.
NO_MATCH_KEYWORD = "nosuchthing"
# What an immortal's `where` answers when nothing matches (perform_immort_where, act_info.cpp).
NOTHING_FOUND_MARKER = "Couldn't find any such thing."

# What `look <recruit>` lists before the weapon once it has worn its gear, in order.
ARMOUR_BEFORE_WIELD = (
    ("<worn on finger>", "an iron ring"),
    ("<worn on finger>", "a bone ring"),
    ("<worn around neck>", "a tooth necklace"),
    ("<worn around neck>", "an iron collar"),
    ("<worn on body>", "a rusted hauberk"),
    ("<worn on head>", "a dented helm"),
    ("<worn on legs>", "a pair of hide greaves"),
    ("<worn on feet>", "a pair of hobnailed boots"),
    ("<worn on hands>", "a pair of spiked gauntlets"),
    ("<worn on arms>", "a pair of scale vambraces"),
    ("<worn about body>", "a ragged black cloak"),
    ("<worn about waist>", "a studded belt"),
    ("<worn around wrist>", "a bronze bracer"),
    ("<worn around wrist>", "a leather wristband"),
)
# What `look <recruit>` lists after the weapon.
ARMOUR_AFTER_WIELD = (("<worn on back>", "a hide pack"),)
# The weapon's label while it is held in both hands (show_equipment_to_char, act_info.cpp).
TWO_HANDED_LABEL = "<wielded two-handed>"
# The weapon's label otherwise.
ONE_HANDED_LABEL = "<wielded>"

# The formats below take short descriptions capitalised as act() starts a line.
# What the leader reads as a mob starts to follow it (add_follower, handler.cpp).
STARTS_FOLLOWING_FORMAT = "{} starts following you."
# What the leader reads as a recruit joins it (do_recruit, act_othe.cpp).
JOINS_COMPANY_FORMAT = "{} joins the company of " + LEADER.name + "."
# What the leader reads as it leads a warg (do_lead, act_move.cpp).
LEADS_FORMAT = "You grab {} and start leading him."
# What the leader reads as a recruit wields its weapon (wear_message, act_obj2.cpp).
WIELDS_FORMAT = "{} wields {}."
# What the leader reads as a recruit takes its weapon in both hands (do_twohand, fight.cpp).
BOTH_HANDS_FORMAT = "{} takes his weapon with both hands."
# What the leader reads as a recruit mounts its wolf (do_ride, ranger.cpp).
MOUNTS_FORMAT = "{} mounts {} and starts riding him."
# What the leader reads at login as a wolf follows its recruit (add_follower, handler.cpp).
NOW_FOLLOWS_FORMAT = "{} now follows {}."
# What the leader reads as a recruit assists it (do_assist, act_offe.cpp).
ASSISTS_FORMAT = "{} assists you!"
# What the leader reads as a recruit strikes the dummy (do_hit, act_offe.cpp).
ATTACKS_DUMMY_FORMAT = "{} attacks a practice dummy!"
# What the leader reads as it strikes the dummy (do_hit).
LEADER_ATTACKS_MARKER = "You attack a practice dummy!"
# What the room reads as the dummy dies (damage, fight.cpp).
DUMMY_DEAD_MARKER = "A practice dummy is dead!  R.I.P."
# What the leader reads as it wears the armband (wear_message, act_obj2.cpp).
ARMBAND_WORN_MARKER = "You wear a red armband on your arms."
# The end of a successful order (do_order, act_offe.cpp).
ORDER_OK_MARKER = "Ok."
# do_order's refusals.
ORDER_REFUSAL_MARKERS = [
    "Order who to do what?",
    "That person isn't here.",
    "has an indifferent look.",
    "Nobody here is willing to carry out your orders!",
]
# do_recruit's refusals.
RECRUIT_REFUSAL_MARKERS = [
    "You can't recruit.",
    "Recruit who?",
    "doesn't want to be recruited.",
    "That target is too high a level to recruit.",
    "You have already recruited too powerful a force",
    "Your target",
    "You are too busy to do that.",
    "Sorry, following in circles is not allowed.",
]
# do_lead's refusals.
LEAD_REFUSAL_MARKERS = ["There is nobody by that name.", "You can not lead this.", "is already following", "doesn't want you to lead"]
# do_hit's refusals.
HIT_REFUSAL_MARKERS = ["They aren't here.", "A peaceful feeling overwhelms you", "You do the best you can!"]
# What a parried, dodged or evaded blow shows its attacker's room (do_parry, do_dodge, do_evade).
MISSED_BLOW_MARKERS = ["deflects", "dodges", "is not where", "distracts"]
# What a walker reads when the way is shut (do_move, act_move.cpp).
NO_WAY_MARKER = "You cannot go that way."

# What the renter reads as the receptionist stores its belongings (gen_receptionist, objsave.cpp).
RENTED_MARKER = "The harness innkeeper stores your belongings and helps you into your private chamber."
# What an angry renter reads from a receptionist of race 1 (gen_receptionist).
TOO_ANGRY_MARKER = "The harness innkeeper tells you, 'You're too angry.  Come back when you've cooled down.'"
# How every answer the innkeeper tells the renter starts.
INNKEEPER_TELLS_FRAGMENT = "The harness innkeeper tells you, '"
# The game hours of anger the leader's blow gives (on_attacked_character, char_utils_combat.cpp).
ANGER_HOURS = 2
# The anger affect's line in `stat player` (do_stat_character, act_wiz.cpp).
ANGER_AFFECT_PATTERN = re.compile(r"^\s*SPL: \(\s*-?\d+hr\) anger", re.MULTILINE)

# One line of the immortal `where <keyword>` (perform_immort_where, act_info.cpp): name, room
# vnum, and the room's name or `>> Carried by <holder>`.
WHERE_LINE_PATTERN = re.compile(r"^\s*\d+\. (.+?)\s+- \[\s*(-?\d+)\] (.*?)\s*$", re.MULTILINE)
# How an object's `where` line starts its place when someone holds or wears it.
CARRIED_BY_PREFIX = ">> Carried by "
# A `look <character>` equipment line: the slot label, then the item.
EQUIPMENT_LINE_PATTERN = re.compile(r"^\s*(<[^>]+>)[ \t]+(.+?)\s*$", re.MULTILINE)
# What `look <character>` prints before the equipment lines.
EQUIPMENT_HEADER = "is using:"
# The `stat` room line (do_stat_character, act_wiz.cpp).
IN_ROOM_PATTERN = re.compile(r"In room \[\s*(-?\d+)\]")
# The `stat` master and followers, which can wrap onto further lines.
MASTER_PATTERN = re.compile(r"Master is: (.*?), Followers are:(.*?)\n\s*Delay command", re.DOTALL)
# The `stat` inventory and equipment counts.
ITEMS_PATTERN = re.compile(r"Items in: inventory: (\d+), eq: (\d+)")
# The `stat` affect bits.
AFFECT_BITS_PATTERN = re.compile(r"^\s*AFF: (.*)$", re.MULTILINE)
# The `stat` real OB (get_real_OB), the first `OB:` number.
REAL_OB_PATTERN = re.compile(r"OB: (-?\d+)\(")
# The `stat` real dodge (get_real_dodge).
DODGE_PATTERN = re.compile(r"dodge: (-?\d+)\(")
# The `stat` real parry (get_real_parry).
PARRY_PATTERN = re.compile(r"parry: (-?\d+)\(")
# The `stat` energy.
ENERGY_PATTERN = re.compile(r"ENERGY: (-?\d+),")
# The `stat` damage field (points.damage).
DAMAGE_PATTERN = re.compile(r", damage: (-?\d+),")
# The `stat` current strength.
STRENGTH_PATTERN = re.compile(r"Str:\[(-?\d+)/")
# The `stat` current constitution; `\s*` covers the space after "Con:".
CONSTITUTION_PATTERN = re.compile(r"Con:\s*\[(-?\d+)/")
# The `stat` current and maximum hit points.
HIT_POINTS_PATTERN = re.compile(r"HP :\[(-?\d+)/(-?\d+)\+")
# The `stat` resistances and vulnerabilities.
RESISTANCES_PATTERN = re.compile(r"RES: (.*?) VUL: (.*?)\s*$", re.MULTILINE)
# The `stat obj` weapon damage, in tenths (get_weapon_damage, utility.cpp).
WEAPON_DAMAGE_PATTERN = re.compile(r"Damage: (-?\d+)/10")
# The `practice` line of natural attacks (SKILL_NATURAL_ATTACK in skills[], consts.cpp).
NATURAL_ATTACK_PATTERN = re.compile(r"^\s*natural attacks\s", re.MULTILINE)
# The `practice` line of find weakness (SKILL_EXTRA_DAMAGE).
FIND_WEAKNESS_PATTERN = re.compile(r"^\s*find weakness\s", re.MULTILINE)
# What `stat mob` answers when no mob matches (do_wizstat, act_wiz.cpp).
NO_SUCH_MOB_MARKER = "No such mobile around."

# Seconds to keep reading after an outcome, for text already sent to arrive.
SETTLE_SECONDS = 1.0
# Seconds to wait for a long reply (`where` lists about fifty lines).
REPLY_TIMEOUT = 10.0


def implementor_reply(implementor: GameSession, command: str) -> str:
    """The implementor's whole reply to command."""
    implementor.send_line(command)
    # `stat` and `where` lines can end like a prompt, so a `where` that matches nothing marks the end.
    implementor.send_line(f"where {NO_MATCH_KEYWORD}")
    text = implementor.expect([NOTHING_FOUND_MARKER], timeout=REPLY_TIMEOUT)
    return text.rsplit(NOTHING_FOUND_MARKER, 1)[0]


def matched_groups(pattern: re.Pattern, text: str) -> tuple:
    """The groups of pattern's first match in text."""
    match = pattern.search(text)
    assert match is not None, text
    return match.groups()


def affect_bits(stat_text: str) -> list:
    """The affect bit names on a `stat` reply's AFF line."""
    match = AFFECT_BITS_PATTERN.search(stat_text)
    assert match is not None, stat_text
    return [name.rstrip(".") for name in match.group(1).split()]


def master_and_followers(stat_text: str) -> tuple:
    """A `stat` reply's master and its followers' names, the followers as a Counter."""
    match = MASTER_PATTERN.search(stat_text)
    assert match is not None, stat_text
    followers = [name.strip() for name in match.group(2).split(",") if name.strip()]
    return match.group(1), Counter(followers)


def where_entries(implementor: GameSession, keyword: str) -> list:
    """Every `where <keyword>` entry: (name, room vnum, the room's name or the holder line)."""
    reply = implementor_reply(implementor, f"where {keyword}")
    return [(name, int(room), place) for name, room, place in WHERE_LINE_PATTERN.findall(reply)]


def warband_census(implementor: GameSession) -> Counter:
    """How many of each recruit, wolf and warg is in each room, as (name, room vnum) counts."""
    entries = where_entries(implementor, WARBAND_KEYWORD)
    return Counter((name, room) for name, room, _place in entries)


def gear_census(implementor: GameSession) -> Counter:
    """How many copies of each zone-27 object each holder has, as (name, room vnum, place)
    counts; place is `>> Carried by <holder>`, or the room's name for an object on the floor."""
    entries = where_entries(implementor, GEAR_KEYWORD)
    return Counter(entries)


def expected_warband(room: int) -> Counter:
    """The warband census with every recruit, wolf and warg once in room."""
    recruit_names = [recruit.name for recruit in RECRUITS]
    wolf_names = [recruit.mount_name for recruit in RECRUITS]
    warg_names = [warg.name for warg in WARGS]
    return Counter((name, room) for name in recruit_names + wolf_names + warg_names)


def gear_in(gear: Counter, room: int) -> Counter:
    """gear, every copy moved to room."""
    return Counter({(name, room, place): count for (name, _room, place), count in gear.items()})


def equipment(implementor: GameSession, keyword: str) -> list:
    """The (slot label, item) lines of `look <keyword>`, in the order the game lists them."""
    reply = implementor_reply(implementor, f"look {keyword}")
    assert EQUIPMENT_HEADER in reply, reply
    worn_text = reply.split(EQUIPMENT_HEADER, 1)[1]
    return EQUIPMENT_LINE_PATTERN.findall(worn_text)


def expected_equipment(recruit: Recruit, *, two_handed: bool) -> list:
    """The equipment `look` lists for a recruit dressed as the setup dresses it."""
    label = TWO_HANDED_LABEL if two_handed else ONE_HANDED_LABEL
    return [*ARMOUR_BEFORE_WIELD, (label, recruit.weapon_name), *ARMOUR_AFTER_WIELD]


def leader_order(leader: GameSession, order: str) -> str:
    """Gives an order and returns what the leader read, ending with its Ok."""
    reply = reply_to(leader, order, [ORDER_OK_MARKER, *ORDER_REFUSAL_MARKERS])
    assert not any(refusal in reply for refusal in ORDER_REFUSAL_MARKERS), reply
    return reply


def walk(leader: GameSession, direction: str, room_name: str) -> None:
    """Walks the leader one room; every follower's step runs in the same pass."""
    arrived = reply_to(leader, direction, [room_name, NO_WAY_MARKER])
    assert room_name in arrived, arrived


def load_warband(implementor: GameSession) -> None:
    """Loads the recruits, wolves and wargs into the implementor's room, the yard."""
    mob_vnums = [recruit.vnum for recruit in RECRUITS] + [recruit.mount_vnum for recruit in RECRUITS] + [warg.vnum for warg in WARGS]
    for vnum in mob_vnums:
        created = implementor.command(f"load mob {vnum}")
        assert created.contains("You create"), created.text


def recruit_and_lead(leader: GameSession) -> None:
    """The leader recruits the three recruits and leads the two wargs; asserts each joins."""
    for recruit in RECRUITS:
        leader.send_line(f"recruit {recruit.keyword}")
        recruit_name = capitalised(recruit.name)
        joined = JOINS_COMPANY_FORMAT.format(recruit_name)
        following = STARTS_FOLLOWING_FORMAT.format(recruit_name)
        reply = leader.expect([joined, *RECRUIT_REFUSAL_MARKERS]) + leader.drain(SETTLE_SECONDS)
        assert joined in reply and following in reply, reply

    for warg in WARGS:
        leader.send_line(f"lead {warg.keyword}")
        led = LEADS_FORMAT.format(warg.name)
        warg_name = capitalised(warg.name)
        following = STARTS_FOLLOWING_FORMAT.format(warg_name)
        reply = leader.expect([led, *LEAD_REFUSAL_MARKERS]) + leader.drain(SETTLE_SECONDS)
        assert led in reply and following in reply, reply


def dress_and_mount(implementor: GameSession, leader: GameSession) -> None:
    """Gives each recruit its gear; the leader orders it to wield, wear all and ride its wolf."""
    for recruit in RECRUITS:
        for vnum in (*ARMOUR_VNUMS, recruit.weapon_vnum):
            created = implementor.command(f"load obj {vnum}")
            assert created.contains("You create"), created.text
        given = implementor.command(f"give all {recruit.keyword}")
        assert given.text.count(f"to {recruit.name}.") == len(ARMOUR_VNUMS) + 1, given.text
        recruit_name = capitalised(recruit.name)
        wielded = leader_order(leader, f"order {recruit.keyword} wield {recruit.weapon_keyword}")
        assert WIELDS_FORMAT.format(recruit_name, recruit.weapon_name) in wielded, wielded
        assert BOTH_HANDS_FORMAT.format(recruit_name) in wielded, wielded
        leader_order(leader, f"order {recruit.keyword} wear all")
        mounted = leader_order(leader, f"order {recruit.keyword} ride {recruit.mount_keyword}")
        assert MOUNTS_FORMAT.format(recruit_name, recruit.mount_name) in mounted, mounted


def give_leader_armband(implementor: GameSession, leader: GameSession) -> None:
    """Hands the leader its armband to wear."""
    created = implementor.command(f"load obj {ARMBAND_VNUM}")
    assert created.contains("You create a red armband."), created.text
    given = implementor.command(f"give armband {LEADER_KEYWORD}")
    assert given.contains(f"You give a red armband to {LEADER.name}."), given.text
    worn = reply_to(leader, "wear armband", [ARMBAND_WORN_MARKER, "You don't seem to have"])
    assert ARMBAND_WORN_MARKER in worn, worn


def check_warband(implementor: GameSession, room: int, gear: Counter, *, riding: bool, two_handed: bool) -> None:
    """Asserts the leader stands in room with every follower once and every item as gear has it.

    Each recruit follows the leader, carries nothing and wears its setup's gear in its setup's
    slots; it rides its wolf, or the wolf follows it. Each warg follows the leader.
    """
    leader_stat = implementor_reply(implementor, f"stat player {LEADER_KEYWORD}")
    assert first_number(IN_ROOM_PATTERN, leader_stat) == room, leader_stat
    followers = Counter([recruit.name for recruit in RECRUITS] + [warg.name for warg in WARGS])
    assert master_and_followers(leader_stat) == ("<none>", followers), leader_stat

    census = warband_census(implementor)
    assert census == expected_warband(room), f"not one of each band mob in {room}: {census}"
    assert gear_census(implementor) == gear

    room_look = implementor_reply(implementor, "look")
    for recruit in RECRUITS:
        recruit_stat = implementor_reply(implementor, f"stat mob {recruit.keyword}")
        mount_followers = Counter() if riding else Counter([recruit.mount_name])
        assert master_and_followers(recruit_stat) == (LEADER.name, mount_followers), recruit_stat
        assert matched_groups(ITEMS_PATTERN, recruit_stat) == ("0", str(len(ARMOUR_VNUMS) + 1)), recruit_stat
        assert ("TWO-HANDED" in affect_bits(recruit_stat)) == two_handed, recruit_stat
        assert equipment(implementor, recruit.keyword) == expected_equipment(recruit, two_handed=two_handed)

        mount_stat = implementor_reply(implementor, f"stat mob {recruit.mount_keyword}")
        mount_master = "<none>" if riding else recruit.name
        assert master_and_followers(mount_stat) == (mount_master, Counter()), mount_stat
        riding_line = f"{capitalised(recruit.name)} is riding on {recruit.mount_name}."
        assert (riding_line in room_look) == riding, room_look

    for warg in WARGS:
        warg_stat = implementor_reply(implementor, f"stat mob {warg.keyword}")
        assert master_and_followers(warg_stat) == (LEADER.name, Counter()), warg_stat


def check_room_of_everyone(implementor: GameSession, room: int, gear: Counter) -> None:
    """Asserts every recruit, wolf, warg and item has walked into room with the leader."""
    leader_stat = implementor_reply(implementor, f"stat player {LEADER_KEYWORD}")
    assert first_number(IN_ROOM_PATTERN, leader_stat) == room, leader_stat
    assert warband_census(implementor) == expected_warband(room)
    assert gear_census(implementor) == gear_in(gear, room)


def walk_out_and_back(implementor: GameSession, leader: GameSession, gear: Counter) -> None:
    """Walks the leader from the inn to the track and back, checking that everyone followed."""
    walk(leader, "east", YARD_NAME)
    walk(leader, "east", TRACK_NAME)
    check_room_of_everyone(implementor, ROOM_TRACK, gear)
    walk(leader, "west", YARD_NAME)
    walk(leader, "west", INN_NAME)
    check_room_of_everyone(implementor, ROOM_INN, gear)


def rent(leader: GameSession) -> None:
    """Rents at the innkeeper and asserts the rent ended the leader's play at the character menu."""
    reply = reply_to(leader, "rent", [CHARACTER_MENU_PROMPT, INNKEEPER_TELLS_FRAGMENT])
    assert RENTED_MARKER in reply and CHARACTER_MENU_PROMPT in reply, reply
    leader.close()


def log_leader_in(server) -> GameSession:
    """Logs the leader in again and asserts it read every follower come back with it."""
    leader = server.login(LEADER.name)
    arrival = leader.everything
    for recruit in RECRUITS:
        assert STARTS_FOLLOWING_FORMAT.format(capitalised(recruit.name)) in arrival, arrival
        assert NOW_FOLLOWS_FORMAT.format(capitalised(recruit.mount_name), recruit.name) in arrival, arrival

    for warg in WARGS:
        assert STARTS_FOLLOWING_FORMAT.format(capitalised(warg.name)) in arrival, arrival
    return leader


def mount_and_two_hand(leader: GameSession) -> None:
    """Orders every recruit to ride the wolf that follows it and to hold its weapon two-handed.

    do_ride with no argument (ranger.cpp) takes the rider's first MOB_MOUNT follower in its room;
    the wargs are not charmed, so `order followers` passes them over (do_order, act_offe.cpp).
    """
    mounted = leader_order(leader, "order followers ride")
    two_handed = leader_order(leader, "order followers twohanded")
    for recruit in RECRUITS:
        assert MOUNTS_FORMAT.format(capitalised(recruit.name), recruit.mount_name) in mounted, mounted
        assert BOTH_HANDS_FORMAT.format(capitalised(recruit.name)) in two_handed, two_handed


def stat_values(text: str, *patterns: re.Pattern) -> tuple:
    """The first number of each pattern in a `stat` reply, in order."""
    return tuple(first_number(pattern, text) for pattern in patterns)


def check_fight_margins(implementor: GameSession, leader: GameSession) -> None:
    """Asserts the numbers that make the fight's outcome independent of every roll."""
    leader_stat = implementor_reply(implementor, f"stat player {LEADER_KEYWORD}")
    assert stat_values(leader_stat, REAL_OB_PATTERN, STRENGTH_PATTERN, DAMAGE_PATTERN, ENERGY_PATTERN) == (78, 18, 0, 1200), leader_stat
    assert "TWO-HANDED" not in affect_bits(leader_stat), leader_stat
    assert matched_groups(ITEMS_PATTERN, leader_stat) == ("0", "1"), leader_stat
    practice = leader.command("practice").text
    assert not NATURAL_ATTACK_PATTERN.search(practice) and not FIND_WEAKNESS_PATTERN.search(practice), practice
    for recruit in RECRUITS:
        recruit_stat = implementor_reply(implementor, f"stat mob {recruit.keyword}")
        assert stat_values(recruit_stat, REAL_OB_PATTERN, STRENGTH_PATTERN, DAMAGE_PATTERN, ENERGY_PATTERN) == (49, 18, 20, 1200), recruit_stat
        assert "TWO-HANDED" in affect_bits(recruit_stat), recruit_stat
        weapon_stat = implementor_reply(implementor, f"stat obj {recruit.weapon_keyword}")
        assert f"Carried by: {recruit.name}," in weapon_stat, weapon_stat
        assert first_number(WEAPON_DAMAGE_PATTERN, weapon_stat) == 54, weapon_stat
    dummy_stat = implementor_reply(implementor, "stat mob dummy")
    assert matched_groups(HIT_POINTS_PATTERN, dummy_stat) == ("75", "75"), dummy_stat
    assert stat_values(dummy_stat, DODGE_PATTERN, PARRY_PATTERN, CONSTITUTION_PATTERN) == (0, 0, 2), dummy_stat
    assert "Pos: Standing," in dummy_stat and affect_bits(dummy_stat) == ["<NONE>"], dummy_stat
    assert matched_groups(RESISTANCES_PATTERN, dummy_stat) == ("<NONE>", "<NONE>"), dummy_stat
    # The blows: do_hit, hit and damage in fight.cpp and act_offe.cpp, in the code's order. The
    # dummy has no AFF_EVASION, wears nothing (armor_effect), resists nothing and has no special;
    # no attacker has a specialization. OB is get_real_OB, the first `OB:` number of stat.
    #
    # The leader's blow, at most (OB: 78, Str 18, damage: 0, ENERGY 1200; no weapon, and neither
    # natural attacks nor find weakness in `practice`):
    #   tmp = number(1, 35)                                       <= 35
    #   OB  = get_real_OB + number(1, 55 + OB / 4) + tmp          <= 78 + (55 + 19) + 35 = 187
    #   OB -= OB / 8                                              <= 187 - 23 = 164
    #   OB -= 40, then OB += 100 when tmp is 35                   <= 224
    #   OB -= dodge_malus = get_real_dodge + evasion = 0 + 0      (the dummy's dodge: 0)
    #   OB += 10 * (POSITION_FIGHTING - pos) only below fighting: + 0 (Pos: Standing)
    #   OB -= get_real_parry * c_parry / 100 = 0                  (the dummy's parry: 0)
    #   dam = natural_attack_dam = BAREHANDED_DAMAGE * 10 = 20, + GET_DAMAGE * 10 = 0
    #   dam = dam * (OB + 100) * (10000 + roll * roll + 1 * 133 * GET_BAL_STR) / 13300000
    #       <= 20 * 324 * (10000 + 100 * 100 + 133 * 18) / 13300000 = 145113120 / 13300000 = 10
    #       (one-handed, not TWO-HANDED; Str 18 is below the orc's 22, so GET_BAL_STR is 18)
    #   check_find_weakness: chance = find weakness 0 / 3 * War / 30 + 0 (War is at most 30) = 0
    #   so the dummy keeps 75 - 10 = 65 hit points or more: the leader's blow alone cannot kill.
    # The leader's blow, at least: OB >= (78 + 1 + 1) - 80 / 8 - 40 - 0 - 0 = 30, not below 0, so it
    #   lands, and dam >= 20 * 130 * (10000 + 0 + 133 * 18) / 13300000 = 32224400 / 13300000 = 2.
    # Each recruit's blow, at least (OB: 49, Str 18, damage: 20, ENERGY 1200, which ENE_TO_HIT 1200
    # allows; TWO-HANDED; its weapon's Damage: 54/10):
    #   OB  >= 49 + 1 + 1 = 51, - 51 / 8 = 45, - 40 = 5, - dodge 0, - parry 0 = 5: it lands
    #   dam  = get_weapon_damage / 2 (a mob's) + GET_DAMAGE * 10 = 54 / 2 + 20 * 10 = 227
    #   dam >= 227 * (5 + 100) * (10000 + 0 + 2 * 133 * 18) / 13300000 = 352471980 / 13300000 = 26
    #   (a dummy below fighting adds dam / 2, and damage() caps a blow at 200: neither lowers 26)
    #   The leader's blow and the three recruits' leave the dummy at 75 - 2 - 3 * 26 = -5 or lower,
    #   past the -Con / 2 = -2 / 2 = -1 at which update_pos makes a mob dead: the third recruit's
    #   blow kills at the latest, and an earlier one may.
    # No product overflows a 32-bit int: the recruit's largest OB is 49 + (55 + 12) + 35 = 151,
    #   - 151 / 8 = 133, - 40, + 100 = 193, + 10 * (7 - 2) = 243 against a dummy left at 0 hit points
    #   (incapacitated), and 227 * 343 * (10000 + 100 * 100 + 2 * 133 * 18) = 1930018468, below
    #   2147483647.
    assert 20 * (78 + 74 + 35 - (78 + 74 + 35) // 8 - 40 + 100 + 100) * (20000 + 133 * 18) // 13300000 == 10
    assert 20 * (80 - 80 // 8 - 40 + 100) * (10000 + 133 * 18) // 13300000 == 2
    assert (54 // 2 + 20 * 10) * (51 - 51 // 8 - 40 + 100) * (10000 + 2 * 133 * 18) // 13300000 == 26
    assert 75 - 2 - 3 * 26 <= -(2 // 2)
    assert (54 // 2 + 20 * 10) * (151 - 151 // 8 - 40 + 100 + 50 + 100) * (20000 + 2 * 133 * 18) < 2**31 - 1


def fight(implementor: GameSession, leader: GameSession) -> None:
    """The leader attacks the dummy and orders its followers to assist; asserts it dies.

    do_assist (act_offe.cpp) hits whoever the leader fights. A recruit whose turn comes after the
    dummy died reads `But nobody is fighting him!`, which the leader does not see.
    """
    check_fight_margins(implementor, leader)
    attacked = reply_to(leader, "hit dummy", [LEADER_ATTACKS_MARKER, *HIT_REFUSAL_MARKERS])
    assert LEADER_ATTACKS_MARKER in attacked and DUMMY_DEAD_MARKER not in attacked, attacked
    assisted = leader_order(leader, f"order followers assist {LEADER_KEYWORD}")
    assert assisted.count(DUMMY_DEAD_MARKER) == 1, assisted
    before_death, after_death = assisted.split(DUMMY_DEAD_MARKER)
    assisting = [recruit for recruit in RECRUITS if ASSISTS_FORMAT.format(capitalised(recruit.name)) in before_death]
    assert assisting, assisted
    for recruit in assisting:
        assert ATTACKS_DUMMY_FORMAT.format(capitalised(recruit.name)) in before_death, assisted
    assert not any(missed in assisted for missed in MISSED_BLOW_MARKERS), assisted
    assert "assists you!" not in after_death, assisted
    gone = implementor_reply(implementor, "stat mob dummy")
    assert NO_SUCH_MOB_MARKER in gone, gone


def check_anger_and_wait_it_out(server, implementor: GameSession, leader: GameSession) -> None:
    """Asserts the receptionist refuses the angry leader, then advances the hours its anger lasts."""
    angry = implementor_reply(implementor, f"stat player {LEADER_KEYWORD}")
    assert ANGER_AFFECT_PATTERN.search(angry), angry
    refused = reply_to(leader, "rent", [INNKEEPER_TELLS_FRAGMENT, CHARACTER_MENU_PROMPT])
    assert TOO_ANGRY_MARKER in refused and CHARACTER_MENU_PROMPT not in refused, refused
    server.advance_hours(ANGER_HOURS + 1)
    leader.drain(SETTLE_SECONDS)
    implementor.drain(SETTLE_SECONDS)
    calm = implementor_reply(implementor, f"stat player {LEADER_KEYWORD}")
    assert not ANGER_AFFECT_PATTERN.search(calm), calm


@pytest.mark.schedule("affect-hours")
@pytest.mark.characters(IMPLEMENTOR, LEADER)
@pytest.mark.world_resets(MobReset(INNKEEPER_VNUM, ROOM_INN))
def test_a_mounted_geared_warband_comes_back_through_rent_a_fight_and_a_service_stop(server) -> None:
    """Pins that an orc leader's followers come back as they left, through three rents and a
    service stop.

    The leader leads two wargs and recruits three orcs, each in fifteen items, wielding its own
    weapon two-handed and riding its own wolf. After each login every follower is back once,
    the recruits in the same gear, each wolf following its recruit unridden and no weapon held
    two-handed: neither is saved. Before the last rent the band kills a dummy and the server is
    stopped and booted again; the leader's anger makes the innkeeper refuse it until the hours
    pass. The wolves' part is a fix pin: #570 made rent take out the wolf a recruit rides when
    its save records it; before it, the wolf stayed in the inn and login brought a second. Under
    affect-hours only the scenario's commands draw: recruiting, leading, riding and renting draw
    nothing, a recruit's step ahead of its leader draws only the order of the arrival lines, and
    the fight cannot turn on a roll (check_fight_margins).
    """
    implementor = server.login(IMPLEMENTOR.name)
    leader = server.login(LEADER.name)
    arrived = implementor.command(f"goto {ROOM_YARD}")
    assert arrived.room_name() == YARD_NAME, arrived.text

    load_warband(implementor)
    recruit_and_lead(leader)
    dress_and_mount(implementor, leader)
    give_leader_armband(implementor, leader)

    for recruit in RECRUITS:
        assert equipment(implementor, recruit.keyword) == expected_equipment(recruit, two_handed=True)
    setup_gear = gear_census(implementor)
    gear = gear_in(setup_gear, ROOM_INN)
    assert sum(gear.values()) == len(RECRUITS) * (len(ARMOUR_VNUMS) + 1) + 1, gear
    assert all(place.startswith(CARRIED_BY_PREFIX) for _name, _room, place in gear), gear
    check_room_of_everyone(implementor, ROOM_YARD, gear)

    # Cycle 1: from the yard to the track and back to the inn, mounted and two-handed; rent.
    walk(leader, "east", TRACK_NAME)
    check_room_of_everyone(implementor, ROOM_TRACK, gear)
    walk(leader, "west", YARD_NAME)
    walk(leader, "west", INN_NAME)
    arrived = implementor.command(f"goto {ROOM_INN}")
    assert arrived.contains(INN_NAME), arrived.text
    check_warband(implementor, ROOM_INN, gear, riding=True, two_handed=True)
    rent(leader)
    leader = log_leader_in(server)
    check_warband(implementor, ROOM_INN, gear, riding=False, two_handed=False)

    # Cycle 2: mount and two-hand again, walk out and back, rent.
    mount_and_two_hand(leader)
    check_warband(implementor, ROOM_INN, gear, riding=True, two_handed=True)
    walk_out_and_back(implementor, leader, gear)
    rent(leader)
    leader = log_leader_in(server)
    check_warband(implementor, ROOM_INN, gear, riding=False, two_handed=False)

    # Cycle 3: mount and two-hand, fight on the track, come back and stop the server.
    mount_and_two_hand(leader)
    arrived = implementor.command(f"goto {ROOM_TRACK}")
    assert arrived.contains(TRACK_NAME), arrived.text
    created = implementor.command(f"load mob {DUMMY_VNUM}")
    assert created.contains(f"You create {DUMMY_NAME}."), created.text
    walk(leader, "east", YARD_NAME)
    walk(leader, "east", TRACK_NAME)
    fight(implementor, leader)
    walk(leader, "west", YARD_NAME)
    walk(leader, "west", INN_NAME)
    arrived = implementor.command(f"goto {ROOM_INN}")
    assert arrived.contains(INN_NAME), arrived.text
    check_warband(implementor, ROOM_INN, gear, riding=True, two_handed=True)

    server.restart()
    implementor = server.login(IMPLEMENTOR.name)
    leader = log_leader_in(server)
    arrived = implementor.command(f"goto {ROOM_INN}")
    assert arrived.contains(INN_NAME), arrived.text
    check_warband(implementor, ROOM_INN, gear, riding=False, two_handed=False)

    # The last rent, with each wolf following its recruit.
    check_anger_and_wait_it_out(server, implementor, leader)
    rent(leader)
    leader = log_leader_in(server)
    check_warband(implementor, ROOM_INN, gear, riding=False, two_handed=False)
