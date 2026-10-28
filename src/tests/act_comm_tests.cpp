#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../mail.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

ACMD(do_alias);
ACMD(do_write);
void clear_char(struct char_data* ch, int mode);
int replace_pattern(descriptor_data* descriptor, const char* pattern, const char* new_pattern);
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
void postmaster_send_mail(
    struct char_data* character, int command_number, const char* argument, char_data* host);

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    return descriptor;
}

class AliasingCharacter {
public:
    AliasingCharacter()
        : m_descriptor(make_descriptor())
    {
        clear_char(&m_character, MOB_VOID);
        m_character.player.name = strdup("Aliaser");
        m_character.player.level = 10;
        m_character.desc = &m_descriptor;
        m_descriptor.character = &m_character;
    }

    ~AliasingCharacter()
    {
        while (m_character.specials.alias != nullptr) {
            alias_list* next_alias = m_character.specials.alias->next;
            free(m_character.specials.alias->command);
            free(m_character.specials.alias);
            m_character.specials.alias = next_alias;
        }
        free(m_character.player.name);
        release_cleared_character(&m_character);
    }

    void alias(const std::string& arguments)
    {
        std::string mutable_arguments = arguments;
        mutable_arguments.push_back('\0');
        do_alias(&m_character, &mutable_arguments[0], nullptr, 0, 0);
    }

    char_data& character() { return m_character; }
    std::string output() const { return std::string(m_descriptor.small_outbuf); }

private:
    descriptor_data m_descriptor;
    char_data m_character {};
};

TEST(ActCommAlias, RefusesAKeywordTooLongForTheStoredField)
{
    AliasingCharacter player;

    player.alias("twenty_characters_ab kill orc");

    EXPECT_EQ(player.character().specials.alias, nullptr);
    EXPECT_NE(player.output().find("19 characters"), std::string::npos);
}

TEST(ActCommAlias, StoresAKeywordThatFillsEveryUsableCharacter)
{
    AliasingCharacter player;

    player.alias("nineteen_characters kill orc");

    ASSERT_NE(player.character().specials.alias, nullptr);
    EXPECT_STREQ(player.character().specials.alias->keyword, "nineteen_characters");
    EXPECT_STREQ(player.character().specials.alias->command, "kill orc");
}

const int NOTE_RNUM = 0;
const int PEN_RNUM = 1;

/* A note prototype with nothing written on it and a pen prototype, and a writer in room 0 carrying a
 * copy of each. */
class NoteWritingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_obj_proto = obj_proto;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;
        m_saved_top_of_world = top_of_world;
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }
        top_of_world = 0;

        clear_object(&m_prototypes[NOTE_RNUM]);
        m_prototypes[NOTE_RNUM].item_number = NOTE_RNUM;
        m_prototypes[NOTE_RNUM].name = m_note_name;
        m_prototypes[NOTE_RNUM].obj_flags.type_flag = ITEM_NOTE;
        clear_object(&m_prototypes[PEN_RNUM]);
        m_prototypes[PEN_RNUM].item_number = PEN_RNUM;
        m_prototypes[PEN_RNUM].name = m_pen_name;
        m_prototypes[PEN_RNUM].obj_flags.type_flag = ITEM_PEN;
        obj_proto = m_prototypes;
        obj_index = m_index;
        top_of_objt = PEN_RNUM;
        object_list = nullptr;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        clear_char(&m_writer, MOB_VOID);
        m_writer.in_room = 0;
        SET_BIT(PRF_FLAGS(&m_writer), PRF_HOLYLIGHT); /* sees in the unlit test room */
        m_writer.desc = &m_descriptor;
        m_descriptor.character = &m_writer;

        m_note = read_object(NOTE_RNUM, REAL);
        obj_to_char(m_note, &m_writer);
        obj_to_char(read_object(PEN_RNUM, REAL), &m_writer);
    }

    void TearDown() override
    {
        /* A copy of the note does not free its text, so what was written on it is freed here. */
        if (m_note->action_description != m_prototypes[NOTE_RNUM].action_description) {
            RELEASE(m_note->action_description);
        }
        m_descriptor.str = nullptr;
        while (object_list != nullptr) {
            extract_obj(object_list);
        }
        release_cleared_character(&m_writer);
        release_test_descriptor(&m_descriptor);
        obj_proto = m_saved_obj_proto;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
        top_of_world = m_saved_top_of_world;
    }

    /* Types `line` into the writer's text editor. */
    void type(const char* line)
    {
        std::string mutable_line = line;
        mutable_line.push_back('\0');
        string_add(&m_descriptor, &mutable_line[0]);
    }

    /* Starts writing on the note with the pen. */
    void start_writing()
    {
        char arguments[] = "note pen";
        do_write(&m_writer, arguments, nullptr, 0, 0);
    }

    char m_note_name[16] = "note paper"; // the note prototype's name, shared by its copies
    char m_pen_name[16] = "pen quill"; // the pen prototype's name, shared by its copies
    obj_data m_prototypes[2] {}; // the note and the pen, indexed by NOTE_RNUM and PEN_RNUM
    index_data m_index[2] {}; // their index entries, counting the copies in the game
    descriptor_data m_descriptor {}; // the writer's connection, holding the text editor
    char_data m_writer {}; // the one writing, carrying both copies
    obj_data* m_note = nullptr; // the writer's copy of the note

