#include "../big_brother.h"
#include "../structs.h"
#include <gtest/gtest.h>

extern room_data world;
extern struct weather_data weather_info;

#ifdef TESTING
int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    // The world can be sized only once per process (create_bulk() exits on a
    // second call), and world[] silently aliases indexes past the allocation
    // onto r_immort_start_room. Size it here, once, above every suite's room
    // numbers (highest claimed: 1001), so no TU's capacity depends on which
    // suite happens to run first.
    world.create_bulk(1024);
    // Boot creates the Big Brother singleton before the game loop (db.cpp);
    // raw_kill() reports every death to it, and a player's or an orc-friend's
    // non-empty corpse touches its members. Create it here, once, so no suite
    // depends on an earlier suite having done so. create() keeps one
    // function-local static, so a suite repeating it is harmless.
    game_rules::big_brother::create(weather_info, &world);
    return RUN_ALL_TESTS();
}
#endif
