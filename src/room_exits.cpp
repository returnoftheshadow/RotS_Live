#include "room_exits.h"

#include "structs.h"
#include "utils.h"

extern room_data world;
extern int top_of_world;

namespace {

// The flags exit_info gives an exit, in the combinations exit_flags describes.
exit_flags flags_of(int exit_info)
{
    exit_flags flags = exit_flags::none;
    if (IS_SET(exit_info, EX_ISDOOR) || IS_SET(exit_info, EX_CLOSED)) {
        flags |= exit_flags::door;
        if (IS_SET(exit_info, EX_ISBROKEN)) {
            flags |= exit_flags::broken;
        } else if (IS_SET(exit_info, EX_CLOSED)) {
            flags |= exit_flags::shut;
            if (IS_SET(exit_info, EX_LOCKED)) {
                flags |= exit_flags::locked;
            }
        }
    }

    if (IS_SET(exit_info, EX_ISHIDDEN)) {
        flags |= exit_flags::hidden;
    }
    if (IS_SET(exit_info, EX_NOWALK)) {
        flags |= exit_flags::window;
    }

    return flags;
}

// Whether the room line shows viewer exit. Gods see every exit; everyone else sees neither a shut
// hidden door nor a window that is not shut.
bool room_line_shows(const char_data& viewer, const room_exit& exit)
{
    if (!exit.to_room) {
        return false;
    }
    if (GET_LEVEL(&viewer) >= LEVEL_GOD) {
        return true;
    }

    const bool shut = has(exit.flags, exit_flags::shut);
    if (shut) {
        return !has(exit.flags, exit_flags::hidden);
    }
    return !has(exit.flags, exit_flags::window);
}

} // namespace

bool is_passable(const room_exit& exit)
{
    return exit.to_room && !has(exit.flags, exit_flags::window)
        && !has(exit.flags, exit_flags::shut);
}

room_exit_view room_exits(int real_room)
{
    room_exit_view exits {};
    if (real_room < 0 || real_room > top_of_world) {
        vmudlog(NRM, "SYSERR: %s: room %d is outside the world.", __func__, real_room);
        return exits;
    }

    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        const room_direction_data* world_exit = world[real_room].dir_option[direction];
        if (world_exit == nullptr || world_exit->to_room == NOWHERE) {
            continue;
        }

        room_exit& exit = exits[direction];
        exit.to_room = world_exit->to_room;
        exit.flags = flags_of(world_exit->exit_info);
        // A door with no keywords is opened by its direction alone.
        if (has(exit.flags, exit_flags::door) && world_exit->keyword != nullptr) {
            exit.door_name = world_exit->keyword;
        }
    }

    return exits;
}

bool can_see_room(const char_data& viewer)
{
    return GET_POS(&viewer) > POSITION_SLEEPING && !IS_AFFECTED(&viewer, AFF_BLIND)
        && CAN_SEE(&viewer);
}

room_exit_view exits_seen_by(const char_data& viewer, const room_exit_view& exits,
    exit_view_purpose purpose)
{
    room_exit_view seen {};
    if (!can_see_room(viewer)) {
        return seen;
    }

    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        const room_exit& exit = exits[direction];
        bool shown = false;
        switch (purpose) {
        case exit_view_purpose::room_line:
            shown = room_line_shows(viewer, exit);
            break;
        }

        if (shown) {
            seen[direction] = exit;
        }
    }

    return seen;
}
