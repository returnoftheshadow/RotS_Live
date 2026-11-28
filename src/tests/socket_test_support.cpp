#include "socket_test_support.h"

#include <gtest/gtest.h>

#if defined(PREDEF_PLATFORM_WINDOWS)
#include <winsock2.h>
#else
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace socket_test_support {

namespace {

    sockaddr_in loopback_address(uint16_t port)
    {
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        return address;
    }

    void set_last_error(int error_code)
    {
#if defined(PREDEF_PLATFORM_WINDOWS)
        WSASetLastError(error_code);
#else
        errno = error_code;
#endif
    }

} // namespace

SocketType open_loopback_listener(uint16_t& out_port)
{
    const SocketType listener = socket(AF_INET, SOCK_STREAM, 0);
    if (!rots_net::is_valid_socket(listener)) {
        ADD_FAILURE() << "socket() failed with error " << rots_net::last_error();
        return rots_net::INVALID_SOCKET_HANDLE;
    }

    sockaddr_in address = loopback_address(0);
    socklen_t address_length = sizeof(address);
    const bool bound = bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    const bool named = bound
        && getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length) == 0;
    const bool listening = named && listen(listener, 1) == 0;
    if (!listening) {
        ADD_FAILURE() << "Opening a loopback listener failed with error " << rots_net::last_error();
        rots_net::close_socket(listener);
        return rots_net::INVALID_SOCKET_HANDLE;
    }

    out_port = ntohs(address.sin_port);
    return listener;
}

SocketType connect_loopback_client(uint16_t port)
{
    const SocketType client = socket(AF_INET, SOCK_STREAM, 0);
    if (!rots_net::is_valid_socket(client)) {
        ADD_FAILURE() << "socket() failed with error " << rots_net::last_error();
        return rots_net::INVALID_SOCKET_HANDLE;
    }

    const sockaddr_in address = loopback_address(port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ADD_FAILURE() << "connect() to loopback port " << port << " failed with error "
                      << rots_net::last_error();
        rots_net::close_socket(client);
        return rots_net::INVALID_SOCKET_HANDLE;
    }
    return client;
}

bool wait_for_readable(SocketType socket_handle, std::chrono::milliseconds timeout)
{
    fd_set readable_set;
    FD_ZERO(&readable_set);
    FD_SET(socket_handle, &readable_set);

    const std::chrono::seconds whole_seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const std::chrono::microseconds remainder = timeout - whole_seconds;
    timeval wait_time {};
    wait_time.tv_sec = static_cast<long>(whole_seconds.count());
    wait_time.tv_usec = static_cast<long>(remainder.count());

    // Winsock ignores select()'s first argument; POSIX needs one more than the highest handle.
    const int ready_count = select(static_cast<int>(socket_handle) + 1, &readable_set, nullptr, nullptr, &wait_time);
    return ready_count > 0;
}

void set_would_block_error()
{
#if defined(PREDEF_PLATFORM_WINDOWS)
    set_last_error(WSAEWOULDBLOCK);
#else
    set_last_error(EWOULDBLOCK);
#endif
}

void set_interrupted_error()
{
#if defined(PREDEF_PLATFORM_WINDOWS)
    set_last_error(WSAEINTR);
#else
    set_last_error(EINTR);
#endif
}

void set_connection_reset_error()
{
#if defined(PREDEF_PLATFORM_WINDOWS)
    set_last_error(WSAECONNRESET);
#else
    set_last_error(ECONNRESET);
#endif
}

LoopbackConnection::LoopbackConnection()
{
    uint16_t port = 0;
    const SocketType listener = open_loopback_listener(port);
    if (!rots_net::is_valid_socket(listener)) {
        return;
    }

    m_client = connect_loopback_client(port);
    if (rots_net::is_valid_socket(m_client)) {
        // The connection is already queued on the listener, so this returns at once.
        m_accepted = accept(listener, nullptr, nullptr);
        if (!rots_net::is_valid_socket(m_accepted)) {
            ADD_FAILURE() << "accept() failed with error " << rots_net::last_error();
        }
    }
    rots_net::close_socket(listener);
}

LoopbackConnection::~LoopbackConnection()
{
    if (rots_net::is_valid_socket(m_client)) {
        rots_net::close_socket(m_client);
    }
    close_accepted();
}

bool LoopbackConnection::is_open() const
{
    return rots_net::is_valid_socket(m_client) && rots_net::is_valid_socket(m_accepted);
}

void LoopbackConnection::close_accepted()
{
    if (rots_net::is_valid_socket(m_accepted)) {
        rots_net::close_socket(m_accepted);
        m_accepted = rots_net::INVALID_SOCKET_HANDLE;
    }
}

} // namespace socket_test_support
