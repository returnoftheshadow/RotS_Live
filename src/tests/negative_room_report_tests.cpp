// world[] given a negative room from one_mobile_activity named only the function, which runs for
// every mob each tick -- so the report could not say which mob, where, or which line to look at.

#include "../db.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern struct room_data world;
extern struct descriptor_data* descriptor_list;

namespace {

std::string negative_lookup_report()
{
    if (room_data::BASE_WORLD == nullptr)
        world.create_bulk(1);
    descriptor_data* saved_descriptors = descriptor_list;
    descriptor_list = nullptr; // the report mudlogs; no stale descriptors from other tests

    testing::internal::CaptureStderr();
    (void)world[-1];
    std::string log = testing::internal::GetCapturedStderr();

    descriptor_list = saved_descriptors;
    return log;
}

std::string caller_name(const char* line)
{
    char copy[256];
    char out[256];
    std::strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    negative_room_caller_name(copy, out, sizeof(out));
    return out;
}

TEST(NegativeRoomReport, NamesTheMobAndTheRoomItStartedTheTurnIn)
{
    std::string log;
    {
        running_mob_guard mob_context(5226, 15115);
        log = negative_lookup_report();
    }
    EXPECT_NE(log.find("world[] called for negative room number"), std::string::npos) << log;
    EXPECT_NE(log.find("mob 5226, room 15115"), std::string::npos) << log;
}

TEST(NegativeRoomReport, AMobWithNoVnumStillNamesTheRoom)
{
    std::string log;
    {
        running_mob_guard mob_context(-1, 15115);
        log = negative_lookup_report();
    }
    EXPECT_NE(log.find("mob -1, room 15115"), std::string::npos) << log;
}

TEST(NegativeRoomReport, NamesNoMobOnceTheActivityHasReturned)
{
    {
        running_mob_guard mob_context(5226, 15115);
    }
    EXPECT_EQ(running_mob_vnum, -1);
    EXPECT_EQ(running_mob_room_vnum, -1);
    std::string log = negative_lookup_report();
    EXPECT_NE(log.find("world[] called for negative room number"), std::string::npos) << log;
    EXPECT_EQ(log.find("mob "), std::string::npos) << log;
}

TEST(NegativeRoomReport, NestedActivityRestoresTheOuterMob)
{
    running_mob_guard outer(100, 1000);
    {
        running_mob_guard inner(200, 2000);
        EXPECT_EQ(running_mob_vnum, 200);
        EXPECT_EQ(running_mob_room_vnum, 2000);
    }
    EXPECT_EQ(running_mob_vnum, 100);
    EXPECT_EQ(running_mob_room_vnum, 1000);
}

TEST(NegativeRoomCallerName, DemanglesTheFunctionAndKeepsTheOffset)
{
    EXPECT_EQ(caller_name("../bin/ageland(_Z19one_mobile_activityP9char_data+0x1a2f) [0x8123456]"),
        "one_mobile_activity(char_data*)+0x1a2f");
}

TEST(NegativeRoomCallerName, KeepsANameThatDoesNotDemangle)
{
    EXPECT_EQ(caller_name("bin(plain_c_function+0x10) [0x1]"), "plain_c_function+0x10");
}

TEST(NegativeRoomCallerName, ANameWithNoOffsetIsKeptAlone)
{
    EXPECT_EQ(caller_name("bin(_Z19one_mobile_activityP9char_data) [0x1]"),
        "one_mobile_activity(char_data*)");
}

TEST(NegativeRoomCallerName, MissingOrMalformedLinesGiveNothing)
{
    // backtrace_symbols gives these for code with no exported symbol, or could in a bad state.
    EXPECT_EQ(caller_name("bin(+0x1f) [0x1]"), "");
    EXPECT_EQ(caller_name("bin() [0x1]"), "");
    EXPECT_EQ(caller_name("bin [0x1]"), "");
    EXPECT_EQ(caller_name("bin(no_close+0x1f"), "");
    EXPECT_EQ(caller_name(""), "");
    EXPECT_EQ(caller_name("bin(name+) [0x1]"), "name");

    char out[8] = "junk";
    negative_room_caller_name(nullptr, out, sizeof(out));
    EXPECT_STREQ(out, "");
    negative_room_caller_name(nullptr, nullptr, 0); // must not crash
}

TEST(NegativeRoomCallerName, ALongNameIsCutToTheBuffer)
{
    char line[] = "bin(a_very_long_function_name+0x1f) [0x1]";
    char out[8];
    negative_room_caller_name(line, out, sizeof(out));
    EXPECT_STREQ(out, "a_very_");
}

} // namespace
