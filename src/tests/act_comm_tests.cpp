#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../mail.h"
#include "../note_writing.h"
#include "../owned_character.h"
#include "../structs.h"
#include "../utils.h"
#include "object_text_test_support.h"
#include "scoped_mail_file.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <string>

ACMD(do_alias);
ACMD(do_write);
void clear_char(struct char_data* ch, int mode);
std::optional<int> replace_pattern(descriptor_data* descriptor, const char* pattern, const char* new_pattern);
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct descriptor_data* descriptor_list;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
void postmaster_send_mail(
    struct char_data* character, int command_number, const char* argument, char_data* host);
void postmaster_receive_mail(
    struct char_data* character, int command_number, const char* argument, char_data* host);
void index_mail(char* raw_name_to_index, long position);
extern int no_mail;

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
        make_text_shared(m_prototypes[NOTE_RNUM]);
        make_text_shared(m_prototypes[PEN_RNUM]);
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
        finish_writing_note(m_descriptor);
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

/* Writing on a note's copy gives it a text of its own, which goes with it, and leaves its prototype
 * blank. */
TEST_F(NoteWritingTest, AWrittenNoteHasATextOfItsOwn)
{
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;

    type("Meet me at the ford.");
    string_add_finish(&m_descriptor);

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
    EXPECT_STREQ(m_note->name, "note paper");
    ASSERT_NE(m_note->shared_text(), nullptr);
    EXPECT_NE(m_note->shared_text(), m_prototypes[NOTE_RNUM].shared_text());
    EXPECT_EQ(m_note->shared_text().use_count(), 1) << "no other object shows the note's text";

    extract_obj(m_note);
    EXPECT_EQ(m_prototypes[NOTE_RNUM].action_description, nullptr);
    EXPECT_STREQ(m_prototypes[NOTE_RNUM].name, "note paper");
}

/* Writing on a note changes only what is written on it: the note keeps the rest of its text. */
TEST_F(NoteWritingTest, AWrittenNoteKeepsTheRestOfItsText)
{
    object_text::fields text = object_text_fields_of(*m_note);
    text.short_description = "a sheet of paper";
    text.description = "A sheet of paper lies here.";
    text.extra_descriptions.push_back({ "watermark", "A faint watermark." });
    m_note->set_text(create_object_text(std::move(text)));
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;

    type("Meet me at the ford.");
    string_add_finish(&m_descriptor);

    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
    EXPECT_STREQ(m_note->name, "note paper");
    EXPECT_STREQ(m_note->short_description, "a sheet of paper");
    EXPECT_STREQ(m_note->description, "A sheet of paper lies here.");
    ASSERT_NE(m_note->ex_description, nullptr);
    EXPECT_STREQ(m_note->ex_description->keyword, "watermark");
    EXPECT_STREQ(m_note->ex_description->description, "A faint watermark.");
}

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
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;
    m_descriptor.len_str = LEFTOVER_TEXT_LENGTH;
    m_descriptor.cur_str = LEFTOVER_CURSOR;
    m_descriptor.max_str = LEFTOVER_BUFFER_SIZE;

    type(GetParam());

    /* The command line itself is added as an empty line, so the text is just its line end. */
    ASSERT_NE(m_descriptor.written_note_text, nullptr);
    EXPECT_EQ(std::strlen(m_descriptor.written_note_text), m_descriptor.len_str);
    EXPECT_LE(m_descriptor.cur_str, m_descriptor.len_str);
}

INSTANTIATE_TEST_SUITE_P(EditorCommands, UnstartedNoteCommandTest, testing::Values("%r", "%l", "%f"));

/* A note abandoned with %q is left blank. */
TEST_F(NoteWritingTest, AnAbandonedNoteIsLeftBlank)
{
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;

    type("Meet me at the");
    type("%q");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_EQ(m_note->action_description, nullptr);
}

/* %e keeps every line typed, though finishing makes the editor regrow the note's first small buffer. */
TEST_F(NoteWritingTest, FinishingWithPercentEKeepsTheNote)
{
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;

    type("Meet me at the ford.");
    type("%e");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
}

