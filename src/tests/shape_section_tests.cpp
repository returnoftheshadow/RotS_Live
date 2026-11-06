/* Section rows in the zone and script editors.
 *
 * A section is a row that does nothing when the zone resets or the script
 * runs: it holds a title and an optional comment, kept together in the row's
 * one text field as "title | comment".  The list draws the title centred in
 * a row of '=' that fills the line to 78 columns.
 *
 * These cover the shared text helpers (shapemob.cpp) and a zone reset
 * stepping over a section without touching the if_flag chain.
 */
#include "../db.h"
#include "../protos.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

extern struct room_data world;
extern int top_of_world;

namespace {

/* ---- the banner ---- */

TEST(ShapeSectionBanner, FillsTheWidthWithTheTitleCentred)
{
    std::string banner = shape_section_banner("Shop Handling", 74);
    ASSERT_EQ(74u, banner.size());
    size_t at = banner.find(" Shop Handling ");
    ASSERT_NE(std::string::npos, at);
    size_t left = at;
    size_t right = banner.size() - at - strlen(" Shop Handling ");
    EXPECT_EQ(std::string(left, '='), banner.substr(0, left));
    EXPECT_EQ(std::string(right, '='), banner.substr(banner.size() - right));
    EXPECT_LE(left > right ? left - right : right - left, 1u);
}

TEST(ShapeSectionBanner, OddAndEvenTitlesBothFillTheWidth)
{
    EXPECT_EQ(74u, shape_section_banner("Odd", 74).size());
    EXPECT_EQ(74u, shape_section_banner("Even", 74).size());
    EXPECT_EQ(73u, shape_section_banner("Odd", 73).size());
    EXPECT_EQ(73u, shape_section_banner("Even", 73).size());
}

TEST(ShapeSectionBanner, AnEmptyTitleIsAPlainRow)
{
    EXPECT_EQ(std::string(74, '='), shape_section_banner("", 74));
}

TEST(ShapeSectionBanner, ATitleTooLongForTheWidthIsCutToKeepItsMarks)
{
    std::string title(80, 'x');
    std::string banner = shape_section_banner(title.c_str(), 74);
    EXPECT_EQ("== " + std::string(68, 'x') + " ==", banner);
    EXPECT_EQ(74u, banner.size());
}

TEST(ShapeSectionBanner, TheLongestTitleThatFitsIsNotCut)
{
    std::string title(68, 'x');
    EXPECT_EQ("== " + title + " ==", shape_section_banner(title.c_str(), 74));
}

/* ---- "title | comment" in one text field ---- */

TEST(ShapeSectionText, SplitsTitleAndComment)
{
    EXPECT_EQ("Shop Handling", shape_section_title("Shop Handling | keep it together"));
    EXPECT_EQ("keep it together", shape_section_comment("Shop Handling | keep it together"));
}

TEST(ShapeSectionText, ATitleAloneHasNoComment)
{
    EXPECT_EQ("Shop Handling", shape_section_title("Shop Handling"));
    EXPECT_EQ("", shape_section_comment("Shop Handling"));
}

TEST(ShapeSectionText, BlanksAndTheFileLineEndAreTrimmed)
{
    EXPECT_EQ("Shop Handling", shape_section_title("  Shop Handling  |  the shop \r"));
    EXPECT_EQ("the shop", shape_section_comment("  Shop Handling  |  the shop \r"));
    EXPECT_EQ("Shop", shape_section_title(" Shop\r"));
}

TEST(ShapeSectionText, OnlyTheFirstBarSeparates)
{
    EXPECT_EQ("a | b", shape_section_comment("Title | a | b"));
}

TEST(ShapeSectionText, NoTextIsAnEmptySection)
{
    EXPECT_EQ("", shape_section_title(nullptr));
    EXPECT_EQ("", shape_section_comment(nullptr));
}

/* ---- typing a title or a comment ---- */

class SectionText {
public:
    explicit SectionText(const char* start = nullptr)
        : text(start ? strdup(start) : nullptr)
    {
    }
    ~SectionText() { free(text); }
    char* text;
};

TEST(ShapeSectionSet, SetsATitleAndKeepsTheComment)
{
    SectionText s("Old | the note");
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, "  Shop Handling  "));
    EXPECT_STREQ("Shop Handling | the note", s.text);
}

