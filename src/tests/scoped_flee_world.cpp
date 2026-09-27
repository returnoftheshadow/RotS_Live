#include "scoped_flee_world.h"

#include "test_random_utils.h"
#include "test_world_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>

extern room_data world;

namespace test_support {

namespace {

// Names and descriptions for the two rooms. do_look() prints both for a player with a descriptor
// (strcat() on a null description would crash), and room_data wants them writable.
char g_origin_name[] = "The flee test's origin";
char g_origin_description[] = "The room a flee test's character leaves.\n\r";
char g_destination_name[] = "The flee test's destination";
char g_destination_description[] = "The room a flee test's character enters.\n\r";
char g_empty_exit_text[] = "";

// How many copies queue_east_everywhere_rolls() queues: more than damage() and a whole flee draw.
constexpr int kEastEverywhereRollCount = 64;

// A roll that makes number(from, to) return `result`, pinned at the midpoint of the result's
// share of [0, 1): the container's x87 arithmetic truncates a product that lands just below an
// integer boundary, and a midpoint truncates the same under x87 and SSE2.
double roll_for(int result, int from, int to) {
    const int range = to - from + 1;
    return (result - from + 0.5) / range;
}

// Makes `room` an empty, named, lit room with no exits, flags, special, contents or occupants.
void reset_room(room_data& room, int room_number, char* name, char* description) {
    room.number = room_number;
    room.zone = 0;
    room.sector_type = SECT_INSIDE;
    room.name = name;
    room.description = description;
    room.ex_description = nullptr;
    std::fill(std::begin(room.dir_option), std::end(room.dir_option), nullptr);
    room.room_flags = 0;
    room.light = 1; // act() drops a line whose subject stands in a dark room
    room.funct = nullptr;
    room.contents = nullptr;
    room.people = nullptr;
    room.affected = nullptr;
}

} // namespace

ScopedFleeWorld::ScopedFleeWorld(int origin_room, int destination_room, int direction)
    : m_origin_room(origin_room), m_destination_room(destination_room), m_previous_origin(),
      m_previous_destination(), m_exit(), m_zone(), m_previous_zone_table(zone_table),
      m_previous_top_of_zone_table(top_of_zone_table) {
    ensure_test_world(std::max(origin_room, destination_room));
    zone_table = &m_zone;
    top_of_zone_table = 0;
    m_previous_origin = world[origin_room];
    m_previous_destination = world[destination_room];

    reset_room(world[origin_room], origin_room, g_origin_name, g_origin_description);
    reset_room(world[destination_room], destination_room, g_destination_name,
               g_destination_description);

    m_exit.general_description = g_empty_exit_text;
    m_exit.keyword = nullptr;
    m_exit.exit_width = 4; // the default width
    m_exit.exit_info = 0;  // open, no door
    m_exit.key = -1;       // no key
    m_exit.to_room = destination_room;
    if (direction < 0 || direction >= NUM_OF_DIRS) {
        ADD_FAILURE() << "ScopedFleeWorld: no direction " << direction;
        return;
    }
    world[origin_room].dir_option[direction] = &m_exit;
}

ScopedFleeWorld::~ScopedFleeWorld() {
    world[m_destination_room] = m_previous_destination;
    world[m_origin_room] = m_previous_origin;
    top_of_zone_table = m_previous_top_of_zone_table;
    zone_table = m_previous_zone_table;
}

// The draws, in the order the flee makes them:
//   do_flee():                        the exit to try, number(0, NUM_OF_DIRS - 1);
//   its check_simple_move():          room_move_cost() of the origin, then of the destination,
//                                     number(0, 99) each;
//   do_flee():                        number(0, 1), drawn only because no special handled the
//                                     command; with the move allowed its value changes nothing;
//   do_move()'s check_simple_move():  the same two room_move_cost() draws again;
//   do_move():                        the track slot left in the origin,
//                                     number(0, NUM_OF_TRACKS - 1).
// Only the first decides the path; the rest are pinned so the flee draws nothing unqueued.
void queue_successful_flee_rolls(int direction) {
    constexpr int kLowestMoveRoll = 0;
    push_test_random_value(roll_for(direction, 0, NUM_OF_DIRS - 1));
    push_test_random_value(roll_for(kLowestMoveRoll, 0, 99));
    push_test_random_value(roll_for(kLowestMoveRoll, 0, 99));
    push_test_random_value(roll_for(0, 0, 1));
    push_test_random_value(roll_for(kLowestMoveRoll, 0, 99));
    push_test_random_value(roll_for(kLowestMoveRoll, 0, 99));
    push_test_random_value(roll_for(0, 0, NUM_OF_TRACKS - 1));
}

// EAST's midpoint roll is a quarter, which also lies in 0's share of number(0, 2).
void queue_east_everywhere_rolls() {
    const double east_everywhere_roll = roll_for(EAST, 0, NUM_OF_DIRS - 1);
    for (int roll = 0; roll < kEastEverywhereRollCount; ++roll) {
        push_test_random_value(east_everywhere_roll);
    }
}

} // namespace test_support
