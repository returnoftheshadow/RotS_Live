#include "test_world_support.h"

#include "../structs.h"

extern room_data world;
extern int top_of_world;

namespace test_support {

void ensure_test_world(int minimum_room_number) {
    if (!room_data::BASE_WORLD) {
        // Rooms 0 to minimum_room_number, plus the last slot, which create_bulk() turns into the
        // dummy extension-list head. top_of_world counts that head slot too, as load_rooms() does.
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

} // namespace test_support
