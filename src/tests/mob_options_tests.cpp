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

namespace {

vendor_lookups everything_exists()
{
    vendor_lookups l;
    l.obj_exists = [](int v) { return v != 9999; };
    l.room_exists = [](int v) { return v != 9998; };
    return l;
}

std::vector<std::string> problem_texts(const std::vector<vendor_problem>& problems)
{
    std::vector<std::string> out;
    for (const vendor_problem& p : problems)
        out.push_back(std::to_string(p.line) + ": " + p.text);
    return out;
}

} // namespace

TEST(VendorParse, GoodConfig)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "store=12345\n\rhours=6-12,14-20\n\rprice 5001 2222x1 3333x2 deduct\n\rprice 5002 3333x4\n\r",
        everything_exists(), &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problem_texts(problems));
    EXPECT_TRUE(c.usable());
    EXPECT_EQ(c.store_vnum, 12345);
    ASSERT_EQ(c.prices.size(), 2u);
    EXPECT_EQ(c.prices[0].item_vnum, 5001);
    ASSERT_EQ(c.prices[0].costs.size(), 2u);
    EXPECT_EQ(c.prices[0].costs[1].obj_vnum, 3333);
    EXPECT_EQ(c.prices[0].costs[1].qty, 2);
    EXPECT_TRUE(c.prices[0].deduct);
    EXPECT_FALSE(c.prices[1].deduct);
    EXPECT_EQ(c.prices[1].line, 4);
}

TEST(VendorParse, StoreMissingOrBadDisables)
{
    std::vector<vendor_problem> problems;
    EXPECT_FALSE(parse_vendor_options("price 1 2x1", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems).back(), "0: store missing - vendor disabled");

    problems.clear();
    EXPECT_FALSE(parse_vendor_options("store=9998", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems)[0], "1: store room vnum 9998 not found - vendor disabled");

    problems.clear();
    EXPECT_FALSE(parse_vendor_options("store=abc", everything_exists(), &problems).usable());
    EXPECT_EQ(problem_texts(problems)[0], "1: bad store - vendor disabled");
}

TEST(VendorParse, BadHoursDisablesStrictly)
{
    const char* bad[] = { "hours=6", "hours=24-2", "hours=5-5", "hours=a-b", "hours=6-20,", "hours=" };
    for (const char* line : bad) {
        std::vector<vendor_problem> problems;
        std::string text = std::string("store=1\n\r") + line;
        vendor_config c = parse_vendor_options(text.c_str(), everything_exists(), &problems);
        EXPECT_FALSE(c.usable()) << line;
        ASSERT_FALSE(problems.empty()) << line;
        EXPECT_EQ(problems[0].text, "bad hours - vendor disabled") << line;
    }
}

TEST(VendorParse, BadPriceLinesAreSkippedOthersKept)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "store=1\n\r"
        "price 10 20x1\n\r" // 2 ok
        "price 11\n\r" // 3 no costs
        "price 12 20x0\n\r" // 4 qty low
        "price 13 20x101\n\r" // 5 qty high
        "price 14 20x1 21x1 22x1 23x1 24x1\n\r" // 6 five currencies
        "price 15 9999x1\n\r" // 7 unknown currency
        "price 9999 20x1\n\r" // 8 unknown item
        "price 10 20x2\n\r" // 9 duplicate item
        "price 16 20x1 20x2\n\r" // 10 currency twice
        "price 17 20X1\n\r" // 11 bad token
        "price 18 deduct 20x1\n\r" // 12 deduct not last
        "bogus=1\n\r", // 13 unknown
        everything_exists(), &problems);
    EXPECT_TRUE(c.usable());
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].item_vnum, 10);
    std::vector<std::string> expected = {
        "3: price: bad format - line skipped",
        "4: price: quantity 0 out of range - line skipped",
        "5: price: quantity 101 out of range - line skipped",
        "6: price: more than 4 currencies - line skipped",
        "7: price: object vnum 9999 not found - line skipped",
        "8: price: object vnum 9999 not found - line skipped",
        "9: price: duplicate item vnum 10 - line skipped",
        "10: price: currency vnum 20 listed twice - line skipped",
        "11: price: bad format - line skipped",
        "12: price: bad format - line skipped",
        "13: unknown setting - line ignored",
    };
    EXPECT_EQ(problem_texts(problems), expected);
}

