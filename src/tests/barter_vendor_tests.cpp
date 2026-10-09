#include "../mob_progs/shopkeeper.h"

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>

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

/* SPECIAL(barter_vendor) against a one-room world: room 0 (vnum 5000) is both
 * the shop floor and the store room, with a belt (100) sold for hides (200). */

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern struct time_info_data time_info;
void remove_char_exists(int num);
void set_char_exists(int num);
int char_exists(int num);
void set_fighting(struct char_data* ch, struct char_data* vict);
int shop_keeper(struct char_data* host, struct char_data* ch, int cmd, char* arg, int callflag, struct waiting_type* wtl);
extern struct char_data* combat_list;
extern struct char_data* waiting_list;
void do_mental(struct char_data* ch, char* argument, struct waiting_type* wtl, int cmd, int subcmd);
combat_result_struct damage_stat(struct char_data* killer, struct char_data* victim, int stat_num, int amount);
extern int no_specials;
void clear_char(struct char_data* ch, int mode);
void clear_object(struct obj_data* obj);

namespace {

constexpr int kStoreVnum = 5000;
constexpr int kBeltVnum = 100;
constexpr int kHideVnum = 200;
constexpr int kLeatherVnum = 300;
constexpr int kVendorVnum = 7000;

class BarterVendorTest : public ::testing::Test {
protected:
    static constexpr int kVendorSlot = 4400; /* a character slot nothing else in the tests uses */

    void SetUp() override
    {
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_saved_obj_proto = obj_proto;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;
        m_saved_top_of_world = top_of_world;
        m_saved_descriptor_list = descriptor_list;
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);
        m_saved_number = world[0].number;
        m_saved_light = world[0].light;
        m_saved_contents = world[0].contents;
        m_saved_people = world[0].people;

        descriptor_list = nullptr;
        object_list = nullptr;
        top_of_world = 0;
        world[0].number = kStoreVnum;
        world[0].light = 1;
        world[0].contents = nullptr;

        for (obj_data& proto : m_obj_proto)
            clear_object(&proto);
        m_obj_proto[0].item_number = 0;
        m_obj_proto[0].name = m_belt_name;
        m_obj_proto[0].short_description = m_belt_short;
        m_obj_proto[0].obj_flags.weight = 10;
        m_obj_proto[1].item_number = 1;
        m_obj_proto[1].name = m_hide_name;
        m_obj_proto[1].short_description = m_hide_short;
        m_obj_proto[1].obj_flags.weight = 10;
        m_obj_proto[2].item_number = 2;
        m_obj_proto[2].name = m_leather_name;
        m_obj_proto[2].short_description = m_leather_short;
        m_obj_proto[2].obj_flags.weight = 10;
        m_obj_index[0].virt = kBeltVnum;
        m_obj_index[1].virt = kHideVnum;
        m_obj_index[2].virt = kLeatherVnum;
        obj_proto = m_obj_proto;
        obj_index = m_obj_index;
        top_of_objt = 2;

        m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
        m_mob_proto[0].specials.store_prog_number = PROG_BARTER_VENDOR;
        m_mob_proto[0].specials.mob_options = m_options;
        m_mob_proto[0].abilities.intel = 12;
        m_mob_index[0].virt = kVendorVnum;
        m_mob_index[0].func = nullptr;
        mob_proto = m_mob_proto;
        mob_index = m_mob_index;
        top_of_mobt = 0;
        vendor_config_rebuild(0, nullptr);

        clear_char(&m_vendor, MOB_ISNPC);
        m_vendor.nr = 0;
        m_vendor.abs_number = kVendorSlot; /* as register_npc_char gives every mob in the game */
        set_char_exists(kVendorSlot);
        m_vendor.specials2.act = MOB_ISNPC | MOB_SPEC;
        m_vendor.player.name = m_vendor_name;
        m_vendor.player.short_descr = m_vendor_short;
        m_vendor.tmpabilities.intel = 12;
        m_vendor.in_room = 0;

        clear_char(&m_buyer, 0);
        m_buyer.player.name = m_buyer_name;
        m_buyer.player.race = RACE_HUMAN;
        m_buyer.player.level = 10;
        m_buyer.tmpabilities.str = 18;
        m_buyer.tmpabilities.dex = 18;
        m_buyer.in_room = 0;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1; /* do_say and do_tell only reach a connected player */
        m_descriptor.character = &m_buyer;
        m_buyer.desc = &m_descriptor;

        world[0].people = &m_vendor;
        m_vendor.next_in_room = &m_buyer;
        m_buyer.next_in_room = nullptr;

        stock_belts(2);
    }

    void TearDown() override
    {
        remove_char_exists(kVendorSlot);
        while (object_list != nullptr)
            extract_obj(object_list);
        m_mob_proto[0].specials.store_prog_number = 0;
        vendor_config_rebuild(0, nullptr); /* erases the registry entry */

        world[0].number = m_saved_number;
        world[0].light = m_saved_light;
        world[0].contents = m_saved_contents;
        world[0].people = m_saved_people;
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        obj_proto = m_saved_obj_proto;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
        top_of_world = m_saved_top_of_world;
        descriptor_list = m_saved_descriptor_list;
    }

    /* Replaces the store stock, e.g. after a test changes the belt's weight
     * (deduct hands over the store copy itself). */
    void restock_belts(int count)
    {
        while (world[0].contents)
            extract_obj(world[0].contents);
        stock_belts(count);
    }

    void stock_belts(int count)
    {
        for (int i = 0; i < count; ++i)
            obj_to_room(read_object(0, REAL), 0);
    }

    obj_data* give_hides(int count)
    {
        obj_data* last = nullptr;
        for (int i = 0; i < count; ++i) {
            last = read_object(1, REAL);
            obj_to_char(last, &m_buyer);
        }
        return last;
    }