TEST(ShapeSectionSet, SetsACommentAndKeepsTheTitle)
{
    SectionText s("Shop Handling");
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_COMMENT, "everything below"));
    EXPECT_STREQ("Shop Handling | everything below", s.text);
}

TEST(ShapeSectionSet, ABlankAnswerKeepsWhatIsThere)
{
    SectionText s("Shop Handling | the note");
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, "   "));
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_COMMENT, ""));
    EXPECT_STREQ("Shop Handling | the note", s.text);
}

TEST(ShapeSectionSet, PercentQEmptiesTheComment)
{
    SectionText s("Shop Handling | the note");
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_COMMENT, "%q"));
    EXPECT_STREQ("Shop Handling", s.text);
}

/* A section with no title is a plain row of '=': a divider, or the end of
 * the part above it. */
TEST(ShapeSectionSet, ASectionMayHaveNoTitle)
{
    SectionText s;
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, ""));
    EXPECT_STREQ("", s.text);
    EXPECT_EQ(std::string(74, '='), shape_section_banner(shape_section_title(s.text).c_str(), 74));
}

TEST(ShapeSectionSet, PercentQEmptiesTheTitleAndKeepsTheComment)
{
    SectionText s("Shop | the note");
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, "%q"));
    EXPECT_STREQ("| the note", s.text);
    EXPECT_EQ("", shape_section_title(s.text));
    EXPECT_EQ("the note", shape_section_comment(s.text));
}

TEST(ShapeSectionSet, ATitleOverTheLimitIsCutAndTheBuilderIsTold)
{
    SectionText s("Shop | the note");
    std::string longest(SHAPE_SECTION_TITLE_MAX, 'x');
    std::string too_long = longest + "yyy";
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, longest.c_str()));
    EXPECT_EQ(longest + " | the note", s.text);
    const char* note = shape_section_set(&s.text, SHAPE_SECTION_TITLE, too_long.c_str());
    ASSERT_NE(nullptr, note);
    EXPECT_NE(nullptr, strstr(note, "cut to 66"));
    EXPECT_EQ(longest + " | the note", s.text);
}

/* The limit is what fits, uncut, on the narrowest list row it must fit: a
 * script row numbered 100-999. */
TEST(ShapeSectionSet, ATitleAtTheLimitFitsUncutOnAThreeDigitScriptRow)
{
    SectionText s;
    std::string longest(SHAPE_SECTION_TITLE_MAX, 'x');
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, longest.c_str()));

    char out[1000];
    shape_section_show(out, sizeof(out), "[999] ", s.text, 0, 0);
    EXPECT_EQ("[999] == " + longest + " ==\n\r", std::string(out));
    EXPECT_EQ(78u + 2u, strlen(out));

    /* One more character would not have fit. */
    shape_section_show(out, sizeof(out), "[999] ", (longest + "x").c_str(), 0, 0);
    EXPECT_EQ("[999] == " + longest + " ==\n\r", std::string(out));
}

TEST(ShapeSectionSet, CharactersTheFilesCannotHoldAreReplaced)
{
    SectionText s;
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_TITLE, "Shop #1 ~ in|out"));
    EXPECT_STREQ("Shop +1 - in/out", s.text);
    EXPECT_EQ(nullptr, shape_section_set(&s.text, SHAPE_SECTION_COMMENT, "mobs #2 ~ a|b"));
    EXPECT_STREQ("Shop +1 - in/out | mobs +2 - a|b", s.text);
}

/* ---- the list rows ---- */

TEST(ShapeSectionShow, ATitleAloneIsOneRowOf78)
{
    char out[1000];
    shape_section_show(out, sizeof(out), " 12 ", "Shop Handling", 0, 2);
    std::string row(out);
    EXPECT_EQ(78u + 2u, row.size());
    EXPECT_EQ(" 12 ===", row.substr(0, 7));
    EXPECT_EQ("===\n\r", row.substr(row.size() - 5));
    EXPECT_NE(std::string::npos, row.find(" Shop Handling "));
}

