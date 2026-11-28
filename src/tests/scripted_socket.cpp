#include "scripted_socket.h"

#include "socket_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace socket_test_support {

namespace {

    // Far above any descriptor the test process opens, so a scripted handle can never be a real
    // socket's.
    constexpr SocketType FIRST_SCRIPTED_SOCKET_HANDLE = 1000000;

    // The scripted sockets that exist, by handle.
    std::map<SocketType, ScriptedSocket*>& live_sockets()
    {
        static std::map<SocketType, ScriptedSocket*> sockets;
        return sockets;
    }

} // namespace

SocketType ScriptedSocket::s_next_handle = FIRST_SCRIPTED_SOCKET_HANDLE;

ScriptedSocket::ScriptedSocket()
    : m_handle(s_next_handle++)
{
    live_sockets()[m_handle] = this;
}

ScriptedSocket::~ScriptedSocket()
{
    live_sockets().erase(m_handle);
}

SocketType ScriptedSocket::handle() const
{
    return m_handle;
}

ScriptedSocket* ScriptedSocket::find(SocketType socket_handle)
{
    std::map<SocketType, ScriptedSocket*>& sockets = live_sockets();
    const auto found = sockets.find(socket_handle);
    if (found == sockets.end()) {
        return nullptr;
    }
    return found->second;
}

bool ScriptedSocket::is_scripted_handle(SocketType socket_handle)
{
    return socket_handle >= FIRST_SCRIPTED_SOCKET_HANDLE && socket_handle < s_next_handle;
}

void ScriptedSocket::queue_input(std::string_view bytes)
{
    if (bytes.empty()) {
        return;
    }
    m_read_steps.push_back({ StepKind::Bytes, std::string(bytes) });
}

void ScriptedSocket::queue_end_of_file()
{
    m_read_steps.push_back({ StepKind::EndOfFile, std::string() });
}

void ScriptedSocket::queue_read_failure()
{
    m_read_steps.push_back({ StepKind::Failure, std::string() });
}

void ScriptedSocket::queue_write_success()
{
    m_write_steps.push_back({ StepKind::Bytes, std::string() });
}

void ScriptedSocket::queue_write_would_block()
{
    m_write_steps.push_back({ StepKind::WouldBlock, std::string() });
}

void ScriptedSocket::queue_write_failure()
{
    m_write_steps.push_back({ StepKind::Failure, std::string() });
}

void ScriptedSocket::limit_write_size(size_t max_bytes)
{
    m_write_limit = max_bytes;
}

void ScriptedSocket::set_send_buffer_full(bool full)
{
    m_send_buffer_full = full;
}

rots_net::ssize_type ScriptedSocket::read(void* out_buffer, size_t length)
{
    ++m_read_calls;
    if (m_read_steps.empty()) {
        set_would_block_error();
        return -1;
    }

    Step& step = m_read_steps.front();
    if (step.kind == StepKind::EndOfFile) {
        m_read_steps.pop_front();
        return 0;
    }

    if (step.kind != StepKind::Bytes) {
        m_read_steps.pop_front();
        set_connection_reset_error();
        return -1;
    }

    const size_t delivered = std::min(length, step.bytes.size());
    std::memcpy(out_buffer, step.bytes.data(), delivered);
    step.bytes.erase(0, delivered);
    if (step.bytes.empty()) {
        m_read_steps.pop_front();
    }
    return static_cast<rots_net::ssize_type>(delivered);
}

rots_net::ssize_type ScriptedSocket::write(const void* buffer, size_t length)
{
    ++m_write_calls;
    if (m_send_buffer_full) {
        set_would_block_error();
        return -1;
    }

    StepKind kind = StepKind::Bytes;
    if (!m_write_steps.empty()) {
        kind = m_write_steps.front().kind;
        m_write_steps.pop_front();
    }

    if (kind == StepKind::WouldBlock) {
        set_would_block_error();
        return -1;
    }

    if (kind != StepKind::Bytes) {
        set_connection_reset_error();
        return -1;
    }

    size_t accepted = length;
    if (m_write_limit != 0) {
        accepted = std::min(accepted, m_write_limit);
    }
    m_written.append(static_cast<const char*>(buffer), accepted);
    return static_cast<rots_net::ssize_type>(accepted);
}

namespace {

    // Fails the test for a read or write on a scripted handle whose socket is destroyed, then
    // returns -1 with a connection-reset error, as a socket would.
    rots_net::ssize_type fail_on_stale_handle(SocketType socket_handle, const char* operation)
    {
        ADD_FAILURE() << "The server tried to " << operation << " scripted socket "
                      << static_cast<long long>(socket_handle) << " after it was destroyed.";
        set_connection_reset_error();
        return -1;
    }

} // namespace

} // namespace socket_test_support

// The test binary links with --wrap for both functions (src/CMakeLists.txt): every call to
// rots_net::read_socket or rots_net::write_socket from another object file arrives here, and
// __real_ names the original. The names are the functions' mangled names for the 32-bit build,
// where size_t is unsigned int; a build where it is not stops here.
static_assert(sizeof(size_t) == sizeof(unsigned int),
    "the --wrap names in src/CMakeLists.txt mangle size_t as unsigned int");
extern "C" rots_net::ssize_type __real__ZN8rots_net11read_socketEiPvj(
    SocketType socket_handle, void* out_buffer, size_t length);
extern "C" rots_net::ssize_type __real__ZN8rots_net12write_socketEiPKvj(
    SocketType socket_handle, const void* buffer, size_t length);

extern "C" rots_net::ssize_type __wrap__ZN8rots_net11read_socketEiPvj(
    SocketType socket_handle, void* out_buffer, size_t length)
{
    using socket_test_support::ScriptedSocket;
    ScriptedSocket* socket = ScriptedSocket::find(socket_handle);
    if (socket != nullptr) {
        return socket->read(out_buffer, length);
    }

    if (ScriptedSocket::is_scripted_handle(socket_handle)) {
        return socket_test_support::fail_on_stale_handle(socket_handle, "read");
    }
    return __real__ZN8rots_net11read_socketEiPvj(socket_handle, out_buffer, length);
}

extern "C" rots_net::ssize_type __wrap__ZN8rots_net12write_socketEiPKvj(
    SocketType socket_handle, const void* buffer, size_t length)
{
    using socket_test_support::ScriptedSocket;
    ScriptedSocket* socket = ScriptedSocket::find(socket_handle);
    if (socket != nullptr) {
        return socket->write(buffer, length);
    }

    if (ScriptedSocket::is_scripted_handle(socket_handle)) {
        return socket_test_support::fail_on_stale_handle(socket_handle, "write to");
    }
    return __real__ZN8rots_net12write_socketEiPKvj(socket_handle, buffer, length);
}
