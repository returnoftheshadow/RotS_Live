#include "../rots_net.h"

#include "socket_test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>

#if !defined(PREDEF_PLATFORM_WINDOWS)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace {

using socket_test_support::LoopbackConnection;

constexpr std::chrono::milliseconds DELIVERY_WAIT { 1000 };

// Reads one int-valued socket option; -1 when it cannot be read.
int read_int_option(SocketType socket_handle, int level, int option_name)
{
    int value = 0;
    socklen_t value_length = sizeof(value);
    if (getsockopt(socket_handle, level, option_name, reinterpret_cast<char*>(&value), &value_length) != 0) {
        return -1;
    }
    return value;
}

// Reads from `socket_handle` until `expected_length` bytes have arrived, the peer closes, or no
// more bytes come within DELIVERY_WAIT.
std::string read_bytes(SocketType socket_handle, size_t expected_length)
{
    std::string received;
    char buffer[64];
    while (received.size() < expected_length
        && socket_test_support::wait_for_readable(socket_handle, DELIVERY_WAIT)) {
        const rots_net::ssize_type bytes_read = rots_net::read_socket(socket_handle, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            break;
        }
        received.append(buffer, static_cast<size_t>(bytes_read));
    }
    return received;
}

TEST(RotsNet, TheInvalidHandleIsNotAValidSocket)
{
    EXPECT_FALSE(rots_net::is_valid_socket(rots_net::INVALID_SOCKET_HANDLE));
}

TEST(RotsNet, HandleZeroIsAValidSocket)
{
    EXPECT_TRUE(rots_net::is_valid_socket(static_cast<SocketType>(0)));
}

TEST(RotsNet, StartupSucceedsAgainWhileTheSocketLibraryIsInUse)
{
    // The test main has already started the library once.
    ASSERT_TRUE(rots_net::startup());
    rots_net::shutdown();

    LoopbackConnection connection;
    EXPECT_TRUE(connection.is_open());
}

TEST(RotsNet, BytesWrittenAtOneEndAreReadAtTheOther)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());

    const std::string message = "rots_net round trip";
    ASSERT_EQ(rots_net::write_socket(connection.client(), message.data(), message.size()),
        static_cast<rots_net::ssize_type>(message.size()));

    EXPECT_EQ(read_bytes(connection.accepted(), message.size()), message);
}

TEST(RotsNet, BytesTravelInBothDirections)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());

    const std::string reply = "reply";
    ASSERT_EQ(rots_net::write_socket(connection.accepted(), reply.data(), reply.size()),
        static_cast<rots_net::ssize_type>(reply.size()));

    EXPECT_EQ(read_bytes(connection.client(), reply.size()), reply);
}

TEST(RotsNet, ANonblockingReadWithNothingToReadWouldBlock)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());
    rots_net::set_nonblocking(connection.accepted());

    char buffer[16];
    const rots_net::ssize_type bytes_read = rots_net::read_socket(connection.accepted(), buffer, sizeof(buffer));
    const int error_code = rots_net::last_error();

    EXPECT_EQ(bytes_read, -1);
    EXPECT_TRUE(rots_net::error_is_would_block(error_code)) << error_code;
    EXPECT_FALSE(rots_net::error_is_interrupted(error_code)) << error_code;
}

TEST(RotsNet, ANonblockingSocketStillDeliversBytesThatHaveArrived)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());
    rots_net::set_nonblocking(connection.accepted());

    const std::string message = "look";
    ASSERT_EQ(rots_net::write_socket(connection.client(), message.data(), message.size()),
        static_cast<rots_net::ssize_type>(message.size()));

    EXPECT_EQ(read_bytes(connection.accepted(), message.size()), message);
}

TEST(RotsNet, AReadReturnsZeroOnceThePeerHasClosed)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());
    connection.close_accepted();

    ASSERT_TRUE(socket_test_support::wait_for_readable(connection.client(), DELIVERY_WAIT));
    char buffer[16];
    EXPECT_EQ(rots_net::read_socket(connection.client(), buffer, sizeof(buffer)), 0);
}

