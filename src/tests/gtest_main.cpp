#include "../structs.h"
#include <gtest/gtest.h>

extern room_data world;

#ifdef TESTING
int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    // The world can be sized only once per process (create_bulk() exits on a
    // second call), and world[] silently aliases indexes past the allocation
    // onto r_immort_start_room. Size it here, once, above every suite's room
    // numbers (highest claimed: 974), so no TU's capacity depends on which
    // suite happens to run first.
    world.create_bulk(1024);
    return RUN_ALL_TESTS();
}
#endif
