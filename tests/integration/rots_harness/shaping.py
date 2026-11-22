"""The shaping editors' fixed facts: each editor's prompt and release marker on its test record.

Each editor shows prompt_text's line for its kind with the record's number while no field is
being answered (prompt_text, consts.cpp), and /free prints its own release line. These are the
records two or more shaping scenario files open; a record one file opens keeps its prompt there.
"""

from __future__ import annotations

from pathlib import Path

from rots_harness import world

# The mob and the object the shaping tests edit in the mobile and object editors: mob 1130, a
# target orc, and object 1130, a harness token (world/mob/11.mob, world/obj/11.obj).
SHAPED_RECORD = 1130
# The script the shaping tests edit in the script editor: 1100, the harness stub (world/scr/11.scr).
SHAPED_SCRIPT = 1100
# The zone the shaping tests edit in the zone editor: zone 11, the harness arena.
ZONE_NUMBER = 11
# The file the zone editor loads ZONE_NUMBER from and /save rewrites, relative to the lib
# (SHAPE_ZON_DIR, protos.h).
ZONE_FILE_RELATIVE = Path("world") / "zon" / f"{ZONE_NUMBER}.zon"

# The mobile editor's prompt on SHAPED_RECORD.
MOBILE_PROMPT = f"Mobile: {SHAPED_RECORD}>"
# The object editor's prompt on SHAPED_RECORD.
OBJECT_PROMPT = f"Object: {SHAPED_RECORD}>"
# The room editor's prompt on the immortal start room, where an implementor arrives.
IMMORTAL_START_ROOM_PROMPT = f"Shaping: {world.ROOM_IMMORTAL_START}>"
# The zone editor's prompt on ZONE_NUMBER.
ZONE_PROMPT = f"Zone: {ZONE_NUMBER}>"
# The script editor's prompt on SHAPED_SCRIPT.
SCRIPT_PROMPT = f"Script: {SHAPED_SCRIPT}>"

# What /free prints in the mobile editor (extra_coms_proto, shapemob.cpp).
MOBILE_RELEASED = "You released the mobile and stopped shaping."
# What /free prints in the object editor (extra_coms_obj, shapeobj.cpp).
OBJECT_RELEASED = "You released an object and stopped shaping."
# What /free prints in the room editor (extra_coms_room, shaperom.cpp).
ROOM_RELEASED = "You released the room and stopped shaping."
# What /free prints in the zone editor (extra_coms_zone, shapezon.cpp).
ZONE_RELEASED = "You released your job and stop shaping."
# What /free prints in the script editor (extra_coms_script, shapescript.cpp).
SCRIPT_RELEASED = "You released the script and stopped shaping."

# The start of the first prompt that follows a new SECTION row in the zone and script editors
# (shape_section_prompt, shapemob.cpp).
SECTION_TITLE_PROMPT = "Enter section TITLE"
# The start of the second, after the title.
SECTION_COMMENT_PROMPT = "Enter section COMMENT"
