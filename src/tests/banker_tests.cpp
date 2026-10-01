#include "../mob_progs/banker.h"

#include "../comm.h"
#include "../db.h"
#include "../game_boot_options.h"
#include "../handler.h"
#include "../interpre.h"
#include "../objects_json.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

std::vector<std::string> texts(const std::vector<vendor_problem>& problems)
{
    std::vector<std::string> out;
    for (const vendor_problem& p : problems)
        out.push_back(std::to_string(p.line) + ": " + p.text);
    return out;
}

/* US Central with daylight saving, written out so no tzdata is needed. */
struct CentralTime {
    CentralTime()
    {
        setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
        tzset();
    }
};

time_t at(int year, int month, int day, int hour, int minute = 0)
{
    static CentralTime zone;
    struct tm tm { };
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

} // namespace

TEST(BankerParse, EmptyOptionsIsAFreeAlwaysOpenBanker)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.fee, 0);
    EXPECT_TRUE(c.hours.empty());
    EXPECT_TRUE(problems.empty());
    EXPECT_TRUE(parse_banker_options(nullptr, &problems).ok);
}

TEST(BankerParse, FullConfig)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("hours=6-20\n\rfee=50\n\rmaxdays=30\n\rracial_markup=yes\n\r", &problems);
    EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(texts(problems));
    EXPECT_TRUE(c.ok);
    ASSERT_EQ(c.hours.size(), 1u);
    EXPECT_EQ(c.fee, 50);
    EXPECT_EQ(c.maxdays, 30);
    EXPECT_EQ(c.markup, 30);
}

TEST(BankerParse, MarkupNumber)
{
    EXPECT_EQ(parse_banker_options("fee=1\nmaxdays=1\nracial_markup=300", nullptr).markup, 300);
    EXPECT_EQ(parse_banker_options("fee=1\nmaxdays=1\nracial_markup=1", nullptr).markup, 1);
}

TEST(BankerParse, BadValuesDisableStrictly)
{
    struct {
        const char* text;
        const char* problem;
    } cases[] = {
        { "hours=6", "1: bad hours - banker disabled" },
        { "fee=0\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=10001\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=abc\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=-5\nmaxdays=5", "1: bad fee - banker disabled" },
        { "fee=5\nmaxdays=0", "2: bad maxdays - banker disabled" },
        { "fee=5\nmaxdays=366", "2: bad maxdays - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=0", "3: bad racial_markup - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=301", "3: bad racial_markup - banker disabled" },
        { "fee=5\nmaxdays=3\nracial_markup=no", "3: bad racial_markup - banker disabled" },
        { "fee=5", "0: fee without maxdays - banker disabled" },
    };
    for (const auto& c : cases) {
        std::vector<vendor_problem> problems;
        EXPECT_FALSE(parse_banker_options(c.text, &problems).ok) << c.text;
        ASSERT_FALSE(problems.empty()) << c.text;
        EXPECT_EQ(texts(problems)[0], c.problem) << c.text;
    }
}

TEST(BankerParse, CommentsBlanksDuplicatesAndUnknowns)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("// note\n\nfee=5\nfee=9\nmaxdays=3\nstore=12\n", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.fee, 5);
    std::vector<std::string> expected = { "4: duplicate fee - line ignored", "6: unknown setting - line ignored" };
    EXPECT_EQ(texts(problems), expected);
}

TEST(BankerParse, MarkupWithoutFeeIsNotStrict)
{
    std::vector<vendor_problem> problems;
    banker_config c = parse_banker_options("racial_markup=yes", &problems);
    EXPECT_TRUE(c.ok);
    EXPECT_EQ(c.markup, 30);
    EXPECT_EQ(c.fee, 0);
    EXPECT_TRUE(problems.empty());
}

TEST(BankSide, EveryPlayableRace)
{
    for (int race : { RACE_HUMAN, RACE_DWARF, RACE_WOOD, RACE_HOBBIT, RACE_HIGH, RACE_BEORNING })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_LIGHT) << race;
    for (int race : { RACE_URUK, RACE_ORC, RACE_OLOGHAI })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_DARK) << race;
    for (int race : { RACE_MAGUS, RACE_HARADRIM })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_THIRD) << race;
    for (int race : { RACE_GOD, RACE_EASTERLING, RACE_HARAD, 7, 16, 19, -1, 200 })
        EXPECT_EQ(bank_side_for_race(race), BANK_SIDE_NONE) << race;
}

TEST(BankSide, FileNames)
{
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_LIGHT), "vault_light.json");
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_DARK), "vault_dark.json");
    EXPECT_STREQ(bank_side_file_name(BANK_SIDE_THIRD), "vault_third.json");
    EXPECT_EQ(bank_side_file_name(BANK_SIDE_NONE), nullptr);
    EXPECT_EQ(bank_side_file_name(4), nullptr);
}

TEST(BankDays, SameBankDayIsZero)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 10), at(2026, 9, 30, 23), 5), 0);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 4, 59), 5), 0);
}

TEST(BankDays, EachFiveAmCrossedAddsOne)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 5), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 4), at(2026, 9, 30, 6), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 27, 12), at(2026, 9, 30, 12), 5), 3);
    EXPECT_EQ(bank_days_stored(at(2026, 12, 31, 12), at(2027, 1, 1, 12), 5), 1);
}

TEST(BankDays, DaylightSavingChangesStillCountOnePerDay)
{
    EXPECT_EQ(bank_days_stored(at(2026, 3, 7, 12), at(2026, 3, 8, 12), 5), 1); /* spring forward */
    EXPECT_EQ(bank_days_stored(at(2026, 3, 7, 12), at(2026, 3, 9, 4), 5), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 10, 31, 12), at(2026, 11, 1, 12), 5), 1); /* fall back */
    EXPECT_EQ(bank_days_stored(at(2026, 10, 31, 12), at(2026, 11, 2, 12), 5), 2);
}

TEST(BankDays, ClockGoingBackwardsIsZeroNotNegative)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 12), at(2026, 9, 20, 12), 5), 0);
}

TEST(BankDays, OtherStartHours)
{
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 23), at(2026, 10, 1, 0, 1), 0), 1);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 1), at(2026, 9, 30, 22), 23), 0);
    EXPECT_EQ(bank_days_stored(at(2026, 9, 30, 22), at(2026, 9, 30, 23), 23), 1);
}

