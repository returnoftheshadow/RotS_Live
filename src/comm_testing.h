#pragma once

// Test-only hooks into comm.cpp's socket I/O, compiled only into the unit-test build. They let a
// test drive the connection read and write paths (would-block, failure, end of file, short
// writes) without a real peer.

#if defined(TESTING)

#include "rots_net.h"

#include <cstddef>

namespace comm_testing {

// Replacements for one socket read or write. A callback follows rots_net::read_socket and
// rots_net::write_socket: it returns the bytes moved, 0 for end of file, or -1 after setting the
// error rots_net::last_error() reports.
using socket_read_fn = rots_net::ssize_type (*)(SocketType socket_handle, void* out_buffer, size_t length);
using socket_write_fn = rots_net::ssize_type (*)(SocketType socket_handle, const void* buffer, size_t length);

// Route every connection read (or write) in comm.cpp to `callback`, or back to the socket when it
// is null. Returns the callback that was installed before, so a test can restore it.
socket_read_fn set_socket_read_override(socket_read_fn callback);
socket_write_fn set_socket_write_override(socket_write_fn callback);

} // namespace comm_testing

#endif