private:
    obj_data* m_saved_obj_proto = nullptr; // restored in TearDown
    index_data* m_saved_obj_index = nullptr; // restored in TearDown
    int m_saved_top_of_objt = 0; // restored in TearDown
    obj_data* m_saved_object_list = nullptr; // restored in TearDown
    int m_saved_top_of_world = 0; // restored in TearDown
};

/* Showing, listing or formatting a note before its first line touches no text, even after an
 * earlier edit on the same connection left its lengths behind. */
class UnstartedNoteCommandTest : public NoteWritingTest, public testing::WithParamInterface<const char*> {
};

/* The lengths an earlier edit on the connection leaves behind: its text's length, its cursor and
 * its buffer's size. */
constexpr unsigned int LEFTOVER_TEXT_LENGTH = 40;
constexpr unsigned int LEFTOVER_CURSOR = 20;
constexpr unsigned int LEFTOVER_BUFFER_SIZE = 64;

TEST_P(UnstartedNoteCommandTest, ReadsNoTextAfterAnEarlierEdit)
{
    start_writing();
    ASSERT_EQ(m_descriptor.str, &m_note->action_description) << m_descriptor.output;
    m_descriptor.len_str = LEFTOVER_TEXT_LENGTH;
    m_descriptor.cur_str = LEFTOVER_CURSOR;
    m_descriptor.max_str = LEFTOVER_BUFFER_SIZE;

    type(GetParam());

    /* The command line itself is added as an empty line, so the text is just its line end. */
    ASSERT_NE(m_note->action_description, nullptr);
    EXPECT_EQ(std::strlen(m_note->action_description), m_descriptor.len_str);
    EXPECT_LE(m_descriptor.cur_str, m_descriptor.len_str);
}

INSTANTIATE_TEST_SUITE_P(EditorCommands, UnstartedNoteCommandTest, testing::Values("%r", "%l", "%f"));

/* %e keeps every line typed, though finishing makes the editor regrow the note's first small buffer. */
TEST_F(NoteWritingTest, FinishingWithPercentEKeepsTheNote)
{
    start_writing();

    type("Meet me at the ford.");
    type("%e");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
}

/* A line that carries the note past one editor block keeps the lines before it and itself whole. */
TEST_F(NoteWritingTest, ALineCrossingABlockKeepsTheWholeNote)
{
    start_writing();
    const std::string long_line(BLOCK_STR_LEN, 'x');

    type("Meet me at the ford.");
    type(long_line.c_str());
    type("%e");

    const std::string expected_note = "Meet me at the ford.\n\r" + long_line + "\n\r";
    EXPECT_EQ(m_descriptor.str, nullptr);
    ASSERT_NE(m_note->action_description, nullptr);
    EXPECT_EQ(std::string(m_note->action_description), expected_note);
}

/* The part of the editor's refusal that names the limit, by default the note's. */
std::string past_limit_message(int limit = MAX_NOTE_LENGTH)
{
    return "past its limit of " + std::to_string(limit) + " characters";
}

/* A first line of MAX_NOTE_LENGTH - 10 characters leaves the note 8 characters short of its limit. */
const size_t NEAR_LIMIT_LINE_LENGTH = MAX_NOTE_LENGTH - 10;