TEST(BankFee, FreeBankerAndSameDay)
{
    banker_config free_banker;
    EXPECT_EQ(bank_fee(free_banker, 30, 4, true), 0);
    banker_config c;
    c.fee = 50;
    c.maxdays = 30;
    EXPECT_EQ(bank_fee(c, 0, 4, false), 0);
}

TEST(BankFee, DaysTimesFeeTimesItemsCappedAtMaxdays)
{
    banker_config c;
    c.fee = 50;
    c.maxdays = 30;
    EXPECT_EQ(bank_fee(c, 3, 1, false), 150);
    EXPECT_EQ(bank_fee(c, 3, 4, false), 600);
    EXPECT_EQ(bank_fee(c, 30, 1, false), 1500);
    EXPECT_EQ(bank_fee(c, 400, 1, false), 1500);
}

TEST(BankFee, MarkupOnlyForAnotherRaceRoundedUp)
{
    banker_config c;
    c.fee = 1;
    c.maxdays = 30;
    c.markup = 30;
    EXPECT_EQ(bank_fee(c, 1, 1, false), 1);
    EXPECT_EQ(bank_fee(c, 1, 1, true), 2); /* 1.3 -> 2 */
    EXPECT_EQ(bank_fee(c, 10, 1, true), 13); /* exact */
    c.markup = 300;
    EXPECT_EQ(bank_fee(c, 10, 1, true), 40);
}

TEST(BankFee, LargestPossibleFeeDoesNotOverflow)
{
    banker_config c;
    c.fee = BANKER_FEE_MAX;
    c.maxdays = BANKER_MAXDAYS_MAX;
    c.markup = BANKER_MARKUP_MAX;
    EXPECT_EQ(bank_fee(c, 365, 1000, true), 14600000000LL);
}

namespace {
objects_json::ObjectRecord record(int vnum, int depth)
{
    objects_json::ObjectRecord r;
    r.item_number = vnum;
    r.wear_pos = depth;
    r.values = { 1, 2, 3, 4, 5 };
    r.extra_flags = 64;
    r.weight = 30;
    r.timer = -1;
    r.bitvector = 8;
    r.loaded_by = 7;
    r.affects[0] = { 18, 6 };
    return r;
}
} // namespace

TEST(BankVaultJson, EmptyVaultRoundTrips)
{
    bank_vault vault, back;
    std::string error;
    ASSERT_TRUE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error)) << error;
    EXPECT_EQ(back.coins, 0);
    EXPECT_TRUE(back.slots.empty());
    EXPECT_TRUE(back.readable);
}

TEST(BankVaultJson, CoinsSlotsAndNestedObjectsRoundTrip)
{
    bank_vault vault;
    vault.coins = 142500;
    vault.slots.push_back({ 1790000000L, { record(100, 0) } });
    vault.slots.push_back({ 1790086400L, { record(200, 0), record(300, 1), record(400, 2), record(500, 1) } });
    bank_vault back;
    std::string error;
    ASSERT_TRUE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error)) << error;
    EXPECT_EQ(back.coins, 142500);
    ASSERT_EQ(back.slots.size(), 2u);
    EXPECT_EQ(back.slots[0].deposited, 1790000000L);
    ASSERT_EQ(back.slots[1].objects.size(), 4u);
    EXPECT_EQ(back.slots[1].objects[2].item_number, 400);
    EXPECT_EQ(back.slots[1].objects[2].wear_pos, 2);
    EXPECT_EQ(back.slots[1].objects[0].affects[0].modifier, 6);
    EXPECT_EQ(back.slots[1].objects[0].values[4], 5);
    EXPECT_EQ(back.slots[1].objects[0].bitvector, 8);
}

TEST(BankVaultJson, RejectsBrokenOrImpossibleFiles)
{
    bank_vault good;
    good.slots.push_back({ 5, { record(100, 0) } });
    std::string json = serialize_bank_vault(good);
    auto with = [&](const std::string& from, const std::string& to) {
        std::string copy = json;
        size_t at = copy.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        return copy.replace(at, from.size(), to);
    };
    const std::string broken[] = {
        "",
        "{",
        "garbage",
        with("\"version\": 1", "\"version\": 2"), /* a newer format */
        with("\"coins\": 0", "\"coins\": -1"),
        with("\"wear_pos\": 0", "\"wear_pos\": 1"), /* first object must be depth 0 */
        with("\"deposited\": 5", "\"deposited\": -5"),
        "{\"version\": 1, \"coins\": 0, \"slots\": [{\"deposited\": 5, \"objects\": []}]}", /* empty slot */
        "{\"version\": 1, \"coins\": 0}", /* slots missing */
    };
    for (const std::string& text : broken) {
        bank_vault vault;
        vault.coins = 77;
        std::string error;
        EXPECT_FALSE(deserialize_bank_vault(text, &vault, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(vault.coins, 77) << "a failed read must not touch the output";
    }
}

TEST(BankVaultJson, RejectsANestingJump)
{
    bank_vault vault;
    vault.slots.push_back({ 5, { record(100, 0), record(200, 2) } }); /* depth 0 -> 2 */
    bank_vault back;
    std::string error;
    EXPECT_FALSE(deserialize_bank_vault(serialize_bank_vault(vault), &back, &error));
}

namespace {

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/* A temp directory standing in for the accounts tree: <root>/<account name>. */
class BankStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        char path[] = "/tmp/bankstore_XXXXXX";
        ASSERT_NE(mkdtemp(path), nullptr);
        m_root = path;
        mkdir((m_root + "/tester").c_str(), 0700);
        mkdir((m_root + "/other").c_str(), 0700);
        bank_vault_forget_all();
        bank_set_directory_resolver([this](const std::string& name) {
            std::string dir = m_root + "/" + name;
            struct stat st { };
            return stat(dir.c_str(), &st) == 0 ? dir : std::string();
        });
    }
    void TearDown() override
    {
        bank_vault_forget_all();
        bank_set_directory_resolver(nullptr);
        std::string command = "rm -rf " + m_root;
        ASSERT_EQ(system(command.c_str()), 0);
    }
    std::string path(const char* account, const char* file) const { return m_root + "/" + account + "/" + file; }
    std::string m_root;
};

} // namespace

