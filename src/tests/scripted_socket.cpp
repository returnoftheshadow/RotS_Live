#include "scripted_socket.h"

#include "socket_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace socket_test_support {

namespace {

    // Far above any descriptor the test process opens, so an access that misses the override
    // fails with "bad descriptor" instead of reaching a real file.
    constexpr SocketType FIRST_SCRIPTED_SOCKET_HANDLE = 1000000;

    // The scripted sockets that exist, by handle.
    std::map<SocketType, ScriptedSocket*>& live_sockets()
    {
        static std::map<SocketType, ScriptedSocket*> sockets;
        return sockets;
    }

} // namespace

comm_testing::socket_read_fn ScriptedSocket::s_previous_read = nullptr;
comm_testing::socket_write_fn ScriptedSocket::s_previous_write = nullptr;
SocketType ScriptedSocket::s_next_handle = FIRST_SCRIPTED_SOCKET_HANDLE;

ScriptedSocket::ScriptedSocket()
    : m_handle(s_next_handle++)
{
    std::map<SocketType, ScriptedSocket*>& sockets = live_sockets();
    if (sockets.empty()) {
        s_previous_read = comm_testing::set_socket_read_override(&ScriptedSocket::read_override);
        s_previous_write = comm_testing::set_socket_write_override(&ScriptedSocket::write_override);
    }
    sockets[m_handle] = this;
}

ScriptedSocket::~ScriptedSocket()
{
    std::map<SocketType, ScriptedSocket*>& sockets = live_sockets();
    sockets.erase(m_handle);
    if (sockets.empty()) {
        comm_testing::set_socket_read_override(s_previous_read);
        comm_testing::set_socket_write_override(s_previous_write);
    }
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

rots_net::ssize_type ScriptedSocket::read_override(SocketType socket_handle, void* out_buffer, size_t length)
{
    ScriptedSocket* socket = find(socket_handle);
    if (socket == nullptr) {
        ADD_FAILURE() << "comm.cpp read socket " << static_cast<long long>(socket_handle)
                      << ", which is not the scripted socket.";
        set_connection_reset_error();
        return -1;
    }
    return socket->read(out_buffer, length);
}

rots_net::ssize_type ScriptedSocket::write_override(SocketType socket_handle, const void* buffer, size_t length)
{
    ScriptedSocket* socket = find(socket_handle);
    if (socket == nullptr) {
        ADD_FAILURE() << "comm.cpp wrote to socket " << static_cast<long long>(socket_handle)
                      << ", which is not the scripted socket.";
        set_connection_reset_error();
        return -1;
    }
    return socket->write(buffer, length);
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

} // namespace socket_test_support
