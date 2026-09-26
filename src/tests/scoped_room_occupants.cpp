#include "scoped_room_occupants.h"

#include "../structs.h"
#include "test_world_support.h"

#include <gtest/gtest.h>

extern room_data world;

namespace test_support {

ScopedRoomOccupants::ScopedRoomOccupants(int room_number,
                                         std::initializer_list<char_data*> occupants)
    : m_room_number(room_number), m_previous_people(nullptr), m_previous_light(0) {
    ensure_test_world(room_number);
    room_data& room = world[room_number];
    m_previous_people = room.people;
    m_previous_light = room.light;
    room.light = 1; // act() drops a line whose subject stands in a dark room
    room.people = nullptr;
    for (char_data* occupant : occupants) {
        if (occupant == nullptr) {
            ADD_FAILURE() << "ScopedRoomOccupants: null occupant";
            continue;
        }
        occupant->in_room = room_number;
        occupant->next_in_room = room.people;
        room.people = occupant;
    }
}

ScopedRoomOccupants::~ScopedRoomOccupants() {
    world[m_room_number].people = m_previous_people;
    world[m_room_number].light = m_previous_light;
}

} // namespace test_support