TEST_F(BankStoreTest, MissingFileIsAnEmptyVaultAndNothingIsWrittenByLooking)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    ASSERT_NE(vault, nullptr) << error;
    EXPECT_EQ(vault->coins, 0);
    EXPECT_TRUE(vault->slots.empty());
    EXPECT_NE(access(path("tester", "vault_light.json").c_str(), F_OK), 0);
}

TEST_F(BankStoreTest, OpenTwiceIsTheSameCopy)
{
    std::string error;
    bank_vault* first = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    first->coins = 500;
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), first);
    EXPECT_EQ(bank_vault_open("Tester", BANK_SIDE_LIGHT, &error), first) << "account names are case-blind";
    EXPECT_NE(bank_vault_open("tester", BANK_SIDE_DARK, &error), first);
    EXPECT_NE(bank_vault_open("other", BANK_SIDE_LIGHT, &error), first);
}

TEST_F(BankStoreTest, WriteThenForgetThenOpenReadsTheFile)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_DARK, &error);
    vault->coins = 1234;
    vault->slots.push_back({ 99, { record(100, 0) } });
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_DARK, &error)) << error;
    EXPECT_NE(access(path("tester", "vault_dark.json.tmp").c_str(), F_OK), 0) << "no temp file left";
    bank_vault_forget_all();
    vault = bank_vault_open("tester", BANK_SIDE_DARK, &error);
    ASSERT_NE(vault, nullptr) << error;
    EXPECT_EQ(vault->coins, 1234);
    ASSERT_EQ(vault->slots.size(), 1u);
    EXPECT_EQ(vault->slots[0].objects[0].item_number, 100);
}

TEST_F(BankStoreTest, UnreadableFileIsRefusedAndNeverOverwritten)
{
    write_file(path("tester", "vault_light.json"), "{ this is not a vault");
    std::string error;
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_LIGHT, &error), nullptr) << "stays refused";
    EXPECT_FALSE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), "{ this is not a vault");
    EXPECT_NE(bank_vault_open("tester", BANK_SIDE_DARK, &error), nullptr) << "other vaults still work";
}

TEST_F(BankStoreTest, NoAccountFolderNoSideNoName)
{
    std::string error;
    EXPECT_EQ(bank_vault_open("nobody", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_EQ(bank_vault_open("tester", BANK_SIDE_NONE, &error), nullptr);
    EXPECT_EQ(bank_vault_open("tester", 4, &error), nullptr);
    EXPECT_EQ(bank_vault_open("", BANK_SIDE_LIGHT, &error), nullptr);
    EXPECT_FALSE(bank_vault_write("nobody", BANK_SIDE_LIGHT, &error));
}

TEST_F(BankStoreTest, FailedWriteLeavesTheOldFile)
{
    std::string error;
    bank_vault* vault = bank_vault_open("tester", BANK_SIDE_LIGHT, &error);
    vault->coins = 10;
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    std::string before = read_file(path("tester", "vault_light.json"));
    vault->coins = 20;
    /* a directory where the temp file must go makes the write fail */
    mkdir(path("tester", "vault_light.json.tmp").c_str(), 0700);
    EXPECT_FALSE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), before);
}

TEST(BankHooks, ClockAndSaverCanBeReplacedAndRestored)
{
    bank_set_clock([] { return (time_t)12345; });
    EXPECT_EQ(bank_now(), 12345);
    bank_set_clock(nullptr);
    EXPECT_GT(bank_now(), 1700000000);

    int saved = 0;
    bank_set_character_saver([&saved](struct char_data*) { ++saved; });
    bank_save_character(nullptr);
    EXPECT_EQ(saved, 1);
    bank_set_character_saver(nullptr);
}

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

TEST(BankBalance, LayoutWithFees)
{
    std::vector<bank_balance_row> rows = {
        { "a bastard sword", -1, "1 silver and 50 copper" },
        { "a leather backpack", 3, "6 silver" },
        { "a crisp ticket", -1, "free" },
    };
    EXPECT_EQ(format_bank_balance("142 gold and 5 silver", 1000, 3, 10, rows, true),
        "Coins: 142 gold and 5 silver (limit 1000 gold)\n\r"
        "Slots: 3 of 10 used\n\r"
        "\n\r"
        " #  Item                                   Fee to withdraw\n\r"
        " 1  a bastard sword                        1 silver and 50 copper\n\r"
        " 2  a leather backpack (sealed, 3 inside)  6 silver\n\r"
        " 3  a crisp ticket                         free\n\r");
}

TEST(BankBalance, NoFeeColumnAtAFreeBankerAndNoTableWhenEmpty)
{
    std::vector<bank_balance_row> rows = { { "a bastard sword", -1, "" } };
    EXPECT_EQ(format_bank_balance("0 copper", 1000, 1, 10, rows, false),
        "Coins: 0 copper (limit 1000 gold)\n\r"
        "Slots: 1 of 10 used\n\r"
        "\n\r"
        " #  Item\n\r"
        " 1  a bastard sword\n\r");
    EXPECT_EQ(format_bank_balance("0 copper", 1000, 0, 10, {}, true),
        "Coins: 0 copper (limit 1000 gold)\n\r"
        "Slots: 0 of 10 used\n\r");
}

TEST(BankBalance, EveryLineFitsIn78Columns)
{
    std::vector<bank_balance_row> rows;
    for (int i = 0; i < 100; ++i)
        rows.push_back({ std::string(80, 'x'), 250, "100000 gold, 9 silver and 99 copper" });
    std::string out = format_bank_balance("100000 gold, 9 silver and 99 copper", 100000, 100, 100, rows, true);
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find("\n\r", start);
        ASSERT_NE(end, std::string::npos);
        EXPECT_LE(end - start, 78u) << out.substr(start, end - start);
        start = end + 2;
    }
    EXPECT_NE(out.find("100  "), std::string::npos);
    EXPECT_NE(out.find("(sealed, 250 inside)"), std::string::npos) << "the sealed note survives a cut name";
}

