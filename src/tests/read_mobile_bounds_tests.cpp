// read_mobile given a mob number outside the mob table, and the script command LOAD_MOB given a
// vnum no mob has: nothing is read from outside the table. The table here is on the heap, so a
// read past either end of it is one AddressSanitizer reports.
#include "scripted_room_fixture.h"

#include <memory>
#include <string>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

// The vnum of the mob table's one prototype: one past its last index, so that indexing mob_index
// by the vnum instead of the real number reads just past the table.
constexpr int PROTOTYPE_VNUM = 1;
// A vnum no mob in the table has.
constexpr int MISSING_VNUM = 9999;
// The real number of the table's one prototype, and the first real number past its end.
constexpr int PROTOTYPE_REAL_NUMBER = 0;
constexpr int PAST_THE_TABLE = 1;
// The real number real_mobile returns for a vnum it does not find.
constexpr int NOT_FOUND = -1;
// The level and every stat of the prototype.
constexpr int PROTOTYPE_LEVEL = 10;
constexpr int PROTOTYPE_STAT = 18;

// A scripted room whose mob table is one prototype on the heap.
class ReadMobileBoundsTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_prototypes = std::make_unique<char_data[]>(1);
        m_index = std::make_unique<index_data[]>(1);
        char_data& prototype = m_prototypes[PROTOTYPE_REAL_NUMBER];
        clear_char(&prototype, MOB_ISNPC);
        prototype.specials2.act = MOB_ISNPC;
        prototype.nr = PROTOTYPE_REAL_NUMBER;
        prototype.player.name = m_prototype_name;
        prototype.player.short_descr = m_prototype_name;
        prototype.player.level = PROTOTYPE_LEVEL;
        prototype.abilities.str = PROTOTYPE_STAT;
        prototype.abilities.intel = PROTOTYPE_STAT;
        prototype.abilities.wil = PROTOTYPE_STAT;
        prototype.abilities.dex = PROTOTYPE_STAT;
        prototype.abilities.con = PROTOTYPE_STAT;
        prototype.abilities.lea = PROTOTYPE_STAT;
        prototype.abilities.hit = MOB_HIT_POINTS;
        prototype.tmpabilities.hit = MOB_HIT_POINTS;
        prototype.specials.position = POSITION_STANDING;
        prototype.specials.default_pos = POSITION_STANDING;
        m_index[PROTOTYPE_REAL_NUMBER].virt = PROTOTYPE_VNUM;
        mob_proto = m_prototypes.get();
        mob_index = m_index.get();
        top_of_mobt = 0;
    }

    void TearDown() override
    {
        // The fixture extracts the mobs read from the table while the table is still in place.
        ScriptedRoomFixture::TearDown();
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        release_cleared_character(&m_prototypes[PROTOTYPE_REAL_NUMBER]);
    }

    // How many characters are in the game.
    static int count_characters()
    {
        int count = 0;
        for (const char_data* listed = character_list; listed; listed = listed->next) {
            ++count;
        }
        return count;
    }

    // The mob table's one prototype.
    char_data& prototype() { return m_prototypes[PROTOTYPE_REAL_NUMBER]; }

private:
    // The mob table and its index, on the heap.
    std::unique_ptr<char_data[]> m_prototypes;
    std::unique_ptr<index_data[]> m_index;
    // The prototype's keywords and short description.
    char m_prototype_name[16] = "orc";
    // The game's mob table, put back by TearDown.
    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
};

} // namespace

// ---- read_mobile ----

TEST_F(ReadMobileBoundsTest, ARealNumberBeforeTheTableReadsNothing)
{
    const int characters_before = count_characters();

    testing::internal::CaptureStderr();
    const char_data* mob = read_mobile(NOT_FOUND, REAL);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(nullptr, mob);
    EXPECT_EQ(characters_before, count_characters());
    EXPECT_NE(std::string::npos,
        logged.find("SYSERR: read_mobile: real number -1 is outside the mob table (0 to 0)."))
        << logged;
}

TEST_F(ReadMobileBoundsTest, ARealNumberPastTheTableReadsNothing)
{
    const int characters_before = count_characters();

    testing::internal::CaptureStderr();
    const char_data* mob = read_mobile(PAST_THE_TABLE, REAL);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(nullptr, mob);
    EXPECT_EQ(characters_before, count_characters());
    EXPECT_NE(std::string::npos,
        logged.find("SYSERR: read_mobile: real number 1 is outside the mob table (0 to 0)."))
        << logged;
}

TEST_F(ReadMobileBoundsTest, TheLastRealNumberInTheTableMakesAMob)
{
    const char_data* mob = read_mobile(PROTOTYPE_REAL_NUMBER, REAL);

    ASSERT_NE(nullptr, mob);
    EXPECT_EQ(PROTOTYPE_REAL_NUMBER, mob->nr);
    EXPECT_EQ(mob, character_list);
}

// A prototype with no strength is given some; the report names the prototype's vnum, looked up
// by its real number.
TEST_F(ReadMobileBoundsTest, AMobReadByVnumWithItsStatsFixedIsReportedByItsVnum)
{
    prototype().abilities.str = 0;

    testing::internal::CaptureStderr();
    const char_data* mob = read_mobile(PROTOTYPE_VNUM, VIRT);
    const std::string logged = testing::internal::GetCapturedStderr();

    ASSERT_NE(nullptr, mob);
    EXPECT_NE(std::string::npos, logged.find("Mobile 1 had its stats fixed.")) << logged;
}

// ---- the script command LOAD_MOB ----

// A mob's script loads a mob no prototype has into ch3, then tells the one entering it ran: the
// line is reported and loads nothing, and the script goes on.
TEST_F(ReadMobileBoundsTest, ALoadMobOfAMissingVnumLoadsNothingAndTheScriptGoesOn)
{
    install_script(ON_ENTER,
        { { SCRIPT_LOAD_MOB, nullptr, { MISSING_VNUM, SCRIPT_PARAM_CH3 } },
            { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH2 } } });
    make_mob("loader", SCRIPT_VNUM);
    const int characters_before = count_characters();

    testing::internal::CaptureStderr();
    call_trigger(ON_ENTER, &world[0], &m_player, 0);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(characters_before, count_characters());
    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE)) << output();
    EXPECT_NE(std::string::npos, logged.find("(load mob): mobile vnum 9999 not found")) << logged;
    EXPECT_EQ(std::string::npos, logged.find("SYSERR: read_mobile")) << logged;
}

TEST_F(ReadMobileBoundsTest, ALoadMobOfAVnumInTheTableLoadsTheMob)
{
    install_script(ON_ENTER,
        { { SCRIPT_LOAD_MOB, nullptr, { PROTOTYPE_VNUM, SCRIPT_PARAM_CH3 } },
            { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH2 } } });
    make_mob("loader", SCRIPT_VNUM);
    const int characters_before = count_characters();

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    ASSERT_EQ(characters_before + 1, count_characters());
    EXPECT_EQ(PROTOTYPE_REAL_NUMBER, character_list->nr);
    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE)) << output();
}
