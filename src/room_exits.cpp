#include "room_exits.h"

#include "structs.h"
#include "utils.h"

extern room_data world;
extern int top_of_world;

namespace {

// The kind of exit exit is: a closed door that is not broken first, then a window, and anything
// else is open. A broken door counts as open even while flagged closed, and a hidden door only
// stays hidden while it is closed.
exit_state state_of(const room_direction_data& exit)
{
    if (IS_SET(exit.exit_info, EX_CLOSED) && !IS_SET(exit.exit_info, EX_ISBROKEN)) {
        if (IS_SET(exit.exit_info, EX_ISHIDDEN)) {
            return exit_state::hidden_door;
        }
        return exit_state::closed_door;
    }

    if (IS_SET(exit.exit_info, EX_NOWALK)) {
        return exit_state::window;
    }

    return exit_state::open;
}

// Whether the room line shows viewer an exit of this kind: gods see hidden doors and windows,
// everyone else sees neither.
bool room_line_shows(const char_data& viewer, exit_state state)
{
    switch (state) {
    case exit_state::open:
    case exit_state::closed_door:
        return true;
    case exit_state::hidden_door:
    case exit_state::window:
        return GET_LEVEL(&viewer) >= LEVEL_GOD;
    case exit_state::none:
        return false;
    }

    return false;
}

} // namespace

room_exit_view room_exits(int room)
{
    room_exit_view exits {};
    if (room < 0 || room > top_of_world) {
        vmudlog(NRM, "SYSERR: %s: room %d is outside the world.", __func__, room);
        return exits;
    }

    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        const room_direction_data* exit = world[room].dir_option[direction];
        if (exit == nullptr || exit->to_room == NOWHERE) {
            continue;
        }

        exits[direction] = { state_of(*exit), exit->to_room };
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
            shown = room_line_shows(viewer, exit.state);
            break;
        }

        if (shown) {
            seen[direction] = exit;
        }
    }

    return seen;
}
