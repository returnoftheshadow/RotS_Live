// Protocol snippet by KaVir.  Released into the public domain in February 2011.
//
// In case this is not legally possible:
//
// The copyright holder grants any entity the right to use this work for any
// purpose, without any conditions, unless such conditions are required by law.

// The MSDP support here is based on the MUD Server Data Protocol specification:
// https://tintin.mudhalla.net/protocols/msdp/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// The server's name, sent as MSDP SERVER_ID and MSSP NAME.
inline constexpr char MUD_NAME[] = "Return of the Shadow";

struct descriptor_data;
// A connection. Its pProtocol member is the protocol state these functions read and change.
using descriptor_t = descriptor_data;

// The protocol snippet's version, sent to clients as MSDP SNIPPET_VERSION.
inline constexpr int SNIPPET_VERSION = 8;

// The most characters one ProtocolInput call passes on, and the most bytes one subnegotiation
// may hold.
inline constexpr int MAX_PROTOCOL_BUFFER = 2048;
// The limit on one MSDP or ATCP message, framing included; a longer one is logged and not sent.
inline constexpr int MAX_VARIABLE_LENGTH = 4096;
// The limit on ProtocolOutput's result and on the variable lists sent for MSDP LIST.
inline constexpr int MAX_OUTPUT_BUFFER = 8192;
// The size of the MSSP status message.
inline constexpr int MAX_MSSP_BUFFER = 4096;

// Telnet subnegotiation codes: SEND asks the client for a value, as in TTYPE SEND, and ACCEPTED
// answers a CHARSET request.
inline constexpr int SEND = 1;
inline constexpr int ACCEPTED = 2;

// Telnet option numbers for the MUD protocols.
inline constexpr int TELOPT_CHARSET = 42;
inline constexpr int TELOPT_MSDP = 69;
inline constexpr int TELOPT_MSSP = 70;
// MCCP version 2.
inline constexpr int TELOPT_MCCP = 86;
inline constexpr int TELOPT_MSP = 90;
inline constexpr int TELOPT_MXP = 91;
inline constexpr int TELOPT_ATCP = 200;

// MSDP's framing bytes: VAR comes before a name and VAL before a value, and the open and close
// bytes surround a table or an array.
inline constexpr int MSDP_VAR = 1;
inline constexpr int MSDP_VAL = 2;
inline constexpr int MSDP_TABLE_OPEN = 3;
inline constexpr int MSDP_TABLE_CLOSE = 4;
inline constexpr int MSDP_ARRAY_OPEN = 5;
inline constexpr int MSDP_ARRAY_CLOSE = 6;
// The most characters of a name or a value kept from a client's MSDP or ATCP message; the rest
// are dropped.
inline constexpr int MAX_MSDP_SIZE = 100;

// MSSP's framing bytes, before each name and each value.
inline constexpr int MSSP_VAR = 1;
inline constexpr int MSSP_VAL = 2;

// The telnet options whose state protocol_t::option_states records, one for each option except
// MXP, which has one for each way of offering it.
enum negotiated_t {
    eNEGOTIATED_TTYPE,
    eNEGOTIATED_ECHO,
    eNEGOTIATED_NAWS,
    eNEGOTIATED_CHARSET,
    eNEGOTIATED_MSDP,
    eNEGOTIATED_MSSP,
    eNEGOTIATED_ATCP,
    eNEGOTIATED_MSP,
    // MXP offered as DO MXP.
    eNEGOTIATED_MXP,
    // MXP offered as WILL MXP, after the client refuses DO MXP, or agreed when the client sends
    // DO MXP.
    eNEGOTIATED_MXP2,
    // Recorded, but never sent: the server neither offers nor answers MCCP.
    eNEGOTIATED_MCCP,
    eNEGOTIATED_EOR,
    eNEGOTIATED_SGA,

    // The number of options; not an option.
    eNEGOTIATED_MAX
};

