#pragma once

#include "../comm_testing.h"
#include "../rots_net.h"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace socket_test_support {

// A pretend connection socket for comm.cpp. While it exists, every connection read and write in
// comm.cpp goes to it instead of the operating system, so a test can script what the peer sends
// and how writes go, and read back what the game wrote. Several may exist at once, each with its
// own handle. Writes made outside comm.cpp, such as echo_on and echo_off in utility.cpp, are not
// caught. A test must not install its own comm_testing override while one exists: the last
// ScriptedSocket to go restores the override that was in place before the first.
class ScriptedSocket {
public:
    // Installs the scripted sockets as comm.cpp's read and write overrides, if none existed.
    ScriptedSocket();
    // Restores the overrides that were installed before, once the last scripted socket goes.
    ~ScriptedSocket();

    ScriptedSocket(const ScriptedSocket&) = delete;
    ScriptedSocket& operator=(const ScriptedSocket&) = delete;

    // The handle to give a descriptor. No real socket has it; a read or write on a handle no
    // scripted socket has fails the test.
    SocketType handle() const;

    // Queue what later reads see, in order. Each read returns bytes from at most one queue_input()
    // call, as many as fit, and leaves the rest for the next read. Once the queue is empty, reads
    // fail with "would block".
    void queue_input(std::string_view bytes);
    void queue_end_of_file();
    void queue_read_failure();

    // Queue how later writes go, in order; a write with nothing queued succeeds.
    void queue_write_success();
    void queue_write_would_block();
    void queue_write_failure();

    // Limits each successful write to at most `max_bytes` bytes, as a full send buffer would.
    void limit_write_size(size_t max_bytes);

    // Every byte successful writes have taken so far.
    const std::string& written() const { return m_written; }

    // How many reads and writes comm.cpp has made on this socket.
    int read_calls() const { return m_read_calls; }
    int write_calls() const { return m_write_calls; }

private:
    enum class StepKind { Bytes, EndOfFile, Failure, WouldBlock };

    // One scripted outcome of a read or a write.
    struct Step {
        // What the call does.
        StepKind kind;
        // The bytes a Bytes read step still has to deliver; empty for every other step.
        std::string bytes;
    };

    // The scripted socket with `socket_handle`; null when none has it.
    static ScriptedSocket* find(SocketType socket_handle);

    static rots_net::ssize_type read_override(SocketType socket_handle, void* out_buffer, size_t length);
    static rots_net::ssize_type write_override(SocketType socket_handle, const void* buffer, size_t length);

    rots_net::ssize_type read(void* out_buffer, size_t length);
    rots_net::ssize_type write(const void* buffer, size_t length);

    // The overrides installed before the first scripted socket, restored after the last.
    static comm_testing::socket_read_fn s_previous_read;
    static comm_testing::socket_write_fn s_previous_write;
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
