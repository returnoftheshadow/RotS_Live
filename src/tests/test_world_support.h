#pragma once

namespace test_support {

// Grows the test binary's shared world[] so `minimum_room_number` is a valid index; never
// shrinks it.
void ensure_test_world(int minimum_room_number);

} // namespace test_support
