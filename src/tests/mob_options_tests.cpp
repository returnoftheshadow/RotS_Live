#include "../boards.h"
#include "../mob_options.h"
#include "../protos.h"
#include "../script.h"

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

TEST(MobOptionsTidy, DropsLeadingAndInternalBlankLines)
{
    char text[] = "\n\r  \n\r\tstore=1\n\r\n\r  \n\rprice 1 2x3\n\r";
    mob_options_tidy(text);
    EXPECT_STREQ(text, "store=1\n\rprice 1 2x3\n\r");
    char blank[] = " \n\r ";
    mob_options_tidy(blank);
    EXPECT_STREQ(blank, "");
    char plain[] = "store=1";
    mob_options_tidy(plain);
    EXPECT_STREQ(plain, "store=1");
    mob_options_tidy(nullptr);
}

/* What the editor stores is exactly what a save and reload gives back, so
 * warnings count the same lines at /save, /imp and boot. */
TEST(MobOptionsTidy, StoredTextSurvivesASaveAndReloadUnchanged)
{
    char ctx[] = "test";
    char text[] = "store=1\n\r\n\r// stock\n\r\n\rprice 1 2x3\n\r";
    mob_options_tidy(text);
    FILE* f = tmpfile();
    write_mob_options(f, text);
    fputs("#2\n", f);
    rewind(f);
    char* back = read_mob_options(f, ctx);
    ASSERT_NE(back, nullptr);
    EXPECT_STREQ(back, text);
    fclose(f);
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

void write_proto(FILE* f, struct char_data* m, int num);
void load_mobiles(FILE* mob_f);
void clear_char(struct char_data* ch, int mode);
extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

namespace {

struct written_mob {
    char name[16] = "golem";
    char short_descr[16] = "a golem";
    char long_descr[24] = "A golem stands here.";
    char description[16] = "Big.\n\r";
    char cry[1] = "";
    char cry2[1] = "";
    char_data mob {};

    explicit written_mob(char* options)
    {
        clear_char(&mob, MOB_ISNPC);
        /* MOB_SPEC, like any vendor: the loader then keeps the program number
         * as is, rather than looking it up in the (unloaded) program table. */
        mob.specials2.act = MOB_ISNPC | MOB_SPEC;
        mob.player.name = name;
        mob.player.short_descr = short_descr;
        mob.player.long_descr = long_descr;
        mob.player.description = description;
        mob.player.death_cry = cry;
        mob.player.death_cry2 = cry2;
        mob.specials.mob_options = options;
    }
};

} // namespace

/* The shape editor writes records with write_proto; the boot loader must
 * read each one's options back, or none, without eating the next record,
 * including the last record before the end-of-file marker. */
TEST(MobFileRoundTrip, WriteProtoThenLoadMobilesKeepsEachRecordsOptions)
{
    char_data* const saved_proto = mob_proto;
    index_data* const saved_index = mob_index;
    const int saved_top = top_of_mobt;
    static char_data protos[256];
    static index_data index[256];
    mob_proto = protos;
    mob_index = index;

    char with[] = "store=5\n\rprice 1 2x3";
    char last[] = "store=6";
    written_mob none(nullptr), middle(with), final_one(last);
    FILE* f = tmpfile();
    write_proto(f, &none.mob, 1001);
    write_proto(f, &middle.mob, 1002);
    write_proto(f, &final_one.mob, 1003);
    fputs("$~\n", f);
    rewind(f);
    load_mobiles(f);
    fclose(f);

    const int first = top_of_mobt - 2;
    ASSERT_GE(first, 0);
    EXPECT_EQ(index[first].virt, 1001);
    EXPECT_EQ(protos[first].specials.mob_options, nullptr);
    EXPECT_STREQ(protos[first + 1].player.name, "golem");
    EXPECT_EQ(index[first + 1].virt, 1002);
    ASSERT_NE(protos[first + 1].specials.mob_options, nullptr);
    EXPECT_STREQ(protos[first + 1].specials.mob_options, "store=5\n\rprice 1 2x3");
    EXPECT_EQ(index[first + 2].virt, 1003);
    ASSERT_NE(protos[first + 2].specials.mob_options, nullptr);
    EXPECT_STREQ(protos[first + 2].specials.mob_options, "store=6");

    mob_proto = saved_proto;
    mob_index = saved_index;
    top_of_mobt = saved_top;
}

TEST(ScriptFormatText, OnlyTheFirstPercentSIsFilledEverythingElseIsLiteral)
{
    char out[64];
    script_format_text(out, sizeof(out), "Hello %s, %s %d%% %n", "Drew");
    EXPECT_STREQ(out, "Hello Drew, %s %d% %n");
    script_format_text(out, sizeof(out), "No arg: %s.", nullptr);
    EXPECT_STREQ(out, "No arg: .");
    script_format_text(out, 8, "truncated text", nullptr);
    EXPECT_STREQ(out, "truncat");
    script_format_text(out, sizeof(out), nullptr, "x");
    EXPECT_STREQ(out, "");
}

TEST(BanSiteOk, TrimsTrailingBlanksAndRefusesInnerSpaces)
{
    char trailing[] = "bad.example.com  \t";
    EXPECT_TRUE(ban_site_ok(trailing));
    EXPECT_STREQ(trailing, "bad.example.com");
    char inner[] = "bad example.com";
    EXPECT_FALSE(ban_site_ok(inner));
    char tab[] = "bad\texample.com";
    EXPECT_FALSE(ban_site_ok(tab));
}

/* A long post full of line breaks: each '\n' grows to "<br>", which used to
 * overflow a fixed buffer. Written straight to the file now. */
TEST(BoardHtml, LongPostExpandsLineBreaksWithoutABuffer)
{
    std::string post;
    for (int i = 0; i < 20000; ++i)
        post += "x\n\r";
    FILE* f = tmpfile();
    write_board_message_html(f, post.c_str(), (int)post.size() + 1);
    long size = ftell(f);
    EXPECT_EQ(size, 20000L * 5);
    rewind(f);
    char head[16] = {};
    ASSERT_EQ(fread(head, 1, 10, f), 10u);
    EXPECT_STREQ(head, "x<br>x<br>");
    fclose(f);
}

TEST(MobGeneralOptions, ListsEveryWalkerKey)
{
    for (const char* key : { "walker_type", "walker_rooms", "walker_move_chance", "path_complete_extract",
             "path_complete_message", "continue_wandering" })
        EXPECT_TRUE(mob_option_is_general(key)) << key;
}

TEST(MobGeneralOptions, ProgramKeysAndTyposAreNotGeneral)
{
    EXPECT_FALSE(mob_option_is_general("store"));
    EXPECT_FALSE(mob_option_is_general("fee"));
    EXPECT_FALSE(mob_option_is_general("walker_typ"));
    EXPECT_FALSE(mob_option_is_general(""));
}

TEST(MobGeneralOptions, EveryKeyHasHelpOfAtMostSixtyColumns)
{
    for (const mob_general_option& option : mob_general_options()) {
        ASSERT_NE(option.help, nullptr) << option.key;
        EXPECT_GT(strlen(option.help), 0u) << option.key;
        EXPECT_LE(strlen(option.help), 60u) << option.key;
    }
}