// What the server knows of a client's support for a feature.
enum support_t {
    // Nothing is known yet.
    eUNKNOWN,
    // The client does not support it.
    eNO,
    // Some versions of the client support it, and this one's version cannot tell.
    eSOMETIMES,
    // The client supports it.
    eYES
};

// The MSDP variables, each named as on the wire without its eMSDP_ prefix. Adding one needs an
// entry at the same place in protocol.cpp's variable table.
enum variable_t {
    // No variable; below every valid one.
    eMSDP_NONE = -1,

    // The server, its time and the snippet's version.
    eMSDP_CHARACTER_NAME,
    eMSDP_SERVER_ID,
    eMSDP_SERVER_TIME,
    eMSDP_SNIPPET_VERSION,

    // The player's character.
    eMSDP_AFFECTS,
    eMSDP_ALIGNMENT,
    eMSDP_EXPERIENCE,
    eMSDP_EXPERIENCE_MAX,
    eMSDP_HEALTH,
    eMSDP_HEALTH_MAX,
    eMSDP_LEVEL,
    eMSDP_RACE,
    eMSDP_CLASS, // Never set, so it always reports its default.
    eMSDP_MANA,
    eMSDP_MANA_MAX,
    eMSDP_WIMPY,
    eMSDP_PRACTICE,
    eMSDP_MONEY,
    eMSDP_MOVEMENT,
    eMSDP_MOVEMENT_MAX,
    eMSDP_HITROLL, // Never set, so it always reports its default.
    eMSDP_DAMROLL, // Never set, so it always reports its default.
    eMSDP_SPIRIT,
    eMSDP_AC, // Never set, so it always reports its default.
    eMSDP_STR,
    eMSDP_INT,
    eMSDP_WILL,
    eMSDP_DEX,
    eMSDP_CON,
    eMSDP_LEA,
    eMSDP_STR_PERM,
    eMSDP_INT_PERM,
    eMSDP_WIL_PERM,
    eMSDP_DEX_PERM,
    eMSDP_CON_PERM,
    eMSDP_LEA_PERM,
    eMSDP_SPELL_SAVE,
    eMSDP_SPELL_PEN,
    eMSDP_SPELL_POWER,
    eMSDP_ARMOUR_ABS,
    eMSDP_OFFENSIVE_BONUS,
    eMSDP_PARRY,
    eMSDP_DODGE,
    eMSDP_ATTACK_SPEED,
    eMSDP_TACTIC,
    eMSDP_SPECIALIZATION,
    eMSDP_PERCEPTION,
    eMSDP_WILLPOWER,
    eMSDP_SKILL_ENCUMBRANCE,
    eMSDP_MOVEMENT_ENCUMBRANCE,
    eMSDP_CARRIED_WEIGHT,
    eMSDP_WARRIOR_LEVEL,
    eMSDP_WARRIOR_LEVEL_MAX,
    eMSDP_RANGER_LEVEL,
    eMSDP_RANGER_LEVEL_MAX,
    eMSDP_MYSTIC_LEVEL,
    eMSDP_MYSTIC_LEVEL_MAX,
    eMSDP_MAGE_LEVEL,
    eMSDP_MAGE_LEVEL_MAX,
    eMSDP_HEALTH_REGENERATION,
    eMSDP_STAMINA_REGENERATION,
    eMSDP_MOVEMENT_REGENERATION,

    // The character's opponent.
    eMSDP_OPPONENT_HEALTH,
    eMSDP_OPPONENT_HEALTH_MAX,
    eMSDP_OPPONENT_LEVEL,
    eMSDP_OPPONENT_NAME,
    eMSDP_OPPONENT_AGE,

    // The character's room and area, and the game's time and weather.
    eMSDP_AREA_NAME,
    eMSDP_ROOM,
    eMSDP_ROOM_EXITS,
    eMSDP_ROOM_NAME,
    eMSDP_ROOM_VNUM,
    eMSDP_WORLD_TIME,
    eMSDP_WEATHER,

