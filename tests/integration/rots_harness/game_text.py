"""Fixed game text that two or more scenario files assert and no shared helper waits for.

A name ends in _FORMAT for a format string, filled in with str.format; in _REPLY for a fixed line
that answers the reader's own command; in _START, _END or _PART for a fragment matched at a
line's start, end or middle; any other fixed line is named for what it is.
"""

from __future__ import annotations

# What force answers when the forced character is below the implementor's level, before the
# command runs (do_force, act_wiz.cpp).
FORCED_REPLY = "Ok."
# What teleport answers when no character it can see has the name (do_teleport, act_wiz.cpp).
NO_ONE_AROUND_REPLY = "No-one by that name around."
# What a word that is neither a command within the speaker's level nor a social answers
# (command_interpreter, interpre.cpp).
UNRECOGNIZED_COMMAND_REPLY = "Unrecognized command."
# What the old shop and rent commands answer where no shopkeeper or receptionist serves them
# (do_not_here, act_othe.cpp).
NOT_HERE_REPLY = "Sorry, but you can't do that here!"
# What the caster reads when it does not know the spell (can_cast_spell, spell_pa.cpp).
UNKNOWN_SPELL_REPLY = "You don't know this spell."
# What the caster reads when it does not carry the object a spell targets (report_wrong_target,
# interpre.cpp).
TARGET_NOT_CARRIED_REPLY = "You don't have that."
# What a mortal reads as it quits, before everything it carries and wears drops (do_quit,
# act_othe.cpp; "posessions" is the game's spelling).
QUIT_DROP_REPLY = "As you quit, all your posessions drop to the ground!"
# What the caster reads once a spell's casting has begun (do_cast, spell_pa.cpp).
CONCENTRATE_REPLY = "You start to concentrate."
# What hit answers when the attacker is already fighting (do_hit, act_offe.cpp).
FIGHT_ONGOING_REPLY = "You do the best you can!"
# What order answers when no follower in the room obeys (do_order, act_offe.cpp).
NOBODY_WILLING_REPLY = "Nobody here is willing to carry out your orders!"
# What eating poisoned food answers (do_eat, act_obj2.cpp).
EATEN_POISON_REPLY = "Oops, that tasted rather strange!"
# What drinking from an empty drink container answers (do_drink, act_obj2.cpp).
DRINK_EMPTY_REPLY = "It's empty."
# What an implementor's `where` answers when nothing has the name (perform_immort_where,
# act_info.cpp).
NOTHING_FOUND_REPLY = "Couldn't find any such thing."
# What the harness innkeeper answers to rent once it has stored the renter's things
# (gen_receptionist, objsave.cpp).
RENTED_REPLY = "The harness innkeeper stores your belongings and helps you into your private chamber."
# The start of each of the harness innkeeper's tells, such as a rent refusal (gen_receptionist and
# Crash_report_unrentables, objsave.cpp).
INNKEEPER_TELLS_START = "The harness innkeeper tells you, '"

# What the room reads as a character dies (damage, fight.cpp); format with its short description
# or name, capitalised as act() starts a line.
IS_DEAD_FORMAT = "{} is dead!  R.I.P."
# What the room reads of the death cry of a character with none of its own (death_cry, fight.cpp);
# format with the mob's short description or the player's name. The rooms around read it too,
# with "someone" for a mob.
DEATH_CRY_FORMAT = "Your blood freezes as you hear {}'s death cry."
# What a dead mortal reads as its spirit wakes in its start room (extract_char, handler.cpp).
NEW_BODY_MESSAGE = "Your spirit found a new body to wear."
# The header of `look in corpse` for a corpse on the floor (show_obj_contents, act_info.cpp,
# prints the container's first keyword, which make_corpse in fight.cpp sets to "corpse").
CORPSE_CONTENTS_HEADER = "corpse (here) :"
# The header of inventory's list (do_inventory, act_info.cpp).
INVENTORY_HEADER = "You are carrying:"
# What a poisoned character reads on each poison tick, and a wearer on each poison blow its gear
# deals: the SPELL_POISON damage message in lib/misc/messages, which damage (fight.cpp) prints.
POISON_TICK = "You writhe in pain as poison courses through your veins."
# What the game shows in place of output that would overflow a connection's buffer
# (process_output, comm.cpp).
OUTPUT_OVERFLOW = "**OVERFLOW**"

# What a follower reads as it starts following (add_follower, handler.cpp); format with the
# leader's name or short description.
NOW_FOLLOWING_FORMAT = "You now follow {}."
# What the room reads as a follower starts following (add_follower, handler.cpp); format with the
# follower's and the leader's names or short descriptions.
NOW_FOLLOWS_FORMAT = "{} now follows {}."
# What a follower reads as it follows its leader out of the room (do_move, act_move.cpp); format
# with the leader's name or short description.
FOLLOWED_FORMAT = "You follow {}."
# What a follower reads when it cannot follow its male leader out of the room (do_move,
# act_move.cpp).
FOLLOWER_ACK = "ACK! You could not follow him!"
# What the wearer reads as it removes an item (take_off_item, act_obj2.cpp); format with the
# item's short description.
STOP_USING_FORMAT = "You stop using {}."
# What the wearer reads as it puts an item on its head (wear_message, act_obj2.cpp); format with
# the item's short description.
WEAR_HEAD_FORMAT = "You wear {} on your head."
# What drop answers (perform_drop, act_obj1.cpp); format with the object's short description.
DROP_FORMAT = "You drop {}."
# What put answers for a container that is not a keyring (perform_put, act_obj1.cpp); format with
# the object's and the container's short descriptions.
PUT_FORMAT = "You put {} in {}."
# What a teleported character reads (do_teleport, act_wiz.cpp); format with the teleporter's
# name.
TELEPORTED_FORMAT = "{} has teleported you!"
# TELEPORTED_FORMAT's fixed ending, for a check that does not name the teleporter.
TELEPORTED_END = "has teleported you!"

# The prompt a connection is shown while it writes a text (send_prompts, comm.cpp).
TEXT_EDITOR_PROMPT = "] "
# What the text editor's %r prints before the text (string_add, modify.cpp).
TEXT_SO_FAR = "Your text so far:"
# What the text editor's %r prints at the cursor, after the text before it (string_add,
# modify.cpp).
TEXT_EDITOR_CURSOR = ">>"
