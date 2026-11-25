#include "prompt_marker.h"

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
