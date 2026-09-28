#include "../mob_options.h"
#include "../protos.h"

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

TEST(MobOptionsStorable, RejectsHashTildeLeadingDollarAndOverLength)
{
    const char* why = nullptr;
    EXPECT_TRUE(mob_options_storable("store=1\n\rprice 1 2x3\n\r", &why));
    EXPECT_TRUE(mob_options_storable(nullptr, &why));
    EXPECT_FALSE(mob_options_storable("store=1~", &why));
    EXPECT_FALSE(mob_options_storable("  #store=1", &why));
    EXPECT_STREQ(why, "options can't contain # or ~");
    EXPECT_FALSE(mob_options_storable("store=1\n\r// see #9999", &why)); // mid-text '#'
    EXPECT_STREQ(why, "options can't contain # or ~");
    EXPECT_TRUE(mob_options_storable("store=1\n\rprice 1 2x3 $", &why)); // '$' only leading
    EXPECT_FALSE(mob_options_storable("\n\r$", &why));
    EXPECT_STREQ(why, "options can't start with $");
    EXPECT_FALSE(mob_options_storable(std::string(MOB_OPTIONS_MAX + 1, 'a').c_str(), &why));
    EXPECT_TRUE(mob_options_storable(std::string(MOB_OPTIONS_MAX, 'a').c_str(), &why));
}

TEST(MobOptionsTrimLeading, DropsLeadingBlankLinesKeepsInternalOnes)
{
    char text[] = "\n\r  \n\r\tstore=1\n\r\n\rprice 1 2x3\n\r";
    mob_options_trim_leading(text);
    EXPECT_STREQ(text, "store=1\n\r\n\rprice 1 2x3\n\r");
    char blank[] = " \n\r ";
    mob_options_trim_leading(blank);
    EXPECT_STREQ(blank, "");
    char plain[] = "store=1";
    mob_options_trim_leading(plain);
    EXPECT_STREQ(plain, "store=1");
    mob_options_trim_leading(nullptr);
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

namespace {

/* The tail of a mob record as the boot loader (db.cpp) leaves it: the last
 * number row has been read, then " \n" was skipped. `options` sits between
 * that row and the next record, `eol` is the file's line ending. */
std::string mob_record_tail(const char* options, const char* eol)
{
    std::string s = std::string("0 5 0 0 0 0 0") + eol;
    if (options)
        s += std::string(options) + "~" + eol;
    return s + "#1235" + eol + "golem~" + eol;
}

void expect_loader_contract(const char* options, const char* eol)
{
    char ctx[] = "test";
    std::string tail = mob_record_tail(options, eol);
    FILE* f = tmpfile();
    fputs(tail.c_str(), f);
    rewind(f);
    int n[7];
    ASSERT_EQ(fscanf(f, " %d %d %d %d %d %d %d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6]), 7);
    EXPECT_EQ(n[1], 5);
    fscanf(f, " \n");
    char* text = read_mob_options(f, ctx);
    if (options) {
        ASSERT_NE(text, nullptr);
        std::string value;
        EXPECT_TRUE(mob_option_find(text, "store", &value));
        EXPECT_EQ(value, "1");
        EXPECT_NE(strstr(text, "price 1 2x1"), nullptr);
    } else {
        EXPECT_EQ(text, nullptr);
    }
    char next[16];
    ASSERT_EQ(fscanf(f, "%15s", next), 1);
    EXPECT_STREQ(next, "#1235"); // the next record's header is the next token
    fclose(f);
}

} // namespace

TEST(ReadMobOptions, LoaderContractWithAndWithoutOptions)
{
    expect_loader_contract(nullptr, "\n");
    expect_loader_contract("store=1\nprice 1 2x1", "\n");
    expect_loader_contract(nullptr, "\n\r");
    expect_loader_contract("store=1\n\rprice 1 2x1", "\n\r");
    expect_loader_contract(nullptr, "\r\n");
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

TEST(CleanText, ReplacesTildeEverywhereAndHashOnlyAtTheStartOfALine)
{
    char text[] = "#one ~ #two\n\r  #three\n\r\n\r#four";
    clean_text(text);
    EXPECT_STREQ(text, "+one - #two\n\r  +three\n\r\n\r+four");
}

TEST(CleanRecordText, ReplacesEveryHashAndTilde)
{
    char text[] = "see #3001 ~\n\r#5";
    clean_record_text(text);
    EXPECT_STREQ(text, "see +3001 -\n\r+5");
    clean_record_text(nullptr);
}

TEST(CleanRecordName, DropsLeadingDollarsAndCleansTheRest)
{
    char name[] = "$$Gold #1 vault $";
    clean_record_name(name);
    EXPECT_STREQ(name, "Gold +1 vault $");
    char plain[] = "Gold vault";
    clean_record_name(plain);
    EXPECT_STREQ(plain, "Gold vault");
    char only[] = "$$";
    clean_record_name(only);
    EXPECT_STREQ(only, "");
    clean_record_name(nullptr);
}
