#include "../game_boot_options.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
std::string temp_file()
{
    char path[] = "/tmp/bootopts_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    close(fd);
    std::remove(path); /* tests start with the file missing */
    return path;
}

/* The running values are one table for the whole test program: each test
 * leaves the defaults behind, whatever it loaded or set and in any order. */
class BootOptions : public ::testing::Test {
protected:
    void TearDown() override { boot_options_load(temp_file().c_str()); /* a missing file: all defaults */ }
};
} // namespace

TEST_F(BootOptions, DefaultsWhenTextIsEmpty)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_EQ(v.v[BOOT_DAILY_REBOOT_HOUR_UTC], 10);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(BootOptions, MissingKeyKeepsItsDefault)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bank_slots\": 20}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 20);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(BootOptions, OutOfRangeValueUsesDefaultAndWarns)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse(
        "{\"bank_slots\": 0, \"daily_reboot_hour_utc\": 24, \"bank_coin_limit_gold\": 500}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_DAILY_REBOOT_HOUR_UTC], 10);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 500);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bank_slots: 0 out of range 1-100 - default 10 used");
}

TEST_F(BootOptions, UnknownKeyWarnsAndIsSkipped)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bogus\": 3, \"bank_slots\": 12}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 12);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bogus: unknown setting - ignored");
}

TEST_F(BootOptions, BrokenFileUsesAllDefaultsAndWarnsOnce)
{
    const char* broken[] = { "{", "not json", "{\"bank_slots\": \"ten\"}", "[1,2]" };
    for (const char* text : broken) {
        std::vector<std::string> warnings;
        boot_options_values v = boot_options_parse(text, &warnings);
        EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10) << text;
        EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000) << text;
        ASSERT_EQ(warnings.size(), 1u) << text;
        EXPECT_EQ(warnings[0].compare(0, 37, "BOOT OPTIONS: file unreadable - defau"), 0) << warnings[0];
    }
}

TEST_F(BootOptions, SerializeRoundTrips)
{
    boot_options_values v = boot_options_defaults();
    v.v[BOOT_BANK_SLOTS] = 25;
    std::vector<std::string> warnings;
    boot_options_values back = boot_options_parse(boot_options_serialize(v), &warnings);
    EXPECT_TRUE(warnings.empty());
    EXPECT_EQ(back.v[BOOT_BANK_SLOTS], 25);
}

TEST_F(BootOptions, IndexByName)
{
    EXPECT_EQ(boot_option_index("bank_slots"), BOOT_BANK_SLOTS);
    EXPECT_EQ(boot_option_index("BANK_SLOTS"), -1);
    EXPECT_EQ(boot_option_index(""), -1);
}

TEST_F(BootOptions, MissingFileLoadsDefaults)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10);
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
}

TEST_F(BootOptions, SetWritesTheFileButNotTheRunningValue)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    std::string error;
    ASSERT_TRUE(boot_option_set(BOOT_BANK_SLOTS, 30, &error, path.c_str())) << error;
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10);
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 30);
    boot_options_load(path.c_str()); /* "reboot" */
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 30);
    std::remove(path.c_str());
}

TEST_F(BootOptions, SetRefusesOutOfRangeAndLeavesTheFileAlone)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    std::string error;
    EXPECT_FALSE(boot_option_set(BOOT_BANK_SLOTS, 101, &error, path.c_str()));
    EXPECT_EQ(error, "bank_slots must be 1-100.");
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
    EXPECT_FALSE(std::ifstream(path).good());
}

TEST_F(BootOptions, SetIsRefusedWhileTheFileOnDiskWasUnreadableAtBoot)
{
    std::string path = temp_file();
    {
        std::ofstream out(path);
        out << "{ \"bank_slots\": 20, \"daily_reboot_hour_utc\": \"5\" }";
    }
    boot_options_load(path.c_str());
    EXPECT_TRUE(boot_options_file_unreadable());
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10) << "boot behaviour unchanged: all defaults";
    std::string error;
    EXPECT_FALSE(boot_option_set(BOOT_BANK_COIN_LIMIT_GOLD, 2000, &error, path.c_str()));
    EXPECT_EQ(error, "The settings file was unreadable at boot. Fix or remove it first.");
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"bank_slots\": 20"), std::string::npos) << "the hand-edited file is left alone";
    std::remove(path.c_str());

    boot_options_load(path.c_str()); /* missing file: fine again */
    EXPECT_FALSE(boot_options_file_unreadable());
    EXPECT_TRUE(boot_option_set(BOOT_BANK_COIN_LIMIT_GOLD, 2000, &error, path.c_str())) << error;
    std::remove(path.c_str());
    boot_options_load(path.c_str());
}

/* The reboot hour used to be written into limits.cpp as 10. */
TEST(DailyReboot, NoticesAndTheRebootFollowTheSetHourInUtc)
{
    const time_t day = 1790812800; /* 2026-10-01 00:00:00 UTC */
    auto at = [day](int hour, int minute) { return day + hour * 3600 + minute * 60 + 7; };
    EXPECT_EQ(daily_reboot_minutes_left(at(9, 30), 10), 30);
    EXPECT_EQ(daily_reboot_minutes_left(at(9, 55), 10), 5);
    EXPECT_EQ(daily_reboot_minutes_left(at(9, 56), 10), 4);
    EXPECT_EQ(daily_reboot_minutes_left(at(9, 59), 10), 1);
    EXPECT_EQ(daily_reboot_minutes_left(at(10, 0), 10), 0);
    EXPECT_EQ(daily_reboot_minutes_left(at(10, 1), 10), 0);
    EXPECT_EQ(daily_reboot_minutes_left(at(10, 2), 10), -1);
    EXPECT_EQ(daily_reboot_minutes_left(at(9, 31), 10), -1);
    EXPECT_EQ(daily_reboot_minutes_left(at(8, 30), 10), -1);
    EXPECT_EQ(daily_reboot_minutes_left(at(22, 0), 10), -1) << "not twice a day";

    EXPECT_EQ(daily_reboot_minutes_left(at(23, 30), 0), 30) << "an hour of 0 warns the evening before";
    EXPECT_EQ(daily_reboot_minutes_left(at(0, 0), 0), 0);
    EXPECT_EQ(daily_reboot_minutes_left(at(14, 59), 15), 1);
    EXPECT_EQ(daily_reboot_minutes_left(at(15, 1), 15), 0);
}