    int call(int cmd, const char* text, int callflag = SPECIAL_COMMAND)
    {
        std::strncpy(m_arg, text, sizeof(m_arg) - 1);
        m_arg[sizeof(m_arg) - 1] = '\0';
        return barter_vendor(&m_vendor, &m_buyer, cmd, m_arg, callflag, nullptr);
    }

    static int count_in(obj_data* list, int rnum)
    {
        int count = 0;
        for (obj_data* obj = list; obj; obj = obj->next_content)
            if (obj->item_number == rnum)
                ++count;
        return count;
    }

    int floor_belts() const { return count_in(world[0].contents, 0); }
    int carried(int rnum) const { return count_in(m_buyer.carrying, rnum); }
    std::string output() const { return std::string(m_descriptor.output); }
    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    char m_options[64] = "store=5000\nprice 100 200x2 deduct";
    char m_belt_name[16] = "belt hunter";
    char m_belt_short[20] = "a hunter's belt";
    char m_hide_name[16] = "hide wolf";
    char m_hide_short[20] = "a wolf hide";
    char m_leather_name[16] = "leather belt";
    char m_leather_short[20] = "a leather belt";
    char m_vendor_name[16] = "trader vendor";
    char m_vendor_short[16] = "the trader";
    char m_buyer_name[16] = "Buyer";
    char m_arg[MAX_INPUT_LENGTH] = "";

    obj_data m_obj_proto[3] {};
    index_data m_obj_index[3] {};
    char_data m_mob_proto[1] {};
    index_data m_mob_index[1] {};
    char_data m_vendor {};
    char_data m_buyer {};
    descriptor_data m_descriptor {};

private:
    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    obj_data* m_saved_obj_proto = nullptr;
    index_data* m_saved_obj_index = nullptr;
    int m_saved_top_of_objt = 0;
    obj_data* m_saved_object_list = nullptr;
    int m_saved_top_of_world = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    int m_saved_number = 0;
    byte m_saved_light = 0;
    obj_data* m_saved_contents = nullptr;
    char_data* m_saved_people = nullptr;
};

TEST_F(BarterVendorTest, RegisteredVendorRefusesDamage)
{
    EXPECT_TRUE(call(0, "", SPECIAL_DAMAGE));
}

TEST_F(BarterVendorTest, SelfDamageIsRefusedSilently)
{
    /* A poison tick is damage(vendor, vendor, ...): the vendor is its own
     * attacker. It must still be cancelled, without the vendor talking. */
    m_descriptor.descriptor = 1; /* do_say skips descriptors without a socket */
    std::strcpy(m_arg, "");
    EXPECT_TRUE(barter_vendor(&m_vendor, &m_vendor, 0, m_arg, SPECIAL_DAMAGE, nullptr));
    EXPECT_EQ(output(), "");
}

TEST_F(BarterVendorTest, DustAimedAtVendorIsRefused)
{
    /* Dust blinds even when its damage is cancelled, and a blind vendor
     * refuses every buyer, so the command itself is refused. */
    m_descriptor.descriptor = 1; /* do_say skips descriptors without a socket */
    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ptr.ch = &m_vendor;
    std::strcpy(m_arg, "trader");
    EXPECT_TRUE(barter_vendor(&m_vendor, &m_buyer, CMD_BLINDING, m_arg, SPECIAL_TARGET, &wtl));
    EXPECT_NE(output().find("Don't even think about it."), std::string::npos);
}

TEST_F(BarterVendorTest, OtherTargetedCommandsPassThrough)
{
    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ptr.ch = &m_vendor;
    std::strcpy(m_arg, "trader");
    EXPECT_FALSE(barter_vendor(&m_vendor, &m_buyer, CMD_LOOK, m_arg, SPECIAL_TARGET, &wtl));
    wtl.targ1.ptr.ch = &m_buyer;
    EXPECT_FALSE(barter_vendor(&m_vendor, &m_buyer, CMD_BLINDING, m_arg, SPECIAL_TARGET, &wtl));
    EXPECT_FALSE(barter_vendor(&m_vendor, &m_buyer, CMD_BLINDING, m_arg, SPECIAL_TARGET, nullptr));
}

TEST_F(BarterVendorTest, PlayerHostIsNeverAVendor)
{
    /* A player whose rnum field happens to match a registered vendor's. */
    m_buyer.nr = 0;
    std::strcpy(m_arg, "");
    EXPECT_FALSE(barter_vendor(&m_buyer, &m_vendor, 0, m_arg, SPECIAL_DAMAGE, nullptr));
    EXPECT_FALSE(barter_vendor(&m_buyer, &m_vendor, CMD_LIST, m_arg, SPECIAL_COMMAND, nullptr));
}

TEST_F(BarterVendorTest, UnregisteredNpcIsNeverAVendor)
{
    m_mob_proto[0].specials.store_prog_number = 0;
    vendor_config_rebuild(0, nullptr);
    ASSERT_EQ(vendor_config_for(0), nullptr);
    EXPECT_FALSE(call(0, "", SPECIAL_DAMAGE));
    EXPECT_FALSE(call(CMD_LIST, ""));
    EXPECT_FALSE(call(CMD_BUY, "1"));
    EXPECT_EQ(floor_belts(), 2);
}

TEST_F(BarterVendorTest, OtherCommandsPassThrough)
{
    EXPECT_FALSE(call(CMD_SELL, "belt"));
}

TEST_F(BarterVendorTest, ListShowsStockCountAndPrice)
{
    EXPECT_TRUE(call(CMD_LIST, ""));
    EXPECT_EQ(output(), "What would you like to trade?\n\r 1. a hunter's belt (2 left)  2 x a wolf hide\n\r");
    EXPECT_EQ(floor_belts(), 2);
}

