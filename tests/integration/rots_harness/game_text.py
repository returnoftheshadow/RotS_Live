"""Fixed game text that two or more scenario files assert and no shared helper waits for.

A text that a shared helper waits for lives with that helper, and a text one scenario file
asserts stays in that file, as does a text two files share only by coincidence: the same words
from another function. A name ends in _FORMAT for a format string, filled in with str.format, and
in _REPLY for a fixed line that answers the reader's own command; any other fixed line is named
for what it is. Every "Ok." but force's stays a literal in its scenario file, since many commands
answer it.
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
# What a poisoned character reads on each poison tick, and a wearer on each poison blow its gear
# deals: the SPELL_POISON damage message in lib/misc/messages, which damage (fight.cpp) prints.
POISON_TICK = "You writhe in pain as poison courses through your veins."

# What a follower reads as it starts following (add_follower, handler.cpp); format with the
# leader's name or short description.
NOW_FOLLOWING_FORMAT = "You now follow {}."
# What the wearer reads as it removes an item (take_off_item, act_obj2.cpp); format with the
# item's short description.
STOP_USING_FORMAT = "You stop using {}."
# What drop answers (perform_drop, act_obj1.cpp); format with the object's short description.
DROP_FORMAT = "You drop {}."
# What a teleported character reads (do_teleport, act_wiz.cpp); format with the teleporter's
# name.
TELEPORTED_FORMAT = "{} has teleported you!"
# TELEPORTED_FORMAT's fixed ending, for a check that does not name the teleporter.
TELEPORTED_BY_ANYONE = "has teleported you!"