    // The client and its features, set by the client over MSDP or found by client detection.
    eMSDP_CLIENT_ID,
    eMSDP_CLIENT_VERSION,
    eMSDP_PLUGIN_ID,
    eMSDP_ANSI_COLORS,
    eMSDP_XTERM_256_COLORS,
    eMSDP_UTF_8,
    eMSDP_SOUND,
    eMSDP_MXP,

    // Button and gauge layouts for clients that offer a generic GUI.
    eMSDP_BUTTON_1,
    eMSDP_BUTTON_2,
    eMSDP_BUTTON_3,
    eMSDP_BUTTON_4,
    eMSDP_BUTTON_5,
    eMSDP_GAUGE_1,
    eMSDP_GAUGE_2,
    eMSDP_GAUGE_3,
    eMSDP_GAUGE_4,
    eMSDP_GAUGE_5,

    // The character's group.
    eMSDP_GROUP,

    // The number of variables; not a variable.
    eMSDP_MAX
};

// The fixed description of one MSDP variable: its name, its kind and what a client may set it to.
struct variable_name_t {
    std::string_view name; // The variable's name on the wire.
    // A GUI variable's initial text; empty for every other variable.
    std::optional<std::string_view> default_text;
    variable_t variable; // The variable described, which is also the entry's table index.
    int minimum; // The least number, or the shortest text, a client may set.
    int maximum; // The greatest number, or the longest text, a client may set; -1 when it may not.
    int default_number; // The initial value of a number variable.
    bool is_string; // Whether the value is text rather than a number.
    bool configurable; // Whether a client may set the variable.
    bool write_once; // Whether a client may set it only while its value is "Unknown".
    bool gui; // Whether it is a GUI layout, listed under GUI_VARIABLES, not with the others.
};

// One MSDP variable's value and reporting state on one connection.
struct MSDP_t {
    // The value: the std::string alternative for a string variable, the int for a number one, as
    // the variable table's is_string says.
    std::variant<int, std::string> value;
    bool reported; // Whether updates send the variable; every variable starts reported.
    bool dirty; // Whether the value has changed since it was last sent.
};

// One entry of the MSSP status message.
struct MSSP_t {
    std::string_view name; // The MSSP variable's name.
    std::string_view value; // Its value, as text.
};

// The protocol state of one connection: what was negotiated, what the client is and its MSDP
// variables. ProtocolCreate makes it and ProtocolDestroy frees it.
struct protocol_t {
    std::array<MSDP_t, eMSDP_MAX> variables; // The MSDP variables, indexed by variable_t.
    std::string mxp_version; // The MXP version from the client's <VERSION> reply, or "Unknown".
    // The terminal type the client sent last, to see when its TTYPE list repeats; empty before the
    // first.
    std::optional<std::string> last_ttype;
    // Raised from a positive value to 2 by every send. It starts at 0 and the server never sets it,
    // so only a caller that sets it to 1 sees a change.
    int write_oob;
    int pending_input_length; // The bytes held in pending_input.
    int subnegotiation_input_length; // The bytes held in subnegotiation_input.
    // What client detection found about XTerm 256 colours; colour output follows the
    // XTERM_256_COLORS variable instead.
    support_t xterm_256_support;
    int screen_width; // The client's window width in characters from NAWS; 0 until it is sent.
    int screen_height; // The client's window height in lines from NAWS; 0 until it is sent.
    // The start of a telnet command or MXP line tag that ended a read, put before the next read.
    std::array<char, 4> pending_input;
    // The subnegotiation collected so far, kept across reads until its IAC SE.
    std::array<char, MAX_PROTOCOL_BUFFER + 1> subnegotiation_input;
    // Each negotiated_t option's last offered or agreed state; an offer is sent only on a change.
    std::array<bool, eNEGOTIATED_MAX> option_states;
    bool in_subnegotiation; // Whether input is inside IAC SB ... IAC SE, which may span reads.
    bool negotiated; // Whether the client has answered TTYPE, so the other options were offered.
    // Set when the TTYPE answer starts negotiation and cleared when the client answers NAWS. While
    // it is set, MXPSendTag offers every option again, for clients such as MUSHclient that miss
    // the first offers when they connect automatically.
    bool renegotiate;
    bool needs_mxp_version; // Whether to send <VERSION> when MXP comes on; set by renegotiation.
    // Set by a ProtocolOutput \t[X...] code whose MXP version the client lacks; it suppresses the
    // next MXP link or tag.
    bool mxp_blocked;
    bool ttype_enabled; // The client agreed to send its terminal type.
    bool echo_enabled; // The client agreed that the server echoes its input (DO ECHO).
    bool naws_enabled; // The client agreed to send its window size.
    bool charset_enabled; // The client agreed to CHARSET and was offered ISO-8859-1.
    bool msdp_enabled; // The server may send MSDP; true from creation until the client sends DONT.
    bool mssp_enabled; // The client asked for MSSP and was sent the status message.
    bool atcp_enabled; // The client uses ATCP, which carries the MSDP variables when MSDP is off.
    bool msp_enabled; // The client accepts MSP sound.
    bool mxp_enabled; // MXP is on for this connection.
    bool mccp_enabled; // The client sent DO MCCP; the server does not compress.
    bool eor_enabled; // The client agreed to IAC EOR after each prompt.
    bool sga_enabled; // The client asked the server to suppress go-ahead.
};