TEST_F(BarterVendorTest, ListStartsWithTheVendorsOwnListLine)
{
    std::strcpy(m_options, "store=5000\nlist=Hides for belts!\nprice 100 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    EXPECT_TRUE(call(CMD_LIST, ""));
    EXPECT_EQ(output(), "Hides for belts!\n\r 1. a hunter's belt (2 left)  2 x a wolf hide\n\r");
}

TEST_F(BarterVendorTest, BuyPaysDeductsAndHandsOverTheItem)
{
    give_hides(3);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 1);
    EXPECT_EQ(floor_belts(), 1);
    EXPECT_EQ(output(), "You hand over:\n\r  2 x a wolf hide\n\rYou now have a hunter's belt.\n\r");
}

/* Payment weight comes off and the bought item's weight goes on, once each.
 * Distinct weights (hide 7, belt 25) so a missed or doubled change shows. */
TEST_F(BarterVendorTest, BuyMovesCarriedWeightAndCount)
{
    m_obj_proto[0].obj_flags.weight = 25;
    m_obj_proto[1].obj_flags.weight = 7;
    restock_belts(2);
    give_hides(3);
    ASSERT_EQ(IS_CARRYING_W(&m_buyer), 21);
    ASSERT_EQ(IS_CARRYING_N(&m_buyer), 3);

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(IS_CARRYING_W(&m_buyer), 21 - 2 * 7 + 25);
    EXPECT_EQ(IS_CARRYING_N(&m_buyer), 3 - 2 + 1);
}

/* A rider's inventory weight is also carried by the mount (obj_to_char and
 * obj_from_char both update it), so a purchase must move the mount's too. */
TEST_F(BarterVendorTest, BuyWhileRidingMovesTheMountsWeight)
{
    constexpr int kMountAbsNumber = 4321;
    char_data mount {};
    clear_char(&mount, MOB_ISNPC);
    mount.abs_number = kMountAbsNumber;
    set_char_exists(kMountAbsNumber);
    m_buyer.mount_data.mount = &mount;
    m_buyer.mount_data.mount_number = kMountAbsNumber;
    ASSERT_TRUE(IS_RIDING(&m_buyer));

    m_obj_proto[0].obj_flags.weight = 25;
    m_obj_proto[1].obj_flags.weight = 7;
    restock_belts(2);
    give_hides(3);
    ASSERT_EQ(IS_CARRYING_W(&mount), 21);

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(IS_CARRYING_W(&m_buyer), 21 - 2 * 7 + 25);
    EXPECT_EQ(IS_CARRYING_W(&mount), 21 - 2 * 7 + 25);

    m_buyer.mount_data.mount = nullptr;
    remove_char_exists(kMountAbsNumber);
}

TEST_F(BarterVendorTest, BuyByKeyword)
{
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "belt"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 0);
}

TEST_F(BarterVendorTest, BuyNumberedKeywordCountsMatchingRowsNotTheList)
{
    std::strcpy(m_options, "store=5000\nprice 100 200x2\nprice 200 300x1\nprice 300 200x2");
    vendor_config_rebuild(0, nullptr);
    obj_to_room(read_object(1, REAL), 0); /* a hide for sale: row 2 */
    obj_to_room(read_object(2, REAL), 0); /* a leather belt: row 3, the 2nd belt */
    give_hides(2);

    EXPECT_TRUE(call(CMD_BUY, "2.belt"));
    EXPECT_EQ(carried(2), 1) << "2.belt is the leather belt, not list row 2";
    EXPECT_EQ(carried(1), 0);
}

TEST_F(BarterVendorTest, BuyArgumentIsAListNumberOnlyWhenAllDigits)
{
    give_hides(4);
    EXPECT_TRUE(call(CMD_BUY, "3.belt"));
    EXPECT_EQ(carried(0), 0) << "only one row matches belt";
    EXPECT_TRUE(call(CMD_BUY, "1x"));
    EXPECT_EQ(carried(0), 0) << "1x is a keyword, not row 1";
    EXPECT_TRUE(call(CMD_BUY, "1.belt"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 2);
}

TEST_F(BarterVendorTest, LowIntelligenceIsWarnedOnRebuild)
{
    m_mob_proto[0].abilities.intel = 3;
    vendor_config_rebuild(0, &m_buyer);
    EXPECT_EQ(output(), "MOB ERROR: mobile #7000: intelligence below 6 - vendor can't speak\n\r");
    m_mob_proto[0].abilities.intel = 12;
}

/* act() would read a '$' in the text as a code and crash on an unknown one. */
TEST_F(BarterVendorTest, ADollarSignInATellIsShownAsTyped)
{
    vendor_tell(&m_vendor, &m_buyer, "That is a $p and a $ and $x.");
    EXPECT_NE(output().find(" tells you 'That is a $p and a $ and $x.'"), std::string::npos) << output();
    clear_output();
    std::string longest = std::string(VENDOR_TELL_MAX - 1, 'a') + "$$$";
    vendor_tell(&m_vendor, &m_buyer, longest.c_str());
    EXPECT_NE(output().find(std::string(VENDOR_TELL_MAX - 1, 'a') + "'"), std::string::npos) << output();
}

TEST_F(BarterVendorTest, ATellReachesABuyerWithTellsTurnedOff)
{
    SET_BIT(PRF_FLAGS(&m_buyer), PRF_NOTELL);
    EXPECT_TRUE(call(CMD_BUY, ""));
    EXPECT_NE(output().find(" tells you 'What do you want to buy?'"), std::string::npos) << output();
    EXPECT_TRUE(PRF_FLAGGED(&m_buyer, PRF_NOTELL)) << "the buyer's own setting is left as it was";
    REMOVE_BIT(PRF_FLAGS(&m_buyer), PRF_NOTELL);
}

/* Gifts, attacks and dust are answered before any can-see check. */
TEST_F(BarterVendorTest, ATellReachesABuyerTheVendorCannotSee)
{
    SET_BIT(m_buyer.specials.affected_by, AFF_INVISIBLE);
    EXPECT_TRUE(call(0, "", SPECIAL_DAMAGE));
    EXPECT_NE(output().find(" tells you 'Don't even think about it.'"), std::string::npos) << output();
    REMOVE_BIT(m_buyer.specials.affected_by, AFF_INVISIBLE);
}

TEST_F(BarterVendorTest, AShortfallOfSeveralItemsIsOneTell)
{
    std::strcpy(m_options, "store=5000\nprice 100 200x2 300x1 deduct");
    vendor_config_rebuild(0, nullptr);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_NE(output().find(" tells you 'You need 2 x a wolf hide (you have 0) and 1 x a leather belt (you have 0).'"),
        std::string::npos)
        << output();
}

TEST_F(BarterVendorTest, RebuildCanSkipTheReport)
{
    m_mob_proto[0].abilities.intel = 3;
    vendor_config_rebuild(0, &m_buyer, false);
    EXPECT_NE(vendor_config_for(0), nullptr);
    EXPECT_EQ(output(), "");
    vendor_config_rebuild(0, &m_buyer);
    EXPECT_EQ(output(), "MOB ERROR: mobile #7000: intelligence below 6 - vendor can't speak\n\r");
    m_mob_proto[0].abilities.intel = 12;
}

/* 67 of the 75 old shopkeepers choose who they serve by race aggression. */
TEST_F(BarterVendorTest, RaceAggressionIsNotReported)
{
    m_mob_proto[0].specials2.pref = 1;
    vendor_config_rebuild(0, &m_buyer);
    EXPECT_EQ(output(), "");
    vendor_config_check(&m_mob_proto[0], kVendorVnum, &m_buyer);
    EXPECT_EQ(output(), "");
    m_mob_proto[0].specials2.pref = 0;
}

TEST_F(BarterVendorTest, AttacksOptionDefaultsToYesAndNoEndsTheMobsTurn)
{
    EXPECT_TRUE(vendor_config_for(0)->attacks);
    EXPECT_FALSE(call(0, "", SPECIAL_SELF)) << "the mob AI carries on, as for an old shopkeeper";

    std::strcpy(m_options, "store=5000\nattacks=no\nprice 100 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    EXPECT_FALSE(vendor_config_for(0)->attacks);
    EXPECT_TRUE(call(0, "", SPECIAL_SELF)) << "the mob AI stops: no attack, assist or wandering";
    EXPECT_TRUE(call(CMD_LIST, ""));
    EXPECT_NE(output().find("hunter's belt"), std::string::npos) << "it still trades: " << output();

    std::vector<vendor_problem> problems;
    EXPECT_TRUE(parse_vendor_options("store=12345\nattacks=maybe", everything_exists(), &problems).attacks);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].text, "bad attacks - line ignored");
}