TEST(ShapeSectionShow, ACommentSitsUnderTheTitleWithAClosingRow)
{
    char out[1000];
    shape_section_show(out, sizeof(out), " 12 ", "Shop Handling | keep it together", 0, 2);
    std::string rows(out);
    size_t first = rows.find("\n\r");
    size_t second = rows.find("\n\r", first + 2);
    ASSERT_NE(std::string::npos, second);
    EXPECT_EQ(78u, first);
    EXPECT_EQ("      keep it together", rows.substr(first + 2, second - first - 2));
    EXPECT_EQ("    " + std::string(74, '=') + "\n\r", rows.substr(second + 2));
}

TEST(ShapeSectionShow, AWiderLabelNarrowsTheBannerToStayAt78)
{
    char out[1000];
    shape_section_show(out, sizeof(out), "[123] ", "Shop Handling | note", 0, 0);
    std::string rows(out);
    size_t first = rows.find("\n\r");
    size_t second = rows.find("\n\r", first + 2);
    EXPECT_EQ(78u, first);
    EXPECT_EQ("      note", rows.substr(first + 2, second - first - 2));
    EXPECT_EQ("      " + std::string(72, '=') + "\n\r", rows.substr(second + 2));
}

TEST(ShapeSectionShow, ALongCommentWrapsInsideTheBanner)
{
    std::string comment;
    for (int i = 0; i < 30; i++)
        comment += (i ? " word" : "word") + std::to_string(i);
    char out[2000];
    shape_section_show(out, sizeof(out), " 12 ", ("Shop | " + comment).c_str(), 0, 2);

    std::string rows(out), joined;
    size_t at = 0, count = 0;
    while (at < rows.size()) {
        size_t end = rows.find("\n\r", at);
        ASSERT_NE(std::string::npos, end);
        std::string line = rows.substr(at, end - at);
        EXPECT_LE(line.size(), 78u) << line;
        if (count > 0 && line.find("====") == std::string::npos) {
            EXPECT_EQ("      ", line.substr(0, 6)) << line;
            EXPECT_NE(' ', line[6]) << line;
            joined += (joined.empty() ? "" : " ") + line.substr(6);
        }
        at = end + 2;
        count++;
    }
    EXPECT_GT(count, 4u); // title row, more than one comment line, closing row
    EXPECT_EQ(comment, joined); // nothing lost, nothing added
    EXPECT_EQ("    " + std::string(74, '='), rows.substr(rows.rfind("    ="), 78));
}

TEST(ShapeSectionShow, AWordLongerThanALineIsSplit)
{
    char out[2000];
    shape_section_show(out, sizeof(out), " 12 ", ("Shop | " + std::string(100, 'z')).c_str(), 0, 2);
    std::string rows(out);
    EXPECT_NE(std::string::npos, rows.find("\n\r      " + std::string(72, 'z') + "\n\r      " + std::string(28, 'z') + "\n\r"));
}

TEST(ShapeSectionShow, ATitleTooLongForTheRowIsCutSoTheRowStaysAt78)
{
    char out[1000];
    shape_section_show(out, sizeof(out), "Curr:  12 ", std::string(66, 'x').c_str(), 0, 2);
    EXPECT_EQ(78u + 2u, strlen(out));
    EXPECT_NE(nullptr, strstr(out, "== xxx"));
}

/* Wrapping adds an indent and a line end to every line, so a comment that
 * fits the list buffer as typed may not fit once drawn.  It is cut at the
 * buffer's end; it used to be copied past it. */
TEST(ShapeSectionShow, RowsThatDoNotFitTheBufferAreCut)
{
    std::string comment;
    while (comment.size() < 3000)
        comment += "word ";
    struct {
        char out[1000];
        char guard[8];
    } b;
    memset(b.guard, 'G', sizeof(b.guard));
    shape_section_show(b.out, sizeof(b.out), " 12 ", ("Big | " + comment).c_str(), 0, 2);

    EXPECT_LT(strlen(b.out), sizeof(b.out));
    EXPECT_EQ(std::string(8, 'G'), std::string(b.guard, 8));
    EXPECT_EQ("\n\r", std::string(b.out).substr(strlen(b.out) - 2));
}

