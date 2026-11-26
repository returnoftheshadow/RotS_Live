#ifndef ROOM_EXITS_H
#define ROOM_EXITS_H

#include "structs.h"

#include <array>

// A room's six exits, as data the room line and MSDP both read, so the two show the same exits.
// A room_exit_view built by room_exits() holds everything the room has; exits_seen_by() narrows
// it to what one character can see.

// What kind of exit leads one way out of a room.
enum class exit_state {
    // No exit this way; to_room is NOWHERE.
    none,
    // An exit that can be walked through: an open exit, an opened door or a broken door.
    open,
    // A closed door that is not broken.
    closed_door,
    // A closed door that is also hidden.
    hidden_door,
    // An exit that can be seen but not walked through (EX_NOWALK), mostly windows.
    window,
};

// One way out of a room.
struct room_exit {
    // What kind of exit it is.
    exit_state state = exit_state::none;
    // The real room it leads to; NOWHERE when state is none.
    int to_room = NOWHERE;
};

// A room's exits indexed by direction, north to down.
using room_exit_view = std::array<room_exit, NUM_OF_DIRS>;

// Every exit real_room has, whoever looks. A room outside the world logs an error and has no
// exits.
room_exit_view room_exits(int real_room);

// The display an exit view is built for.
enum class exit_view_purpose {
    // The "Exits are:" line under a room's name, which the client protocols also send.
    room_line,
};

// Whether viewer can see the room it stands in: awake, not blind, and the room lit for it.
bool can_see_room(const char_data& viewer);

// The exits viewer sees for purpose, built from exits, the view of the room viewer stands in.
// An exit viewer cannot see becomes { none, NOWHERE }, and when viewer cannot see the room at all
// every exit does.
room_exit_view exits_seen_by(const char_data& viewer, const room_exit_view& exits,
    exit_view_purpose purpose);

#endif /* ROOM_EXITS_H */
