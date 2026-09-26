#pragma once

namespace test_support {

// Makes `minimum_room_number` at most top_of_world; allocates the world only when nothing has yet,
// otherwise storage is whatever the first allocation (gtest_main's create_bulk) provided.
void ensure_test_world(int minimum_room_number);

} // namespace test_support