namespace {

constexpr int kSwordVnum = 100;
constexpr int kPackVnum = 200;
constexpr int kKeyVnum = 300;
constexpr int kBankerVnum = 7100;

class BankerTest : public BankStoreTest {
protected:
    void SetUp() override
    {
        BankStoreTest::SetUp();
        save_world();

        for (obj_data& proto : m_obj_proto)
            clear_object(&proto);
        set_proto(0, kSwordVnum, m_sword_name, m_sword_short, ITEM_WEAPON, 30);
        set_proto(1, kPackVnum, m_pack_name, m_pack_short, ITEM_CONTAINER, 10);
        set_proto(2, kKeyVnum, m_key_name, m_key_short, ITEM_KEY, 1);
        obj_proto = m_obj_proto;
        obj_index = m_obj_index;
        top_of_objt = 2;

        m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
        m_mob_proto[0].specials.store_prog_number = PROG_BANKER;
        m_mob_proto[0].specials.mob_options = m_options;
        m_mob_proto[0].abilities.intel = 12;
        m_mob_proto[0].player.race = RACE_HUMAN;
        m_mob_index[0].virt = kBankerVnum;
        m_mob_index[0].func = nullptr;
        mob_proto = m_mob_proto;
        mob_index = m_mob_index;
        top_of_mobt = 0;
        banker_config_rebuild(0, nullptr);

        clear_char(&m_banker, MOB_ISNPC);
        m_banker.nr = 0;
        m_banker.specials2.act = MOB_ISNPC | MOB_SPEC;
        m_banker.player.name = m_banker_name;
        m_banker.player.short_descr = m_banker_short;
        m_banker.player.race = RACE_HUMAN;
        m_banker.tmpabilities.intel = 12;
        m_banker.in_room = 0;

        clear_char(&m_player, 0);
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.tmpabilities.str = 18;
        m_player.tmpabilities.dex = 18;
        m_player.in_room = 0;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.character = &m_player;
        std::strcpy(m_descriptor.account_name, "tester");
        m_descriptor.descriptor = 1; /* do_say only speaks to a connected, awake listener */
        GET_POS(&m_player) = POSITION_STANDING;
        m_player.desc = &m_descriptor;

        world[0].people = &m_banker;
        m_banker.next_in_room = &m_player;
        m_player.next_in_room = nullptr;

        m_now = at(2026, 9, 30, 12);
        bank_set_clock([this] { return m_now; });
        bank_set_character_saver([this](char_data* ch) {
            ++m_saves;
            m_file_at_save = read_file(path("tester", "vault_light.json"));
            (void)ch;
        });
        boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 10);
        boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 1000);
        boot_options_set_running_for_tests(BOOT_BANK_DAY_START_HOUR, 5);
    }

    void TearDown() override
    {
        while (object_list != nullptr)
            extract_obj(object_list);
        m_mob_proto[0].specials.store_prog_number = 0;
        banker_config_rebuild(0, nullptr); /* erases the registry entry */
        bank_set_clock(nullptr);
        bank_set_character_saver(nullptr);
        restore_world();
        BankStoreTest::TearDown();
    }

    void set_proto(int rnum, int vnum, char* name, char* short_desc, int type, int weight)
    {
        m_obj_proto[rnum].item_number = rnum;
        m_obj_proto[rnum].name = name;
        m_obj_proto[rnum].short_description = short_desc;
        m_obj_proto[rnum].obj_flags.type_flag = type;
        m_obj_proto[rnum].obj_flags.weight = weight;
        m_obj_index[rnum].virt = vnum;
    }

    void options(const char* text)
    {
        std::strncpy(m_options, text, sizeof(m_options) - 1);
        banker_config_rebuild(0, nullptr);
    }

    obj_data* give(int rnum)
    {
        obj_data* obj = read_object(rnum, REAL);
        obj_to_char(obj, &m_player);
        return obj;
    }

    int call(int cmd, const char* text, int callflag = SPECIAL_COMMAND)
    {
        std::strncpy(m_arg, text, sizeof(m_arg) - 1);
        m_arg[sizeof(m_arg) - 1] = '\0';
        clear_output();
        return banker(&m_banker, &m_player, cmd, m_arg, callflag, nullptr);
    }

    bank_vault* vault(int side = BANK_SIDE_LIGHT)
    {
        std::string error;
        return bank_vault_open("tester", side, &error);
    }

    int carried(int rnum) const
    {
        int count = 0;
        for (obj_data* obj = m_player.carrying; obj; obj = obj->next_content)
            if (obj->item_number == rnum)
                ++count;
        return count;
    }
    std::string output() const { return std::string(m_descriptor.output); }
    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    /* The same world globals BarterVendorTest saves and replaces. */
    void save_world()
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
        world[0].number = 5000;
        world[0].light = 1;
        world[0].contents = nullptr;
    }

    void restore_world()
    {
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

    char m_options[256] = "";
    char m_sword_name[16] = "sword bastard";
    char m_sword_short[20] = "a bastard sword";
    char m_pack_name[20] = "backpack leather";
    char m_pack_short[24] = "a leather backpack";
    char m_key_name[16] = "key iron";
    char m_key_short[16] = "an iron key";
    char m_banker_name[16] = "banker griswold";
    char m_banker_short[16] = "the banker";
    char m_player_name[16] = "Player";
    char m_arg[MAX_INPUT_LENGTH] = "";
    obj_data m_obj_proto[3] {};
    index_data m_obj_index[3] {};
    char_data m_mob_proto[1] {};
    index_data m_mob_index[1] {};
    char_data m_banker {};
    char_data m_player {};
    descriptor_data m_descriptor {};
    time_t m_now = 0;
    int m_saves = 0;
    std::string m_file_at_save;

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

} // namespace

TEST_F(BankerTest, OnlyARegisteredBankerMobIsABanker)
{
    EXPECT_EQ(banker(&m_player, &m_player, CMD_BALANCE, m_arg, SPECIAL_COMMAND, nullptr), FALSE);
    m_mob_proto[0].specials.store_prog_number = 0;
    banker_config_rebuild(0, nullptr);
    EXPECT_EQ(call(CMD_BALANCE, ""), FALSE);
}

TEST_F(BankerTest, OtherCommandsPassThrough)
{
    EXPECT_EQ(call(CMD_LIST, ""), FALSE);
    EXPECT_EQ(call(CMD_BUY, "sword"), FALSE);
}

TEST_F(BankerTest, DamageDustAndGiftsAreRefusedLikeAVendor)
{
    EXPECT_EQ(call(0, "", SPECIAL_DAMAGE), TRUE);
    EXPECT_EQ(call(CMD_GIVE, "sword banker"), TRUE);
    EXPECT_NE(output().find("I don't take gifts."), std::string::npos);
    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ptr.ch = &m_banker;
    EXPECT_EQ(banker(&m_banker, &m_player, CMD_BLINDING, m_arg, SPECIAL_TARGET, &wtl), TRUE);
}

