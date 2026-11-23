/* scoped_test_rooms.h */

#ifndef LOGIN_TESTS_SCOPED_TEST_ROOMS_H
#define LOGIN_TESTS_SCOPED_TEST_ROOMS_H

#include "login_test_fixtures.h"

#include "structs.h"
#include "zone.h"

#include <string>

extern int top_of_world;
extern struct room_data world;
extern struct zone_data* zone_table;

namespace login_tests {

// A two-room world in one zone for the life of the scope: another room, then the load room. The
// rooms are lit and empty. The suite's world is shared, so both rooms are restored afterwards.
class ScopedTestRooms {
public:
    ScopedTestRooms()
        : saved_top_of_world(top_of_world)
        , saved_first_room(world[0])
        , saved_load_room(world[LOAD_ROOM_RNUM])
        , saved_zone_table(zone_table)
    {
        zone_table = &zone;
        prepare_room(world[0], OTHER_ROOM_VNUM, other_room_name, other_room_description);
        prepare_room(world[LOAD_ROOM_RNUM], LOAD_ROOM_VNUM, load_room_name, load_room_description);
        top_of_world = LOAD_ROOM_RNUM;
    }

    ~ScopedTestRooms()
    {
        world[0] = saved_first_room;
        world[LOAD_ROOM_RNUM] = saved_load_room;
        top_of_world = saved_top_of_world;
        zone_table = saved_zone_table;
    }

    ScopedTestRooms(const ScopedTestRooms&) = delete;
    ScopedTestRooms& operator=(const ScopedTestRooms&) = delete;

private:
    // Makes out_room an empty, lit room with no exits, numbered vnum, whose text points into name
    // and description.
    static void prepare_room(room_data& out_room, int vnum, std::string& name, std::string& description)
    {
        out_room.number = vnum;
        out_room.zone = 0;
        out_room.sector_type = SECT_INSIDE;
        out_room.name = name.data();
        out_room.description = description.data();
        out_room.ex_description = nullptr;
        for (room_direction_data*& exit : out_room.dir_option) {
            exit = nullptr;
        }
        out_room.room_flags = 0;
        out_room.light = 1;
        out_room.funct = nullptr;
        out_room.contents = nullptr;
        out_room.people = nullptr;
        out_room.affected = nullptr;
    }

    // The highest room index before the scope.
    int saved_top_of_world;
    // The shared rooms this scope overwrites, restored when it ends.
    room_data saved_first_room;
    room_data saved_load_room;
    // The zone table before the scope.
    zone_data* saved_zone_table;
    // The one zone both rooms belong to; a character entering a room adds to its zone's power.
    zone_data zone { };
    // The rooms' text, which the rooms point into.
    std::string other_room_name { "A Room Elsewhere" };
    std::string other_room_description { "Another room.\n\r" };
    std::string load_room_name { LOAD_ROOM_NAME };
    std::string load_room_description { "A quiet room used for login tests.\n\r" };
};

} // namespace login_tests

#endif /* LOGIN_TESTS_SCOPED_TEST_ROOMS_H */