/* Writing on a note limits it to MAX_NOTE_LENGTH characters. */
TEST_F(NoteWritingTest, WritingLimitsTheNoteToMaxNoteLength)
{
    start_writing();

    EXPECT_EQ(m_descriptor.str_limit, static_cast<unsigned int>(MAX_NOTE_LENGTH));
}

/* A line that leaves the note under its limit is added. */
TEST_F(NoteWritingTest, ALineUnderTheLimitIsAdded)
{
    start_writing();
    const std::string first_line(NEAR_LIMIT_LINE_LENGTH, 'x');

    type(first_line.c_str());
    type("ford");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\rford\n\r");
    EXPECT_EQ(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
}

/* A line that would take the note past its limit is refused with a message, and the note keeps its text. */
TEST_F(NoteWritingTest, ALinePastTheLimitIsRefusedAndTheTextKept)
{
    start_writing();
    const std::string first_line(NEAR_LIMIT_LINE_LENGTH, 'x');

    type(first_line.c_str());
    type("the river");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\r");
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);

    type("%e");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(m_note->action_description), first_line + "\n\r");
}

/* A line that brings the note to exactly its limit is added. */
TEST_F(NoteWritingTest, ALineReachingExactlyTheLimitIsAdded)
{
    start_writing();
    const std::string first_line(NEAR_LIMIT_LINE_LENGTH, 'x');

    type(first_line.c_str());
    type("ford!!");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\rford!!\n\r");
    EXPECT_EQ(std::strlen(*m_descriptor.str), static_cast<size_t>(MAX_NOTE_LENGTH));
    EXPECT_EQ(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
}

/* %s is refused when its replacement would take the note past its limit, and allowed when it fits. */
TEST_F(NoteWritingTest, AReplacementPastTheLimitIsRefusedAndTheTextKept)
{
    start_writing();
    const std::string first_word = "ford ";
    const std::string rest_of_line(NEAR_LIMIT_LINE_LENGTH - first_word.size(), 'x');
    const std::string first_line = first_word + rest_of_line;

    type(first_line.c_str());
    type("%sford~shallow river crossing");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\r");
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
    EXPECT_EQ(std::string(m_descriptor.output).find("Replaced"), std::string::npos);

    type("%sford~ferry");

    EXPECT_EQ(std::string(*m_descriptor.str), "ferry " + rest_of_line + "\n\r");
}

/* Finishing a note lifts its limit, so the next text edited is not held to it. */
TEST_F(NoteWritingTest, FinishingTheNoteClearsTheLimit)
{
    start_writing();

    type("Meet me at the ford.");
    type("%e");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_EQ(m_descriptor.str_limit, 0u);
}

/* A line longer than a note may hold. */
constexpr size_t PAST_NOTE_LIMIT_LINE_LENGTH = MAX_NOTE_LENGTH + 10;

/* The text editor opened on other text has no limit, whatever limit the descriptor held before. */
TEST_F(NoteWritingTest, TheTextEditorHasNoLimit)
{
    char* text = nullptr;
    m_descriptor.str_limit = MAX_NOTE_LENGTH;

    string_add_init(&m_descriptor, &text);

    EXPECT_EQ(m_descriptor.str_limit, 0u);
    const std::string long_line(PAST_NOTE_LIMIT_LINE_LENGTH, 'x');
    type(long_line.c_str());
    type("%e");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(std::string(text), "\n\r" + long_line + "\n\r");
    EXPECT_EQ(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
    RELEASE(text);
}

/* %s replaces every match, reports how many, and keeps the cursor in its place in the text. */
TEST_F(NoteWritingTest, AReplacementKeepsTheCursorInPlace)
{
    char* text = str_dup("ford\n\rford\n\r");
    string_add_init(&m_descriptor, &text);

    type("%l1");
    type("%sford~river");

    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "river\n\rriver\n\r");
    EXPECT_NE(std::string(m_descriptor.output).find("Replaced 2 occurences"), std::string::npos);
    EXPECT_EQ(m_descriptor.len_str, std::strlen(text));
    EXPECT_EQ(m_descriptor.cur_str, std::strlen("river\n\r"));

    type("mid");
    type("%e");
    EXPECT_STREQ(text, "river\n\rmid\n\rriver\n\r");
    RELEASE(text);
}

/* %s on a note with nothing written yet replaces nothing and leaves the note blank. */
TEST_F(NoteWritingTest, AReplacementOnAnEmptyNoteReplacesNothing)
{
    start_writing();

    type("%sford~river");

    EXPECT_STREQ(m_note->action_description, "\n\r"); /* a command starts an empty text with a line break */
    EXPECT_NE(std::string(m_descriptor.output).find("Replaced 0 occurence"), std::string::npos);
}

/* A replacement of a match the cursor falls inside leaves the cursor at the end of the replacement. */
TEST_F(NoteWritingTest, AReplacementAroundTheCursorLeavesItAfterTheReplacement)
{
    char* text = str_dup("abcdef");
    m_descriptor.str = &text;
    m_descriptor.len_str = std::strlen(text);
    m_descriptor.max_str = std::strlen(text);
    m_descriptor.cur_str = std::strlen("abc"); /* abc|def */

    EXPECT_EQ(replace_pattern(&m_descriptor, "bcde", "X"), 1);

    EXPECT_STREQ(text, "aXf");
    EXPECT_EQ(m_descriptor.len_str, std::strlen("aXf"));
    EXPECT_EQ(m_descriptor.cur_str, std::strlen("aX")); /* aX|f */
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* A replacement that shrinks the text around the cursor never leaves the cursor past the text's end. */
TEST_F(NoteWritingTest, AShrinkingReplacementKeepsTheCursorWithinTheText)
{
    char* text = str_dup("abcdef");
    m_descriptor.str = &text;
    m_descriptor.len_str = std::strlen(text);
    m_descriptor.max_str = std::strlen(text);
    m_descriptor.cur_str = std::strlen("abcde"); /* abcde|f */

    EXPECT_EQ(replace_pattern(&m_descriptor, "abcdef", ""), 1);

    EXPECT_STREQ(text, "");
    EXPECT_EQ(m_descriptor.len_str, 0u);
    EXPECT_EQ(m_descriptor.cur_str, 0u);
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* "%sa~aaa" replaces each 'a' with this many. */
constexpr size_t REPLACEMENT_GROWTH = 3;
/* LONG_LINE_COUNT lines of LONG_LINE_LENGTH 'a's and a line of LAST_LINE_A_COUNT 'a's and a 'b':
 * tripling every 'a' gives exactly MAX_EDITED_TEXT_LENGTH characters. */
constexpr int LONG_LINE_COUNT = 26;
constexpr size_t LONG_LINE_LENGTH = 98;
constexpr size_t LAST_LINE_A_COUNT = 78;

/* %s may grow a text to exactly MAX_EDITED_TEXT_LENGTH characters. */
TEST_F(NoteWritingTest, AReplacementMayGrowATextToTheEditorsCap)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    const std::string line(LONG_LINE_LENGTH, 'a');
    const std::string last_line = std::string(LAST_LINE_A_COUNT, 'a') + "b";
    std::string expected_after = "\n\r";
    for (int line_number = 0; line_number < LONG_LINE_COUNT; line_number++) {
        type(line.c_str());
        expected_after += std::string(REPLACEMENT_GROWTH * line.size(), 'a') + "\n\r";
    }
    type(last_line.c_str());
    expected_after += std::string(REPLACEMENT_GROWTH * LAST_LINE_A_COUNT, 'a') + "b\n\r";
    ASSERT_EQ(expected_after.size(), static_cast<size_t>(MAX_EDITED_TEXT_LENGTH));

    type("%sa~aaa");

    EXPECT_EQ(std::string(text), expected_after);
    EXPECT_EQ(m_descriptor.len_str, expected_after.size());
    EXPECT_GT(m_descriptor.max_str, m_descriptor.len_str);
    EXPECT_EQ(m_descriptor.cur_str, m_descriptor.len_str);
    EXPECT_EQ(std::string(m_descriptor.output).find(past_limit_message(MAX_EDITED_TEXT_LENGTH)), std::string::npos);
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* Lines of LONG_LINE_LENGTH 'a's, 100 characters with their line end: this many come to about
 * 3,000 characters, which tripling takes past the cap. */
constexpr int TRIPLED_PAST_CAP_LINE_COUNT = 30;

/* %s growing a text past MAX_EDITED_TEXT_LENGTH is refused in an editor with no limit of its own, and
 * the text and cursor stay as they were. */
TEST_F(NoteWritingTest, AReplacementPastTheEditorsCapIsRefused)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    const std::string line(LONG_LINE_LENGTH, 'a');
    std::string expected = "\n\r";
    for (int line_number = 0; line_number < TRIPLED_PAST_CAP_LINE_COUNT; line_number++) {
        type(line.c_str());
        expected += line + "\n\r";
    }
    ASSERT_EQ(std::string(text), expected);
    const unsigned int cursor_before = m_descriptor.cur_str;

    type("%sa~aaa");

    EXPECT_EQ(std::string(text), expected);
    EXPECT_EQ(m_descriptor.len_str, expected.size());
    EXPECT_EQ(m_descriptor.cur_str, cursor_before);
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message(MAX_EDITED_TEXT_LENGTH)), std::string::npos);
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* Lines of FULL_LINE_LENGTH 'x's, 1,002 characters with their line end: NEAR_CAP_LINE_COUNT of
 * them come to about 7,000 characters, and one more passes the cap. */
constexpr size_t FULL_LINE_LENGTH = 1000;
constexpr int NEAR_CAP_LINE_COUNT = 7;

/* A line that would take a text past MAX_EDITED_TEXT_LENGTH is refused in an editor with no limit of
 * its own. */
TEST_F(NoteWritingTest, ALinePastTheEditorsCapIsRefused)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    const std::string line(FULL_LINE_LENGTH, 'x');
    std::string expected = "\n\r";
    for (int line_number = 0; line_number < NEAR_CAP_LINE_COUNT; line_number++) {
        type(line.c_str());
        expected += line + "\n\r";
    }
    ASSERT_EQ(std::string(text), expected);

    type(line.c_str());

    EXPECT_EQ(std::string(text), expected);
    EXPECT_EQ(m_descriptor.len_str, expected.size());
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message(MAX_EDITED_TEXT_LENGTH)), std::string::npos);
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* Writing with a paper the writer carries and a pen it does not names the missing pen. */
TEST_F(NoteWritingTest, WritingWithAMissingPenNamesThePen)
{
    char arguments[] = "note feather";

    do_write(&m_writer, arguments, nullptr, 0, 0);

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_NE(std::string(m_descriptor.output).find("You have no feather.\n\r"), std::string::npos)
        << m_descriptor.output;
    EXPECT_EQ(std::string(m_descriptor.output).find("You have no note."), std::string::npos)
        << m_descriptor.output;
}

