#pragma once

#include "../structs.h"

#include <array>

extern room_data world;
extern int top_of_world;

// A hall (real room 992) with an exit each way into rooms 993 to 998, restored on destruction.
// The default exits cover every kind the exit displays tell apart:
//   north  an open exit
//   east   a closed door
//   south  a closed hidden door
//   west   a window (EX_NOWALK)
//   up     a broken closed door
//   down   an opened hidden door
// All seven rooms are indoors and lit.
class ScopedExitTestRooms {
public:
    // The real room the fixture turns into the hall.
    static constexpr int HALL = 992;
    // The real room the hall's north exit leads to; the other directions follow in order.
    static constexpr int FIRST_DESTINATION = 993;

    // The vnum the fixture gives real_room: the real number times 100.
    static constexpr int vnum_of(int real_room) { return real_room * 100; }

    // The real room the hall's exit in direction leads to by default.
    static constexpr int destination(int direction) { return FIRST_DESTINATION + direction; }

    // The hall's vnum. It is vnum_of(HALL), which cannot be called until the class is complete.
    static constexpr int HALL_VNUM = HALL * 100;

    ScopedExitTestRooms()
        : m_top_of_world(top_of_world)
    {
        if (top_of_world < destination(DOWN)) {
            top_of_world = destination(DOWN);
        }

        for (int room_offset = 0; room_offset < ROOM_COUNT; ++room_offset) {
            room_data& room = world[HALL + room_offset];
            saved_room& saved = m_saved_rooms[room_offset];
            saved.number = room.number;
            saved.name = room.name;
            saved.description = room.description;
            saved.room_flags = room.room_flags;
            saved.sector_type = room.sector_type;
            saved.light = room.light;
            saved.people = room.people;
            saved.contents = room.contents;
            saved.affected = room.affected;
            for (int direction = NORTH; direction <= DOWN; ++direction) {
                saved.dir_option[direction] = room.dir_option[direction];
                room.dir_option[direction] = nullptr;
            }

            room.number = vnum_of(HALL + room_offset);
            room.name = m_room_names[room_offset];
            room.description = m_description;
            room.room_flags = INDOORS;
            room.sector_type = SECT_INSIDE;
            room.light = 1;
            room.people = nullptr;
            room.contents = nullptr;
            room.affected = nullptr;
        }

        set_exit(NORTH, 0, destination(NORTH));
        set_exit(EAST, EX_ISDOOR | EX_CLOSED, destination(EAST));
        set_exit(SOUTH, EX_ISDOOR | EX_CLOSED | EX_ISHIDDEN, destination(SOUTH));
        set_exit(WEST, EX_NOWALK, destination(WEST));
        set_exit(UP, EX_ISDOOR | EX_CLOSED | EX_ISBROKEN, destination(UP));
        set_exit(DOWN, EX_ISDOOR | EX_ISHIDDEN, destination(DOWN));
    }

    ~ScopedExitTestRooms()
    {
        for (int room_offset = 0; room_offset < ROOM_COUNT; ++room_offset) {
            room_data& room = world[HALL + room_offset];
            const saved_room& saved = m_saved_rooms[room_offset];
            room.number = saved.number;
            room.name = saved.name;
            room.description = saved.description;
            room.room_flags = saved.room_flags;
            room.sector_type = saved.sector_type;
            room.light = saved.light;
            room.people = saved.people;
            room.contents = saved.contents;
            room.affected = saved.affected;
            for (int direction = NORTH; direction <= DOWN; ++direction) {
                room.dir_option[direction] = saved.dir_option[direction];
            }
        }
        top_of_world = m_top_of_world;
    }

    ScopedExitTestRooms(const ScopedExitTestRooms&) = delete;
    ScopedExitTestRooms& operator=(const ScopedExitTestRooms&) = delete;

    // Gives the hall an exit in direction with these EX_ flags, leading to real room to_room.
    void set_exit(int direction, int exit_info, int to_room)
    {
        room_direction_data& exit = m_exits[direction];
        exit = {};
        exit.general_description = m_empty_text;
        exit.keyword = m_door_keyword;
        exit.exit_width = DEFAULT_EXIT_WIDTH;
        exit.exit_info = exit_info;
        exit.key = NO_KEY;
        exit.to_room = to_room;
        world[HALL].dir_option[direction] = &exit;
    }

    // Leaves the hall with no exit at all in direction.
    void remove_exit(int direction) { world[HALL].dir_option[direction] = nullptr; }

    // Leaves the hall's exit in direction, set by set_exit, with no keywords.
    void remove_keyword(int direction) { m_exits[direction].keyword = nullptr; }

private:
    // The hall plus one destination room per direction.
    static constexpr int ROOM_COUNT = 1 + NUM_OF_DIRS;
    // The width world files give an ordinary exit.
    static constexpr ubyte DEFAULT_EXIT_WIDTH = 4;
    // The key number of an exit no key opens.
    static constexpr int NO_KEY = -1;

    // One room's fields as they were before the fixture changed them.
    struct saved_room {
        // The room's vnum.
        int number = 0;
        // The room's name.
        char* name = nullptr;
        // The room's description.
        char* description = nullptr;
        // The room's ROOM_ flags.
        long room_flags = 0;
        // The room's SECT_ terrain.
        int sector_type = 0;
        // The room's count of light sources.
        byte light = 0;
        // The characters in the room.
        char_data* people = nullptr;
        // The objects on the floor.
        obj_data* contents = nullptr;
        // The room's affects.
        affected_type* affected = nullptr;
        // The room's exits.
        room_direction_data* dir_option[NUM_OF_DIRS] = {};
    };

    // top_of_world before the fixture raised it.
    int m_top_of_world;
    // The fields the fixture changed in each of its rooms, hall first.
    std::array<saved_room, ROOM_COUNT> m_saved_rooms {};
    // The hall's exits, one per direction; world[HALL].dir_option points here.
    std::array<room_direction_data, NUM_OF_DIRS> m_exits {};
    // Room names, hall first, then the room each direction leads to.
    char m_room_names[ROOM_COUNT][24] = { "Exit Test Hall", "North Room", "East Room", "South Room",
        "West Room", "Upper Room", "Lower Room" };
    // The description every fixture room shares.
    char m_description[32] = "A room for exit tests.\n\r";
    // The keyword every fixture exit shares.
    char m_door_keyword[8] = "door";
    // An empty exit description.
    char m_empty_text[1] = "";
};

static_assert(
    ScopedExitTestRooms::HALL_VNUM == ScopedExitTestRooms::vnum_of(ScopedExitTestRooms::HALL));
