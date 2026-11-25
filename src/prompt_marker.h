#ifndef PROMPT_MARKER_H
#define PROMPT_MARKER_H

#include <string_view>

// The telnet bytes that tell a MUD client a prompt has ended, so it can tell the prompt line
// from game text.

// Returns IAC EOR when the client agreed to EOR, IAC GA when it did not and has not
// suppressed go-ahead, and an empty view when it suppressed go-ahead without agreeing to EOR.
// The view refers to static bytes and never allocates.
std::string_view prompt_end_marker(bool client_agreed_eor, bool client_suppressed_ga);

#endif // PROMPT_MARKER_H
