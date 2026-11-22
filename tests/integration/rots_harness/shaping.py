"""The shaping editors' fixed facts on their test records, and the release step every editor shares.

Each editor shows prompt_text's line for its kind with the record's number while no field is
being answered (prompt_text, consts.cpp), and /free prints its own release line. These are the
records two or more shaping scenario files open.
"""

from __future__ import annotations

from pathlib import Path

from rots_harness import world
from rots_harness.replies import reply_to
from rots_harness.session import GameSession

# The mob and the object the shaping tests edit in the mobile and object editors: mob 1130, a
# target orc, and object 1130, a harness token (world/mob/11.mob, world/obj/11.obj).
SHAPED_RECORD = 1130
# The script the shaping tests edit in the script editor: 1100, the harness stub (world/scr/11.scr).
SHAPED_SCRIPT = 1100
# The script whose three ON_ENTER rows are numbered 1, 2 and 3, with the texts first, second and
# third (world/scr/11.scr).
ORDERED_SCRIPT = 1180
# The zone the shaping tests edit in the zone editor: zone 11, the harness arena.
ZONE_NUMBER = 11
# The file the zone editor loads ZONE_NUMBER from and /save rewrites, relative to the lib
# (SHAPE_ZON_DIR, protos.h).
ZONE_FILE_RELATIVE = Path("world") / "zon" / f"{ZONE_NUMBER}.zon"
# The file the script editor loads SHAPED_SCRIPT and ORDERED_SCRIPT from and /save rewrites,
# relative to the lib (SHAPE_SCRIPT_DIR, protos.h).
SCRIPT_FILE_RELATIVE = Path("world") / "scr" / "11.scr"
# Seconds to keep reading after an editor's reply arrives, for the rest of the reply; every
# shaping and text editor scenario file passes it in place of replies.SETTLE_SECONDS.
EDITOR_SETTLE_SECONDS = 0.5

# The mobile editor's prompt on SHAPED_RECORD.
MOBILE_PROMPT = f"Mobile: {SHAPED_RECORD}>"
# The object editor's prompt on SHAPED_RECORD.
OBJECT_PROMPT = f"Object: {SHAPED_RECORD}>"
# The room editor's prompt on the immortal start room, where an implementor arrives.
IMMORTAL_START_ROOM_PROMPT = f"Shaping: {world.ROOM_IMMORTAL_START}>"
# The room editor's prompt on the signpost room.
SIGNPOST_ROOM_PROMPT = f"Shaping: {world.ROOM_SIGNPOST}>"
# The zone editor's prompt on ZONE_NUMBER.
ZONE_PROMPT = f"Zone: {ZONE_NUMBER}>"
# The script editor's prompt on SHAPED_SCRIPT.
SCRIPT_PROMPT = f"Script: {SHAPED_SCRIPT}>"
# The script editor's prompt on ORDERED_SCRIPT.
ORDERED_SCRIPT_PROMPT = f"Script: {ORDERED_SCRIPT}>"

# What /free prints in the mobile editor (extra_coms_proto, shapemob.cpp).
MOBILE_RELEASED_REPLY = "You released the mobile and stopped shaping."
# What /free prints in the object editor (extra_coms_obj, shapeobj.cpp).
OBJECT_RELEASED_REPLY = "You released an object and stopped shaping."
# What /free prints in the room editor (extra_coms_room, shaperom.cpp).
ROOM_RELEASED_REPLY = "You released the room and stopped shaping."
# What /free prints in the zone editor (extra_coms_zone, shapezon.cpp).
ZONE_RELEASED_REPLY = "You released your job and stop shaping."
# What /free prints in the script editor (extra_coms_script, shapescript.cpp).
SCRIPT_RELEASED_REPLY = "You released the script and stopped shaping."

# What every editor prints when it cannot open the file of the record it is asked to shape (the
# load functions of shapemob.cpp, shapeobj.cpp, shaperom.cpp, shapezon.cpp and shapescript.cpp;
# the mobile and script editors print a space before it).
COULD_NOT_OPEN_FILE_REPLY = "could not open that file."
# The start of what /done and /save print when they cannot write a record file's backup (the save
# functions of shape*.cpp; some print "-2" after it).
NO_BACKUP_FILE_START = "could not open backup file"
# What the room editor's /implement prints once the room is live (implement_room, shaperom.cpp).
ROOM_IMPLEMENTED_REPLY = "Room implemented."
# What the zone editor's /implement prints when every command loads (implement_zone,
# shapezon.cpp).
ZONE_IMPLEMENTED_REPLY = "The zone was implemented."
# The middle of the line every editor command but a field number prints first (the extra_coms_*
# functions of shape*.cpp).
EDITOR_COMMAND_PART = "shapeless indefinity"

# The start of the first prompt that follows a new SECTION row in the zone and script editors
# (shape_section_prompt, shapemob.cpp).
SECTION_TITLE_PROMPT_START = "Enter section TITLE"
# The start of the second, after the title.
SECTION_COMMENT_PROMPT_START = "Enter section COMMENT"


def release_editor(session: GameSession, released_reply: str, *, settle: float = EDITOR_SETTLE_SECONDS) -> str:
    """Send /free, assert the reply holds released_reply, and return the reply.

    The reply is read as reply_to reads it, up to released_reply and settle seconds after it.
    SessionTimeout passes through when released_reply does not arrive within expect's default
    timeout.
    """
    released = reply_to(session, "/free", [released_reply], settle=settle)
    assert released_reply in released, released
    return released
