// Writing on a note while the writer's connection closes.

#include "../comm.h"
#include "../db.h"
#include "../note_writing.h"
#include "../object_text.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <utility>

void string_add(struct descriptor_data* d, char* str);
extern struct descriptor_data* descriptor_list;
extern SocketType maxdesc;

namespace {

/* A connection writing on a note when close_socket drops it. The descriptor is allocated as the
 * game allocates one, with socket 0 so close_socket closes no real socket. */
class ClosingWriterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_descriptor_list = descriptor_list;
        m_saved_maxdesc = maxdesc;

        clear_object(&m_note);
        object_text::fields text;
        text.name = "note paper";
        m_note.set_text(create_object_text(std::move(text)));

        CREATE1(m_descriptor, descriptor_data);
        m_descriptor->output = m_descriptor->small_outbuf;
        m_descriptor->bufspace = SMALL_BUFSIZE - 1;
        m_descriptor->connected = CON_PLYNG;
        clear_char(&m_writer, MOB_VOID);
        m_writer.desc = m_descriptor;
        m_descriptor->character = &m_writer;
        descriptor_list = m_descriptor;
    }

    void TearDown() override
    {
        global_release_flag = 1;
        if (m_descriptor != nullptr) {
            finish_writing_note(*m_descriptor);
            release_test_descriptor(m_descriptor);
            free(m_descriptor);
        }
        release_cleared_character(&m_writer);
        descriptor_list = m_saved_descriptor_list;
        maxdesc = m_saved_maxdesc;
    }

    /* Types `line` into the writer's text editor. */
    void type(const char* line)
    {
        std::string mutable_line = line;
        mutable_line.push_back('\0');
        string_add(m_descriptor, &mutable_line[0]);
    }

    obj_data m_note {}; // the note being written on
    char_data m_writer {}; // the one writing, until its connection loses it
    descriptor_data* m_descriptor = nullptr; // the writer's connection, freed by the test

private:
    descriptor_data* m_saved_descriptor_list = nullptr; // restored in TearDown
    SocketType m_saved_maxdesc = 0; // restored in TearDown, as close_socket lowers it
};

/* A connection dropped while writing on a note leaves on the note what was typed and lets go of
 * the editor's buffer. "setfree off" keeps the descriptor after close_socket so the test can see
 * that its editor was finished; the test then frees both itself. */
TEST_F(ClosingWriterTest, DroppingAWritersConnectionKeepsTheNoteAndReleasesTheEditor)
{
    start_writing_note(*m_descriptor, m_note);
    type("Meet me at the");
    char* const editor_buffer = m_descriptor->written_note_text;
    ASSERT_NE(editor_buffer, nullptr);
    /* A connection closing after its character has gone: close_socket saves no player for it. */
    m_descriptor->character = nullptr;
    m_descriptor->connected = CON_CLOSE;
    m_writer.desc = nullptr;

    global_release_flag = 0;
    close_socket(m_descriptor, TRUE);
    global_release_flag = 1;

    EXPECT_EQ(descriptor_list, nullptr);
    EXPECT_STREQ(m_note.action_description, "Meet me at the\n\r");
    EXPECT_EQ(m_descriptor->written_note, nullptr);
    EXPECT_EQ(m_descriptor->written_note_text, nullptr);
    EXPECT_EQ(m_descriptor->str, nullptr);
    free(editor_buffer);
    free(m_descriptor);
    m_descriptor = nullptr;
}

} // namespace