/* A line that carries the note past one editor block keeps the lines before it and itself whole. */
TEST_F(NoteWritingTest, ALineCrossingABlockKeepsTheWholeNote)
{
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;
    const std::string long_line(BLOCK_STR_LEN, 'x');

    type("Meet me at the ford.");
    type(long_line.c_str());
    type("%e");

    const std::string expected_note = "Meet me at the ford.\n\r" + long_line + "\n\r";
    EXPECT_EQ(m_descriptor.str, nullptr);
    ASSERT_NE(m_note->action_description, nullptr);
    EXPECT_EQ(std::string(m_note->action_description), expected_note);
}

/* A note's copy that nobody has written on shows its prototype's text. */
TEST_F(NoteWritingTest, AnUnwrittenNoteSharesItsPrototypesText)
{
    ASSERT_NE(m_prototypes[NOTE_RNUM].shared_text(), nullptr);
    EXPECT_EQ(m_note->shared_text(), m_prototypes[NOTE_RNUM].shared_text());
    EXPECT_EQ(m_note->name, m_prototypes[NOTE_RNUM].name);
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

    EXPECT_EQ(m_descriptor.written_note, m_note);
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

/* A note freed while it is being written on ends the writing, so the next line typed does not go
 * into the freed note. */
TEST_F(NoteWritingTest, ANoteFreedWhileBeingWrittenOnEndsTheWriting)
{
    descriptor_data* const saved_descriptor_list = descriptor_list;
    descriptor_list = &m_descriptor;
    start_writing();
    type("Meet me at the");
    SET_BIT(PLR_FLAGS(&m_writer), PLR_WRITING);

    extract_obj(m_note);
    m_note = nullptr;

    char** const still_writing_into = m_descriptor.str;
    descriptor_list = saved_descriptor_list;
    EXPECT_EQ(nullptr, still_writing_into);
    EXPECT_EQ(m_descriptor.str_limit, 0);
    EXPECT_FALSE(PLR_FLAGGED(&m_writer, PLR_WRITING)) << "others can reach it again";
    EXPECT_NE(std::string(m_descriptor.output).find("What you were writing is gone."),
        std::string::npos)
        << m_descriptor.output;
    EXPECT_EQ(m_descriptor.written_note, nullptr);
    EXPECT_EQ(m_descriptor.written_note_text, nullptr);
}

/* Each line typed shows on the note at once, in a copy of its own: the editor keeps its buffer. */
TEST_F(NoteWritingTest, EachLineTypedShowsOnTheNote)
{
    start_writing();

    type("Meet me at the");
    EXPECT_STREQ(m_note->action_description, "Meet me at the\n\r");
    type("ford.");
    EXPECT_STREQ(m_note->action_description, "Meet me at the\n\rford.\n\r");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_NE(static_cast<const void*>(m_note->action_description),
        static_cast<const void*>(*m_descriptor.str));
}

/* A writer whose connection goes keeps on the note what it wrote so far and lets go of the
 * editor's buffer. */
TEST_F(NoteWritingTest, FinishingAWriterKeepsWhatItWrote)
{
    start_writing();
    type("Meet me at the");

    finish_writing_note(m_descriptor);

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_EQ(m_descriptor.written_note, nullptr);
    EXPECT_EQ(m_descriptor.written_note_text, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the\n\r");
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

/* %f formats the note and shows it when the result fits the note's limit. */
TEST_F(NoteWritingTest, FormattingWithinTheLimitFormatsTheText)
{
    start_writing();

    type("Meet me at the ford.");
    type("%f");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_STREQ(*m_descriptor.str, "   Meet me at the ford. \n\r"); /* the line end becomes a space */
    EXPECT_EQ(m_descriptor.len_str, std::strlen(*m_descriptor.str));
    EXPECT_EQ(m_descriptor.cur_str, m_descriptor.len_str);
    EXPECT_NE(std::string(m_descriptor.output).find("Your text is formatted."), std::string::npos);
}

/* A word and the space after it, repeated to make a line %f breaks many times. */
const std::string FORMATTED_WORD = "ford ";

/* %f is refused when the indent and line breaks it adds would take the note past its limit, and the
 * note keeps its text and cursor. */
TEST_F(NoteWritingTest, FormattingPastTheLimitIsRefusedAndTheTextKept)
{
    start_writing();
    std::string first_line;
    while (first_line.size() + FORMATTED_WORD.size() <= NEAR_LIMIT_LINE_LENGTH) {
        first_line += FORMATTED_WORD;
    }
    type(first_line.c_str());
    ASSERT_NE(m_descriptor.str, nullptr);
    const unsigned int cursor_before = m_descriptor.cur_str;

    type("%f");

    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\r");
    EXPECT_EQ(m_descriptor.len_str, (first_line + "\n\r").size());
    EXPECT_EQ(m_descriptor.cur_str, cursor_before);
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
    EXPECT_EQ(std::string(m_descriptor.output).find("Your text is formatted."), std::string::npos);
}

/* %s with no '~' after the pattern, or with nothing after it, says how to use it and replaces
 * nothing. */
TEST_F(NoteWritingTest, AReplacementWithoutATildeIsRefused)
{
    start_writing();
    type("Meet me at the ford.");

    type("%sford");
    type("%s");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_STREQ(*m_descriptor.str, "Meet me at the ford.\n\r");
    const std::string output = m_descriptor.output;
    const std::string usage = "Use %s<old>~<new>.\n\r";
    const size_t first_usage = output.find(usage);
    ASSERT_NE(first_usage, std::string::npos) << output;
    EXPECT_NE(output.find(usage, first_usage + usage.size()), std::string::npos) << output;
    EXPECT_EQ(output.find("Replaced"), std::string::npos) << output;
}

/* %s with nothing before the '~' still says there is no pattern. */
TEST_F(NoteWritingTest, AReplacementWithAnEmptyPatternSaysSo)
{
    start_writing();
    type("Meet me at the ford.");

    type("%s~river");

    EXPECT_STREQ(*m_descriptor.str, "Meet me at the ford.\n\r");
    EXPECT_NE(std::string(m_descriptor.output).find("No pattern to replace."), std::string::npos);
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

/* A writer collecting its mail from the postmaster, with a mail file of its own. */
class MailReceivingTest : public MailWritingTest {
protected:
    void SetUp() override
    {
        MailWritingTest::SetUp();
        m_saved_object_list = object_list;
        m_saved_no_mail = no_mail;
        object_list = nullptr;
    }

    void TearDown() override
    {
        while (m_writer.carrying != nullptr) {
            extract_obj(m_writer.carrying);
        }
        object_list = m_saved_object_list;
        no_mail = m_saved_no_mail;
        MailWritingTest::TearDown();
    }

    /* Asks the postmaster for the writer's mail. */
    void receive() { postmaster_receive_mail(&m_writer, 0, "", &m_postmaster); }

private:
    scoped_mail_file m_mail_file; // the mail file the letters go through
    obj_data* m_saved_object_list = nullptr; // restored in TearDown
    int m_saved_no_mail = 0; // restored in TearDown, as a mail error sets it
};

/* A letter handed over shows the letter's own words, in a text of its own that goes with it. */
TEST_F(MailReceivingTest, ALetterShowsWhatWasWritten)
{
    char sender[] = "Frodo";
    char words[] = "Meet me at the ford.\n\r";
    store_mail(m_writer_name, sender, words);

    receive();

    const obj_data* const letter = m_writer.carrying;
    ASSERT_NE(letter, nullptr);
    EXPECT_STREQ(letter->name, "mail paper letter");
    EXPECT_STREQ(letter->short_description, "a letter");
    EXPECT_STREQ(letter->description, "Someone has dropped a letter here.");
    ASSERT_NE(letter->action_description, nullptr);
    const std::string read_letter = letter->action_description;
    EXPECT_NE(read_letter.find("From: Frodo"), std::string::npos) << read_letter;
    EXPECT_NE(read_letter.find(words), std::string::npos) << read_letter;
    EXPECT_EQ(letter->ex_description, nullptr);
    ASSERT_NE(letter->shared_text(), nullptr);
    EXPECT_EQ(letter->shared_text().use_count(), 1) << "no other object shows the letter's text";
    EXPECT_FALSE(has_mail("sam"));
}

/* A letter the mail file has lost is handed over saying so, rather than blank. */
TEST_F(MailReceivingTest, ALetterLostToAMailErrorSaysSo)
{
    /* A block that is no letter's first, where the index says one is. */
    {
        std::ofstream mail_file(MAIL_FILE, std::ios::binary);
        mail_file << std::string(BLOCK_SIZE, '\0');
    }
    index_mail(m_writer_name, 0);

    testing::internal::CaptureStderr();
    receive();
    testing::internal::GetCapturedStderr();

    const obj_data* const letter = m_writer.carrying;
    ASSERT_NE(letter, nullptr);
    EXPECT_STREQ(letter->short_description, "a letter");
    EXPECT_STREQ(letter->action_description, "Mail system error - please report.  Error #11.\n\r");
    EXPECT_EQ(letter->shared_text().use_count(), 1);
}

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

/* Two writers on the descriptor list, each with the text editor open on a field of its own: the
 * first on a field about to be freed, the second on one that is not. */
class StopEditingTextTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_descriptor_list = descriptor_list;
        m_freed_field = str_dup("The old text.\n\r");
        m_kept_field = str_dup("Other text.\n\r");
        SET_BIT(MOB_FLAGS(m_mob_writer), MOB_ISNPC);
        connect(m_kept_descriptor, m_kept_writer);
        string_add_init(&m_kept_descriptor, &m_kept_field);
        m_freed_descriptor.next = &m_kept_descriptor;
        descriptor_list = &m_freed_descriptor;
    }

    void TearDown() override
    {
        descriptor_list = m_saved_descriptor_list;
        for (descriptor_data* descriptor : { &m_freed_descriptor, &m_kept_descriptor }) {
            if (descriptor->character != nullptr) {
                descriptor->character->desc = nullptr;
            }
            descriptor->str = nullptr;
            release_test_descriptor(descriptor);
        }
        RELEASE(m_freed_field);
        RELEASE(m_kept_field);
        RELEASE(m_unopened_field);
    }

    /* Connects <writer> through <descriptor>, playing and writing to its small output buffer. */
    static void connect(descriptor_data& descriptor, char_data* writer)
    {
        descriptor.output = descriptor.small_outbuf;
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        descriptor.connected = CON_PLYNG;
        descriptor.character = writer;
        writer->desc = &descriptor;
    }

    /* Has <writer> open the text editor on the field about to be freed. */
    void open_freed_field(char_data* writer)
    {
        connect(m_freed_descriptor, writer);
        string_add_init(&m_freed_descriptor, &m_freed_field);
        m_freed_descriptor.str_limit = MAX_NOTE_LENGTH;
        m_freed_descriptor.small_outbuf[0] = '\0';
        m_freed_descriptor.bufptr = 0;
        m_freed_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    /* Ends the edits of the field about to be freed and of one nobody has open. */
    void stop_editing() { stop_editing_text({ &m_freed_field, &m_unopened_field }); }

    /* Types <line> on the first writer's connection. */
    void type_after_stop(const char* line)
    {
        std::string mutable_line = line;
        mutable_line.push_back('\0');
        string_add(&m_freed_descriptor, &mutable_line[0]);
    }

    descriptor_data m_freed_descriptor {}; // the first writer's connection
    descriptor_data m_kept_descriptor {}; // the second writer's connection
    owned_character m_player_writer; // a player who may open the field about to be freed
    owned_character m_mob_writer { MOB_ISNPC }; // a mob who may open it, as a switched immortal
    owned_character m_kept_writer; // the player editing the field that is kept
    char* m_freed_field = nullptr; // the text about to be freed; the test frees it as the caller would
    char* m_kept_field = nullptr; // the text the second writer goes on editing
    char* m_unopened_field = nullptr; // a field about to be freed that nobody has open

private:
    descriptor_data* m_saved_descriptor_list = nullptr; // restored in TearDown
};

/* A player writing into a freed field is stopped and told; the player editing another field goes on. */
TEST_F(StopEditingTextTest, StopsOnlyAPlayerWritingIntoAFreedField)
{
    open_freed_field(m_player_writer);
    ASSERT_TRUE(IS_SET(PLR_FLAGS(m_player_writer), PLR_WRITING));

    stop_editing();

    EXPECT_EQ(m_freed_descriptor.str, nullptr);
    EXPECT_EQ(m_freed_descriptor.str_limit, 0u);
    EXPECT_FALSE(IS_SET(PLR_FLAGS(m_player_writer), PLR_WRITING));
    EXPECT_NE(std::string(m_freed_descriptor.output).find("What you were writing is gone.\n\r"),
        std::string::npos)
        << m_freed_descriptor.output;
    EXPECT_EQ(m_kept_descriptor.str, &m_kept_field);
    EXPECT_TRUE(IS_SET(PLR_FLAGS(m_kept_writer), PLR_WRITING));
    EXPECT_EQ(std::string(m_kept_descriptor.output).find("What you were writing is gone."),
        std::string::npos);

    type_after_stop("A line typed after the stop.");

    EXPECT_EQ(m_freed_descriptor.str, nullptr);
}

/* A mob a switched immortal writes as is stopped too. The writing bit is the mob's own flag and stays
 * set, so the input loop still sends its lines to the editor, which ignores them. */
TEST_F(StopEditingTextTest, AMobWritingIntoAFreedFieldIgnoresLaterLines)
{
    open_freed_field(m_mob_writer);
    const long flags_before = MOB_FLAGS(m_mob_writer);

    stop_editing();

    EXPECT_EQ(m_freed_descriptor.str, nullptr);
    EXPECT_EQ(MOB_FLAGS(m_mob_writer), flags_before);
    EXPECT_NE(std::string(m_freed_descriptor.output).find("What you were writing is gone.\n\r"),
        std::string::npos)
        << m_freed_descriptor.output;

    type_after_stop("A line typed after the stop.");

    EXPECT_EQ(m_freed_descriptor.str, nullptr);
    EXPECT_EQ(m_kept_descriptor.str, &m_kept_field);
}

/* Half of format_string's 79-column line, so no line holds two of these words with the space between
 * them, and formatting adds a line break for nearly every one. */
constexpr std::size_t LINE_LONG_WORD_LENGTH = 39;
/* A word of LINE_LONG_WORD_LENGTH and the space after it. */
const std::string LINE_LONG_WORD = std::string(LINE_LONG_WORD_LENGTH, 'x') + " ";
/* The words in stored_long_text(), enough for formatting to grow it past MAX_STRING_LENGTH. */
constexpr int STORED_LONG_TEXT_WORDS = 200;

/* A stored text longer than the editor lets a text grow, as an old world or player file can hold. */
std::string stored_long_text()
{
    std::string text;
    for (int word = 0; word < STORED_LONG_TEXT_WORDS; ++word) {
        text += LINE_LONG_WORD;
    }
    return text + "\n";
}

/* %f on a stored text longer than the editor allows is refused, and the text is kept: formatting it
 * would overrun format_string's buffer. */
TEST(StoredTextFormattingTest, FormattingATextPastTheEditorsCapIsRefused)
{
    descriptor_data descriptor {};
    TestDescriptorRelease descriptor_release(&descriptor);
    descriptor.output = descriptor.small_outbuf;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    owned_character builder;
    builder->desc = &descriptor;
    descriptor.character = builder;
    const std::string stored_text = stored_long_text();
    char* text = str_dup(stored_text.c_str());
    string_add_init(&descriptor, &text);
    ASSERT_GT(descriptor.len_str, static_cast<unsigned int>(MAX_EDITED_TEXT_LENGTH));

    std::string format_line = "%f";
    format_line.push_back('\0');
    string_add(&descriptor, &format_line[0]);

    EXPECT_EQ(std::string(text), stored_text);
    const std::string output(descriptor.output);
    EXPECT_NE(output.find("This text is too long to format.\n\r"), std::string::npos);
    EXPECT_EQ(output.find("Your text is formatted."), std::string::npos);

    builder->desc = nullptr;
    descriptor.str = nullptr;
    RELEASE(text);
}

} // namespace
