#include "../mob_progs/passive.h"

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
void clear_char(struct char_data* ch, int mode);
void clear_object(struct obj_data* obj);

namespace {

constexpr int kStoreVnum = 5000;
constexpr int kBeltVnum = 100;
constexpr int kHideVnum = 200;
constexpr int kVendorVnum = 7000;

class BarterVendorTest : public ::testing::Test {
protected:
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
        m_obj_index[0].virt = kBeltVnum;
        m_obj_index[1].virt = kHideVnum;
        obj_proto = m_obj_proto;
        obj_index = m_obj_index;
        top_of_objt = 1;

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
        m_descriptor.character = &m_buyer;
        m_buyer.desc = &m_descriptor;

        world[0].people = &m_vendor;
        m_vendor.next_in_room = &m_buyer;
        m_buyer.next_in_room = nullptr;

        stock_belts(2);
    }

    void TearDown() override
    {
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

    char m_options[64] = "store=5000\nprice 100 200x2 deduct";
    char m_belt_name[16] = "belt hunter";
    char m_belt_short[20] = "a hunter's belt";
    char m_hide_name[16] = "hide wolf";
    char m_hide_short[20] = "a wolf hide";
    char m_vendor_name[16] = "trader vendor";
    char m_vendor_short[16] = "the trader";
    char m_buyer_name[16] = "Buyer";
    char m_arg[MAX_INPUT_LENGTH] = "";

    obj_data m_obj_proto[2] {};
    index_data m_obj_index[2] {};
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
    EXPECT_EQ(output(), " 1. a hunter's belt (2 left)  2 x a wolf hide\n\r");
    EXPECT_EQ(floor_belts(), 2);
}

TEST_F(BarterVendorTest, BuyPaysDeductsAndHandsOverTheItem)
{
    give_hides(3);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 1);
    EXPECT_EQ(floor_belts(), 1);
    EXPECT_NE(output().find("You hand over 2 x a wolf hide."), std::string::npos) << output();
}

TEST_F(BarterVendorTest, BuyByKeyword)
{
    give_hides(2);
    EXPECT_TRUE(call(CMD_BUY, "belt"));
    EXPECT_EQ(carried(0), 1);
    EXPECT_EQ(carried(1), 0);
}

TEST_F(BarterVendorTest, ShortfallTakesNothing)
{
    give_hides(1);
    EXPECT_TRUE(call(CMD_BUY, "1"));
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(carried(1), 1);
    EXPECT_EQ(floor_belts(), 2);
    EXPECT_EQ(output(), "You need 2 x a wolf hide and have 1.\n\r");
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
    EXPECT_EQ(output(), "You need 2 x a wolf hide and have 1.\n\r");
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

TEST_F(BarterVendorTest, RefusesGiftsOnlyWhenTheVendorIsTheTarget)
{
    give_hides(1);
    EXPECT_TRUE(call(CMD_GIVE, "hide trader"));
    EXPECT_FALSE(call(CMD_GIVE, "hide buyer"));
    EXPECT_EQ(carried(1), 1);
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

} // namespace