TEST_F(BankerTest, BalanceOnAnEmptyVault)
{
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("Coins: 0 copper (limit 1000 gold)"), std::string::npos) << output();
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos);
    EXPECT_EQ(m_saves, 0) << "looking saves nothing";
}

TEST_F(BankerTest, ImmortalsAndSidelessRacesAreRefused)
{
    for (int race : { (int)RACE_GOD, (int)RACE_EASTERLING }) {
        m_player.player.race = race;
        EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
        EXPECT_NE(output().find("I hold nothing for your kind."), std::string::npos) << output();
        EXPECT_EQ(output().find("Coins:"), std::string::npos);
    }
}

TEST_F(BankerTest, NoAccountNameOrNoDescriptorIsRefusedWithoutOpeningAVault)
{
    m_descriptor.account_name[0] = '\0';
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I can't find your account."), std::string::npos) << output();
    m_player.desc = nullptr;
    EXPECT_EQ(call(CMD_DEPOSIT, "5 gold"), TRUE); /* must not crash */
    m_player.desc = &m_descriptor;
}

TEST_F(BankerTest, ClosedBankerRefuses)
{
    extern struct time_info_data time_info;
    int saved_hour = time_info.hours;
    options("hours=6-20");
    time_info.hours = 22;
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I'm closed. Come back later."), std::string::npos);
    time_info.hours = saved_hour;
}

TEST_F(BankerTest, BadOptionsMeanNoBusiness)
{
    options("fee=5");
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("The bank is closed for now."), std::string::npos) << output();
}

TEST_F(BankerTest, UnreadableVaultIsRefusedAndLeftAlone)
{
    write_file(path("tester", "vault_light.json"), "junk");
    EXPECT_EQ(call(CMD_BALANCE, ""), TRUE);
    EXPECT_NE(output().find("I can't open your vault right now."), std::string::npos) << output();
    EXPECT_EQ(read_file(path("tester", "vault_light.json")), "junk");
}

TEST_F(BankerTest, CandidateRule)
{
    EXPECT_TRUE(is_banker_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC; /* no MOB_SPEC */
    EXPECT_FALSE(is_banker_candidate(&m_mob_proto[0], 0));
    m_mob_proto[0].specials2.act = MOB_ISNPC | MOB_SPEC;
    m_mob_proto[0].specials.store_prog_number = PROG_BARTER_VENDOR;
    EXPECT_FALSE(is_banker_candidate(&m_mob_proto[0], 0));
}

TEST_F(BankerTest, ObjectRecordsRoundTripANestedContainer)
{
    obj_data* pack = read_object(1, REAL);
    obj_data* inner = read_object(1, REAL);
    obj_to_obj(read_object(0, REAL), inner);
    obj_to_obj(inner, pack);
    obj_to_obj(read_object(0, REAL), pack);
    std::vector<objects_json::ObjectRecord> records;
    bank_records_from_obj(pack, &records);
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records[0].item_number, kPackVnum);
    EXPECT_EQ(records[0].wear_pos, 0);
    extract_obj(pack);
    ASSERT_EQ(object_list, nullptr);

    obj_data* back = bank_obj_from_records(records);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->item_number, 1);
    int direct = 0, swords_inside_inner = 0;
    for (obj_data* o = back->contains; o; o = o->next_content) {
        ++direct;
        if (o->item_number == 1)
            for (obj_data* p = o->contains; p; p = p->next_content)
                swords_inside_inner += p->item_number == 0;
    }
    EXPECT_EQ(direct, 2);
    EXPECT_EQ(swords_inside_inner, 1);
    EXPECT_EQ(GET_OBJ_WEIGHT(back), 10 + 10 + 30 + 30) << "a container weighs itself plus contents";
    extract_obj(back);
}

TEST_F(BankerTest, AStoredItemWhosePrototypeIsGoneBuildsNothing)
{
    std::vector<objects_json::ObjectRecord> records = { record(kPackVnum, 0), record(9999, 1) };
    EXPECT_EQ(bank_obj_from_records(records), nullptr);
    EXPECT_EQ(object_list, nullptr) << "no half-built objects left behind";
}

TEST_F(BankerTest, StorableFollowsRentIncludingContents)
{
    obj_data* pack = read_object(1, REAL);
    EXPECT_TRUE(bank_obj_storable(pack));
    obj_to_obj(read_object(2, REAL), pack); /* a key: rent refuses keys */
    EXPECT_FALSE(bank_obj_storable(pack));
    extract_obj(pack);
}

TEST_F(BankerTest, DepositItemMovesItToTheVaultFileFirst)
{
    give(0);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_EQ(carried(0), 0);
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].objects[0].item_number, kSwordVnum);
    EXPECT_EQ(vault()->slots[0].deposited, (long)m_now);
    EXPECT_EQ(m_saves, 1);
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos)
        << "the vault file already held the item when the character was saved";
    EXPECT_NE(output().find("You hand a bastard sword to the banker."), std::string::npos) << output();
}

TEST_F(BankerTest, DepositAContainerIsOneSlotWithItsContents)
{
    obj_data* pack = give(1);
    obj_to_obj(read_object(0, REAL), pack);
    obj_to_obj(read_object(0, REAL), pack);
    EXPECT_EQ(call(CMD_DEPOSIT, "backpack"), TRUE);
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].objects.size(), 3u);
    EXPECT_EQ(object_list, nullptr) << "the stored objects are gone from the world";
    EXPECT_EQ(IS_CARRYING_N(&m_player), 0);
    EXPECT_EQ(IS_CARRYING_W(&m_player), 0);
}

TEST_F(BankerTest, DepositRefusals)
{
    EXPECT_EQ(call(CMD_DEPOSIT, ""), TRUE);
    EXPECT_NE(output().find("What would you like to deposit?"), std::string::npos);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("You don't have that."), std::string::npos);

    give(2); /* a key */
    EXPECT_EQ(call(CMD_DEPOSIT, "key"), TRUE);
    EXPECT_NE(output().find("I can't keep that for you."), std::string::npos);
    EXPECT_EQ(carried(2), 1);

    obj_data* pack = give(1);
    obj_to_obj(read_object(2, REAL), pack); /* a key hidden in a pack */
    EXPECT_EQ(call(CMD_DEPOSIT, "backpack"), TRUE);
    EXPECT_NE(output().find("I can't keep that for you."), std::string::npos);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(m_saves, 0);
}