// Returns new protocol state for one connection, with nothing negotiated; free it with
// ProtocolDestroy. Every MSDP variable starts reported, and the server's own variables start
// dirty, so the first update sends them even at their default.
protocol_t* ProtocolCreate();

// Frees protocol and everything it owns. protocol must not be null.
void ProtocolDestroy(protocol_t* protocol);

// Starts negotiation by offering TTYPE; the client's answer brings the offers of the other
// options. A repeated call sends nothing while the TTYPE offer stands. descriptor and its protocol
// must not be null.
void ProtocolNegotiate(const descriptor_t* descriptor);

// Acts on the telnet negotiation, subnegotiations and MXP <VERSION> replies in data, and appends
// the rest to out_text with a NUL. out_text must be NUL-terminated and have room for
// MAX_PROTOCOL_BUFFER more characters and the NUL. A sequence split across reads is held for the
// next call. A read with more text or subnegotiation than MAX_PROTOCOL_BUFFER, or too long for an
// int length, is logged, and its text is not appended. A null descriptor or out_text, or empty
// data, does nothing.
void ProtocolInput(const descriptor_t* descriptor, std::string_view data, char* out_text);

// ProtocolInput for the size bytes at data. A null data or a size of 0 or less does nothing.
void ProtocolInput(const descriptor_t* descriptor, const char* data, int size, char* out_text);

// Returns text with its codes applied for descriptor's client, reading text as though a NUL
// followed it. A NUL ends the text, unless it cuts off an [F...] or [B...] colour code or a
// stripped MXP tag, when reading goes on after it. The result views a static buffer and is valid
// until the next call. A result of MAX_OUTPUT_BUFFER characters or more is logged and comes back
// empty. With a null descriptor or protocol, returns text unchanged.
//
// A code is a tab followed by:
//   \t                  a tab.
//   n                   reset colour; sent even when colour is off.
//   r g y b m c w       dark red, green, yellow, blue, magenta, cyan or white.
//   a j l o p t v       dark azure, jade, lime, orange, pink, tan or violet.
//   R G Y B M C W A J L O P T V   the light version of the lowercase colour.
//   [F<rgb>] [B<rgb>]   a foreground or background colour, as for ColourRGB: \t[F010].
//   [U<number>/<text>]  the Unicode character with that decimal number, or for a client without
//                       UTF-8 the text, up to 7 characters: \t[U9814/C].
//   [X<version>]        blocks the next MXP link or tag unless the client's MXP version is at
//                       least version, compared as strings.
//   ( and )             start and end an MXP link that sends the text between them:
//                       \t(north\t).
//   <tag>               an MXP tag, up to its '>': \t<send href="buy pie">pie\t</send>.
//   !                   an MSP "!!" trigger.
// Any other code is dropped, and an unclosed or overlong [...] code is logged and dropped.
// Colours need the ANSI_COLORS variable on. MXP links and tags need MXP on, and are removed
// without it, so every tag must be closed. For a client with MSP or SOUND on, a literal
// "!!SOUND(" in text is sent as "!?SOUND(".
std::string_view ProtocolOutput(const descriptor_t* descriptor, std::string_view text);

