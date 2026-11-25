#ifndef PROMPT_MARKER_H
#define PROMPT_MARKER_H

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

#endif // PROMPT_MARKER_H