/* Starting a note tells the writer to end it with %e, the editor's command for finishing. */
TEST_F(NoteWritingTest, StartingANoteSaysToEndItWithPercentE)
{
    start_writing();

    const std::string prompt = "Ok.. go ahead and write.. end the note with %e.\n\r";
    EXPECT_NE(std::string(m_descriptor.output).find(prompt), std::string::npos)
        << m_descriptor.output;
}

/* A writer of MIN_MAIL_LEVEL with the price of a stamp, in room 0 with a postmaster, and a player
 * table holding the one the letter is to. */
class MailWritingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_top_of_world = top_of_world;
        m_saved_player_table = player_table;
        m_saved_top_of_p_table = top_of_p_table;
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }
        top_of_world = 0;
        m_saved_room_people = world[0].people;

        m_recipient_entry.name = m_recipient_name;
        player_table = &m_recipient_entry;
        top_of_p_table = 0;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        clear_char(&m_writer, MOB_VOID);
        m_writer.player.name = m_writer_name;
        m_writer.player.level = MIN_MAIL_LEVEL;
        GET_GOLD(&m_writer) = STAMP_PRICE;
        m_writer.in_room = 0;
        GET_POS(&m_writer) = POSITION_STANDING;
        /* Sees the postmaster in the unlit test room. */
        SET_BIT(PRF_FLAGS(&m_writer), PRF_HOLYLIGHT);
        m_writer.desc = &m_descriptor;
        m_descriptor.character = &m_writer;

        clear_char(&m_postmaster, MOB_ISNPC);
        SET_BIT(MOB_FLAGS(&m_postmaster), MOB_ISNPC);
        m_postmaster.player.name = m_postmaster_name;
        m_postmaster.player.short_descr = m_postmaster_name;
        m_postmaster.in_room = 0;
        GET_POS(&m_postmaster) = POSITION_STANDING;

        world[0].people = &m_writer;
        m_writer.next_in_room = &m_postmaster;
        m_postmaster.next_in_room = nullptr;
    }

    void TearDown() override
    {
        /* Nothing is sent: the letter and the editor's buffers are given back as they are. */
        if (m_descriptor.str != nullptr) {
            RELEASE(*m_descriptor.str);
            RELEASE(m_descriptor.str);
        }
        RELEASE(m_descriptor.name);
        m_descriptor.str_limit = 0;
        world[0].people = m_saved_room_people;
        release_cleared_character(&m_postmaster);
        release_cleared_character(&m_writer);
        release_test_descriptor(&m_descriptor);
        player_table = m_saved_player_table;
        top_of_p_table = m_saved_top_of_p_table;
        top_of_world = m_saved_top_of_world;
    }

    /* Asks the postmaster to send a letter to the recipient. */
    void start_letter() { postmaster_send_mail(&m_writer, 0, m_recipient_name, &m_postmaster); }

    /* Types <line> into the writer's text editor. */
    void type(const char* line)
    {
        std::string mutable_line = line;
        mutable_line.push_back('\0');
        string_add(&m_descriptor, &mutable_line[0]);
    }

    std::string output() const { return std::string(m_descriptor.output); }

    char m_recipient_name[16] = "frodo"; // the one the letter is to, the player table's one entry
    char m_writer_name[16] = "Sam"; // the writer's name
    char m_postmaster_name[16] = "postmaster"; // the postmaster's keywords and short description
    player_index_element m_recipient_entry {}; // the player table during the test
    descriptor_data m_descriptor {}; // the writer's connection, holding the text editor
    char_data m_writer {}; // the one writing the letter
    char_data m_postmaster {}; // the postmaster taking the letter

