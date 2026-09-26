// process_output flushes a descriptor's queued text through one static buffer. With `set wrap` on,
// append_lines adds a line break for every 80 characters without one -- into that same buffer, which
// is only 20 bytes larger than a full output queue.

#include "../comm.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern char process_output_buffer[LARGE_BUFSIZE + 20];
int process_output(struct descriptor_data* t);
void clear_char(struct char_data* ch, int mode);

namespace {

// A wrapping player with no socket: write_to_descriptor treats descriptor 0 as sent, so
// process_output runs its whole flush.
class WrappingPlayer {
public:
    WrappingPlayer()
    {
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 0;

        clear_char(&m_character, MOB_VOID);
        SET_BIT(PRF_FLAGS(&m_character), PRF_WRAP);
        m_character.desc = &m_descriptor;
        m_descriptor.character = &m_character;
    }

    descriptor_data& descriptor() { return m_descriptor; }

    // The text process_output built, bounded to the buffer it owns.
    static std::string flushed(size_t* length)
    {
        *length = strnlen(process_output_buffer + 2, sizeof(process_output_buffer) - 2);
        return std::string(process_output_buffer + 2, *length);
    }

private:
    descriptor_data m_descriptor {};
    char_data m_character {};
};

TEST(ProcessOutput, AWrappedOverflowingQueueStaysInsideTheOutputBuffer)
{
    WrappingPlayer player;
    const std::string long_line = std::string(150, 'a') + "\n\r";
    while (player.descriptor().bufptr >= 0)
        write_to_output(long_line.c_str(), &player.descriptor());

    ASSERT_EQ(process_output(&player.descriptor()), 1);

    size_t length = 0;
    const std::string flushed = WrappingPlayer::flushed(&length);
    EXPECT_LT(length, sizeof(process_output_buffer) - 2)
        << "the wrapped text ran past the end of process_output_buffer";
    EXPECT_NE(flushed.find("**OVERFLOW**\n\r"), std::string::npos)
        << "a player whose output was cut must still see the overflow marker";
}

TEST(ProcessOutput, WrapsALongLineAtEightyCharacters)
{
    WrappingPlayer player;
    write_to_output((std::string(100, 'a') + "\n\r").c_str(), &player.descriptor());

    ASSERT_EQ(process_output(&player.descriptor()), 1);

    size_t length = 0;
    EXPECT_EQ(WrappingPlayer::flushed(&length), std::string(80, 'a') + "\n\r" + std::string(20, 'a') + "\n\r\n\r");
}

} // namespace