TEST_F(BarterVendorTest, FightFlagsAreWarnedOnImplementOnlyWithAttacksOff)
{
    m_mob_proto[0].specials2.act |= MOB_NOBASH | MOB_MEMORY | MOB_HELPER;
    vendor_implement_check(0, &m_buyer);
    EXPECT_EQ(output(), "") << "a vendor that attacks may carry them";
    std::strcpy(m_options, "store=5000\nattacks=no\nprice 100 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    vendor_implement_check(0, &m_buyer);
    EXPECT_EQ(output(), "MOB WARNING: mobile #7000: MEMORY flag with attacks=no\n\r"
                        "MOB WARNING: mobile #7000: HELPER flag with attacks=no\n\r");
}

TEST(VendorList, ALongCurrencyNameWrapsInsteadOfPassing78Columns)
{
    std::vector<vendor_list_row> rows = { { std::string(38, 'n'), -1,
        { { 100, "a currency with a very long name that would otherwise run off the line" } } } };
    std::string out = format_vendor_list(rows);
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find("\n\r", start);
        ASSERT_NE(end, std::string::npos);
        EXPECT_LE(end - start, 78u) << out.substr(start, end - start);
        start = end + 2;
    }
    EXPECT_NE(out.find("100 x a currency with a very long"), std::string::npos) << out;
    EXPECT_NE(out.find("the line"), std::string::npos) << out;
}

