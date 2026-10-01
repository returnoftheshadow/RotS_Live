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
} // namespace

TEST(BootOptions, DefaultsWhenTextIsEmpty)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_EQ(v.v[BOOT_BANK_DAY_START_HOUR], 5);
    EXPECT_TRUE(warnings.empty());
}

TEST(BootOptions, MissingKeyKeepsItsDefault)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bank_slots\": 20}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 20);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 1000);
    EXPECT_TRUE(warnings.empty());
}

TEST(BootOptions, OutOfRangeValueUsesDefaultAndWarns)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse(
        "{\"bank_slots\": 0, \"bank_day_start_hour\": 24, \"bank_coin_limit_gold\": 500}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 10);
    EXPECT_EQ(v.v[BOOT_BANK_DAY_START_HOUR], 5);
    EXPECT_EQ(v.v[BOOT_BANK_COIN_LIMIT_GOLD], 500);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bank_slots: 0 out of range 1-100 - default 10 used");
}

TEST(BootOptions, UnknownKeyWarnsAndIsSkipped)
{
    std::vector<std::string> warnings;
    boot_options_values v = boot_options_parse("{\"bogus\": 3, \"bank_slots\": 12}", &warnings);
    EXPECT_EQ(v.v[BOOT_BANK_SLOTS], 12);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0], "BOOT OPTIONS: bogus: unknown setting - ignored");
}

TEST(BootOptions, BrokenFileUsesAllDefaultsAndWarnsOnce)
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

TEST(BootOptions, SerializeRoundTrips)
{
    boot_options_values v = boot_options_defaults();
    v.v[BOOT_BANK_SLOTS] = 25;
    std::vector<std::string> warnings;
    boot_options_values back = boot_options_parse(boot_options_serialize(v), &warnings);
    EXPECT_TRUE(warnings.empty());
    EXPECT_EQ(back.v[BOOT_BANK_SLOTS], 25);
}

TEST(BootOptions, IndexByName)
{
    EXPECT_EQ(boot_option_index("bank_slots"), BOOT_BANK_SLOTS);
    EXPECT_EQ(boot_option_index("BANK_SLOTS"), -1);
    EXPECT_EQ(boot_option_index(""), -1);
}

TEST(BootOptions, MissingFileLoadsDefaults)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    EXPECT_EQ(boot_option(BOOT_BANK_SLOTS), 10);
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
}

TEST(BootOptions, SetWritesTheFileButNotTheRunningValue)
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

TEST(BootOptions, SetRefusesOutOfRangeAndLeavesTheFileAlone)
{
    std::string path = temp_file();
    boot_options_load(path.c_str());
    std::string error;
    EXPECT_FALSE(boot_option_set(BOOT_BANK_SLOTS, 101, &error, path.c_str()));
    EXPECT_EQ(error, "bank_slots must be 1-100.");
    EXPECT_EQ(boot_option_pending(BOOT_BANK_SLOTS), 10);
    EXPECT_FALSE(std::ifstream(path).good());
}