TEST_F(BankerTest, WornItemsCannotBeDeposited)
{
    obj_data* sword = read_object(0, REAL);
    equip_char(&m_player, sword, WIELD);
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("You don't have that."), std::string::npos);
    EXPECT_EQ(m_player.equipment[WIELD], sword);
    obj_to_char(unequip_char(&m_player, WIELD), &m_player);
}

TEST_F(BankerTest, FullVaultRefusesAndALoweredLimitRemovesNothing)
{
    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 2);
    for (int i = 0; i < 3; ++i)
        give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_DEPOSIT, "sword");
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("Your vault is full."), std::string::npos);
    EXPECT_EQ(vault()->slots.size(), 2u);
    EXPECT_EQ(carried(0), 1);

    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 1); /* lowered below contents */
    call(CMD_DEPOSIT, "sword");
    EXPECT_EQ(vault()->slots.size(), 2u);
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("Slots: 2 of 1 used"), std::string::npos);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_EQ(vault()->slots.size(), 1u) << "withdrawing still works over the limit";
}

TEST_F(BankerTest, DepositCoinsAndThePartialCase)
{
    GET_GOLD(&m_player) = 5 * COPP_IN_GOLD;
    EXPECT_EQ(call(CMD_DEPOSIT, "2 gold"), TRUE);
    EXPECT_EQ(vault()->coins, 2000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000);
    EXPECT_NE(output().find("You deposit 2 gold."), std::string::npos) << output();
    EXPECT_NE(m_file_at_save.find("\"coins\": 2000"), std::string::npos);

    call(CMD_DEPOSIT, "5 silver");
    EXPECT_EQ(vault()->coins, 2500);
    call(CMD_DEPOSIT, "7 coins");
    EXPECT_EQ(vault()->coins, 2507);

    boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 3);
    call(CMD_DEPOSIT, "2 gold"); /* only 493 copper fit */
    EXPECT_EQ(vault()->coins, 3000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000 - 500 - 7 - 493);
    EXPECT_NE(output().find("was refused: your vault is full."), std::string::npos) << output();
    call(CMD_DEPOSIT, "1 copper");
    EXPECT_NE(output().find("Your vault can hold no more coins."), std::string::npos);
}

TEST_F(BankerTest, OddCoinAmountsChangeNothing)
{
    GET_GOLD(&m_player) = 1000;
    const char* bad[] = { "0 gold", "999999999 gold", "99999999999999999999 gold", "-5 gold", "2 gold" /* > carried */ };
    for (const char* text : bad) {
        int saves = m_saves;
        EXPECT_EQ(call(CMD_DEPOSIT, text), TRUE) << text;
        EXPECT_EQ(vault()->coins, 0) << text;
        EXPECT_EQ(GET_GOLD(&m_player), 1000) << text;
        EXPECT_EQ(m_saves, saves) << text;
    }
    vault()->coins = 1000;
    for (const char* text : { "0 gold", "999999999 gold", "2 gold" }) {
        EXPECT_EQ(call(CMD_WITHDRAW, text), TRUE) << text;
        EXPECT_EQ(vault()->coins, 1000) << text;
        EXPECT_EQ(GET_GOLD(&m_player), 1000) << text;
    }
}

TEST_F(BankerTest, WithdrawCoinsSavesTheCharacterBeforeTheVaultFile)
{
    vault()->coins = 5000;
    std::string error;
    ASSERT_TRUE(bank_vault_write("tester", BANK_SIDE_LIGHT, &error));
    EXPECT_EQ(call(CMD_WITHDRAW, "3 gold"), TRUE);
    EXPECT_EQ(vault()->coins, 2000);
    EXPECT_EQ(GET_GOLD(&m_player), 3000);
    EXPECT_NE(m_file_at_save.find("\"coins\": 5000"), std::string::npos)
        << "when the character was saved the vault file still held the coins";
    EXPECT_NE(read_file(path("tester", "vault_light.json")).find("\"coins\": 2000"), std::string::npos);
}

TEST_F(BankerTest, WithdrawItemByNumberAndByKeyword)
{
    give(0);
    give(1);
    call(CMD_DEPOSIT, "sword");
    call(CMD_DEPOSIT, "backpack");
    EXPECT_EQ(call(CMD_WITHDRAW, "2"), TRUE);
    EXPECT_EQ(carried(1), 1);
    EXPECT_NE(output().find("The banker hands you a leather backpack."), std::string::npos) << output();
    EXPECT_EQ(call(CMD_WITHDRAW, "sword"), TRUE);
    EXPECT_EQ(carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(call(CMD_WITHDRAW, "sword"), TRUE);
    EXPECT_NE(output().find("I hold nothing like that for you."), std::string::npos);
    EXPECT_EQ(call(CMD_WITHDRAW, "7"), TRUE);
    EXPECT_NE(output().find("I hold nothing like that for you."), std::string::npos);
}

TEST_F(BankerTest, WithdrawItemSavesTheCharacterBeforeTheVaultFile)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_file_at_save.clear();
    call(CMD_WITHDRAW, "1");
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos)
        << "the vault file still held the item when the character was saved";
    EXPECT_EQ(read_file(path("tester", "vault_light.json")).find("\"item_number\": 100"), std::string::npos);
}

TEST_F(BankerTest, FeeIsTakenFromThePurseFirstThenTheVault)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12); /* three 5am points later: 150 copper */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("1 silver and 50 copper"), std::string::npos) << output();

    GET_GOLD(&m_player) = 100;
    vault()->coins = 1000;
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_EQ(GET_GOLD(&m_player), 0);
    EXPECT_EQ(vault()->coins, 950);
    EXPECT_EQ(carried(0), 1);
    EXPECT_NE(output().find("You pay 1 silver from your purse."), std::string::npos) << output();
    EXPECT_NE(output().find("50 copper comes out of your vault."), std::string::npos) << output();
}

TEST_F(BankerTest, CannotPayTakesNothing)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    GET_GOLD(&m_player) = 100;
    vault()->coins = 49;
    int saves = m_saves;
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("That costs 1 silver and 50 copper. You don't have it."), std::string::npos) << output();
    EXPECT_EQ(GET_GOLD(&m_player), 100);
    EXPECT_EQ(vault()->coins, 49);
    EXPECT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(carried(0), 0);
    EXPECT_EQ(object_list, nullptr) << "the rebuilt item was destroyed again";
    EXPECT_EQ(m_saves, saves);
}