// ProtocolOutput for NUL-terminated text, returning the static buffer, which holds a NUL after
// the result. A code may start only before *out_length characters, when that is above 0, but
// reads on to the NUL to finish. *out_length is set to the result's length; out_length must not
// be null. With a null descriptor, protocol or text, returns text and leaves *out_length alone.
const char* ProtocolOutput(const descriptor_t* descriptor, const char* text, int* out_length);

// Sends every reported, dirty variable through MSDPSend and marks each one it sends clean; the
// others stay dirty for a later call. A null descriptor does nothing.
void MSDPUpdate(const descriptor_t* descriptor);

// MSDPUpdate for one variable; an invalid variable does nothing. Call it after setting a variable
// to send it now rather than on the next update.
void MSDPFlush(const descriptor_t* descriptor, variable_t variable);

// Marks every reported variable dirty, so the next MSDPUpdate sends them all. Call it when a
// client's MSDP becomes deliverable, such as when the player turns MSDP on after login, so the
// client gets values that will not change again. A null descriptor does nothing.
void MSDPMarkAllReportedDirty(const descriptor_t* descriptor);

// Sends variable's value over MSDP, or over ATCP for a client without MSDP, and returns whether
// it sent. It sends only for a logged-in character with PRF_MSDP on. It returns false, sending
// nothing, for a null descriptor, an invalid variable, a client with neither protocol, or a
// message too long for MAX_VARIABLE_LENGTH, which is logged. The dirty flag is left alone.
bool MSDPSend(const descriptor_t* descriptor, variable_t variable);

// Sends name and value at once as an MSDP variable, or over ATCP for a client without MSDP, with
// value escaped as by MSDPSanitizeValue. There is no login or PRF_MSDP check, so it works during
// negotiation, and nothing is stored. A null descriptor, or a client with neither protocol, sends
// nothing; so does a message too long for MAX_VARIABLE_LENGTH, which is logged. name and value
// must hold no NUL.
void MSDPSendPair(const descriptor_t* descriptor, std::string_view name, std::string_view value);

// MSDPSendPair for NUL-terminated text; a null name or value sends nothing.
void MSDPSendPair(const descriptor_t* descriptor, const char* name, const char* value);

// MSDPSendPair for a list of values separated by spaces, which MSDP receives as an array.
void MSDPSendList(const descriptor_t* descriptor, std::string_view name, std::string_view value);

// MSDPSendList for NUL-terminated text; a null name or value sends nothing.
void MSDPSendList(const descriptor_t* descriptor, const char* name, const char* value);

// Stores value as a number variable's value, and marks the variable dirty if the value changed;
// an unchanged value stays clean. It never allocates. A null descriptor, an invalid variable or a
// string variable is ignored. It suits any integral value, such as a bool or an enum.
void MSDPSetNumber(const descriptor_t* descriptor, variable_t variable, int value);

// Stores value, escaped as by MSDPSanitizeValue, as a string variable's value, and marks the
// variable dirty if the value changed. Storing the value already held allocates nothing and
// stays clean; a changed value is written into the stored string, which allocates only when it
// outgrows its capacity, so value must not point into it. There is no login or PRF_MSDP check:
// it only stores, and MSDPSend sends. A null value, a null descriptor, an invalid variable or a
// number variable changes nothing.
void MSDPSetString(const descriptor_t* descriptor, variable_t variable, const char* value);

