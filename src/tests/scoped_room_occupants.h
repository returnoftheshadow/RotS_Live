#pragma once

#include <initializer_list>

struct char_data;

namespace test_support {

// Makes `occupants` the only people in world[room_number] for the scope, raising top_of_world to
// cover that room (see ensure_test_world()) and lighting it, and restores the room's people and
// light on exit. Each occupant's in_room is set to the room; the list is built by prepending, so
// the last occupant given heads it. A null occupant is reported as a test failure and skipped.
// The combat list is left alone: pair this with a ScopedCombatList when the test can start a
// fight.
class ScopedRoomOccupants {
  public:
    ScopedRoomOccupants(int room_number, std::initializer_list<char_data*> occupants);
    ~ScopedRoomOccupants();
    ScopedRoomOccupants(const ScopedRoomOccupants&) = delete;
    ScopedRoomOccupants& operator=(const ScopedRoomOccupants&) = delete;

  private:
    int m_room_number;              // the world[] index this scope owns
    char_data* m_previous_people;   // the room's occupant list before the scope
    unsigned char m_previous_light; // the room's light count before the scope
};

} // namespace test_support