TEST_F(BarterVendorTest, VendorCandidateRule)
{
    EXPECT_TRUE(is_vendor_candidate(&m_mob_proto[0], 0));
    EXPECT_TRUE(is_vendor_candidate(&m_mob_proto[0], -1)) << "not in the table yet";
    m_mob_index[0].func = (special_func)barter_vendor;
    EXPECT_TRUE(is_vendor_candidate(&m_mob_proto[0], 0));
    m_mob_index[0].func = +[](char_data*, char_data*, int, char*, int, waiting_type*) { return 0; };
    EXPECT_FALSE(is_vendor_candidate(&m_mob_proto[0], 0)) << "hard-coded function owns the slot";
    m_mob_index[0].func = nullptr;
    m_mob_proto[0].specials2.act = MOB_ISNPC;
    EXPECT_FALSE(is_vendor_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
    m_mob_proto[0].specials.store_prog_number = 32;
    EXPECT_FALSE(is_vendor_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials.store_prog_number = PROG_BARTER_VENDOR;
}

/* "buy hide" makes the command parser pick the buyer's own hide as the
 * command's target; the hide is then destroyed as payment, and the caller
 * still reads the target afterwards. */
TEST_F(BarterVendorTest, APaymentObjectIsDroppedFromTheCommandTargets)
{
    obj_data* hide = give_hides(2);
    waiting_type wtl {};
    wtl.targ1.type = TARGET_OBJ;
    wtl.targ1.ptr.obj = hide;
    std::strcpy(m_arg, "1");
    EXPECT_TRUE(barter_vendor(&m_vendor, &m_buyer, CMD_BUY, m_arg, SPECIAL_COMMAND, &wtl));
    EXPECT_EQ(carried(1), 0) << "both hides were paid";
    EXPECT_EQ(wtl.targ1.type, TARGET_NONE);
    EXPECT_EQ(wtl.targ1.ptr.other, nullptr);
}

/* trim() keeps \v and \f, the word splitter drops them. */
TEST(VendorParse, ALineOfOnlyVerticalTabOrFormFeedIsSkipped)
{
    std::vector<vendor_problem> problems;
    vendor_config c = parse_vendor_options("store=12345\n\v\n\f\f\nprice 5002 3333x4\n", everything_exists(), &problems);
    EXPECT_TRUE(c.usable());
    EXPECT_EQ(c.prices.size(), 1u);
}

/* damage() and the mental attack ask a victim's program through special().
 * It used to ask only while character slot 0 existed (the callers never set
 * the target's ch_num), so every keeper lost its protection once the first
 * mob loaded at boot was gone. The callers now name the victim's own slot,
 * which runs to MAX_CHARACTERS: past what the 16-bit ch_num once held. */
TEST_F(BarterVendorTest, TheDamageQuestionReachesTheVendorWithoutCharacterSlotZero)
{
    constexpr int kHighSlot = 40000;
    const bool slot_zero_existed = char_exists(0);
    remove_char_exists(0);
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    m_vendor.abs_number = kHighSlot;
    set_char_exists(kHighSlot);
    waiting_type wtl {};
    wtl.targ1.set_character(&m_vendor); /* as damage(), hit(), damage_stat() and do_mental() name their victim */
    EXPECT_EQ(wtl.targ1.type, TARGET_CHAR);
    EXPECT_EQ(wtl.targ1.ptr.ch, &m_vendor);
    EXPECT_EQ(wtl.targ1.ch_num, kHighSlot) << "ch_num holds every slot number";
    std::strcpy(m_arg, "");
    EXPECT_EQ(special(&m_buyer, 0, m_arg, SPECIAL_DAMAGE, &wtl), 1) << "the hit is cancelled";
    EXPECT_NE(output().find(" tells you 'Don't even think about it.'"), std::string::npos) << output();

    /* A target that has gone since (its slot is free) is not asked, for a hit as for a command. */
    remove_char_exists(kHighSlot);
    EXPECT_EQ(special(&m_buyer, 0, m_arg, SPECIAL_DAMAGE, &wtl), 0);
    if (slot_zero_existed)
        set_char_exists(0);
}

/* A character that was never given a slot (stat file and set file build one
 * to read a player file into) is slot -1, not slot 0: freeing it used to mark
 * the first character registered at boot as gone. */
TEST_F(BarterVendorTest, FreeingACharacterWithNoSlotLeavesSlotZeroAlone)
{
    const bool slot_zero_existed = char_exists(0);
    set_char_exists(0);
    char_data* loaded = (char_data*)calloc(1, sizeof(char_data));
    clear_char(loaded, MOB_VOID);
    EXPECT_EQ(loaded->abs_number, -1);
    waiting_type wtl {};
    wtl.targ1.set_character(loaded);
    EXPECT_EQ(wtl.targ1.ch_num, -1);
    EXPECT_FALSE(char_exists(-1)) << "no slot is no character";
    EXPECT_FALSE(char_exists(MAX_CHARACTERS)) << "nor is a number past the table";
    free_char(loaded);
    EXPECT_TRUE(char_exists(0)) << "the character in slot 0 is still there";
    if (!slot_zero_existed)
        remove_char_exists(0);
}

/* A program that only counts its calls. */
int g_spy_program_calls = 0;
int spy_program(struct char_data*, struct char_data*, int, char*, int, struct waiting_type*)
{
    ++g_spy_program_calls;
    return 0;
}

/* The one place the rest of the game asks "is this a keeper?". */
TEST_F(BarterVendorTest, MobIsKeeperGoesByTheProgramTheGameWouldCall)
{
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_TRUE(mob_is_keeper(&m_vendor));
    m_vendor.specials.store_prog_number = 5; /* an ordinary mob program */
    EXPECT_FALSE(mob_is_keeper(&m_vendor));
    m_vendor.specials.store_prog_number = 0;
    EXPECT_FALSE(mob_is_keeper(&m_vendor));
    m_mob_index[0].func = shop_keeper; /* an old shopkeeper: assigned by vnum at boot */
    EXPECT_TRUE(mob_is_keeper(&m_vendor));
    REMOVE_BIT(m_vendor.specials2.act, MOB_SPEC);
    EXPECT_FALSE(mob_is_keeper(&m_vendor)) << "without MOB_SPEC the index function is never called";
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_TRUE(mob_is_keeper(&m_vendor)) << "but the mob's own program number still is (activate_char_special)";
    m_vendor.specials.store_prog_number = 0;
    SET_BIT(m_vendor.specials2.act, MOB_SPEC);

    /* MOB_SPEC with an ordinary index function: the game calls only that one,
     * never the program number, so a keeper number does not make a keeper. */
    m_mob_index[0].func = spy_program;
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_FALSE(mob_is_keeper(&m_vendor));
    /* Specials switched off: the index function is skipped, the number is called. */
    m_mob_index[0].func = shop_keeper;
    m_vendor.specials.store_prog_number = 0;
    no_specials = 1;
    EXPECT_FALSE(mob_is_keeper(&m_vendor));
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_TRUE(mob_is_keeper(&m_vendor));
    no_specials = 0;
    m_vendor.specials.store_prog_number = 0;
    m_mob_index[0].func = nullptr;
    EXPECT_FALSE(mob_is_keeper(&m_buyer)) << "a player";
    EXPECT_FALSE(mob_is_keeper(nullptr));
}

/* A mental attack asks the same question before it engages, so a keeper is
 * not drawn into the fight and the attacker is not left fighting it. */
TEST_F(BarterVendorTest, AMentalAttackIsRefusedLikeAPhysicalOne)
{
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    SET_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    std::strcpy(m_arg, "trader");
    const bool slot_zero_existed = char_exists(0);
    remove_char_exists(0); /* do_mental names the vendor's own slot, not this one */
    do_mental(&m_buyer, m_arg, nullptr, 0, 0);
    if (slot_zero_existed)
        set_char_exists(0);
    EXPECT_NE(output().find(" tells you 'Don't even think about it.'"), std::string::npos) << output();
    EXPECT_EQ(m_buyer.specials.fighting, nullptr) << "not left fighting the vendor";
    EXPECT_EQ(m_vendor.specials.fighting, nullptr);

    REMOVE_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
}

/* Mental damage (a curse, a mental hit that lands) asks the victim's program
 * through damage_stat(), which names the victim's own slot as the others do. */
TEST_F(BarterVendorTest, MentalDamageAsksTheVendorByItsOwnSlot)
{
    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    const int hit_before = m_vendor.tmpabilities.hit;
    const bool slot_zero_existed = char_exists(0);
    remove_char_exists(0);
    damage_stat(&m_buyer, &m_vendor, 0, 5);
    if (slot_zero_existed)
        set_char_exists(0);
    EXPECT_NE(output().find(" tells you 'Don't even think about it.'"), std::string::npos) << output();
    EXPECT_EQ(m_buyer.specials.fighting, nullptr);
    EXPECT_EQ(m_vendor.specials.fighting, nullptr);
    EXPECT_EQ(m_vendor.tmpabilities.hit, hit_before);
}

/* An attacker already fighting a keeper that is not fighting back (it was
 * stunned, or turned on someone else) is refused once and stops: left
 * fighting, the refusal would come again on every round. */
TEST_F(BarterVendorTest, ARefusedMentalAttackerStopsFighting)
{
    char_data* saved_combat_list = combat_list;
    char_data* saved_waiting_list = waiting_list;
    combat_list = nullptr;
    waiting_list = nullptr;

    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    SET_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    set_fighting(&m_buyer, &m_vendor);
    std::strcpy(m_arg, "");
    do_mental(&m_buyer, m_arg, nullptr, 0, 0); /* as perform_violence calls it each round */
    EXPECT_NE(output().find(" tells you 'Don't even think about it.'"), std::string::npos) << output();
    EXPECT_EQ(m_buyer.specials.fighting, nullptr) << "so the next round does not ask again";

    REMOVE_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    combat_list = saved_combat_list;
    waiting_list = saved_waiting_list;
}

/* Only keepers are asked before a mental attack engages. Any other program
 * (the pale vampire's heals her on every call) must not get an extra turn
 * out of every mental round aimed at its mob. */
TEST_F(BarterVendorTest, AMentalAttackDoesNotCallTheProgramOfAMobThatIsNotAKeeper)
{
    char_data* saved_combat_list = combat_list;
    char_data* saved_waiting_list = waiting_list;
    combat_list = nullptr;
    waiting_list = nullptr;

    g_spy_program_calls = 0;
    m_mob_index[0].func = spy_program;
    SET_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    std::strcpy(m_arg, "trader");
    do_mental(&m_buyer, m_arg, nullptr, 0, 0);
    EXPECT_EQ(g_spy_program_calls, 0);
    EXPECT_EQ(m_buyer.specials.fighting, &m_vendor) << "the attack itself goes ahead";

    m_mob_index[0].func = nullptr;
    REMOVE_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    m_buyer.specials.fighting = nullptr;
    combat_list = saved_combat_list;
    waiting_list = saved_waiting_list;
}

/* The other half: a keeper that started the fight is fair game, so the
 * mental attack is not refused and the attacker engages. (A vendor's mind
 * cannot be fathomed, which ends the attack there, with no dice rolled.) */
TEST_F(BarterVendorTest, AKeeperThatStartedTheFightCanBeAttackedMentally)
{
    char_data* saved_combat_list = combat_list; /* the attack puts the buyer on both lists */
    char_data* saved_waiting_list = waiting_list;
    combat_list = nullptr;
    waiting_list = nullptr;

    m_vendor.specials.store_prog_number = PROG_BARTER_VENDOR;
    m_vendor.specials.fighting = &m_buyer;
    SET_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    std::strcpy(m_arg, "trader");
    do_mental(&m_buyer, m_arg, nullptr, 0, 0);
    EXPECT_EQ(output().find("Don't even think about it."), std::string::npos) << output();
    EXPECT_EQ(m_buyer.specials.fighting, &m_vendor) << "the attacker engages";
    EXPECT_NE(output().find("You cannot fathom"), std::string::npos) << output();

    REMOVE_BIT(PRF_FLAGS(&m_buyer), PRF_MENTAL);
    m_buyer.specials.fighting = nullptr;
    m_vendor.specials.fighting = nullptr;
    combat_list = saved_combat_list;
    waiting_list = saved_waiting_list;
}

TEST_F(BarterVendorTest, ShortfallTakesNothing)
{
    give_hides(1);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(carried(1), 1);
    EXPECT_EQ(floor_belts(), 2);
    EXPECT_NE(output().find(" tells you 'You need 2 x a wolf hide (you have 1).'"), std::string::npos) << output();
}

TEST_F(BarterVendorTest, AFullContainerIsNeverTakenAsPayment)
{
    obj_data* full = give_hides(2);
    obj_data* inside = read_object(0, REAL);
    inside->in_obj = full;
    full->contains = inside;

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(full->contains, inside);
    EXPECT_NE(output().find(" tells you 'You need 2 x a wolf hide (you have 1).'"), std::string::npos) << output();
}

TEST_F(BarterVendorTest, SoldOutItemIsNotListedOrSold)
{
    while (world[0].contents)
        extract_obj(world[0].contents);
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(carried(0), 0);
}

TEST_F(BarterVendorTest, PurchaseSummaryListsOneCurrencyPerLine)
{
    std::strcpy(m_options, "store=5000\nprice 100 300x1 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    give_hides(2);
    obj_to_char(read_object(2, REAL), &m_buyer);

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(output(), "You hand over:\n\r"
                        "  1 x a leather belt\n\r"
                        "  2 x a wolf hide\n\r"
                        "You now have a hunter's belt.\n\r");
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 0);
    EXPECT_EQ(carried(2), 0);
}

TEST_F(BarterVendorTest, ClosedVendorRefusesTrade)
{
    int saved_hours = time_info.hours;
    std::strcpy(m_options, "store=5000\nhours=6-12\nprice 100 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    give_hides(2);

    time_info.hours = 20;
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(floor_belts(), 2);

    time_info.hours = 8;
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    time_info.hours = saved_hours;
}

/* The vendor refuses a gift exactly when do_give would hand it to the vendor. */
TEST_F(BarterVendorTest, GiveToTheVendorIsIntercepted)
{
    for (const char* arg : { "sword trader", "sword to trader", "sword trader x", "10 coins trader",
             "10 coins trader junk", "all trader", "all.sword trader" })
        EXPECT_TRUE(call(CMD_GIVE, arg)) << arg;
}

TEST_F(BarterVendorTest, GiveToAnyoneElseOrNobodyPassesThrough)
{
    for (const char* arg : { "sword someoneelse", "sword", "", "sword buyer", "10 coins" })
        EXPECT_FALSE(call(CMD_GIVE, arg)) << arg;
}

TEST_F(BarterVendorTest, GiveByNumberedNameFindsTheRightTrader)
{
    char_data other {};
    clear_char(&other, MOB_ISNPC);
    other.specials2.act = MOB_ISNPC;
    other.player.name = m_vendor_name; /* another "trader" ahead of the vendor */
    other.player.short_descr = m_vendor_short;
    other.in_room = 0;
    other.next_in_room = world[0].people;
    world[0].people = &other;

    EXPECT_TRUE(call(CMD_GIVE, "sword 2.trader"));
    EXPECT_FALSE(call(CMD_GIVE, "sword 1.trader"));
    EXPECT_FALSE(call(CMD_GIVE, "sword trader")) << "plain 'trader' is the other one";

    world[0].people = other.next_in_room;
}

TEST_F(BarterVendorTest, UnusableConfigRefusesTrade)
{
    std::strcpy(m_options, "price 100 200x2 deduct"); /* no store */
    vendor_config_rebuild(0, nullptr);
    ASSERT_NE(vendor_config_for(0), nullptr);
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(floor_belts(), 2);
    EXPECT_TRUE(call(0, "", SPECIAL_DAMAGE)) << "still protected";
}

/* Stock is read from the store room named in the options, never from the
 * room the vendor and buyer stand in (a belt lies there too, as bait). */
TEST_F(BarterVendorTest, StockComesFromTheStoreRoomNotTheVendorsRoom)
{
    room_data& store = world[1];
    const int saved_number = store.number;
    const byte saved_light = store.light;
    obj_data* const saved_contents = store.contents;
    char_data* const saved_people = store.people;
    world[0].number = kStoreVnum - 1;
    store.number = kStoreVnum;
    store.light = 1;
    store.contents = nullptr;
    store.people = nullptr;
    top_of_world = 1;
    while (world[0].contents) {
        obj_data* belt = world[0].contents;
        obj_from_room(belt);
        obj_to_room(belt, 1);
    }
    obj_to_room(read_object(0, REAL), 0); /* not stock */
    give_hides(3);

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(count_in(store.contents, 0), 1) << "deducted from the store room";
    EXPECT_EQ(count_in(world[0].contents, 0), 1) << "the vendor's room is untouched";

    while (store.contents)
        extract_obj(store.contents);
    store.number = saved_number;
    store.light = saved_light;
    store.contents = saved_contents;
    store.people = saved_people;
}

TEST_F(BarterVendorTest, OneShortCurrencyOfTwoTakesNothing)
{
    std::strcpy(m_options, "store=5000\nprice 100 200x2 300x1 deduct");
    vendor_config_rebuild(0, nullptr);
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(1), 2) << "the covered currency is not taken either";
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(floor_belts(), 2);
    EXPECT_NE(output().find(" tells you 'You need 1 x a leather belt (you have 0).'"), std::string::npos) << output();
}

TEST_F(BarterVendorTest, CurrencyInABagOrWornDoesNotCount)
{
    give_hides(1);
    obj_data* bag = read_object(2, REAL);
    obj_to_char(bag, &m_buyer);
    obj_to_obj(read_object(1, REAL), bag);
    obj_data* worn = read_object(1, REAL);
    m_buyer.equipment[WEAR_BODY] = worn;

    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_NE(output().find(" tells you 'You need 2 x a wolf hide (you have 1).'"), std::string::npos) << output();
    EXPECT_EQ(carried(1), 1);
    EXPECT_NE(bag->contains, nullptr);
    EXPECT_EQ(floor_belts(), 2);
    m_buyer.equipment[WEAR_BODY] = nullptr;
}

/* The item-count check counts the payment as already gone. CAN_CARRY_N is
 * 5 + DEX/2 + level/2; the buyer is filled to 19 items, then DEX sets the cap. */
TEST_F(BarterVendorTest, CarryCountAllowsForThePaymentLeaving)
{
    give_hides(2);
    while (IS_CARRYING_N(&m_buyer) < 19)
        obj_to_char(read_object(2, REAL), &m_buyer);

    m_buyer.tmpabilities.dex = 14; /* cap 17; after the trade 18 */
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(output(), "You can't carry that many items.\n\r");
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(floor_belts(), 2);

    clear_output();
    m_buyer.tmpabilities.dex = 16; /* cap 18; the old check refused at 19 + 1 */
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(IS_CARRYING_N(&m_buyer), 18);
}

/* CAN_CARRY_W is 2000 + 1000 * STR. Three 5000 hides = 15000 carried. */
TEST_F(BarterVendorTest, CarryWeightAllowsForThePaymentLeaving)
{
    m_obj_proto[1].obj_flags.weight = 5000;
    m_obj_proto[0].obj_flags.weight = 1000;
    restock_belts(2);
    give_hides(3);
    m_buyer.tmpabilities.str = 10; /* cap 12000; after the trade 6000 */
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(IS_CARRYING_W(&m_buyer), 6000);

    clear_output();
    GET_OBJ_WEIGHT(world[0].contents) = 20000; /* the last belt in stock */
    give_hides(1); /* 11000 carried; after the trade 11000 - 10000 + 20000 */
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(output(), "You can't carry that much weight.\n\r");
    EXPECT_EQ(carried(1), 2);
    EXPECT_EQ(floor_belts(), 1);
}

TEST_F(BarterVendorTest, WithoutDeductTheStoreCopyStays)
{
    std::strcpy(m_options, "store=5000\nprice 100 200x2");
    vendor_config_rebuild(0, nullptr);
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 0);
    EXPECT_EQ(floor_belts(), 2);
}

/* deduct hands over the store copy itself, so whatever is inside comes along. */
TEST_F(BarterVendorTest, DeductHandsOverTheStoreCopyWithItsContents)
{
    obj_data* stocked = world[0].contents;
    obj_data* inside = read_object(2, REAL);
    obj_to_obj(inside, stocked);
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(stocked->carried_by, &m_buyer);
    EXPECT_EQ(stocked->contains, inside);
    EXPECT_EQ(floor_belts(), 1);
}

TEST_F(BarterVendorTest, PurchaseIsLoggedAndTheItemMarkedHandled)
{
    give_hides(2);
    testing::internal::CaptureStderr();
    EXPECT_TRUE(call(CMD_BUY, "1"));
    std::string log_text = testing::internal::GetCapturedStderr();
    EXPECT_NE(log_text.find("VENDOR: Buyer buys a hunter's belt (100) from mobile #7000, paid 2 x a wolf hide (200)"),
        std::string::npos)
        << log_text;
    obj_data* bought = nullptr;
    for (obj_data* obj = m_buyer.carrying; obj; obj = obj->next_content)
        if (obj->item_number == 0)
            bought = obj;
    ASSERT_NE(bought, nullptr);
    EXPECT_EQ(bought->touched, 1);
}

TEST_F(BarterVendorTest, UnusableVendorIsLoggedOnceUntilRebuilt)
{
    std::strcpy(m_options, "price 100 200x2 deduct"); /* no store */
    vendor_config_rebuild(0, nullptr, false);
    auto logged = [this](int times) {
        testing::internal::CaptureStderr();
        for (int i = 0; i < times; ++i)
            call(CMD_LIST, "");
        std::string text = testing::internal::GetCapturedStderr();
        int count = 0;
        for (size_t at = text.find("bad options"); at != std::string::npos; at = text.find("bad options", at + 1))
            ++count;
        return count;
    };
    EXPECT_EQ(logged(3), 1);
    vendor_config_rebuild(0, nullptr, false);
    EXPECT_EQ(logged(2), 1) << "a rebuild logs it again";
}

TEST_F(BarterVendorTest, NoSpecialsBootHasNoVendors)
{
    no_specials = 1;
    vendor_config_rebuild(0, nullptr);
    EXPECT_EQ(vendor_config_for(0), nullptr);
    EXPECT_FALSE(call(0, "", SPECIAL_DAMAGE));
    no_specials = 0;
}

} // namespace