// MSDPSetString for a value that need not be NUL-terminated, such as a message without its line
// break. value must hold no NUL.
void MSDPSetString(const descriptor_t* descriptor, variable_t variable, std::string_view value);

// Returns value with '"', '\' and control characters such as \n and \r replaced by their JSON
// escapes, so clients that turn MSDP into JSON get a valid string. MSDPSetString and
// MSDPSendPair apply it themselves. Call it for free text in a table or array value, since
// MSDPSetTable and MSDPSetArray store their value as it is.
std::string MSDPSanitizeValue(std::string_view value);

// MSDPSanitizeValue for NUL-terminated text; a null value gives an empty string.
std::string MSDPSanitizeValue(const char* value);

// Appends the MSDPSanitizeValue text for value to out_text, so a caller building a table in a
// reused string needs no copy of its own. value must not point into out_text.
void MSDPAppendSanitized(std::string& out_text, std::string_view value);

// MSDPAppendSanitized for NUL-terminated text; a null value appends nothing.
void MSDPAppendSanitized(std::string& out_text, const char* value);

// Stores value as a string variable's MSDP table, between MSDP_TABLE_OPEN and MSDP_TABLE_CLOSE,
// and marks the variable dirty if it changed; an unchanged value allocates nothing and stays
// clean, and a changed one is written in place as by MSDPSetString. value is stored as it is: the
// caller puts MSDP_VAR before each name and MSDP_VAL before each value. An empty value is stored
// empty, without the markers. A null descriptor, an invalid variable or a number variable is
// ignored.
void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, std::string_view value);

// MSDPSetTable for a NUL-terminated value; a null value is ignored.
void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, const char* value);

// MSDPSetTable for an MSDP array, between MSDP_ARRAY_OPEN and MSDP_ARRAY_CLOSE: the caller puts
// MSDP_VAL before each element.
void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, std::string_view value);

// MSDPSetArray for a NUL-terminated value; a null value is ignored.
void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, const char* value);

// Whether variable is an MSDP variable: above eMSDP_NONE and below eMSDP_MAX.
bool MSDPIsValidVariable(variable_t variable);

// Sends tag, such as "<VERSION>", on a secure MXP line when the client has MXP on. A tag of 1000
// characters or more is dropped. When MXP is off and renegotiation is pending (see
// protocol_t::renegotiate), it offers every option except TTYPE again and sends <VERSION> once
// MXP comes on; tag itself is not sent. A null descriptor does nothing. tag must hold no NUL.
void MXPSendTag(const descriptor_t* descriptor, std::string_view tag);

// MXPSendTag for a NUL-terminated tag; a null tag does nothing.
void MXPSendTag(const descriptor_t* descriptor, const char* tag);

// Returns the escape code for an RGB colour code such as "F500": 'F' for foreground or 'B' for
// background, in either case, then red, green and blue digits from 0 to 5; characters after the
// fourth are ignored. With the XTERM_256_COLORS variable on, the result is an xterm colour that
// views a static buffer and is valid until the next call; otherwise it is the nearest ANSI colour.
// An invalid code gives the code that clears colour. When ANSI colour is off, or descriptor is
// null, it returns an empty view. To put colours in text, use ProtocolOutput.
std::string_view ColourRGB(const descriptor_t* descriptor, std::string_view rgb_code);

// ColourRGB for a NUL-terminated code; a null code is an invalid one.
std::string_view ColourRGB(const descriptor_t* descriptor, const char* rgb_code);

// Returns the UTF-8 encoding of code_point, which views a static buffer and is valid until the
// next call; the buffer holds a NUL after it. It ends at the first NUL byte, so 0, a value of
// 0x200000 or more, and a negative value whose low byte is 0 give an empty view.
std::string_view UnicodeGet(int code_point);

// Writes the UTF-8 encoding of code_point at *out_cursor, without a NUL, and moves the cursor
// past it: 1 to 4 bytes, which the caller must have room for. A value of 0x200000 or more writes
// nothing.
void UnicodeAdd(char** out_cursor, int code_point);

#endif // PROTOCOL_H
