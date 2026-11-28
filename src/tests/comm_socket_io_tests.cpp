#include "../comm.h"
#include "../protocol.h"
#include "../rots_net.h"
#include "../structs.h"

#include "scripted_socket.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

int process_input(struct descriptor_data* descriptor);
int get_from_q(struct txt_q* queue, char* dest);
void flush_queues(struct descriptor_data* descriptor);

namespace {

using socket_test_support::ScriptedSocket;

// A connection at the account menu whose socket is `socket`, with its queues emptied and any
// protocol state freed when the test ends.
class ScriptedConnection {
public:
    explicit ScriptedConnection(const ScriptedSocket& socket)
    {
        descriptor.output = descriptor.small_outbuf;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        descriptor.pos = -1;
        descriptor.connected = CON_ACCTMENU;
        descriptor.descriptor = socket.handle();
        std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "acct");
    }

    ~ScriptedConnection()
    {
        flush_queues(&descriptor);
        if (descriptor.pProtocol != nullptr) {
            ProtocolDestroy(descriptor.pProtocol);
        }
    }

    // Gives the connection telnet protocol state, so process_input filters negotiation out of
    // what it reads, as it does for every real connection.
    void use_protocol() { descriptor.pProtocol = ProtocolCreate(); }

    ScriptedConnection(const ScriptedConnection&) = delete;
    ScriptedConnection& operator=(const ScriptedConnection&) = delete;

    // The next complete line process_input() queued, or an empty string when none is waiting.
    std::string next_queued_line()
    {
        char line[MAX_INPUT_LENGTH] = {};
        if (!get_from_q(&descriptor.input, line)) {
            return std::string();
        }
        return line;
    }

    // The connection the test drives; its socket handle is the scripted socket's.
    descriptor_data descriptor {};
};

TEST(WriteToDescriptor, WritesTheWholeTextAndReturnsZero)
{
    ScriptedSocket socket;

    EXPECT_EQ(write_to_descriptor(socket.handle(), "Welcome back.\n\r"), 0);

    EXPECT_EQ(socket.written(), "Welcome back.\n\r");
}

TEST(WriteToDescriptor, KeepsWritingUntilAShortWriteHasSentEverything)
{
    ScriptedSocket socket;
    socket.limit_write_size(4);

    EXPECT_EQ(write_to_descriptor(socket.handle(), "You are hungry.\n\r"), 0);

    EXPECT_EQ(socket.written(), "You are hungry.\n\r");
    EXPECT_EQ(socket.write_calls(), 5);
}

TEST(WriteToDescriptor, ReturnsMinusTwoWhenTheFirstWriteWouldBlock)
{
    ScriptedSocket socket;
    socket.queue_write_would_block();

    EXPECT_EQ(write_to_descriptor(socket.handle(), "Hello"), -2);

    EXPECT_EQ(socket.written(), "");
}

TEST(WriteToDescriptor, ReturnsMinusOneWhenAWriteWouldBlockAfterPartOfTheTextIsSent)
{
    ScriptedSocket socket;
    socket.limit_write_size(3);
    socket.queue_write_success();
    socket.queue_write_would_block();

    // The bytes already sent cannot be taken back, so this is not the retryable -2.
    EXPECT_EQ(write_to_descriptor(socket.handle(), "abcdef"), -1);

    EXPECT_EQ(socket.written(), "abc");
}

TEST(WriteToDescriptor, ReturnsMinusOneWhenTheSocketFails)
{
    ScriptedSocket socket;
    socket.queue_write_failure();

    EXPECT_EQ(write_to_descriptor(socket.handle(), "Hello"), -1);

    EXPECT_EQ(socket.written(), "");
}

TEST(WriteToDescriptor, WritesNothingToAClosedOrInvalidDescriptor)
{
    ScriptedSocket socket;

    EXPECT_EQ(write_to_descriptor(static_cast<SocketType>(0), "Hello"), 0);
    EXPECT_EQ(write_to_descriptor(rots_net::INVALID_SOCKET_HANDLE, "Hello"), 0);

    EXPECT_EQ(socket.write_calls(), 0);
}