TEST_F(BankerTest, SameDayIsFreeAndAnotherRacePaysTheMarkup)
{
    options("fee=10\nmaxdays=30\nracial_markup=yes");
    give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("free"), std::string::npos) << output();
    m_now = at(2026, 10, 1, 12);
    m_player.player.race = RACE_DWARF; /* banker is human; still the light vault */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("13 copper"), std::string::npos) << output();
}

TEST_F(BankerTest, CarryLimitsRefuseAndChargeNothing)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    GET_GOLD(&m_player) = 5000;
    m_player.specials.carry_items = CAN_CARRY_N(&m_player);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("You can't carry that many items."), std::string::npos);
    m_player.specials.carry_items = 0;
    m_player.specials.carry_weight = CAN_CARRY_W(&m_player);
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("You can't carry that much weight."), std::string::npos);
    m_player.specials.carry_weight = 0;
    EXPECT_EQ(GET_GOLD(&m_player), 5000);
    EXPECT_EQ(vault()->slots.size(), 1u);
}

TEST_F(BankerTest, AStoredItemWithNoPrototypeStaysInTheVault)
{
    vault()->slots.push_back({ (long)m_now, { record(9999, 0) } });
    EXPECT_EQ(call(CMD_WITHDRAW, "1"), TRUE);
    EXPECT_NE(output().find("I can't get that out right now."), std::string::npos) << output();
    EXPECT_EQ(vault()->slots.size(), 1u);
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("something"), std::string::npos);
}

TEST_F(BankerTest, FailedVaultWriteRefusesTheDeposit)
{
    give(0);
    mkdir(path("tester", "vault_light.json.tmp").c_str(), 0700); /* blocks the temp file */
    EXPECT_EQ(call(CMD_DEPOSIT, "sword"), TRUE);
    EXPECT_NE(output().find("I can't reach the vault right now."), std::string::npos) << output();
    EXPECT_EQ(carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(m_saves, 0);
}

TEST_F(BankerTest, EachSideHasItsOwnVaultAndCharactersShareTheirSides)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_player.player.race = RACE_DWARF; /* another light character of the account */
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("a bastard sword"), std::string::npos);
    m_player.player.race = RACE_URUK;
    m_banker.player.race = RACE_URUK;
    call(CMD_BALANCE, "");
    EXPECT_EQ(output().find("a bastard sword"), std::string::npos);
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos);
}

TEST_F(BankerTest, TransactionsAreLogged)
{
    testing::internal::CaptureStderr();
    give(0);
    call(CMD_DEPOSIT, "sword");
    call(CMD_WITHDRAW, "1");
    std::string logged = testing::internal::GetCapturedStderr();
    EXPECT_NE(logged.find("BANK: Player deposits a bastard sword (100) at mobile #7100, account tester, side 1"),
        std::string::npos)
        << logged;
    EXPECT_NE(logged.find("BANK: Player withdraws a bastard sword (100) at mobile #7100, account tester, side 1, fee 0"),
        std::string::npos)
        << logged;
}

namespace {

class VaultCommandTest : public BankerTest {
protected:
    void SetUp() override
    {
        BankerTest::SetUp();
        clear_char(&m_imm, 0);
        m_imm.player.name = m_imm_name;
        m_imm.player.race = RACE_GOD;
        m_imm.player.level = LEVEL_GRGOD;
        m_imm.tmpabilities.str = 18;
        m_imm.tmpabilities.dex = 18;
        m_imm.in_room = 0;
        m_imm_descriptor.output = m_imm_descriptor.small_outbuf;
        m_imm_descriptor.small_outbuf[0] = '\0';
        m_imm_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_imm_descriptor.connected = CON_PLYNG;
        m_imm_descriptor.character = &m_imm;
        m_imm.desc = &m_imm_descriptor;
        bank_set_account_lookups(
            [](const std::string& id, bank_account_ref* out) {
                if (id != "tester" && id != "tester@example.com")
                    return false;
                *out = { "tester", "tester@example.com" };
                return true;
            },
            [](const std::string& name, bank_account_ref* out) {
                if (name != "tester")
                    return false;
                *out = { "tester", "tester@example.com" };
                return true;
            },
            [](const std::string& character, bank_account_ref* out, int* race) {
                if (character != "thorin")
                    return false;
                *out = { "tester", "tester@example.com" };
                *race = RACE_DWARF;
                return true;
            });
    }
    void TearDown() override
    {
        bank_set_account_lookups(nullptr, nullptr, nullptr);
        BankerTest::TearDown();
    }
    std::string run(const char* text)
    {
        std::strncpy(m_arg, text, sizeof(m_arg) - 1);
        m_imm_descriptor.small_outbuf[0] = '\0';
        m_imm_descriptor.bufptr = 0;
        m_imm_descriptor.bufspace = SMALL_BUFSIZE - 1;
        do_vault(&m_imm, m_arg, nullptr, 253, 0);
        return std::string(m_imm_descriptor.output);
    }
    int imm_carried(int rnum) const
    {
        int count = 0;
        for (obj_data* obj = m_imm.carrying; obj; obj = obj->next_content)
            count += obj->item_number == rnum;
        return count;
    }
    char m_imm_name[16] = "Forge";
    char_data m_imm {};
    descriptor_data m_imm_descriptor {};
};

} // namespace

TEST_F(VaultCommandTest, UsageWithNoArgument)
{
    EXPECT_NE(run("").find("Usage: vault <character | email | account> [1|2|3]"), std::string::npos);
}

TEST_F(VaultCommandTest, ViewByAccountShowsAllThreeSidesWithNameAndEmail)
{
    vault(BANK_SIDE_LIGHT)->coins = 142500;
    vault(BANK_SIDE_LIGHT)->slots.push_back({ (long)at(2026, 9, 27, 12), { record(kPackVnum, 0), record(kSwordVnum, 1) } });
    std::string out = run("tester");
    EXPECT_NE(out.find("Account: tester (tester@example.com)"), std::string::npos) << out;
    EXPECT_EQ(out.find("Character:"), std::string::npos);
    EXPECT_NE(out.find("Light vault: 142 gold and 5 silver, 1 of 10 slots"), std::string::npos) << out;
    EXPECT_NE(out.find(" 1  a leather backpack"), std::string::npos) << out;
    EXPECT_NE(out.find("stored 3 days"), std::string::npos) << out;
    EXPECT_NE(out.find("      a bastard sword"), std::string::npos) << "contents are indented";
    EXPECT_NE(out.find("Dark vault: 0 copper, 0 of 10 slots"), std::string::npos) << out;
    EXPECT_NE(out.find("Third vault:"), std::string::npos);
    EXPECT_NE(run("tester@example.com").find("Light vault:"), std::string::npos);
}

