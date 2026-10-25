#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

ACMD(do_alias);
ACMD(do_write);
void clear_char(struct char_data* ch, int mode);
void string_add(struct descriptor_data* d, char* str);
void string_add_finish(struct descriptor_data* d);
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
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
        m_descriptor.str = nullptr;
        while (object_list != nullptr) {
            extract_obj(object_list);
        }
        RELEASE(m_writer.profs);
        RELEASE(m_writer.skills);
        RELEASE(m_writer.knowledge);
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
    ASSERT_EQ(m_descriptor.str, &m_note->action_description) << m_descriptor.output;

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
    ASSERT_EQ(m_descriptor.str, &m_note->action_description) << m_descriptor.output;

    type("Meet me at the");
    type("%q");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_EQ(m_note->action_description, nullptr);
}

/* %e keeps every line typed, though finishing makes the editor regrow the note's first small buffer. */
TEST_F(NoteWritingTest, FinishingWithPercentEKeepsTheNote)
{
    start_writing();
    ASSERT_EQ(m_descriptor.str, &m_note->action_description) << m_descriptor.output;

    type("Meet me at the ford.");
    type("%e");

    EXPECT_EQ(m_descriptor.str, nullptr);
    EXPECT_STREQ(m_note->action_description, "Meet me at the ford.\n\r");
}

/* A line that carries the note past one editor block keeps the lines before it and itself whole. */
TEST_F(NoteWritingTest, ALineCrossingABlockKeepsTheWholeNote)
{
    start_writing();
    ASSERT_EQ(m_descriptor.str, &m_note->action_description) << m_descriptor.output;
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

} // namespace