TEST(ProcessInput, QueuesACompleteLine)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    socket.queue_input("look\n");

    EXPECT_EQ(process_input(&connection.descriptor), 1);

    EXPECT_EQ(connection.next_queued_line(), "look");
}

TEST(ProcessInput, HoldsAPartLineUntilTheRestArrives)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    socket.queue_input("lo");

    EXPECT_EQ(process_input(&connection.descriptor), 0);
    EXPECT_EQ(connection.next_queued_line(), "");

    socket.queue_input("ok\n");
    EXPECT_EQ(process_input(&connection.descriptor), 1);
    EXPECT_EQ(connection.next_queued_line(), "look");
}

TEST(ProcessInput, KeepsReadingWhileALineArrivesInPieces)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    socket.queue_input("no");
    socket.queue_input("rth\n");

    EXPECT_EQ(process_input(&connection.descriptor), 1);

    EXPECT_EQ(connection.next_queued_line(), "north");
    EXPECT_EQ(socket.read_calls(), 2);
}

// IAC NOP: a telnet command the protocol layer drops without answering.
const std::string TELNET_NO_OPERATION("\xff\xf1", 2);

TEST(ProcessInput, KeepsReadingAfterAFirstReadThatHeldOnlyTelnetNegotiation)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.use_protocol();
    socket.queue_input(TELNET_NO_OPERATION);
    socket.queue_input("look\n");

    EXPECT_EQ(process_input(&connection.descriptor), 1);

    EXPECT_EQ(connection.next_queued_line(), "look");
    EXPECT_EQ(socket.read_calls(), 2);
}

TEST(ProcessInput, QueuesNothingWhenTheOnlyBytesAreTelnetNegotiation)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.use_protocol();
    socket.queue_input(TELNET_NO_OPERATION);

    EXPECT_EQ(process_input(&connection.descriptor), 0);

    EXPECT_EQ(connection.next_queued_line(), "");
    EXPECT_STREQ(connection.descriptor.buf, "");
    EXPECT_EQ(socket.read_calls(), 2);
    EXPECT_EQ(socket.written(), "");
}

TEST(ProcessInput, ReturnsZeroWhenNothingHasArrived)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);

    EXPECT_EQ(process_input(&connection.descriptor), 0);

    EXPECT_EQ(socket.read_calls(), 1);
}

TEST(ProcessInput, ReturnsMinusOneWhenThePeerHasClosed)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    socket.queue_end_of_file();

    EXPECT_EQ(process_input(&connection.descriptor), -1);
}

TEST(ProcessInput, ReturnsMinusOneWhenTheSocketFails)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    socket.queue_read_failure();

    EXPECT_EQ(process_input(&connection.descriptor), -1);
}

TEST(ProcessInput, ReadsNothingFromAClosedDescriptor)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.descriptor.descriptor = 0;

    EXPECT_EQ(process_input(&connection.descriptor), 0);

    EXPECT_EQ(socket.read_calls(), 0);
}

TEST(ProcessInput, WaitsForTheRestOfASplitProxyHeader)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.descriptor.waiting_for_proxy_header = true;
    socket.queue_input(std::string("\x7f\x00", 2));

    EXPECT_EQ(process_input(&connection.descriptor), 0);

    EXPECT_TRUE(connection.descriptor.waiting_for_proxy_header);
    EXPECT_EQ(connection.descriptor.proxy_peer_bytes_read, 2);
    EXPECT_EQ(socket.written(), "");
}

TEST(ProcessInput, ReturnsMinusOneWhenThePeerClosesDuringTheProxyHeader)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.descriptor.waiting_for_proxy_header = true;
    socket.queue_input(std::string("\x7f", 1));
    socket.queue_end_of_file();

    EXPECT_EQ(process_input(&connection.descriptor), -1);

    EXPECT_EQ(socket.written(), "");
}

TEST(ProcessInput, ReturnsMinusOneWhenTheSocketFailsDuringTheProxyHeader)
{
    ScriptedSocket socket;
    ScriptedConnection connection(socket);
    connection.descriptor.waiting_for_proxy_header = true;
    socket.queue_read_failure();

    EXPECT_EQ(process_input(&connection.descriptor), -1);

    EXPECT_EQ(socket.written(), "");
}

} // namespace