TEST_F(VaultCommandTest, ViewOneSideAndByCharacter)
{
    std::string out = run("tester 2");
    EXPECT_NE(out.find("Dark vault:"), std::string::npos);
    EXPECT_EQ(out.find("Light vault:"), std::string::npos);
    out = run("thorin");
    EXPECT_NE(out.find("Account: tester (tester@example.com)   Character: Thorin"), std::string::npos) << out;
    EXPECT_NE(out.find("Light vault:"), std::string::npos);
    EXPECT_EQ(out.find("Dark vault:"), std::string::npos);
    EXPECT_NE(run("nobody").find("No account or character by that name."), std::string::npos);
    EXPECT_NE(run("tester 4").find("Usage:"), std::string::npos);
}

TEST_F(VaultCommandTest, UnreadableSideIsShownAndTheOthersStillList)
{
    write_file(path("tester", "vault_dark.json"), "junk");
    std::string out = run("tester");
    EXPECT_NE(out.find("Dark vault: FILE UNREADABLE"), std::string::npos) << out;
    EXPECT_NE(out.find("Light vault:"), std::string::npos);
}

TEST_F(VaultCommandTest, TakeItemGivesItToTheImmortalFreeAndSavesTheImmortalFirst)
{
    options("fee=50\nmaxdays=30");
    give(0);
    call(CMD_DEPOSIT, "sword");
    m_now = at(2026, 10, 3, 12);
    m_file_at_save.clear();
    std::string out = run("take tester 1 1");
    EXPECT_NE(out.find("You take a bastard sword from the vault."), std::string::npos) << out;
    EXPECT_EQ(imm_carried(0), 1);
    EXPECT_TRUE(vault()->slots.empty());
    EXPECT_EQ(GET_GOLD(&m_imm), 0) << "no fee";
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos) << "immortal saved before the vault file";
}

TEST_F(VaultCommandTest, ThePlayerSeesAnImmortalsChangeAtOnce)
{
    give(0);
    call(CMD_DEPOSIT, "sword");
    run("take tester 1 1");
    call(CMD_BALANCE, "");
    EXPECT_NE(output().find("Slots: 0 of 10 used"), std::string::npos) << output();
}

TEST_F(VaultCommandTest, TakeAndPutCoins)
{
    vault()->coins = 5000;
    EXPECT_NE(run("take tester 1 coins 2 gold").find("You take 2 gold from the vault."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3000);
    EXPECT_EQ(GET_GOLD(&m_imm), 2000);
    EXPECT_NE(run("put tester 1 coins 500").find("You put 5 silver into the vault."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3500);
    EXPECT_NE(run("take tester 1 coins 9 gold").find("The vault doesn't hold that much."), std::string::npos);
    EXPECT_NE(run("put tester 1 coins 9 gold").find("You don't have that much."), std::string::npos);
    boot_options_set_running_for_tests(BOOT_BANK_COIN_LIMIT_GOLD, 3);
    GET_GOLD(&m_imm) = 9000;
    EXPECT_NE(run("put tester 1 coins 1 gold").find("That would pass the vault's coin limit."), std::string::npos);
    EXPECT_EQ(vault()->coins, 3500);
}

TEST_F(VaultCommandTest, PutItemStoresItAsDepositedNowVaultFileFirst)
{
    obj_data* sword = read_object(0, REAL);
    obj_to_char(sword, &m_imm);
    m_file_at_save.clear();
    std::string out = run("put tester 1 sword");
    EXPECT_NE(out.find("You put a bastard sword into the vault."), std::string::npos) << out;
    ASSERT_EQ(vault()->slots.size(), 1u);
    EXPECT_EQ(vault()->slots[0].deposited, (long)m_now);
    EXPECT_EQ(imm_carried(0), 0);
    EXPECT_NE(m_file_at_save.find("\"item_number\": 100"), std::string::npos);
}

TEST_F(VaultCommandTest, PutRefusals)
{
    obj_to_char(read_object(2, REAL), &m_imm);
    EXPECT_NE(run("put tester 1 key").find("The bank can't hold that."), std::string::npos);
    EXPECT_NE(run("put tester 1 sword").find("You don't have that."), std::string::npos);
    boot_options_set_running_for_tests(BOOT_BANK_SLOTS, 0 + 1);
    vault()->slots.push_back({ 5, { record(kSwordVnum, 0) } });
    obj_to_char(read_object(0, REAL), &m_imm);
    EXPECT_NE(run("put tester 1 sword").find("That vault is full."), std::string::npos);
    EXPECT_EQ(imm_carried(0), 1);
}

TEST_F(VaultCommandTest, TakeAndPutNeedTheExactAccountName)
{
    for (const char* text : { "take tester@example.com 1 1", "take thorin 1 1", "put thorin 1 sword", "take nobody 1 1" })
        EXPECT_NE(run(text).find("Use the account name shown by 'vault <name>'."), std::string::npos) << text;
    for (const char* text : { "take tester", "take tester 1", "take tester 4 1", "take tester 1 0", "take tester 1 9",
             "put tester 1", "take tester 1 coins", "take tester 1 coins 0" })
        EXPECT_FALSE(run(text).empty()) << text; /* a message, no crash, nothing changed */
    EXPECT_TRUE(vault()->slots.empty());
}

TEST_F(VaultCommandTest, EveryVaultCommandIsLoggedOnceWithoutItsOutput)
{
    vault()->slots.push_back({ 5, { record(kSwordVnum, 0) } });
    testing::internal::CaptureStderr();
    run("tester");
    run("take tester 1 1");
    std::string logged = testing::internal::GetCapturedStderr();
    EXPECT_NE(logged.find("(GC) Forge: vault tester"), std::string::npos) << logged;
    EXPECT_NE(logged.find("(GC) Forge: vault take tester 1 1"), std::string::npos) << logged;
    EXPECT_EQ(logged.find("a bastard sword"), std::string::npos) << "output is not logged";
}
