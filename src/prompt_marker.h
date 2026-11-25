#ifndef PROMPT_MARKER_H
#define PROMPT_MARKER_H

#include <cstddef>
#include <string_view>

// The telnet bytes that tell a MUD client a prompt has ended, so it can tell the prompt line
// from game text.

// Whether the client agreed to telnet end-of-record (DO EOR).
enum class EorAgreement { Refused, Agreed };

// Whether the client lets the server send go-ahead, or suppressed it (DO SGA).
enum class GoAhead { Sent, Suppressed };

// Returns IAC EOR when the client agreed to EOR, IAC GA when it did not and has not
// suppressed go-ahead, and an empty view when it suppressed go-ahead without agreeing to EOR.
// The view refers to static storage and stays valid for the life of the program.
std::string_view prompt_end_marker(EorAgreement eor, GoAhead go_ahead);

// The most bytes prompt_end_marker() returns.
inline constexpr std::size_t PROMPT_END_MARKER_MAX_LENGTH = 2;

// Copies prompt_text, then marker, then a terminating NUL into out_buffer, which holds
// buffer_size bytes. Returns false, writing nothing, when they do not fit.
[[nodiscard]] bool compose_prompt_with_marker(std::string_view prompt_text, std::string_view marker,
    char* out_buffer, std::size_t buffer_size);

#endif // PROMPT_MARKER_H