TEST(ShapeSectionShow, TextAlreadyOnTheLineCountsAgainstTheWidth)
{
    char out[1000];
    shape_section_show(out, sizeof(out), " 12 ", "Shop Handling", 6, 2); // after "Curr: "
    EXPECT_EQ(72u + 2u, strlen(out));
}

/* ---- a zone reset steps over a section ---- */

int set_exit_state(struct room_data* room, int dir, int newstate);

/* Room 0 with a door north and a door east, both open, and a zone of three
 * commands whose middle one is a section. */
class SectionZone {
public:
    explicit SectionZone(bool first_door_exists)
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(2);
        m_saved_top = top_of_world;
        top_of_world = 1;
        for (int r = 0; r < 2; ++r)
            for (int d = 0; d < NUM_OF_DIRS; ++d) {
                m_saved_exits[r][d] = world[r].dir_option[d];
                world[r].dir_option[d] = nullptr;
            }
        north.exit_info = EX_ISDOOR;
        north.to_room = 1; // room 1 has no exits, so no other side to set
        east.exit_info = EX_ISDOOR;
        east.to_room = 1;
        if (first_door_exists)
            world[0].dir_option[NORTH] = &north;
        world[0].dir_option[EAST] = &east;

        m_saved_table = zone_table;
        m_saved_top_zone = top_of_zone_table;
        zone_table = &m_zone;
        top_of_zone_table = 0;
        m_zone.cmd = m_cmds;
        m_zone.cmdno = 3;
        m_cmds[0].command = 'D'; // close the north door
        m_cmds[0].arg1 = 0;
        m_cmds[0].arg2 = NORTH;
        m_cmds[0].arg3 = 1;
        m_cmds[1].command = '=';
        m_cmds[2].command = 'D'; // close the east door, if the last command ran
        m_cmds[2].if_flag = 1;
        m_cmds[2].arg1 = 0;
        m_cmds[2].arg2 = EAST;
        m_cmds[2].arg3 = 1;
    }

    ~SectionZone()
    {
        for (int r = 0; r < 2; ++r)
            for (int d = 0; d < NUM_OF_DIRS; ++d)
                world[r].dir_option[d] = m_saved_exits[r][d];
        top_of_world = m_saved_top;
        zone_table = m_saved_table;
        top_of_zone_table = m_saved_top_zone;
    }

    room_direction_data north {};
    room_direction_data east {};
    reset_com m_cmds[3] {};

private:
    zone_data m_zone {};
    zone_data* m_saved_table;
    int m_saved_top_zone;
    int m_saved_top;
    room_direction_data* m_saved_exits[2][NUM_OF_DIRS];
};

TEST(ZoneResetSection, ACommandAfterASectionStillDependsOnTheOneBeforeIt)
{
    SectionZone ran(true);
    /* An if_flag on the section itself must not matter: 9 is "only if the
     * last command did NOT run", which would mark the section as failed. */
    ran.m_cmds[1].if_flag = 9;
    reset_zone(0);
    EXPECT_TRUE(IS_SET(ran.north.exit_info, EX_CLOSED));
    EXPECT_TRUE(IS_SET(ran.east.exit_info, EX_CLOSED));
}

/* The same rule with a different if_flag on the section: 2 is "only if the
 * last mobile loaded", and none has.  Read as a command, the section would
 * fail and block the command after it. */
TEST(ZoneResetSection, ASectionIsNotACommandThatFailed)
{
    SectionZone ran(true);
    ran.m_cmds[1].if_flag = 2;
    reset_zone(0);
    EXPECT_TRUE(IS_SET(ran.east.exit_info, EX_CLOSED));
}

/* The other way round.  This one passes on a server that knows nothing of
 * sections, which leaves an unknown row alone; it is here so that a later
 * change cannot make a section count as a command that ran. */
TEST(ZoneResetSection, ASectionIsNotACommandThatRan)
{
    SectionZone failed(false); // no north door: the first command cannot run
    failed.m_cmds[0].if_flag = 1; // and is marked as not run
    reset_zone(0);
    EXPECT_FALSE(IS_SET(failed.east.exit_info, EX_CLOSED));
}

} // namespace
