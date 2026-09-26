#include "../mob_options.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

FILE* file_with(const char* text)
{
    FILE* f = tmpfile();
    fputs(text, f);
    rewind(f);
    return f;
}

} // namespace

TEST(MobOptionFind, FindsBareKeyAndKeyValue)
{
    std::string value;
    EXPECT_TRUE(mob_option_find("conj\n\rstore=12\n\r", "conj", &value));
    EXPECT_EQ(value, "");
    EXPECT_TRUE(mob_option_find("conj\n\rstore = 12 \n\r", "store", &value));
    EXPECT_EQ(value, "12");
    EXPECT_FALSE(mob_option_find("storeroom=1\n\r", "store", &value));
    EXPECT_FALSE(mob_option_find(nullptr, "store", &value));
    EXPECT_FALSE(mob_option_find("// conj\n\r", "// conj", &value)); // comments never match
}

TEST(MobOptionsStorable, RejectsTildeLeadingHashDollarAndOverLength)
{
    const char* why = nullptr;
    EXPECT_TRUE(mob_options_storable("store=1\n\rprice 1 2x3\n\r", &why));
    EXPECT_TRUE(mob_options_storable(nullptr, &why));
    EXPECT_FALSE(mob_options_storable("store=1~", &why));
    EXPECT_FALSE(mob_options_storable("  #store=1", &why));
    EXPECT_FALSE(mob_options_storable("\n\r$", &why));
    EXPECT_FALSE(mob_options_storable(std::string(MOB_OPTIONS_MAX + 1, 'a').c_str(), &why));
    EXPECT_TRUE(mob_options_storable(std::string(MOB_OPTIONS_MAX, 'a').c_str(), &why));
}

TEST(ReadMobOptions, NoOptionsWhenNextRecordFollows)
{
    char ctx[] = "test";
    FILE* f = file_with("\n\r#1235\n");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    char next[16];
    ASSERT_EQ(fscanf(f, "%15s", next), 1);
    EXPECT_STREQ(next, "#1235"); // the next record is left unread
    fclose(f);

    f = file_with("  \r\n$~\n");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    fclose(f);

    f = file_with("");
    EXPECT_EQ(read_mob_options(f, ctx), nullptr);
    fclose(f);
}

TEST(ReadMobOptions, ReadsTextThenStopsBeforeNextRecord)
{
    char ctx[] = "test";
    FILE* f = file_with("store=5\n\rprice 1 2x3~\n\r#1235\n");
    char* text = read_mob_options(f, ctx);
    ASSERT_NE(text, nullptr);
    std::string value;
    EXPECT_TRUE(mob_option_find(text, "store", &value));
    EXPECT_EQ(value, "5");
    char next[16];
    ASSERT_EQ(fscanf(f, "%15s", next), 1);
    EXPECT_STREQ(next, "#1235");
    fclose(f);
}

TEST(WriteMobOptions, WritesOnlyNonEmptyAndRoundTrips)
{
    char ctx[] = "test";
    FILE* f = tmpfile();
    write_mob_options(f, nullptr);
    write_mob_options(f, "");
    EXPECT_EQ(ftell(f), 0);
    write_mob_options(f, "store=5\n\rhours=6-20");
    fputs("#2\n", f);
    rewind(f);
    char* text = read_mob_options(f, ctx);
    ASSERT_NE(text, nullptr);
    std::string value;
    EXPECT_TRUE(mob_option_find(text, "hours", &value));
    EXPECT_EQ(value, "6-20");
    fclose(f);
}
