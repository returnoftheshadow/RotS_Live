#include "prompt_marker.h"

#include <algorithm>

namespace {

// Telnet's "interpret as command" byte, which starts every telnet command.
constexpr char TELNET_IAC = static_cast<char>(255);

// Telnet's end-of-record command (RFC 885).
constexpr char TELNET_EOR = static_cast<char>(239);

// Telnet's go-ahead command (RFC 854).
constexpr char TELNET_GA = static_cast<char>(249);

// IAC EOR, for a client that agreed to end-of-record.
constexpr char END_OF_RECORD_MARKER[] = { TELNET_IAC, TELNET_EOR };

// IAC GA, for a client that has not suppressed go-ahead.
constexpr char GO_AHEAD_MARKER[] = { TELNET_IAC, TELNET_GA };

static_assert(sizeof(END_OF_RECORD_MARKER) <= PROMPT_END_MARKER_MAX_LENGTH);
static_assert(sizeof(GO_AHEAD_MARKER) <= PROMPT_END_MARKER_MAX_LENGTH);

} // namespace

std::string_view prompt_end_marker(EorAgreement eor, GoAhead go_ahead)
{
    if (eor == EorAgreement::Agreed) {
        return std::string_view(END_OF_RECORD_MARKER, sizeof(END_OF_RECORD_MARKER));
    }

    if (go_ahead == GoAhead::Suppressed) {
        return std::string_view();
    }

    return std::string_view(GO_AHEAD_MARKER, sizeof(GO_AHEAD_MARKER));
}

bool compose_prompt_with_marker(std::string_view prompt_text, std::string_view marker,
    char* out_buffer, std::size_t buffer_size)
{
    // The terminating NUL needs one byte past the prompt and marker.
    if (prompt_text.size() >= buffer_size || marker.size() >= buffer_size - prompt_text.size()) {
        return false;
    }

    char* const marker_start = std::copy(prompt_text.begin(), prompt_text.end(), out_buffer);
    char* const terminator = std::copy(marker.begin(), marker.end(), marker_start);
    *terminator = '\0';
    return true;
}
