#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../note_writing.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

ACMD(do_alias);
ACMD(do_write);
void clear_char(struct char_data* ch, int mode);
void string_add(struct descriptor_data* d, char* str);
void string_add_finish(struct descriptor_data* d);
int replace_pattern(descriptor_data* d, const char* pattern, const char* new_pattern);
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct descriptor_data* descriptor_list;
extern struct room_data world;
extern int top_of_world;

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

/* A note's copy owns the text written on it, so extracting the note frees that text and leaves its
 * prototype blank. */
TEST_F(NoteWritingTest, AWrittenNoteOwnsItsText)
{
    start_writing();
    ASSERT_EQ(m_descriptor.written_note, m_note) << m_descriptor.output;

    type("Meet me at the ford.");
    /* Finished directly: %e regrows the text with a copy that reads past the old buffer. */
    string_add_finish(&m_descriptor);

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
    EXPECT_TRUE(m_note->owns_text(object_text::action_description));
    EXPECT_FALSE(m_note->owns_text(object_text::name));

    extract_obj(m_note);
    EXPECT_EQ(m_prototypes[NOTE_RNUM].action_description, nullptr);
    EXPECT_STREQ(m_prototypes[NOTE_RNUM].name, "note paper");
}

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

/* A note's copy that nobody has written on owns none of its text. */
TEST_F(NoteWritingTest, AnUnwrittenNoteOwnsNoText)
{
    EXPECT_FALSE(m_note->owns_text(object_text::name));
    EXPECT_FALSE(m_note->owns_text(object_text::short_description));
    EXPECT_FALSE(m_note->owns_text(object_text::description));
    EXPECT_FALSE(m_note->owns_text(object_text::action_description));
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
    const std::string first_line = "ford " + std::string(NEAR_LIMIT_LINE_LENGTH - 5, 'x');

    type(first_line.c_str());
    type("%sford~shallow river crossing");

    ASSERT_NE(m_descriptor.str, nullptr);
    EXPECT_EQ(std::string(*m_descriptor.str), first_line + "\n\r");
    EXPECT_NE(std::string(m_descriptor.output).find(past_limit_message()), std::string::npos);
    EXPECT_EQ(std::string(m_descriptor.output).find("Replaced"), std::string::npos);

    type("%sford~ferry");

    EXPECT_EQ(std::string(*m_descriptor.str), "ferry " + std::string(NEAR_LIMIT_LINE_LENGTH - 5, 'x') + "\n\r");
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

/* The text editor opened on other text has no limit, whatever limit the descriptor held before. */
TEST_F(NoteWritingTest, TheTextEditorHasNoLimit)
{
    char* text = nullptr;
    m_descriptor.str_limit = MAX_NOTE_LENGTH;

    string_add_init(&m_descriptor, &text);

    EXPECT_EQ(m_descriptor.str_limit, 0u);
    const std::string long_line(MAX_NOTE_LENGTH + 10, 'x');
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
    m_descriptor.len_str = 6;
    m_descriptor.max_str = 6;
    m_descriptor.cur_str = 3; /* abc|def */

    EXPECT_EQ(replace_pattern(&m_descriptor, "bcde", "X"), 1);

    EXPECT_STREQ(text, "aXf");
    EXPECT_EQ(m_descriptor.len_str, 3u);
    EXPECT_EQ(m_descriptor.cur_str, 2u); /* aX|f */
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* A replacement that shrinks the text around the cursor never leaves the cursor past the text's end. */
TEST_F(NoteWritingTest, AShrinkingReplacementKeepsTheCursorWithinTheText)
{
    char* text = str_dup("abcdef");
    m_descriptor.str = &text;
    m_descriptor.len_str = 6;
    m_descriptor.max_str = 6;
    m_descriptor.cur_str = 5;

    EXPECT_EQ(replace_pattern(&m_descriptor, "abcdef", ""), 1);

    EXPECT_STREQ(text, "");
    EXPECT_EQ(m_descriptor.len_str, 0u);
    EXPECT_EQ(m_descriptor.cur_str, 0u);
    m_descriptor.str = nullptr;
    RELEASE(text);
}

/* %s may grow a text to exactly MAX_EDITED_TEXT_LENGTH characters. */
TEST_F(NoteWritingTest, AReplacementMayGrowATextToTheEditorsCap)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    /* 26 lines of 98 and a line of 78 'a's and a 'b': tripling every 'a' gives exactly the cap. */
    const int LONG_LINE_COUNT = 26;
    const std::string line(98, 'a');
    const std::string last_line = std::string(78, 'a') + "b";
    std::string expected_after = "\n\r";
    for (int line_number = 0; line_number < LONG_LINE_COUNT; line_number++) {
        type(line.c_str());
        expected_after += std::string(3 * line.size(), 'a') + "\n\r";
    }
    type(last_line.c_str());
    expected_after += std::string(3 * 78, 'a') + "b\n\r";
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

/* %s growing a text past MAX_EDITED_TEXT_LENGTH is refused in an editor with no limit of its own, and
 * the text and cursor stay as they were. */
TEST_F(NoteWritingTest, AReplacementPastTheEditorsCapIsRefused)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    const int LINE_COUNT = 30; /* 100-character lines: about 3,000 characters, tripled past the cap */
    const std::string line(98, 'a');
    std::string expected = "\n\r";
    for (int line_number = 0; line_number < LINE_COUNT; line_number++) {
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

/* A line that would take a text past MAX_EDITED_TEXT_LENGTH is refused in an editor with no limit of
 * its own. */
TEST_F(NoteWritingTest, ALinePastTheEditorsCapIsRefused)
{
    char* text = nullptr;
    string_add_init(&m_descriptor, &text);
    const int LINE_COUNT = 7; /* 1,002-character lines: about 7,000 characters, one more passes the cap */
    const std::string line(1000, 'x');
    std::string expected = "\n\r";
    for (int line_number = 0; line_number < LINE_COUNT; line_number++) {
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

    extract_obj(m_note);
    m_note = nullptr;

    char** const still_writing_into = m_descriptor.str;
    descriptor_list = saved_descriptor_list;
    EXPECT_EQ(nullptr, still_writing_into);
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

} // namespace
