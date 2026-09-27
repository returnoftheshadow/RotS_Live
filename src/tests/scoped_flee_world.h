#pragma once

#include "../structs.h"
#include "../zone.h"

namespace test_support {

// Two rooms joined by one open exit, for tests that flee or walk a character between them:
// world[origin_room]'s exit in `direction` leads to world[destination_room], and the rooms have no
// other exits. Both rooms are named, described, lit, inside, and have no flags, special,
// contents, occupants or affects. They are in zone 0, which the scope publishes as a one-entry
// zone_table: moving a player into or out of a room updates its zone's race power. Each room and
// the zone table are restored on exit, so a test may change either room further (a DEATH flag,
// occupants, a special) for the scope; objects left in a room are the test's to release first.
// A `direction` outside the six is reported as a test failure and leaves the rooms unjoined.
class ScopedFleeWorld {
  public:
    ScopedFleeWorld(int origin_room, int destination_room, int direction);
    ~ScopedFleeWorld();
    ScopedFleeWorld(const ScopedFleeWorld&) = delete;
    ScopedFleeWorld& operator=(const ScopedFleeWorld&) = delete;

  private:
    int m_origin_room;                // the world[] index the exit leaves from
    int m_destination_room;           // the world[] index the exit leads to
    room_data m_previous_origin;      // world[m_origin_room] before the scope
    room_data m_previous_destination; // world[m_destination_room] before the scope
    room_direction_data m_exit;       // the one exit, owned here for the scope
    zone_data m_zone;                 // zone 0 of the scope's zone_table
    zone_data* m_previous_zone_table; // zone_table before the scope (normally null)
    int m_previous_top_of_zone_table; // top_of_zone_table before the scope
};

// Queues the random rolls one successful do_flee() through `direction` draws, in order, for a
// fleer that is standing or fighting and is not hazed, sneaking, flying or a shadow, when no
// special handles the flee's command and nothing draws before do_flee() does. The exit must be
// open, lead to a room the fleer may enter, and cost it no more moves than it has.
void queue_successful_flee_rolls(int direction);

// Queues one roll many times over, for a flee through EAST that other draws precede, such as
// damage()'s own before the wimpy flee a blow sets off, or for a walk east whose entry can kill,
// so the test need not count the draws:
// number(0, NUM_OF_DIRS - 1) comes out EAST however many draws come first, and number(0, 2) comes
// out 0, so a physical blow's resistance roll does not change its damage. Every other draw on the
// test's path must only set a magnitude. More are queued than such a path draws; the test clears
// the rest.
void queue_east_everywhere_rolls();

} // namespace test_support