/* As the old shopkeepers: a reply to the buyer is a tell nobody else sees, a
 * refusal to serve at all is a say the room hears. */
TEST_F(BarterVendorTest, RepliesAreTellsAndRefusalsAreSays)
{
    char_data watcher {};
    descriptor_data watcher_descriptor {};
    char watcher_name[] = "Watcher";
    clear_char(&watcher, 0);
    watcher.player.name = watcher_name;
    watcher.player.race = RACE_HUMAN;
    watcher.in_room = 0;
    watcher_descriptor.output = watcher_descriptor.small_outbuf;
    watcher_descriptor.small_outbuf[0] = '\0';
    watcher_descriptor.bufspace = SMALL_BUFSIZE - 1;
    watcher_descriptor.connected = CON_PLYNG;
    watcher_descriptor.descriptor = 2;
    watcher_descriptor.character = &watcher;
    watcher.desc = &watcher_descriptor;
    GET_POS(&watcher) = POSITION_STANDING;
    m_buyer.next_in_room = &watcher;
    m_descriptor.descriptor = 1;
    GET_POS(&m_buyer) = POSITION_STANDING;

    EXPECT_TRUE(call(CMD_BUY, ""));
    EXPECT_NE(output().find(" tells you 'What do you want to buy?'"), std::string::npos) << output();
    EXPECT_TRUE(call(CMD_BUY, "nosuchthing"));
    EXPECT_NE(output().find(" tells you 'I don't have that. Try 'list'.'"), std::string::npos) << output();
    EXPECT_EQ(std::string(watcher_descriptor.output), "");

    m_vendor.tmpabilities.intel = 3; /* do_tell has no "too stupid to talk" */
    EXPECT_TRUE(call(CMD_BUY, ""));
    EXPECT_NE(output().find(" tells you 'What do you want to buy?'"), std::string::npos) << output();
    m_vendor.tmpabilities.intel = 12;

    int saved_hours = time_info.hours;
    std::strcpy(m_options, "store=5000\nhours=6-12\nprice 100 200x2 deduct");
    vendor_config_rebuild(0, nullptr);
    time_info.hours = 20;
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_NE(output().find(" says 'I'm closed. Come back later.'"), std::string::npos) << output();
    EXPECT_NE(std::string(watcher_descriptor.output).find(" says 'I'm closed. Come back later.'"), std::string::npos);
    time_info.hours = saved_hours;
    m_buyer.next_in_room = nullptr;
}

TEST(VendorParse, GeneralLinesAreSkippedAndTyposStillReported)
{
    vendor_lookups all { [](int) { return true; }, [](int) { return true; } };
    std::vector<vendor_problem> problems;
    parse_vendor_options("store=100\n\rwalker_type=path\n\rwalker_rooms=1-3\n\rwalker_typ=path\n\r", all, &problems);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].line, 4);
    EXPECT_EQ(problems[0].text, "unknown setting - line ignored");
}
