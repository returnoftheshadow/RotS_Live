#include "../mob_progs/banker.h"

#include "../objects_json.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdlib>
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
