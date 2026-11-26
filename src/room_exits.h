#ifndef ROOM_EXITS_H
#define ROOM_EXITS_H

#include "structs.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

// A room's six exits, as data the room line and MSDP both read, so the two show the same exits.
// A room_exit_view built by room_exits() holds everything the room has; exits_seen_by() narrows
// it to what one character can see.

// What is true of an exit. room_exits() sets only combinations that can happen: shut, locked and
// broken only with door, locked only with shut, and never shut with broken.
enum class exit_flags : std::uint8_t {
    // Nothing is true of it: no door, and nothing stops a walker.
    none = 0,
    // It has a door: EX_ISDOOR, or EX_CLOSED on an exit not marked as a door.
    door = 1 << 0,
    // Its door is closed and not broken.
    shut = 1 << 1,
    // Its door is shut and locked.
    locked = 1 << 2,
    // Its door is broken, so it stays open.
    broken = 1 << 3,
    // Marked hidden in the world (EX_ISHIDDEN).
    hidden = 1 << 4,
    // It can be looked through but not walked through (EX_NOWALK), mostly windows.
    window = 1 << 5,
};

// Combine and intersect exit_flags as bit sets; has() tests the result.
constexpr exit_flags operator|(exit_flags left, exit_flags right)
{
    return static_cast<exit_flags>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr exit_flags operator&(exit_flags left, exit_flags right)
{
    return static_cast<exit_flags>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}

constexpr exit_flags& operator|=(exit_flags& flags, exit_flags added)
{
    flags = flags | added;
    return flags;
}

// Whether flags includes every flag in wanted.
constexpr bool has(exit_flags flags, exit_flags wanted)
{
    return (flags & wanted) == wanted;
}

// One way out of a room.
struct room_exit {
    // The real room it leads to; empty when there is no exit this way.
    std::optional<int> to_room;
    // What is true of it.
    exit_flags flags = exit_flags::none;
    // The keywords the open command accepts for its door; empty when it has no door or the door
    // has no keywords. It points into the world, so a view must not be kept past the command
    // that built it.
    std::string_view door_name;
};

// Whether exit can be walked through: it leads somewhere, is not a window, and is not shut.
bool is_passable(const room_exit& exit);

// A room's exits indexed by direction, north to down.
using room_exit_view = std::array<room_exit, NUM_OF_DIRS>;

// Every exit real_room has, whoever looks. An exit leading to NOWHERE is no exit. A room outside
// the world logs an error and has no exits.
room_exit_view room_exits(int real_room);

// The display an exit view is built for.
enum class exit_view_purpose {
    // The "Exits are:" line under a room's name, which the client protocols also send.
    room_line,
};

// Whether viewer can see the room it stands in: awake, not blind, and the room lit for it.
bool can_see_room(const char_data& viewer);

// The exits viewer sees for purpose, built from exits, the view of the room viewer stands in.
// An exit viewer cannot see becomes an empty room_exit, and when viewer cannot see the room at
// all every exit does.
room_exit_view exits_seen_by(const char_data& viewer, const room_exit_view& exits,
    exit_view_purpose purpose);

#endif /* ROOM_EXITS_H */
