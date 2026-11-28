#include "rots_net.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

#if !defined(PREDEF_PLATFORM_WINDOWS)
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rots_net {

namespace {

    // Sets one int-valued socket option to 1. Winsock declares the value as const char*;
    // POSIX takes const void*, which the same pointer converts to.
    bool enable_socket_option(SocketType socket_handle, int level, int option_name)
    {
        const int enabled = 1;
        const char* option_value = reinterpret_cast<const char*>(&enabled);
        return setsockopt(socket_handle, level, option_name, option_value, sizeof(enabled)) == 0;
    }

} // namespace

bool set_tcp_nodelay(SocketType socket_handle)
{
    return enable_socket_option(socket_handle, IPPROTO_TCP, TCP_NODELAY);
}

bool set_keepalive(SocketType socket_handle)
{
    return enable_socket_option(socket_handle, SOL_SOCKET, SO_KEEPALIVE);
}

#if defined(PREDEF_PLATFORM_WINDOWS)

bool startup()
{
    WSADATA winsock_data;
    const int startup_result = WSAStartup(MAKEWORD(2, 2), &winsock_data);
    if (startup_result != 0) {
        // WSAStartup reports its error as the return value; WSAGetLastError needs Winsock.
        WSASetLastError(startup_result);
        return false;
    }
    return true;
}

void shutdown()
{
    WSACleanup();
}

int close_socket(SocketType socket_handle)
{
    return ::closesocket(socket_handle);
}

void set_nonblocking(SocketType socket_handle)
{
    u_long nonblocking_mode = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking_mode) != 0) {
        fprintf(stderr, "Fatal error making socket %llu nonblocking: Winsock error %d\n",
            static_cast<unsigned long long>(socket_handle), WSAGetLastError());
        exit(1);
    }
}

namespace {

    // recv and send take an int length; a longer request moves INT_MAX bytes and the caller's
    // loop sends the rest.
    int winsock_length(size_t length)
    {
        const size_t max_length = static_cast<size_t>(std::numeric_limits<int>::max());
        return static_cast<int>(std::min(length, max_length));
    }

} // namespace

ssize_type read_socket(SocketType socket_handle, void* out_buffer, size_t length)
{
    return ::recv(socket_handle, static_cast<char*>(out_buffer), winsock_length(length), 0);
}

ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length)
{
    return ::send(socket_handle, static_cast<const char*>(buffer), winsock_length(length), 0);
}

int last_error()
{
    return WSAGetLastError();
}

bool error_is_would_block(int error_code)
{
    return error_code == WSAEWOULDBLOCK;
}

bool error_is_interrupted(int error_code)
{
    return error_code == WSAEINTR;
}

#else

bool startup()
{
    return true;
}

void shutdown()
{
}

int close_socket(SocketType socket_handle)
{
    return ::close(socket_handle);
}

void set_nonblocking(SocketType socket_handle)
{
    const int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("Fatal error making socket nonblocking");
        exit(1);
    }
}

ssize_type read_socket(SocketType socket_handle, void* out_buffer, size_t length)
{
    return ::read(socket_handle, out_buffer, length);
}

ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length)
{
    return ::write(socket_handle, buffer, length);
}

int last_error()
{
    return errno;
}

bool error_is_would_block(int error_code)
{
    // POSIX lets EAGAIN and EWOULDBLOCK be distinct values; either means "try again later".
    return error_code == EWOULDBLOCK || error_code == EAGAIN;
}

bool error_is_interrupted(int error_code)
{
    return error_code == EINTR;
}

#endif

bool resolve_host_name(uint32_t ipv4_address_network_order, char* out_host_buffer,
    size_t host_buffer_length)
{
    if (out_host_buffer == nullptr || host_buffer_length == 0) {
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ipv4_address_network_order;

    // getnameinfo writes into the caller's buffer, where gethostbyaddr returns a shared static
    // result. NI_NAMEREQD makes a nameless address a failure instead of returning its numeric
    // form. A buffer longer than socklen_t can describe is still offered only that much.
    const size_t max_length = static_cast<size_t>(std::numeric_limits<socklen_t>::max());
    const socklen_t offered_length = static_cast<socklen_t>(std::min(host_buffer_length, max_length));
    const int lookup_result = getnameinfo(reinterpret_cast<const sockaddr*>(&address),
        sizeof(address), out_host_buffer, offered_length, nullptr, 0, NI_NAMEREQD);
    return lookup_result == 0;
}

} // namespace rots_net