TEST(RotsNet, ClosingAnOpenSocketSucceeds)
{
    uint16_t port = 0;
    const SocketType listener = socket_test_support::open_loopback_listener(port);
    ASSERT_TRUE(rots_net::is_valid_socket(listener));

    EXPECT_EQ(rots_net::close_socket(listener), 0);
}

TEST(RotsNet, ClosingTheInvalidHandleFails)
{
    EXPECT_EQ(rots_net::close_socket(rots_net::INVALID_SOCKET_HANDLE), -1);
    const int error_code = rots_net::last_error();
    EXPECT_NE(error_code, 0);
    EXPECT_FALSE(rots_net::error_is_would_block(error_code)) << error_code;
}

TEST(RotsNet, SetTcpNodelayTurnsTheOptionOn)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());
    ASSERT_EQ(read_int_option(connection.accepted(), IPPROTO_TCP, TCP_NODELAY), 0);

    EXPECT_TRUE(rots_net::set_tcp_nodelay(connection.accepted()));

    EXPECT_NE(read_int_option(connection.accepted(), IPPROTO_TCP, TCP_NODELAY), 0);
}

TEST(RotsNet, SetKeepaliveTurnsTheOptionOn)
{
    LoopbackConnection connection;
    ASSERT_TRUE(connection.is_open());
    ASSERT_EQ(read_int_option(connection.accepted(), SOL_SOCKET, SO_KEEPALIVE), 0);

    EXPECT_TRUE(rots_net::set_keepalive(connection.accepted()));

    EXPECT_NE(read_int_option(connection.accepted(), SOL_SOCKET, SO_KEEPALIVE), 0);
}

TEST(RotsNet, SocketOptionsCannotBeSetOnTheInvalidHandle)
{
    EXPECT_FALSE(rots_net::set_tcp_nodelay(rots_net::INVALID_SOCKET_HANDLE));
    EXPECT_FALSE(rots_net::set_keepalive(rots_net::INVALID_SOCKET_HANDLE));
}

TEST(RotsNet, AWouldBlockErrorIsNotAnInterruption)
{
    socket_test_support::set_would_block_error();
    const int error_code = rots_net::last_error();

    EXPECT_TRUE(rots_net::error_is_would_block(error_code));
    EXPECT_FALSE(rots_net::error_is_interrupted(error_code));
}

TEST(RotsNet, AnInterruptionIsNotAWouldBlockError)
{
    socket_test_support::set_interrupted_error();
    const int error_code = rots_net::last_error();

    EXPECT_TRUE(rots_net::error_is_interrupted(error_code));
    EXPECT_FALSE(rots_net::error_is_would_block(error_code));
}

TEST(RotsNet, AConnectionResetIsNeitherWouldBlockNorAnInterruption)
{
    socket_test_support::set_connection_reset_error();
    const int error_code = rots_net::last_error();

    EXPECT_FALSE(rots_net::error_is_would_block(error_code));
    EXPECT_FALSE(rots_net::error_is_interrupted(error_code));
}

TEST(RotsNet, ResolveHostNameNamesTheLoopbackAddress)
{
    char host_name[256];
    std::memset(host_name, 'x', sizeof(host_name));

    ASSERT_TRUE(rots_net::resolve_host_name(htonl(INADDR_LOOPBACK), host_name, sizeof(host_name)));

    const void* terminator = std::memchr(host_name, '\0', sizeof(host_name));
    ASSERT_NE(terminator, nullptr);
    EXPECT_GT(std::strlen(host_name), 0u);
}

TEST(RotsNet, ResolveHostNameFailsWithoutRoomForAName)
{
    char host_name[1] = { 'x' };

    EXPECT_FALSE(rots_net::resolve_host_name(htonl(INADDR_LOOPBACK), host_name, 0));
    EXPECT_FALSE(rots_net::resolve_host_name(htonl(INADDR_LOOPBACK), nullptr, 16));
    EXPECT_EQ(host_name[0], 'x');
}

} // namespace
