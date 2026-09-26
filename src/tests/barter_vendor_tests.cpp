#include "../mob_progs/passive.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

TEST(VendorList, AlignsCostsInOneColumnWithExtraCostsBelow)
{
    std::vector<vendor_list_row> rows = {
        { "a hunter's belt", 2, { { 1, "a leather belt" }, { 2, "a grey wolf hide" } } },
        { "a fur-lined cloak", -1, { { 4, "a grey wolf hide" } } },
    };
    std::string expected = " 1. a hunter's belt (2 left)  1 x a leather belt\n\r"
                           "                              2 x a grey wolf hide\n\r"
                           " 2. a fur-lined cloak         4 x a grey wolf hide\n\r";
    EXPECT_EQ(format_vendor_list(rows), expected);
}

TEST(VendorList, NumbersRightAlignPastNine)
{
    std::vector<vendor_list_row> rows;
    for (int i = 0; i < 10; ++i)
        rows.push_back({ "a pebble", -1, { { 1, "a coin" } } });
    std::string out = format_vendor_list(rows);
    EXPECT_NE(out.find(" 9. a pebble  1 x a coin\n\r"), std::string::npos);
    EXPECT_NE(out.find("10. a pebble  1 x a coin\n\r"), std::string::npos);
}

TEST(VendorList, LongNamesWrapAndCostsStayWithin78)
{
    std::vector<vendor_list_row> rows = {
        { "an enormous two-handed executioner's axe of Angmar", -1,
            { { 100, "a black arrowhead" }, { 2, "a grey wolf hide" } } },
    };
    std::string out = format_vendor_list(rows);
    size_t start = 0;
    int lines = 0;
    while (start < out.size()) {
        size_t end = out.find("\n\r", start);
        ASSERT_NE(end, std::string::npos);
        std::string line = out.substr(start, end - start);
        EXPECT_LE(line.size(), 78u) << line;
        if (line.size() > 44)
            EXPECT_TRUE(line.compare(44, 1, "1") == 0 || line.compare(44, 1, "2") == 0) << line; // costs at column 44
        start = end + 2;
        ++lines;
    }
    EXPECT_EQ(lines, 2);
    EXPECT_EQ(out.find(" 1. an enormous two-handed executioner's"), 0u);
}

TEST(VendorShortfalls, ListsEveryShortCurrencyAndNothingWhenCovered)
{
    std::map<int, int> have = { { 2222, 1 }, { 3333, 5 } };
    auto count = [&](int vnum) { return have.count(vnum) ? have[vnum] : 0; };
    std::vector<vendor_cost> costs = { { 2222, 2 }, { 3333, 2 }, { 4444, 1 } };
    std::vector<vendor_shortfall> s = vendor_shortfalls(costs, count);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].obj_vnum, 2222);
    EXPECT_EQ(s[0].need, 2);
    EXPECT_EQ(s[0].have, 1);
    EXPECT_EQ(s[1].obj_vnum, 4444);
    EXPECT_TRUE(vendor_shortfalls({ { 3333, 5 } }, count).empty());
}

TEST(VendorProblemLine, HouseStyle)
{
    EXPECT_EQ(vendor_problem_line(1234, { 3, "price: bad format - line skipped" }),
        "MOB ERROR: mobile #1234, options line 3: price: bad format - line skipped");
    EXPECT_EQ(vendor_problem_line(1234, { 0, "store missing - vendor disabled" }),
        "MOB ERROR: mobile #1234: store missing - vendor disabled");
}