TEST(VendorParse, ThirtyLineLimit)
{
    std::string text = "store=1\n\r";
    for (int i = 0; i < 31; ++i)
        text += "price " + std::to_string(100 + i) + " 20x1\n\r";
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(text.c_str(), everything_exists(), &problems);
    EXPECT_EQ(c.prices.size(), 30u);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problem_texts(problems)[0], "32: price: more than 30 lines - line skipped");
}

TEST(VendorParse, CrLfAndBlankLinesAndDuplicateSettings)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options("\r\n  store = 7 \r\n\r\nstore=8\r\nprice 1 2x3 \r\n",
        everything_exists(), &problems);
    EXPECT_EQ(c.store_vnum, 7);
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].line, 5);
    EXPECT_EQ(problem_texts(problems), std::vector<std::string> { "4: duplicate store - line ignored" });
}

TEST(VendorParse, CommentLinesAreSkippedAndNeverWarned)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options(
        "// winter stock\n\rstore=1\n\r  // price 1 9999x1 (off for now)\n\rprice 2 3x1\n\r",
        everything_exists(), &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problem_texts(problems));
    ASSERT_EQ(c.prices.size(), 1u);
    EXPECT_EQ(c.prices[0].line, 4);
}

TEST(VendorParse, ListMessageKeptVerbatimEmptyMeansDefault)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options("store=1\n\rlist=  Ah, a customer! 50% off:  \n\rlist=second\n\r",
        everything_exists(), &problems);
    EXPECT_EQ(c.list_message, "Ah, a customer! 50% off:");
    EXPECT_EQ(problem_texts(problems), std::vector<std::string> { "3: duplicate list - line ignored" });

    problems.clear();
    EXPECT_EQ(parse_vendor_options("store=1\n\rlist=\n\r", everything_exists(), &problems).list_message, "");
    EXPECT_EQ(parse_vendor_options("store=1", everything_exists(), &problems).list_message, "");
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problem_texts(problems));
}

TEST(VendorHours, OpenWindows)
{
    vendor_config c;
    ASSERT_TRUE(vendor_hours_parse("6-12,14-20", &c.hours));
    EXPECT_FALSE(vendor_is_open(c, 5));
    EXPECT_TRUE(vendor_is_open(c, 6));
    EXPECT_TRUE(vendor_is_open(c, 11));
    EXPECT_FALSE(vendor_is_open(c, 12));
    EXPECT_FALSE(vendor_is_open(c, 13));
    EXPECT_TRUE(vendor_is_open(c, 14));
    EXPECT_FALSE(vendor_is_open(c, 20));

    ASSERT_TRUE(vendor_hours_parse("20-4", &c.hours));
    EXPECT_TRUE(vendor_is_open(c, 20));
    EXPECT_TRUE(vendor_is_open(c, 23));
    EXPECT_TRUE(vendor_is_open(c, 0));
    EXPECT_TRUE(vendor_is_open(c, 3));
    EXPECT_FALSE(vendor_is_open(c, 4));
    EXPECT_FALSE(vendor_is_open(c, 12));

    c.hours.clear();
    EXPECT_TRUE(vendor_is_open(c, 12)); // no hours = always open
}

/* World text written by the shaping editors.  The mob/obj/room/script record
 * scanners take any '#' as a record header, zone and mudlle scanners only a
 * '#' that starts a line, and the room/script loaders stop the file at a name
 * that starts with '$'. */
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
