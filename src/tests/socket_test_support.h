#pragma once

// Real TCP sockets on the loopback interface for tests, built only from calls POSIX and Winsock
// share, plus setters for the socket error rots_net::last_error() reports.

#include "../rots_net.h"

#include <chrono>
#include <cstdint>

namespace socket_test_support {

// Opens a listening TCP socket on 127.0.0.1 at a port the system picks, and stores that port
// in `out_port`. Returns INVALID_SOCKET_HANDLE, after reporting a test failure, when any step
// fails. The caller closes the socket.
SocketType open_loopback_listener(uint16_t& out_port);

// Connects a blocking TCP socket to 127.0.0.1:`port`. Returns INVALID_SOCKET_HANDLE, after
// reporting a test failure, when the connection fails. The caller closes the socket.
SocketType connect_loopback_client(uint16_t port);

// Waits up to `timeout` for `socket_handle` to have bytes to read, or for its peer to close.
// Returns false when the time runs out first.
bool wait_for_readable(SocketType socket_handle, std::chrono::milliseconds timeout);

// Sets the error rots_net::last_error() reports next to one the platform uses for: a
// nonblocking socket with nothing to do, a call a signal interrupted, and a peer that reset the
// connection.
void set_would_block_error();
void set_interrupted_error();
void set_connection_reset_error();

// Both ends of one TCP connection over loopback, closed on destruction. The listener used to
// make it is closed before the constructor returns.
class LoopbackConnection {
public:
    LoopbackConnection();
    ~LoopbackConnection();

    LoopbackConnection(const LoopbackConnection&) = delete;
    LoopbackConnection& operator=(const LoopbackConnection&) = delete;

    // True when both ends were opened; a failure has already been reported to the test.
    bool is_open() const;

    // Closes the accepted end now, as a peer hanging up would.
    void close_accepted();

    // The end that connected; INVALID_SOCKET_HANDLE when it could not be opened.
    SocketType client() const { return m_client; }

    // The end the listener accepted; INVALID_SOCKET_HANDLE when it could not be accepted, or
    // after close_accepted().
    SocketType accepted() const { return m_accepted; }

private:
    // The connecting end; owned and closed by this object.
    SocketType m_client = rots_net::INVALID_SOCKET_HANDLE;
    // The accepted end; owned and closed by this object unless close_accepted() already did.
    SocketType m_accepted = rots_net::INVALID_SOCKET_HANDLE;
};

} // namespace socket_test_support
