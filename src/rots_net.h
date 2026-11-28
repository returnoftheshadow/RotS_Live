#pragma once

// rots_net: the socket operations whose calls differ between POSIX BSD sockets and Winsock.
// The game's select() loop and the calls both platforms share (socket, bind, listen, accept,
// select, setsockopt with a portable option, getpeername) are used directly; everything else
// goes through this namespace. On POSIX each function makes the libc call of the same purpose,
// with the argument checks and failure handling its comment states.

#include "platdef.h"

#include <cstddef>
#include <cstdint>

#if !defined(PREDEF_PLATFORM_WINDOWS)
#include <sys/types.h>
#endif

namespace rots_net {

// A signed byte count, as read_socket and write_socket return: the bytes moved, 0 for end of
// file, or -1 for an error.
#if defined(PREDEF_PLATFORM_WINDOWS)
using ssize_type = SSIZE_T;
#else
using ssize_type = ssize_t;
#endif

// The handle value meaning "no socket": what socket() and accept() return on failure. It is -1
// on POSIX and INVALID_SOCKET on Windows, where SocketType is unsigned.
#if defined(PREDEF_PLATFORM_WINDOWS)
constexpr SocketType INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
constexpr SocketType INVALID_SOCKET_HANDLE = -1;
#endif

// True for every handle except INVALID_SOCKET_HANDLE, including 0. Use it in place of a `< 0`
// test, which can never be true for Windows' unsigned handles.
constexpr bool is_valid_socket(SocketType socket_handle)
{
    return socket_handle != INVALID_SOCKET_HANDLE;
}

// Prepares the process for socket calls; call once before the first one. Returns false, with
// last_error() set, when Windows refuses to start Winsock. Always true on POSIX.
bool startup();

// Releases what startup() acquired; call once for each successful startup(), after the last
// socket call. Does nothing on POSIX.
void shutdown();

// Closes a socket handle. Returns 0 on success, or -1 with last_error() set. Every close of a
// game socket goes through here, since Windows cannot close() a socket.
int close_socket(SocketType socket_handle);

// Makes reads and writes on the socket return at once instead of waiting. The select() loop
// cannot run with a blocking socket, so a failure is fatal: it is reported on stderr and the
// process exits with status 1.
void set_nonblocking(SocketType socket_handle);

// Turn on TCP_NODELAY (send small writes at once) and SO_KEEPALIVE (probe a silent peer). Each
// returns false, with last_error() set, when the option cannot be set; the socket stays usable.
bool set_tcp_nodelay(SocketType socket_handle);
bool set_keepalive(SocketType socket_handle);

// Read up to `length` bytes into `out_buffer`, or write up to `length` bytes from `buffer`.
// Return the number of bytes moved, 0 when a read finds the peer has closed the connection,
// or -1 with last_error() set. Fewer bytes than `length` can move, so callers loop; on Windows
// one call moves at most INT_MAX bytes.
ssize_type read_socket(SocketType socket_handle, void* out_buffer, size_t length);
ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length);

// The error code of the most recent failed socket call on this thread: errno on POSIX,
// WSAGetLastError() on Windows. Read it straight after the failure, before any other call that
// can change it.
int last_error();

// Classify a last_error() code: "would block" means a nonblocking socket had nothing to read
// or no room to write; "interrupted" means a signal cut the call short and it can be retried.
bool error_is_would_block(int error_code);
bool error_is_interrupted(int error_code);

// Looks up the host name of an IPv4 address given in network byte order (as in
// sockaddr_in::sin_addr.s_addr). On success writes the null-terminated name to
// `out_host_buffer` and returns true. Returns false when the address has no name, the name and
// its terminator do not fit in `host_buffer_length` bytes, the lookup fails, or
// `out_host_buffer` is null or the length is 0; the buffer's contents are then unspecified.
// The lookup can wait on the network, so it blocks the caller.
bool resolve_host_name(uint32_t ipv4_address_network_order, char* out_host_buffer,
    size_t host_buffer_length);

} // namespace rots_net