private:
    int m_saved_top_of_world = 0; // restored in TearDown
    player_index_element* m_saved_player_table = nullptr; // restored in TearDown
    int m_saved_top_of_p_table = 0; // restored in TearDown
    char_data* m_saved_room_people = nullptr; // restored in TearDown
};

/* A first line of MAX_MAIL_SIZE - 10 characters leaves the letter 8 characters short of its
 * limit. */
const size_t NEAR_MAIL_LIMIT_LINE_LENGTH = MAX_MAIL_SIZE - 10;

/* Writing a letter limits it to MAX_MAIL_SIZE characters. */
TEST_F(MailWritingTest, WritingLimitsTheLetterToMaxMailSize)
{
    start_letter();

    ASSERT_NE(m_descriptor.str, nullptr) << output();
    EXPECT_EQ(m_descriptor.str_limit, static_cast<unsigned int>(MAX_MAIL_SIZE));
}

/* A line that would take the letter past its limit is refused with a message, and the letter keeps
 * its text. */
TEST_F(MailWritingTest, ALinePastTheLimitIsRefusedAndTheTextKept)
{
    start_letter();
    ASSERT_NE(m_descriptor.str, nullptr) << output();
    const std::string first_line(NEAR_MAIL_LIMIT_LINE_LENGTH, 'x');

    type(first_line.c_str());
    type("the river");

    ASSERT_NE(*m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\r");
    const std::string refusal = "That would take the text past its limit of "
        + std::to_string(MAX_MAIL_SIZE) + " characters, so it was not changed.";
    EXPECT_NE(output().find(refusal), std::string::npos) << output();
}

/* A line that brings the letter to exactly its limit is added. */
TEST_F(MailWritingTest, ALineReachingExactlyTheLimitIsAdded)
{
    start_letter();
    ASSERT_NE(m_descriptor.str, nullptr) << output();
    const std::string first_line(NEAR_MAIL_LIMIT_LINE_LENGTH, 'x');

    type(first_line.c_str());
    type("ford!!");

    ASSERT_NE(*m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\rford!!\n\r");
    EXPECT_EQ(std::strlen(*m_descriptor.str), static_cast<size_t>(MAX_MAIL_SIZE));
    EXPECT_EQ(output().find(past_limit_message(MAX_MAIL_SIZE)), std::string::npos) << output();
}

} // namespace
