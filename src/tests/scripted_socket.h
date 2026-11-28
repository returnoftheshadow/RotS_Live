#pragma once

#include "../rots_net.h"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace socket_test_support {

// A pretend connection socket. The test binary is linked with rots_net::read_socket and
// rots_net::write_socket wrapped (src/CMakeLists.txt), so every read and write the server makes
// on this socket's handle comes here instead of the operating system: a test scripts what the
// peer sends and how writes go, and reads back what the server wrote. Reads and writes on any
// other handle reach the real socket. Several may exist at once, each with its own handle.
class ScriptedSocket {
public:
    // Makes the socket's handle scripted until the socket is destroyed.
    ScriptedSocket();
    ~ScriptedSocket();

    ScriptedSocket(const ScriptedSocket&) = delete;
    ScriptedSocket& operator=(const ScriptedSocket&) = delete;

    // The handle to give a descriptor. No real socket has it, and a read or write on a scripted
    // handle whose socket is gone fails the test.
    SocketType handle() const;

    // Queue what later reads see, in order. Each read returns bytes from at most one queue_input()
    // call, as many as fit, and leaves the rest for the next read. An end-of-file step makes its
    // read return 0, and a failure step fails it with "connection reset". Once the queue is
    // empty, reads fail with "would block".
    void queue_input(std::string_view bytes);
    void queue_end_of_file();
    void queue_read_failure();

    // Queue how later writes go, in order: success, "would block" or "connection reset". A write
    // with nothing queued succeeds.
    void queue_write_success();
    void queue_write_would_block();
    void queue_write_failure();

    // Limits each successful write to at most `max_bytes` bytes, as a full send buffer would.
    void limit_write_size(size_t max_bytes);

    // Every byte successful writes have taken so far.
    const std::string& written() const { return m_written; }

    // How many reads and writes the server has made on this socket.
    int read_calls() const { return m_read_calls; }
    int write_calls() const { return m_write_calls; }

    // The scripted socket with `socket_handle`; null when none has it.
    static ScriptedSocket* find(SocketType socket_handle);

    // True for a handle a ScriptedSocket has had during this run, live or gone.
    static bool is_scripted_handle(SocketType socket_handle);

    // Play this socket's script for one read or write, with read_socket's and write_socket's
    // contract. Called by the link-time wrappers.
    rots_net::ssize_type read(void* out_buffer, size_t length);
    rots_net::ssize_type write(const void* buffer, size_t length);

private:
    enum class StepKind { Bytes, EndOfFile, Failure, WouldBlock };

    // One scripted outcome of a read or a write.
    struct Step {
        // What the call does.
        StepKind kind;
        // The bytes a Bytes read step still has to deliver; empty for every other step.
        std::string bytes;
    };

    // The handle the next scripted socket gets; handles are never reused within a run.
    static SocketType s_next_handle;

    // The handle descriptors are given and the key in the live-socket registry; unique for the
    // run.
    const SocketType m_handle;
    // Read outcomes not yet used, oldest first.
    std::deque<Step> m_read_steps;
    // Write outcomes not yet used, oldest first; a Bytes step is a successful write.
    std::deque<Step> m_write_steps;
    // The most bytes one successful write takes; 0 means no limit.
    size_t m_write_limit = 0;
    // Everything successful writes have taken, in order.
    std::string m_written;
    // Counts of the reads and writes made, failed ones included.
    int m_read_calls = 0;
    int m_write_calls = 0;
};

} // namespace socket_test_support
