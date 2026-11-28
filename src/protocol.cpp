// Protocol snippet by KaVir.  Released into the public domain in February 2011.
//
// In case this is not legally possible:
//
// The copyright holder grants any entity the right to use this work for any
// purpose, without any conditions, unless such conditions are required by law.

#include <arpa/telnet.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "protocol.h"

// The telnet command bytes from <arpa/telnet.h>, as the char values the input and output buffers
// hold.
constexpr char TELNET_IAC = static_cast<char>(IAC);
constexpr char TELNET_SB = static_cast<char>(SB);
constexpr char TELNET_SE = static_cast<char>(SE);
constexpr char TELNET_WILL = static_cast<char>(WILL);
constexpr char TELNET_WONT = static_cast<char>(WONT);
constexpr char TELNET_DO = static_cast<char>(DO);
constexpr char TELNET_DONT = static_cast<char>(DONT);

// The byte that starts an ANSI escape sequence, including the MXP line tags.
constexpr char ANSI_ESCAPE = '\033';

// The size of the stack buffers that hold an MXP tag, and the longest tag they accept with room
// for its framing.
constexpr int MXP_BUFFER_SIZE = 1024;
constexpr int MAX_MXP_TAG_LENGTH = 1000;

// The bytes allowed for the telnet and MSDP framing around an MSDP variable's name and value.
constexpr std::size_t MSDP_FRAMING_LENGTH = 12;

// The length of an RGB colour code such as "F500": the layer letter, then three digits.
constexpr int RGB_COLOUR_CODE_LENGTH = 4;

// The printable ASCII range kept from client-supplied names and values.
constexpr unsigned char FIRST_PRINTABLE_ASCII = 0x20;
constexpr unsigned char LAST_PRINTABLE_ASCII = 0x7e;

// The ASCII tests below give the C locale's answers and are defined for every char value,
// including the bytes from 0x80 that are negative where char is signed.

// Whether character is an ASCII decimal digit.
static constexpr bool is_ascii_digit(char character)
{
    return character >= '0' && character <= '9';
}

// Whether character is an ASCII letter.
static constexpr bool is_ascii_alpha(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

// The lower-case form of character if it is an ASCII capital letter; otherwise character.
static constexpr char ascii_lower(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

// An MSDP or ATCP pair has two parts, the variable and the value, indexed by MSDP_VAR - 1 and
// MSDP_VAL - 1.
constexpr int MSDP_PAIR_PARTS = 2;
static_assert(MSDP_VAR - 1 < MSDP_PAIR_PARTS && MSDP_VAL - 1 < MSDP_PAIR_PARTS,
    "An MSDP pair part's index is outside the parsed pair.");

#include "comm.h"

// Writes text to descriptor's socket, which must not be null, and raises a positive write_oob to 2.
static void send_to_client(const descriptor_t* descriptor, const char* text)
{
    if (descriptor != nullptr) {
        if (descriptor->pProtocol->write_oob > 0) {
            descriptor->pProtocol->write_oob = 2;
        }
    }
    write_to_descriptor(descriptor->descriptor, text);
}

// Logs text to the server log as it is, never as a printf format.
static void report_bug(const char* text) { vmudlog(NRM, "%s", text); }

// Sends text to descriptor after an "[INFO]" label. The label's colour codes are ProtocolOutput
// codes, and they reach the client untranslated.
static void send_info_message(const descriptor_t* descriptor, const char* text)
{
    send_to_client(descriptor, "\t[F210][\toINFO\t[F210]]\tn ");
    send_to_client(descriptor, text);
}

// The protocol state of descriptor, or nullptr for a null descriptor.
static protocol_t* protocol_of(const descriptor_t* descriptor)
{
    if (descriptor == nullptr) {
        return nullptr;
    }
    return descriptor->pProtocol;
}

// The default layouts of the GUI variables, an MSDP extension for clients that offer a generic
// GUI. Each is an MSDP array: a button's label and command, or a gauge's label, colour and the
// MSDP variables for its value and maximum.
constexpr std::string_view GUI_BUTTON_1 = "\005\002Help\002help\006";
constexpr std::string_view GUI_BUTTON_2 = "\005\002Look\002look\006";
constexpr std::string_view GUI_BUTTON_3 = "\005\002Score\002score\006";
constexpr std::string_view GUI_BUTTON_4 = "\005\002Equipment\002equipment\006";
constexpr std::string_view GUI_BUTTON_5 = "\005\002Inventory\002inventory\006";

constexpr std::string_view GUI_GAUGE_1 = "\005\002Health\002red\002HEALTH\002HEALTH_MAX\006";
constexpr std::string_view GUI_GAUGE_2 = "\005\002Mana\002blue\002MANA\002MANA_MAX\006";
constexpr std::string_view GUI_GAUGE_3
    = "\005\002Movement\002green\002MOVEMENT\002MOVEMENT_MAX\006";
constexpr std::string_view GUI_GAUGE_4
    = "\005\002Exp TNL\002yellow\002EXPERIENCE\002EXPERIENCE_MAX\006";
constexpr std::string_view GUI_GAUGE_5
    = "\005\002Opponent\002darkred\002OPPONENT_HEALTH\002OPPONENT_HEALTH_MAX\006";

// VARIABLE_NAME_TABLE entries for each kind of variable.
#define NUMBER_READ_ONLY(variable, name) \
    { name, std::nullopt, variable, 0, -1, 0, false, false, false, false }
#define NUMBER_READ_ONLY_SET_TO(variable, name, default_value) \
    { name, std::nullopt, variable, 0, -1, default_value, false, false, false, false }
#define STRING_READ_ONLY(variable, name) \
    { name, std::nullopt, variable, 0, -1, 0, true, false, false, false }
#define BOOLEAN_SET_TO(variable, name, default_value) \
    { name, std::nullopt, variable, 0, 1, default_value, false, true, false, false }
#define STRING_WITH_LENGTH_OF(variable, name, min_length, max_length) \
    { name, std::nullopt, variable, min_length, max_length, 0, true, true, false, false }
#define STRING_WRITE_ONCE(variable, name, min_length, max_length) \
    { name, std::nullopt, variable, min_length, max_length, 0, true, true, true, false }
#define STRING_GUI(variable, name, default_text) \
    { name, default_text, variable, 0, -1, 0, true, false, false, true }

// Each variable's description, at its variable_t index; ProtocolCreate checks the order.
static constexpr variable_name_t VARIABLE_NAME_TABLE[eMSDP_MAX + 1] = {
    // General
    STRING_READ_ONLY(eMSDP_CHARACTER_NAME, "CHARACTER_NAME"),
    STRING_READ_ONLY(eMSDP_SERVER_ID, "SERVER_ID"),
    NUMBER_READ_ONLY(eMSDP_SERVER_TIME, "SERVER_TIME"),
    NUMBER_READ_ONLY_SET_TO(eMSDP_SNIPPET_VERSION, "SNIPPET_VERSION", SNIPPET_VERSION),

    // Character
    STRING_READ_ONLY(eMSDP_AFFECTS, "AFFECTS"),
    NUMBER_READ_ONLY(eMSDP_ALIGNMENT, "ALIGNMENT"),
    NUMBER_READ_ONLY(eMSDP_EXPERIENCE, "EXPERIENCE"),
    NUMBER_READ_ONLY(eMSDP_EXPERIENCE_MAX, "EXPERIENCE_MAX"),
    NUMBER_READ_ONLY(eMSDP_HEALTH, "HEALTH"),
    NUMBER_READ_ONLY(eMSDP_HEALTH_MAX, "HEALTH_MAX"),
    NUMBER_READ_ONLY(eMSDP_LEVEL, "LEVEL"),
    STRING_READ_ONLY(eMSDP_RACE, "RACE"),
    STRING_READ_ONLY(eMSDP_CLASS, "CLASS"),
    NUMBER_READ_ONLY(eMSDP_MANA, "MANA"),
    NUMBER_READ_ONLY(eMSDP_MANA_MAX, "MANA_MAX"),
    NUMBER_READ_ONLY(eMSDP_WIMPY, "WIMPY"),
    NUMBER_READ_ONLY(eMSDP_PRACTICE, "PRACTICE"),
    NUMBER_READ_ONLY(eMSDP_MONEY, "MONEY"),
    NUMBER_READ_ONLY(eMSDP_MOVEMENT, "MOVEMENT"),
    NUMBER_READ_ONLY(eMSDP_MOVEMENT_MAX, "MOVEMENT_MAX"),
    NUMBER_READ_ONLY(eMSDP_HITROLL, "HITROLL"),
    NUMBER_READ_ONLY(eMSDP_DAMROLL, "DAMROLL"),
    NUMBER_READ_ONLY(eMSDP_SPIRIT, "SPIRIT"),
    NUMBER_READ_ONLY(eMSDP_AC, "AC"),
    NUMBER_READ_ONLY(eMSDP_STR, "STR"),
    NUMBER_READ_ONLY(eMSDP_INT, "INT"),
    NUMBER_READ_ONLY(eMSDP_WILL, "WILL"),
    NUMBER_READ_ONLY(eMSDP_DEX, "DEX"),
    NUMBER_READ_ONLY(eMSDP_CON, "CON"),
    NUMBER_READ_ONLY(eMSDP_LEA, "LEA"),
    NUMBER_READ_ONLY(eMSDP_STR_PERM, "STR_PERM"),
    NUMBER_READ_ONLY(eMSDP_INT_PERM, "INT_PERM"),
    NUMBER_READ_ONLY(eMSDP_WIL_PERM, "WIL_PERM"),
    NUMBER_READ_ONLY(eMSDP_DEX_PERM, "DEX_PERM"),
    NUMBER_READ_ONLY(eMSDP_CON_PERM, "CON_PERM"),
    NUMBER_READ_ONLY(eMSDP_LEA_PERM, "LEA_PERM"),
    NUMBER_READ_ONLY(eMSDP_SPELL_SAVE, "SPELL_SAVE"),
    NUMBER_READ_ONLY(eMSDP_SPELL_PEN, "SPELL_PEN"),
    NUMBER_READ_ONLY(eMSDP_SPELL_POWER, "SPELL_POWER"),
    NUMBER_READ_ONLY(eMSDP_ARMOUR_ABS, "ARMOUR_ABS"),
    NUMBER_READ_ONLY(eMSDP_OFFENSIVE_BONUS, "OFFENSIVE_BONUS"),
    NUMBER_READ_ONLY(eMSDP_PARRY, "PARRY"),
    NUMBER_READ_ONLY(eMSDP_DODGE, "DODGE"),
    NUMBER_READ_ONLY(eMSDP_ATTACK_SPEED, "ATTACK_SPEED"),
    STRING_READ_ONLY(eMSDP_TACTIC, "TACTIC"),
    STRING_READ_ONLY(eMSDP_SPECIALIZATION, "SPECIALIZATION"),
    NUMBER_READ_ONLY(eMSDP_PERCEPTION, "PERCEPTION"),
    NUMBER_READ_ONLY(eMSDP_WILLPOWER, "WILLPOWER"),
    NUMBER_READ_ONLY(eMSDP_SKILL_ENCUMBRANCE, "SKILL_ENCUMBRANCE"),
    NUMBER_READ_ONLY(eMSDP_MOVEMENT_ENCUMBRANCE, "MOVEMENT_ENCUMBRANCE"),
    NUMBER_READ_ONLY(eMSDP_CARRIED_WEIGHT, "CARRIED_WEIGHT"),
    NUMBER_READ_ONLY(eMSDP_WARRIOR_LEVEL, "WARRIOR_LEVEL"),
    NUMBER_READ_ONLY(eMSDP_WARRIOR_LEVEL_MAX, "WARRIOR_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMSDP_RANGER_LEVEL, "RANGER_LEVEL"),
    NUMBER_READ_ONLY(eMSDP_RANGER_LEVEL_MAX, "RANGER_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMSDP_MYSTIC_LEVEL, "MYSTIC_LEVEL"),
    NUMBER_READ_ONLY(eMSDP_MYSTIC_LEVEL_MAX, "MYSTIC_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMSDP_MAGE_LEVEL, "MAGE_LEVEL"),
    NUMBER_READ_ONLY(eMSDP_MAGE_LEVEL_MAX, "MAGE_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMSDP_HEALTH_REGENERATION, "HEALTH_REGENERATION"),
    NUMBER_READ_ONLY(eMSDP_STAMINA_REGENERATION, "STAMINA_REGENERATION"),
    NUMBER_READ_ONLY(eMSDP_MOVEMENT_REGENERATION, "MOVEMENT_REGENERATION"),

    // Combat
    NUMBER_READ_ONLY(eMSDP_OPPONENT_HEALTH, "OPPONENT_HEALTH"),
    NUMBER_READ_ONLY(eMSDP_OPPONENT_HEALTH_MAX, "OPPONENT_HEALTH_MAX"),
    STRING_READ_ONLY(eMSDP_OPPONENT_LEVEL, "OPPONENT_LEVEL"),
    STRING_READ_ONLY(eMSDP_OPPONENT_NAME, "OPPONENT_NAME"),
    STRING_READ_ONLY(eMSDP_OPPONENT_AGE, "OPPONENT_AGE"),

    // World
    STRING_READ_ONLY(eMSDP_AREA_NAME, "AREA_NAME"),
    STRING_READ_ONLY(eMSDP_ROOM, "ROOM"),
    STRING_READ_ONLY(eMSDP_ROOM_EXITS, "ROOM_EXITS"),
    STRING_READ_ONLY(eMSDP_ROOM_NAME, "ROOM_NAME"),
    NUMBER_READ_ONLY(eMSDP_ROOM_VNUM, "ROOM_VNUM"),
    STRING_READ_ONLY(eMSDP_WORLD_TIME, "WORLD_TIME"),
    STRING_READ_ONLY(eMSDP_WEATHER, "WEATHER"),

    // Configurable variables
    STRING_WRITE_ONCE(eMSDP_CLIENT_ID, "CLIENT_ID", 1, 40),
    STRING_WRITE_ONCE(eMSDP_CLIENT_VERSION, "CLIENT_VERSION", 1, 40),
    STRING_WITH_LENGTH_OF(eMSDP_PLUGIN_ID, "PLUGIN_ID", 1, 40),
    BOOLEAN_SET_TO(eMSDP_ANSI_COLORS, "ANSI_COLORS", 1),
    BOOLEAN_SET_TO(eMSDP_XTERM_256_COLORS, "XTERM_256_COLORS", 0),
    BOOLEAN_SET_TO(eMSDP_UTF_8, "UTF_8", 0),
    BOOLEAN_SET_TO(eMSDP_SOUND, "SOUND", 0),
    BOOLEAN_SET_TO(eMSDP_MXP, "MXP", 0),

    // GUI variables
    STRING_GUI(eMSDP_BUTTON_1, "BUTTON_1", GUI_BUTTON_1),
    STRING_GUI(eMSDP_BUTTON_2, "BUTTON_2", GUI_BUTTON_2),
    STRING_GUI(eMSDP_BUTTON_3, "BUTTON_3", GUI_BUTTON_3),
    STRING_GUI(eMSDP_BUTTON_4, "BUTTON_4", GUI_BUTTON_4),
    STRING_GUI(eMSDP_BUTTON_5, "BUTTON_5", GUI_BUTTON_5),
    STRING_GUI(eMSDP_GAUGE_1, "GAUGE_1", GUI_GAUGE_1),
    STRING_GUI(eMSDP_GAUGE_2, "GAUGE_2", GUI_GAUGE_2),
    STRING_GUI(eMSDP_GAUGE_3, "GAUGE_3", GUI_GAUGE_3),
    STRING_GUI(eMSDP_GAUGE_4, "GAUGE_4", GUI_GAUGE_4),
    STRING_GUI(eMSDP_GAUGE_5, "GAUGE_5", GUI_GAUGE_5),

    // Group information
    STRING_READ_ONLY(eMSDP_GROUP, "GROUP"),

    // Fills the eMSDP_MAX slot; no variable has it.
    NUMBER_READ_ONLY(eMSDP_MAX, "")
};

// The most characters a client may store in a configurable string variable.
constexpr int MAX_CONFIGURABLE_STRING_LENGTH = 40;

// The largest maximum of any configurable string variable in VARIABLE_NAME_TABLE.
static constexpr int longest_configurable_string()
{
    int longest = 0;
    for (const variable_name_t& entry : VARIABLE_NAME_TABLE) {
        if (entry.configurable && entry.is_string && entry.maximum > longest) {
            longest = entry.maximum;
        }
    }
    return longest;
}

static_assert(longest_configurable_string() <= MAX_CONFIGURABLE_STRING_LENGTH,
    "A configurable string variable is longer than set_configurable_text's buffer.");

// The length of a space-separated list of every name in VARIABLE_NAME_TABLE, with its NUL.
static constexpr std::size_t all_variable_names_length()
{
    std::size_t length = 0;
    for (int index = eMSDP_NONE + 1; index < eMSDP_MAX; ++index) {
        length += VARIABLE_NAME_TABLE[index].name.size() + 1;
    }
    return length;
}

static_assert(all_variable_names_length() <= MAX_OUTPUT_BUFFER,
    "send_variable_list's variable lists do not fit in MAX_OUTPUT_BUFFER.");

// Appends name to the space-separated list of list_length characters in out_list, which has room,
// and returns the list's new length. Writes no NUL.
static std::size_t append_list_name(char* out_list, std::size_t list_length, std::string_view name)
{
    if (list_length != 0) {
        out_list[list_length] = ' ';
        ++list_length;
    }
    std::memcpy(out_list + list_length, name.data(), name.size());
    return list_length + name.size();
}

// text's length as the precision a %.*s conversion takes, which is an int. A length past the
// largest int is cut to it, so the cast never narrows.
static int printf_precision(std::string_view text)
{
    constexpr std::size_t largest_precision = std::numeric_limits<int>::max();
    return static_cast<int>(std::min(text.size(), largest_precision));
}

static void negotiate(const descriptor_t* descriptor);
static void perform_handshake(const descriptor_t* descriptor, char command, char option);
static void perform_subnegotiation(
    const descriptor_t* descriptor, char option, const char* data, int size);
static void send_negotiation_sequence(const descriptor_t* descriptor, char command, char option);
static bool confirm_negotiation(
    const descriptor_t* descriptor, negotiated_t option, bool will_do, bool send_reply);

static void parse_msdp(const descriptor_t* descriptor, const char* data);
static void execute_msdp_pair(
    const descriptor_t* descriptor, const char* name, const char* value);

static void parse_atcp(const descriptor_t* descriptor, const char* data);

static void send_mssp(const descriptor_t* descriptor);

static char* find_mxp_tag_value(const char* tag, const char* text);

static const char* get_ansi_colour(bool background, int red, int green, int blue);
static const char* get_xterm_colour(bool background, int red, int green, int blue);
static bool is_valid_colour(std::string_view rgb_code);

static bool equals_ignoring_case(std::string_view first, std::string_view second);
static bool starts_with_ignoring_case(const char* prefix, const char* text);
static bool is_all_digits(const char* text);
static std::size_t msdp_sanitized_length(std::string_view value);
static char* msdp_write_sanitized(std::string_view value, char* out_text);

// Resets every colour attribute.
static const char ANSI_CLEAR[] = "\033[0;00m";

// The eight foreground colours at normal intensity.
static const char ANSI_DARK_BLACK[] = "\033[0;30m";
static const char ANSI_DARK_RED[] = "\033[0;31m";
static const char ANSI_DARK_GREEN[] = "\033[0;32m";
static const char ANSI_DARK_YELLOW[] = "\033[0;33m";
static const char ANSI_DARK_BLUE[] = "\033[0;34m";
static const char ANSI_DARK_MAGENTA[] = "\033[0;35m";
static const char ANSI_DARK_CYAN[] = "\033[0;36m";
static const char ANSI_DARK_WHITE[] = "\033[0;37m";

// The bold, brighter foreground colours; bold black shows as grey.
static const char ANSI_BOLD_BLACK[] = "\033[1;30m";
static const char ANSI_BOLD_RED[] = "\033[1;31m";
static const char ANSI_BOLD_GREEN[] = "\033[1;32m";
static const char ANSI_BOLD_YELLOW[] = "\033[1;33m";
static const char ANSI_BOLD_BLUE[] = "\033[1;34m";
static const char ANSI_BOLD_MAGENTA[] = "\033[1;35m";
static const char ANSI_BOLD_CYAN[] = "\033[1;36m";
static const char ANSI_BOLD_WHITE[] = "\033[1;37m";

// The eight background colours, which also make the foreground bold.
static const char ANSI_BACKGROUND_BLACK[] = "\033[1;40m";
static const char ANSI_BACKGROUND_RED[] = "\033[1;41m";
static const char ANSI_BACKGROUND_GREEN[] = "\033[1;42m";
static const char ANSI_BACKGROUND_YELLOW[] = "\033[1;43m";
static const char ANSI_BACKGROUND_BLUE[] = "\033[1;44m";
static const char ANSI_BACKGROUND_MAGENTA[] = "\033[1;45m";
static const char ANSI_BACKGROUND_CYAN[] = "\033[1;46m";
static const char ANSI_BACKGROUND_WHITE[] = "\033[1;47m";

protocol_t* ProtocolCreate()
{
    protocol_t* protocol;

    // Check once that VARIABLE_NAME_TABLE is in variable_t order.
    static bool table_checked = false;
    if (!table_checked) {
        table_checked = true;
        for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
            if (VARIABLE_NAME_TABLE[variable_index].variable != variable_index) {
                report_bug("MSDP: Variable table does not match the enums in the header.");
                break;
            }
        }
    }

    protocol = new protocol_t;
    protocol->write_oob = 0;
    protocol->option_states.fill(false);
    protocol->in_subnegotiation = false;
    protocol->negotiated = false;
    protocol->renegotiate = false;
    protocol->needs_mxp_version = false;
    protocol->mxp_blocked = false;
    protocol->pending_input_length = 0;
    protocol->pending_input.fill('\0');
    protocol->subnegotiation_input_length = 0;
    protocol->subnegotiation_input.fill('\0');
    protocol->ttype_enabled = false;
    protocol->echo_enabled = false;
    protocol->naws_enabled = false;
    protocol->charset_enabled = false;
    protocol->msdp_enabled = true;
    protocol->mssp_enabled = false;
    protocol->atcp_enabled = false;
    protocol->msp_enabled = false;
    protocol->mxp_enabled = false;
    protocol->mccp_enabled = false;
    protocol->eor_enabled = false;
    protocol->sga_enabled = false;
    protocol->xterm_256_support = eUNKNOWN;
    protocol->screen_width = 0;
    protocol->screen_height = 0;
    protocol->mxp_version = "Unknown";
    protocol->last_ttype = std::nullopt;

    return protocol;
}

void ProtocolDestroy(protocol_t* protocol) { delete protocol; }

// The size of a struct with no padding between its members: their sizes added, rounded up to the
// struct's alignment. The structs below assert it, so their fields stay in size order.
static constexpr std::size_t unpadded_size(std::size_t members_size, std::size_t alignment)
{
    return (members_size + alignment - 1) / alignment * alignment;
}

msdp_variables_t::msdp_variables_t()
{
    // Here rather than after the class, since it reads the private fields.
    static_assert(sizeof(msdp_variables_t)
            == unpadded_size(sizeof(m_values) + sizeof(m_reported) + sizeof(m_dirty),
                alignof(msdp_variables_t)),
        "msdp_variables_t has padding between its fields.");

    m_reported.set();
    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        const variable_name_t& description = VARIABLE_NAME_TABLE[variable_index];
        // Server-reported variables start dirty so the first update sends them even at their
        // default, since a client takes a stat it is never sent, such as a spell power of 0, as
        // unsupported. Client-set and GUI variables start clean.
        m_dirty[variable_index] = !description.configurable && !description.gui;

        msdp_value_t& value = m_values[variable_index];
        if (description.is_string) {
            std::string_view initial_text = "";
            if (description.default_text.has_value()) {
                initial_text = *description.default_text;
            } else if (description.configurable) {
                initial_text = "Unknown";
            }
            value.emplace<std::string>(initial_text);
        } else {
            value.emplace<int>(description.default_number);
        }
    }
}

const msdp_value_t& msdp_variables_t::value(variable_t variable) const
{
    return m_values[variable];
}

const std::string& msdp_variables_t::text(variable_t variable) const
{
    return std::get<std::string>(m_values[variable]);
}

int msdp_variables_t::number(variable_t variable) const
{
    return std::get<int>(m_values[variable]);
}

void msdp_variables_t::set_number(variable_t variable, int value)
{
    int& stored = stored_number(variable);
    if (stored != value) {
        stored = value;
        m_dirty[variable] = true;
    }
}

std::string& msdp_variables_t::text_to_change(variable_t variable)
{
    m_dirty[variable] = true;
    return stored_text(variable);
}

int& msdp_variables_t::stored_number(variable_t variable)
{
    return std::get<int>(m_values[variable]);
}

std::string& msdp_variables_t::stored_text(variable_t variable)
{
    return std::get<std::string>(m_values[variable]);
}

bool msdp_variables_t::is_reported(variable_t variable) const { return m_reported[variable]; }

bool msdp_variables_t::is_dirty(variable_t variable) const { return m_dirty[variable]; }

void msdp_variables_t::set_reported(variable_t variable, bool reported)
{
    m_reported[variable] = reported;
}

void msdp_variables_t::mark_dirty(variable_t variable) { m_dirty[variable] = true; }

void msdp_variables_t::mark_clean(variable_t variable) { m_dirty[variable] = false; }

void msdp_variables_t::report(variable_t variable)
{
    m_reported[variable] = true;
    m_dirty[variable] = true;
}

void msdp_variables_t::unreport(variable_t variable)
{
    m_reported[variable] = false;
    m_dirty[variable] = false;
}

void msdp_variables_t::unreport_all()
{
    m_dirty &= ~m_reported;
    m_reported.reset();
}

void msdp_variables_t::mark_reported_dirty() { m_dirty |= m_reported; }

// What one step of ProtocolInput's scan tells its loop to do next.
enum class input_step {
    next_byte, // Go on to the next byte.
    stop, // Stop reading; any unread bytes are held for the next read.
    abandon, // A buffer filled: drop everything this read gathered.
};

// One ProtocolInput call's place in the client's bytes and the text gathered so far.
struct input_scan {
    const descriptor_t* descriptor; // The connection whose bytes are read.
    protocol_t* protocol; // The descriptor's protocol state, never null.
    const char* data; // The bytes held from the last read, then this read's.
    char* command_text; // The text passed on to the caller, MAX_PROTOCOL_BUFFER + 1 chars.
    char* subnegotiation; // The protocol's subnegotiation_input buffer.
    int size; // How many bytes data holds.
    int position; // The byte being read.
    int command_length; // The chars in command_text.
    int subnegotiation_length; // The chars in subnegotiation.
};

static_assert(sizeof(input_scan)
        == unpadded_size(sizeof(input_scan::descriptor) + sizeof(input_scan::protocol)
                + sizeof(input_scan::data) + sizeof(input_scan::command_text)
                + sizeof(input_scan::subnegotiation) + sizeof(input_scan::size)
                + sizeof(input_scan::position) + sizeof(input_scan::command_length)
                + sizeof(input_scan::subnegotiation_length),
            alignof(input_scan)),
    "input_scan has padding between its fields.");

// Whether the scan has more than count bytes left from its position.
static bool has_bytes_after(const input_scan& scan, int count)
{
    return (scan.position + count) < scan.size;
}

// Keeps count bytes from the scan's position for the next read, and stops the scan. count is at
// most the size of pending_input.
static input_step hold_for_next_read(input_scan& scan, int count)
{
    std::memcpy(scan.protocol->pending_input.data(), scan.data + scan.position, count);
    scan.protocol->pending_input_length = count;
    return input_step::stop;
}

// Whether the text or the subnegotiation buffer is already full.
static bool is_input_buffer_full(const input_scan& scan)
{
    return scan.command_length >= MAX_PROTOCOL_BUFFER
        || scan.subnegotiation_length >= MAX_PROTOCOL_BUFFER;
}

// Logs a read too long for a buffer so that its text is dropped, and abandons a subnegotiation
// that filled its buffer.
static input_step abandon_full_read(const input_scan& scan)
{
    report_bug("ProtocolInput: Too much incoming data to store in the buffer.");
    if (scan.subnegotiation_length >= MAX_PROTOCOL_BUFFER) {
        scan.protocol->in_subnegotiation = false;
        scan.protocol->subnegotiation_input_length = 0;
        scan.protocol->subnegotiation_input[0] = '\0';
    }
    return input_step::abandon;
}

// Whether IAC IAC, a literal 255 byte, starts at the scan's position.
static bool is_doubled_iac_at(const input_scan& scan)
{
    return scan.data[scan.position] == TELNET_IAC && has_bytes_after(scan, 1)
        && scan.data[scan.position + 1] == TELNET_IAC;
}

// Adds the 255 byte of an IAC IAC to the subnegotiation or the text, and moves onto the second IAC.
static input_step append_literal_iac(input_scan& scan)
{
    if (scan.protocol->in_subnegotiation) {
        scan.subnegotiation[scan.subnegotiation_length++] = TELNET_IAC;
    } else {
        scan.command_text[scan.command_length++] = TELNET_IAC;
    }
    ++scan.position;
    return input_step::next_byte;
}

// Ends the subnegotiation at the IAC SE at the scan's position, acting on it when it holds at
// least an option and one byte, and moves onto the SE.
static void end_subnegotiation(input_scan& scan)
{
    ++scan.position;
    scan.protocol->in_subnegotiation = false;
    scan.subnegotiation[scan.subnegotiation_length] = '\0';
    if (scan.subnegotiation_length >= 2) {
        perform_subnegotiation(scan.descriptor, scan.subnegotiation[0], &scan.subnegotiation[1],
            scan.subnegotiation_length - 1);
    }
    scan.subnegotiation_length = 0;
    scan.protocol->subnegotiation_input_length = 0;
}

// Reads a byte inside a subnegotiation: IAC SE ends it, a trailing IAC is held since it may start
// IAC SE, and any other byte is added to it.
static input_step read_subnegotiation_byte(input_scan& scan)
{
    if (scan.data[scan.position] == TELNET_IAC) {
        if (!has_bytes_after(scan, 1)) {
            return hold_for_next_read(scan, 1);
        }
        if (scan.data[scan.position + 1] == TELNET_SE) {
            end_subnegotiation(scan);
            return input_step::next_byte;
        }
    }
    scan.subnegotiation[scan.subnegotiation_length++] = scan.data[scan.position];
    return input_step::next_byte;
}

// Whether the scan's position holds an ESC with fewer than three bytes after it.
static bool is_escape_in_last_three_bytes(const input_scan& scan)
{
    return scan.data[scan.position] == ANSI_ESCAPE && !has_bytes_after(scan, 3);
}

// Holds an ESC near the end of the bytes that may start an MXP line tag split across reads, with
// the bytes after it. An ESC that cannot start one is dropped.
static input_step read_escape_near_end(input_scan& scan)
{
    const char* escape = scan.data + scan.position;
    const int remaining = scan.size - scan.position;
    const bool may_start_mxp_line_tag = remaining == 1 || (remaining >= 2 && escape[1] == '[')
        || (remaining >= 3 && escape[1] == '[' && is_ascii_digit(escape[2]));
    if (may_start_mxp_line_tag) {
        return hold_for_next_read(scan, remaining);
    }
    return input_step::next_byte;
}

// Whether an MXP line tag, ESC [ <digit> z, starts at position in data of size bytes.
static bool is_mxp_line_tag_at(const char* data, int position, int size)
{
    const bool has_three_more = (position + 3) < size;
    return data[position] == ANSI_ESCAPE && has_three_more && data[position + 1] == '['
        && is_ascii_digit(data[position + 2]) && data[position + 3] == 'z';
}

// Records that the client supports XTerm 256 colours.
static void enable_xterm_256(protocol_t* protocol)
{
    protocol->variables.stored_number(eMSDP_XTERM_256_COLORS) = 1;
    protocol->xterm_256_support = eYES;
}

// Records 256-colour support for a client whose first version with it is first_version; versions
// compare as strings.
static void apply_minimum_256_colour_version(
    protocol_t* protocol, const char* version, const char* first_version)
{
    if (std::strcmp(version, first_version) >= 0) {
        enable_xterm_256(protocol);
    } else {
        protocol->xterm_256_support = eNO;
    }
}

// Acts on an MXP <VERSION> reply's tag text: records its CLIENT=, VERSION= and MXP= values, the
// 256-colour support they show, and tells the player the MXP version whenever one is known.
static void apply_mxp_version_reply(
    const descriptor_t* descriptor, protocol_t* protocol, const char* tag_text)
{
    char* tag_value = find_mxp_tag_value("CLIENT=", tag_text);
    if (tag_value != nullptr) {
        // Replace any earlier client name: an MXP <VERSION> reply is harder to fake.
        protocol->variables.stored_text(eMSDP_CLIENT_ID).assign(tag_value);
    }

    tag_value = find_mxp_tag_value("VERSION=", tag_text);
    if (tag_value != nullptr) {
        const std::string& client_name = protocol->variables.text(eMSDP_CLIENT_ID);

        protocol->variables.stored_text(eMSDP_CLIENT_VERSION).assign(tag_value);

        if (equals_ignoring_case("MUSHCLIENT", client_name)) {
            apply_minimum_256_colour_version(protocol, tag_value, "4.02");
        } else if (equals_ignoring_case("CMUD", client_name)) {
            apply_minimum_256_colour_version(protocol, tag_value, "3.04");
        } else if (equals_ignoring_case("ATLANTIS", client_name)) {
            // Every Atlantis that answers <VERSION> supports 256 colours.
            enable_xterm_256(protocol);
        }
    }

    tag_value = find_mxp_tag_value("MXP=", tag_text);
    if (tag_value != nullptr) {
        protocol->mxp_version.assign(tag_value);
    }

    if (protocol->mxp_version != "Unknown") {
        char message[MXP_BUFFER_SIZE];
        send_to_client(descriptor, "\n");
        std::snprintf(message, sizeof(message), "MXP version %.*s detected and enabled.\r\n",
            printf_precision(protocol->mxp_version), protocol->mxp_version.data());
        send_info_message(descriptor, message);
    }
}

// Reads the MXP line tag at the scan's position: the tag text up to its '>', cut at
// MAX_MXP_TAG_LENGTH bytes, is acted on as a <VERSION> reply. The position is left on the '>',
// which the loop's step then passes; on the byte after a cut, which is dropped; or past the end
// of the bytes when no '>' arrives.
static input_step read_mxp_line_tag(input_scan& scan)
{
    constexpr int line_tag_length = 4;
    char tag_text[MXP_BUFFER_SIZE];
    int tag_length = 0;

    scan.position += line_tag_length;
    while (scan.position < scan.size && scan.data[scan.position] != '>'
        && tag_length < MAX_MXP_TAG_LENGTH) {
        tag_text[tag_length++] = scan.data[scan.position++];
    }
    tag_text[tag_length++] = '>';
    tag_text[tag_length] = '\0';

    apply_mxp_version_reply(scan.descriptor, scan.protocol, tag_text);
    return input_step::next_byte;
}

// Reads the telnet command whose IAC is at the scan's position. IAC SB starts a subnegotiation,
// DO, DONT, WILL and WONT are answered, and any other command is dropped. A command cut off by the
// end of the bytes is held for the next read.
static input_step read_telnet_command(input_scan& scan)
{
    if (!has_bytes_after(scan, 1)) {
        return hold_for_next_read(scan, 1);
    }

    const char command = scan.data[scan.position + 1];
    switch (command) {
    case TELNET_SB:
        ++scan.position;
        scan.protocol->in_subnegotiation = true;
        return input_step::next_byte;

    case TELNET_DO:
    case TELNET_DONT:
    case TELNET_WILL:
    case TELNET_WONT:
        if (!has_bytes_after(scan, 2)) {
            return hold_for_next_read(scan, 2);
        }
        perform_handshake(scan.descriptor, command, scan.data[scan.position + 2]);
        scan.position += 2;
        return input_step::next_byte;

    default:
        ++scan.position;
        return input_step::next_byte;
    }
}

// Reads the byte at the scan's position and any that follow it in the same sequence, leaving the
// position where the loop's step moves on from. The order of the checks decides which bytes each
// one sees.
static input_step read_input_byte(input_scan& scan)
{
    if (is_input_buffer_full(scan)) {
        return abandon_full_read(scan);
    }
    if (is_doubled_iac_at(scan)) {
        return append_literal_iac(scan);
    }
    if (scan.protocol->in_subnegotiation) {
        return read_subnegotiation_byte(scan);
    }
    if (is_escape_in_last_three_bytes(scan)) {
        return read_escape_near_end(scan);
    }
    if (is_mxp_line_tag_at(scan.data, scan.position, scan.size)) {
        return read_mxp_line_tag(scan);
    }
    if (scan.data[scan.position] == TELNET_IAC) {
        return read_telnet_command(scan);
    }
    scan.command_text[scan.command_length++] = scan.data[scan.position];
    return input_step::next_byte;
}

// Ends both buffers with a NUL, stores the length of a subnegotiation still open, and appends the
// gathered text to out_text.
static void finish_input_scan(const input_scan& scan, char* out_text)
{
    scan.subnegotiation[scan.subnegotiation_length] = '\0';
    scan.command_text[scan.command_length] = '\0';
    if (scan.protocol->in_subnegotiation) {
        scan.protocol->subnegotiation_input_length = scan.subnegotiation_length;
    } else {
        scan.protocol->subnegotiation_input_length = 0;
    }

    std::strcat(out_text, scan.command_text);
}

void ProtocolInput(const descriptor_t* descriptor, const char* data, int size, char* out_text)
{
    if (data != nullptr && size > 0) {
        ProtocolInput(descriptor, std::string_view(data, static_cast<std::size_t>(size)), out_text);
    }
}

void ProtocolInput(const descriptor_t* descriptor, std::string_view input, char* out_text)
{
    static char command_text[MAX_PROTOCOL_BUFFER + 1];

    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr || input.empty() || out_text == nullptr) {
        return;
    }
    if (input.size() > static_cast<std::size_t>(MAX_PROTOCOL_BUFFER)) {
        report_bug("ProtocolInput: Too much incoming data to store in the buffer.");
        return;
    }

    input_scan scan { };
    scan.descriptor = descriptor;
    scan.protocol = protocol;
    scan.data = input.data();
    scan.size = static_cast<int>(input.size());
    scan.command_text = command_text;
    scan.subnegotiation = protocol->subnegotiation_input.data();
    if (protocol->in_subnegotiation) {
        scan.subnegotiation_length = protocol->subnegotiation_input_length;
    }

    // The held bytes go before this read's. The read was refused above if it was over
    // MAX_PROTOCOL_BUFFER, so both fit.
    char combined[MAX_PROTOCOL_BUFFER + PENDING_INPUT_CAPACITY];
    if (protocol->pending_input_length > 0) {
        std::memcpy(combined, protocol->pending_input.data(), protocol->pending_input_length);
        std::memcpy(combined + protocol->pending_input_length, scan.data, scan.size);
        scan.data = combined;
        scan.size += protocol->pending_input_length;
        protocol->pending_input_length = 0;
    }

    for (scan.position = 0; scan.position < scan.size; ++scan.position) {
        const input_step step = read_input_byte(scan);
        if (step == input_step::abandon) {
            return;
        }
        if (step == input_step::stop) {
            break;
        }
    }

    finish_input_scan(scan, out_text);
}

// The text a \t\t code writes.
constexpr std::string_view TAB_TEXT = "\t";

// An MSP sound trigger, which a \t! code writes.
constexpr std::string_view MSP_TRIGGER = "!!";

// What follows "!!" in an MSP sound trigger.
constexpr std::string_view MSP_SOUND_PREFIX = "SOUND(";

// The MXP framing written for a \t< tag's start and for the '>' that ends it.
constexpr std::string_view MXP_TAG_START = "\033[1z<";
constexpr std::string_view MXP_TAG_END = ">\033[7z";

// The MXP send link that a \t( code starts and a \t) code ends.
constexpr std::string_view LINK_START = "\033[1z<send>\033[7z";
constexpr std::string_view LINK_END = "\033[1z</send>\033[7z";

// The most characters a \t[...] code's text may hold, such as a Unicode fallback.
constexpr int MAX_BRACKET_TEXT_LENGTH = 7;

static_assert(RGB_COLOUR_CODE_LENGTH <= MAX_BRACKET_TEXT_LENGTH,
    "An RGB colour code does not fit a \\t[...] code's text.");

// The RGB code each \t<letter> colour code stands for.
struct colour_letter {
    char letter; // The letter after the tab.
    std::string_view rgb_code; // Its ColourRGB code, such as "F200".
};

static_assert(sizeof(colour_letter)
        == unpadded_size(sizeof(colour_letter::letter) + sizeof(colour_letter::rgb_code),
            alignof(colour_letter)),
    "colour_letter has padding between its fields.");

constexpr colour_letter COLOUR_LETTERS[] = {
    { 'r', "F200" },
    { 'R', "F500" },
    { 'g', "F020" },
    { 'G', "F050" },
    { 'y', "F220" },
    { 'Y', "F550" },
    { 'b', "F002" },
    { 'B', "F005" },
    { 'm', "F202" },
    { 'M', "F505" },
    { 'c', "F022" },
    { 'C', "F055" },
    { 'w', "F222" },
    { 'W', "F555" },
    { 'a', "F014" },
    { 'A', "F025" },
    { 'j', "F031" },
    { 'J', "F052" },
    { 'l', "F140" },
    { 'L', "F250" },
    { 'o', "F520" },
    { 'O', "F530" },
    { 'p', "F301" },
    { 'P', "F502" },
    { 't', "F210" },
    { 'T', "F321" },
    { 'v', "F104" },
    { 'V', "F205" },
};

// One apply_output_codes call's place in the text and the result it is writing.
struct output_scan {
    const descriptor_t* descriptor; // The connection the text is written for.
    protocol_t* protocol; // The descriptor's protocol state, never null.
    const char* text; // The text whose codes are applied.
    std::size_t text_size; // The characters of text that are read.
    std::size_t position; // The character being read.
    char* result; // apply_output_codes's result buffer, MAX_OUTPUT_BUFFER + 1 chars.
    int result_length; // The chars in result.
    bool in_mxp_tag; // Whether a \t< tag was sent, so the next '>' ends it.
    bool reached_end; // Whether a code read to the text's end or NUL, which ends the text.

    // text's character at text_position, or a NUL past text_size.
    char at(std::size_t text_position) const
    {
        if (text_position < text_size) {
            return text[text_position];
        }
        return '\0';
    }

    // The character at the position.
    char current() const
    {
        return at(position);
    }

    // The character at the position, moving the position past it.
    char take()
    {
        const char character = current();
        ++position;
        return character;
    }

    // Up to count characters from the position, fewer where text_size ends them, moving the
    // position count characters on. The characters must already have been read with at().
    std::string_view take_many(std::size_t count)
    {
        std::string_view taken;
        if (position < text_size) {
            taken = std::string_view(text + position, std::min(count, text_size - position));
        }
        position += count;
        return taken;
    }
};

static_assert(sizeof(output_scan)
        == unpadded_size(sizeof(output_scan::descriptor) + sizeof(output_scan::protocol)
                + sizeof(output_scan::text) + sizeof(output_scan::text_size)
                + sizeof(output_scan::position) + sizeof(output_scan::result)
                + sizeof(output_scan::result_length) + sizeof(output_scan::in_mxp_tag)
                + sizeof(output_scan::reached_end),
            alignof(output_scan)),
    "output_scan has padding between its fields.");

// What to do with a \t[...] code's characters past the buffer it is read into.
enum class bracket_overflow {
    skip, // Read on to the ']' without storing them.
    stop, // Stop reading on the first of them.
};

// What reading a \t[...] code's text found.
struct bracket_text {
    bool closed; // Whether the read reached the ']'.
    bool fits; // Whether every character read was stored.
};

static_assert(sizeof(bracket_text)
        == unpadded_size(sizeof(bracket_text::closed) + sizeof(bracket_text::fits),
            alignof(bracket_text)),
    "bracket_text has padding between its fields.");

// Adds text to the result, cut off once the result holds MAX_OUTPUT_BUFFER chars.
static void append_output(output_scan& scan, std::string_view text)
{
    for (std::size_t copy_index = 0;
        copy_index < text.size() && scan.result_length < MAX_OUTPUT_BUFFER; ++copy_index) {
        scan.result[scan.result_length++] = text[copy_index];
    }
}

// Formats a malformed code's message into a 256-character buffer, as std::snprintf does, and
// logs it.
static void report_code_bug(const char* format, ...)
{
    constexpr std::size_t bug_text_size = 256;
    char bug_text[bug_text_size];
    std::va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(bug_text, sizeof(bug_text), format, arguments);
    va_end(arguments);
    report_bug(bug_text);
}

static_assert(RGB_COLOUR_CODE_LENGTH > 1 && MAX_BRACKET_TEXT_LENGTH > 0,
    "read_bracket_text's capacities must be positive.");

// Reads a \t[...] code's text from the scan's position into out_text, storing at most capacity
// chars; the caller NUL-fills out_text past them. The read ends on the ']', at the end of the
// text, or, for bracket_overflow::stop, on the first character that does not fit.
static bracket_text read_bracket_text(
    output_scan& scan, char* out_text, std::size_t capacity, bracket_overflow overflow)
{
    // stop looks no further than one character past what fits.
    std::size_t length_limit = std::string_view::npos;
    if (overflow == bracket_overflow::stop) {
        length_limit = capacity + 1;
    }

    bracket_text read { false, true };
    std::size_t text_length = 0;
    while (text_length < length_limit) {
        const char character = scan.at(scan.position + text_length);
        if (character == ']') {
            read.closed = true;
            break;
        }

        if (character == '\0') {
            break;
        }
        ++text_length;
    }
    read.fits = text_length <= capacity;

    // skip moves past the whole text, stop only past what is stored.
    std::size_t taken_length = text_length;
    if (overflow == bracket_overflow::stop) {
        taken_length = std::min(text_length, capacity);
    }
    const std::string_view taken = scan.take_many(taken_length);
    taken.copy(out_text, capacity);
    return read;
}

// Appends the character a \t[U<code point>/<fallback>] code names, the scan on its 'U' or 'u':
// the code point's UTF-8 for a client with UTF-8 on, else the fallback. An unclosed code ends the
// text.
static void append_unicode_code(output_scan& scan)
{
    char fallback_text[MAX_BRACKET_TEXT_LENGTH + 1] = { '\0' };
    int code_point = 0;

    ++scan.position;
    while (is_ascii_digit(scan.current())) {
        code_point *= 10;
        code_point += scan.take() - '0';
    }

    if (scan.current() == '/') {
        ++scan.position;
    }

    const bracket_text read = read_bracket_text(
        scan, fallback_text, MAX_BRACKET_TEXT_LENGTH, bracket_overflow::skip);
    if (!read.closed) {
        report_code_bug(
            "BUG: Unicode substitute '%s' wasn't terminated with ']'.", fallback_text);
    } else if (!read.fits) {
        report_code_bug("BUG: Unicode substitute '%s' truncated.  Missing ']'?", fallback_text);
    } else if (scan.protocol->variables.number(eMSDP_UTF_8) != 0) {
        const std::string_view utf_8_text = UnicodeGet(code_point);
        append_output(scan, utf_8_text);
    } else {
        append_output(scan, fallback_text);
    }

    // An unclosed code ends the text.
    scan.reached_end = !read.closed;
}

// Appends the colour a \t[F<rgb>] or \t[B<rgb>] code names, the scan on its layer letter. A
// malformed code is logged and writes nothing, and one cut off by the end of the text ends it.
static void append_rgb_code(output_scan& scan)
{
    char code_text[MAX_BRACKET_TEXT_LENGTH + 1] = { '\0' };

    code_text[0] = scan.take();
    const bracket_text read = read_bracket_text(
        scan, &code_text[1], RGB_COLOUR_CODE_LENGTH - 1, bracket_overflow::stop);

    const char* layer_name = "back";
    if (ascii_lower(code_text[0]) == 'f') {
        layer_name = "fore";
    }

    if (!read.closed || !read.fits) {
        report_code_bug("BUG: RGB %sground colour '%s' wasn't terminated with ']'.",
            layer_name, &code_text[1]);
        // An unclosed code that fits was cut off by the end of the text.
        scan.reached_end = read.fits;
    } else if (!is_valid_colour(code_text)) {
        report_code_bug("BUG: RGB %sground colour '%s' invalid (each digit must be "
                        "in the range 0-5).",
            layer_name, &code_text[1]);
    } else {
        const std::string_view colour_text = ColourRGB(scan.descriptor, code_text);
        append_output(scan, colour_text);
    }
}

// Applies a \t[X<version>] code, the scan on its 'X': MXP codes are blocked until the next \t) or
// \t< unless the client's MXP version is at least version. An unclosed code ends the text.
static void apply_required_mxp_version(output_scan& scan)
{
    char version_text[MAX_BRACKET_TEXT_LENGTH + 1] = { '\0' };

    ++scan.position;
    const bracket_text read = read_bracket_text(
        scan, version_text, MAX_BRACKET_TEXT_LENGTH, bracket_overflow::skip);
    if (!read.closed) {
        report_code_bug(
            "BUG: Required MXP version '%s' wasn't terminated with ']'.", version_text);
    } else if (!read.fits) {
        report_code_bug("BUG: Required MXP version '%s' too long.  Missing ']'?", version_text);
    } else {
        // Versions compare as strings.
        const bool mxp_version_unknown = scan.protocol->mxp_version == "Unknown";
        const bool mxp_version_too_low
            = mxp_version_unknown || scan.protocol->mxp_version.compare(version_text) < 0;
        scan.protocol->mxp_blocked = mxp_version_too_low;
    }

    // An unclosed code ends the text.
    scan.reached_end = !read.closed;
}

// Applies the \t[...] code whose '[' is at the scan's position, by the letter after it. The scan
// ends on the code's last character read; an unknown letter writes nothing, and a '[' at the end
// of the text ends it.
static void append_bracket_code(output_scan& scan)
{
    ++scan.position;
    const char code_letter = ascii_lower(scan.current());
    if (code_letter == 'u') {
        append_unicode_code(scan);
    } else if (code_letter == 'f' || code_letter == 'b') {
        append_rgb_code(scan);
    } else if (code_letter == 'x') {
        apply_required_mxp_version(scan);
    } else if (code_letter == '\0') {
        scan.reached_end = true;
    }
}

// Applies the MXP code \t(, \t) or \t<, the scan on code. Each sends its MXP only while MXP is on
// and no \t[X] code blocks it; a \t< tag that is not sent is skipped up to its '>', and one
// without a '>' ends the text. \t) and \t< end a \t[X] code's block.
static void append_mxp_code(output_scan& scan, char code)
{
    const bool mxp_allowed
        = !scan.protocol->mxp_blocked && scan.protocol->variables.number(eMSDP_MXP) != 0;
    if (code == '(') {
        if (mxp_allowed) {
            append_output(scan, LINK_START);
        }
        return;
    }

    if (code == ')') {
        if (mxp_allowed) {
            append_output(scan, LINK_END);
        }
    } else if (mxp_allowed) {
        append_output(scan, MXP_TAG_START);
        scan.in_mxp_tag = true;
    } else {
        while (scan.current() != '\0' && scan.current() != '>') {
            ++scan.position;
        }
        scan.reached_end = scan.current() == '\0';
    }
    scan.protocol->mxp_blocked = false;
}

// Appends the colour a \t<letter> colour code stands for; a letter that names no colour writes
// nothing.
static void append_colour_code(output_scan& scan, char letter)
{
    for (const colour_letter& colour : COLOUR_LETTERS) {
        if (colour.letter == letter) {
            const std::string_view colour_text = ColourRGB(scan.descriptor, colour.rgb_code);
            append_output(scan, colour_text);
            return;
        }
    }
}

// Applies the code whose character after the tab is at the scan's position. An unknown character
// writes nothing, so both it and the tab are dropped; a tab at the end of the text ends it.
static void append_output_code(output_scan& scan)
{
    const char code = scan.current();
    switch (code) {
    case '\t':
        append_output(scan, TAB_TEXT);
        break;
    case 'n':
        append_output(scan, ANSI_CLEAR);
        break;
    case '(':
    case ')':
    case '<':
        append_mxp_code(scan, code);
        break;
    case '[':
        append_bracket_code(scan);
        break;
    case '!':
        append_output(scan, MSP_TRIGGER);
        break;
    case '\0':
        scan.reached_end = true;
        break;
    default:
        append_colour_code(scan, code);
        break;
    }
}

// Whether the scan's position holds the second '!' of an MSP sound trigger, "!!SOUND(" in any
// case, written in the text itself rather than by a \t! code.
static bool is_old_msp_trigger_at(const output_scan& scan)
{
    if (scan.position == 0 || scan.at(scan.position - 1) != '!' || scan.current() != '!') {
        return false;
    }
    for (std::size_t prefix_index = 0; prefix_index < MSP_SOUND_PREFIX.size(); ++prefix_index) {
        const char character = scan.at(scan.position + 1 + prefix_index);
        if (ascii_lower(character) != ascii_lower(MSP_SOUND_PREFIX[prefix_index])) {
            return false;
        }
    }
    return true;
}

// Ends the result with a NUL and returns it. A result that filled the buffer is logged and comes
// back empty.
static std::string_view finish_output(output_scan& scan)
{
    if (scan.result_length >= MAX_OUTPUT_BUFFER) {
        scan.result_length = 0;
        report_bug("ProtocolOutput: Too much outgoing data to store in the buffer.");
    }

    scan.result[scan.result_length] = '\0';
    return std::string_view(scan.result, static_cast<std::size_t>(scan.result_length));
}

// ProtocolOutput's work for a protocol that is not null.
static std::string_view apply_output_codes(
    const descriptor_t* descriptor, protocol_t* protocol, std::string_view text)
{
    static char result[MAX_OUTPUT_BUFFER + 1];

    output_scan scan { };
    scan.descriptor = descriptor;
    scan.protocol = protocol;
    scan.text = text.data();
    scan.text_size = text.size();
    scan.result = result;

    // A client with MSP or SOUND on would play a literal "!!SOUND(" in the text, so its second
    // '!' becomes '?'.
    const bool strips_msp_triggers
        = protocol->msp_enabled || protocol->variables.number(eMSDP_SOUND) != 0;

    // The stop flag is tested first, so the character after a stop is never read.
    for (; scan.result_length < MAX_OUTPUT_BUFFER && !scan.reached_end && scan.current() != '\0';
        ++scan.position) {
        if (scan.current() == '\t') {
            ++scan.position;
            append_output_code(scan);
        } else if (scan.in_mxp_tag && scan.current() == '>') {
            append_output(scan, MXP_TAG_END);
            scan.in_mxp_tag = false;
        } else if (strips_msp_triggers && is_old_msp_trigger_at(scan)) {
            scan.result[scan.result_length++] = '?';
        } else {
            scan.result[scan.result_length++] = scan.current();
        }
    }

    return finish_output(scan);
}

std::string_view ProtocolOutput(const descriptor_t* descriptor, std::string_view text)
{
    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return text;
    }
    return apply_output_codes(descriptor, protocol, text);
}

// Only TTYPE is offered here, because some clients, such as GMud, print negotiation instead of
// answering it. TTYPE's option byte is not printable; the other options, some of whose bytes are,
// are offered only once the client answers.
void ProtocolNegotiate(const descriptor_t* descriptor)
{
    confirm_negotiation(descriptor, eNEGOTIATED_TTYPE, true, true);
}

void MSDPUpdate(const descriptor_t* descriptor)
{
    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return;
    }

    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        const variable_t variable = static_cast<variable_t>(variable_index);
        if (protocol->variables.is_reported(variable)) {
            if (protocol->variables.is_dirty(variable)) {
                if (MSDPSend(descriptor, variable)) {
                    protocol->variables.mark_clean(variable);
                }
            }
        }
    }
}

void MSDPFlush(const descriptor_t* descriptor, variable_t variable)
{
    if (variable > eMSDP_NONE && variable < eMSDP_MAX) {
        protocol_t* protocol = protocol_of(descriptor);

        if (protocol != nullptr && protocol->variables.is_reported(variable)) {
            if (protocol->variables.is_dirty(variable)) {
                if (MSDPSend(descriptor, variable)) {
                    protocol->variables.mark_clean(variable);
                }
            }
        }
    }
}

void MSDPMarkAllReportedDirty(const descriptor_t* descriptor)
{
    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return;
    }

    protocol->variables.mark_reported_dirty();
}

bool MSDPSend(const descriptor_t* descriptor, variable_t variable)
{
    char msdp_text[MAX_VARIABLE_LENGTH + 1] = { '\0' };
    protocol_t* protocol = protocol_of(descriptor);

    if (protocol == nullptr || descriptor->character == nullptr) {
        return false;
    }
    const bool wants_msdp = PRF_FLAGGED(descriptor->character, PRF_MSDP);
    if (!wants_msdp) {
        return false;
    }

    if (variable > eMSDP_NONE && variable < eMSDP_MAX) {
        const std::string_view name = VARIABLE_NAME_TABLE[variable].name;
        const int name_precision = printf_precision(name);
        if (VARIABLE_NAME_TABLE[variable].is_string) {
            const std::string& value = protocol->variables.text(variable);
            const std::size_t required_length = name.size() + value.size() + MSDP_FRAMING_LENGTH;

            if (required_length >= static_cast<std::size_t>(MAX_VARIABLE_LENGTH)) {
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSend: %.*s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).",
                    name_precision, name.data(), required_length, MAX_VARIABLE_LENGTH);
                report_bug(msdp_text);
                msdp_text[0] = '\0';
            } else if (protocol->msdp_enabled) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%.*s%c%.*s%c%c", IAC, SB,
                    TELOPT_MSDP, MSDP_VAR, name_precision, name.data(), MSDP_VAL,
                    printf_precision(value), value.data(), IAC, SE);
            } else if (protocol->atcp_enabled) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%.*s %.*s%c%c", IAC, SB,
                    TELOPT_ATCP, name_precision, name.data(), printf_precision(value), value.data(),
                    IAC, SE);
            }
        } else {
            const int value = protocol->variables.number(variable);
            if (protocol->msdp_enabled) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%.*s%c%d%c%c", IAC, SB,
                    TELOPT_MSDP, MSDP_VAR, name_precision, name.data(), MSDP_VAL, value, IAC, SE);
            } else if (protocol->atcp_enabled) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%.*s %d%c%c", IAC, SB,
                    TELOPT_ATCP, name_precision, name.data(), value, IAC, SE);
            }
        }

        // Empty when the client has neither MSDP nor ATCP, or the message was too long.
        if (msdp_text[0] != '\0') {
            send_to_client(descriptor, msdp_text);
            return true;
        }
    }
    return false;
}

void MSDPSendPair(const descriptor_t* descriptor, const char* name, const char* value)
{
    if (name != nullptr && value != nullptr) {
        MSDPSendPair(descriptor, std::string_view(name), std::string_view(value));
    }
}

void MSDPSendPair(const descriptor_t* descriptor, std::string_view name, std::string_view value)
{
    char msdp_text[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return;
    }

    const std::size_t value_length = msdp_sanitized_length(value);

    // The limit as std::size_t, so the length checks need no sign conversion.
    constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
    const std::size_t name_length = name.size();
    const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

    if (required_length >= buffer_limit) {
        if (required_length - value_length < buffer_limit) {
            std::snprintf(msdp_text, sizeof(msdp_text),
                "MSDPSendPair: %.*s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).",
                printf_precision(name), name.data(), required_length, MAX_VARIABLE_LENGTH);
        } else {
            // The name alone is too long.
            std::snprintf(msdp_text, sizeof(msdp_text),
                "MSDPSendPair: Variable name has a length of %zu bytes (exceeds "
                "MAX_VARIABLE_LENGTH of %d).",
                name_length, MAX_VARIABLE_LENGTH);
        }

        report_bug(msdp_text);
        msdp_text[0] = '\0';
    } else if (protocol->msdp_enabled) {
        const int prefix_length = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%.*s%c",
            IAC, SB, TELOPT_MSDP, MSDP_VAR, printf_precision(name), name.data(), MSDP_VAL);
        char* const value_end = msdp_write_sanitized(value, msdp_text + prefix_length);
        const std::size_t room_left
            = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
        std::snprintf(value_end, room_left, "%c%c", IAC, SE);
    } else if (protocol->atcp_enabled) {
        const int prefix_length = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%.*s ",
            IAC, SB, TELOPT_ATCP, printf_precision(name), name.data());
        char* const value_end = msdp_write_sanitized(value, msdp_text + prefix_length);
        const std::size_t room_left
            = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
        std::snprintf(value_end, room_left, "%c%c", IAC, SE);
    }

    // Empty when the client has neither MSDP nor ATCP, or the message was too long.
    if (msdp_text[0] != '\0') {
        send_to_client(descriptor, msdp_text);
    }
}

void MSDPSendList(const descriptor_t* descriptor, const char* name, const char* value)
{
    if (name != nullptr && value != nullptr) {
        MSDPSendList(descriptor, std::string_view(name), std::string_view(value));
    }
}

void MSDPSendList(const descriptor_t* descriptor, std::string_view name, std::string_view value)
{
    char msdp_text[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return;
    }

    const std::size_t value_length = msdp_sanitized_length(value);

    // The limit as std::size_t, so the length checks need no sign conversion.
    constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
    const std::size_t name_length = name.size();
    const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

    if (required_length >= buffer_limit) {
        if (required_length - value_length < buffer_limit) {
            std::snprintf(msdp_text, sizeof(msdp_text),
                "MSDPSendList: %.*s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).",
                printf_precision(name), name.data(), required_length, MAX_VARIABLE_LENGTH);
        } else {
            // The name alone is too long.
            std::snprintf(msdp_text, sizeof(msdp_text),
                "MSDPSendList: Variable name has a length of %zu bytes (exceeds "
                "MAX_VARIABLE_LENGTH of %d).",
                name_length, MAX_VARIABLE_LENGTH);
        }

        report_bug(msdp_text);
        msdp_text[0] = '\0';
    } else if (protocol->msdp_enabled) {
        const int prefix_length
            = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%.*s%c%c%c", IAC, SB,
                TELOPT_MSDP, MSDP_VAR, printf_precision(name), name.data(), MSDP_VAL,
                MSDP_ARRAY_OPEN, MSDP_VAL);
        char* const value_end = msdp_write_sanitized(value, msdp_text + prefix_length);
        const std::size_t room_left
            = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
        std::snprintf(value_end, room_left, "%c%c%c", MSDP_ARRAY_CLOSE, IAC, SE);

        // Each space separates two array values.
        for (int text_position = 0; msdp_text[text_position] != '\0'; ++text_position) {
            if (msdp_text[text_position] == ' ') {
                msdp_text[text_position] = MSDP_VAL;
            }
        }
    } else if (protocol->atcp_enabled) {
        const int prefix_length = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%.*s ",
            IAC, SB, TELOPT_ATCP, printf_precision(name), name.data());
        char* const value_end = msdp_write_sanitized(value, msdp_text + prefix_length);
        const std::size_t room_left
            = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
        std::snprintf(value_end, room_left, "%c%c", IAC, SE);
    }

    // Empty when the client has neither MSDP nor ATCP, or the message was too long.
    if (msdp_text[0] != '\0') {
        send_to_client(descriptor, msdp_text);
    }
}

void MSDPSetNumber(const descriptor_t* descriptor, variable_t variable, int value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (!VARIABLE_NAME_TABLE[variable].is_string) {
            protocol->variables.set_number(variable, value);
        }
    }
}

bool MSDPIsValidVariable(variable_t variable)
{
    return variable > eMSDP_NONE && variable < eMSDP_MAX;
}

// The longest text MSDPSanitizeValue returns for one byte: a "\u001f" escape.
constexpr std::size_t MSDP_LONGEST_ESCAPE = 6;

// Writes the text MSDPSanitizeValue returns for byte into out_escape and returns its length. The
// text never holds a NUL.
static std::size_t msdp_escape_byte(unsigned char byte, char (&out_escape)[MSDP_LONGEST_ESCAPE])
{
    static constexpr char hex_digits[] = "0123456789abcdef";

    out_escape[0] = '\\';
    switch (byte) {
    case '\"':
        out_escape[1] = '\"';
        return 2;
    case '\\':
        out_escape[1] = '\\';
        return 2;
    case '\n':
        out_escape[1] = 'n';
        return 2;
    case '\r':
        out_escape[1] = 'r';
        return 2;
    case '\t':
        out_escape[1] = 't';
        return 2;
    default:
        break;
    }

    if (byte >= 0x20) {
        out_escape[0] = static_cast<char>(byte);
        return 1;
    }

    out_escape[1] = 'u';
    out_escape[2] = '0';
    out_escape[3] = '0';
    out_escape[4] = hex_digits[byte >> 4];
    out_escape[5] = hex_digits[byte & 0x0f];
    return MSDP_LONGEST_ESCAPE;
}

// The length of the MSDPSanitizeValue text for value.
static std::size_t msdp_sanitized_length(std::string_view value)
{
    char escape[MSDP_LONGEST_ESCAPE];
    return std::accumulate(value.begin(), value.end(), std::size_t { 0 },
        [&escape](std::size_t length, char character) -> std::size_t {
            return length + msdp_escape_byte(static_cast<unsigned char>(character), escape);
        });
}

// Writes the MSDPSanitizeValue text for value to out_text, which is never null and has room for
// msdp_sanitized_length(value) characters, and returns the end of what it wrote. Writes no
// terminating NUL.
static char* msdp_write_sanitized(std::string_view value, char* out_text)
{
    char escape[MSDP_LONGEST_ESCAPE];
    std::for_each(value.begin(), value.end(), [&escape, &out_text](char character) -> void {
        const std::size_t escape_length
            = msdp_escape_byte(static_cast<unsigned char>(character), escape);
        out_text = std::copy_n(escape, escape_length, out_text);
    });
    return out_text;
}

// Whether stored is exactly the MSDPSanitizeValue text for value, compared without building it.
static bool equals_sanitized(std::string_view stored, std::string_view value)
{
    char escape[MSDP_LONGEST_ESCAPE];
    for (const char character : value) {
        const std::size_t escape_length
            = msdp_escape_byte(static_cast<unsigned char>(character), escape);
        // A stored text too short for the escape gives a shorter substring, which mismatches.
        if (stored.substr(0, escape_length) != std::string_view(escape, escape_length)) {
            return false;
        }
        stored.remove_prefix(escape_length);
    }
    return stored.empty();
}

// The bytes msdp_set_wrapped puts around a payload: its open and close markers.
constexpr std::size_t MSDP_WRAPPER_LENGTH = 2;

// Whether stored is exactly open_marker, then payload, then close_marker.
static bool equals_wrapped(
    std::string_view stored, char open_marker, std::string_view payload, char close_marker)
{
    if (stored.size() != payload.size() + MSDP_WRAPPER_LENGTH) {
        return false;
    }
    return stored.front() == open_marker && stored.substr(1, payload.size()) == payload
        && stored.back() == close_marker;
}

// Stores open_marker, payload and close_marker as the string variable's value and marks it dirty,
// unless that is its value already. Returns whether the value changed. The value is written into
// the stored string, which allocates only when it outgrows its capacity. payload must not point
// into the stored string.
static bool msdp_set_wrapped(msdp_variables_t& variables, variable_t variable, char open_marker,
    std::string_view payload, char close_marker)
{
    if (equals_wrapped(variables.text(variable), open_marker, payload, close_marker)) {
        return false;
    }

    std::string& stored = variables.text_to_change(variable);
    stored.resize(payload.size() + MSDP_WRAPPER_LENGTH);
    stored.front() = open_marker;
    payload.copy(stored.data() + 1, payload.size());
    stored.back() = close_marker;
    return true;
}

void MSDPAppendSanitized(std::string& out_text, const char* value)
{
    if (value != nullptr) {
        MSDPAppendSanitized(out_text, std::string_view(value));
    }
}

void MSDPAppendSanitized(std::string& out_text, std::string_view value)
{
    const std::size_t old_size = out_text.size();
    const std::size_t length = msdp_sanitized_length(value);
    out_text.resize(old_size + length);
    msdp_write_sanitized(value, out_text.data() + old_size);
}

std::string MSDPSanitizeValue(const char* value)
{
    std::string sanitized;
    MSDPAppendSanitized(sanitized, value);
    return sanitized;
}

std::string MSDPSanitizeValue(std::string_view value)
{
    std::string sanitized;
    MSDPAppendSanitized(sanitized, value);
    return sanitized;
}

void MSDPSetString(const descriptor_t* descriptor, variable_t variable, const char* value)
{
    if (value != nullptr) {
        MSDPSetString(descriptor, variable, std::string_view(value));
    }
}

void MSDPSetString(const descriptor_t* descriptor, variable_t variable, std::string_view value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && MSDPIsValidVariable(variable);
    if (can_set && VARIABLE_NAME_TABLE[variable].is_string) {
        if (!equals_sanitized(protocol->variables.text(variable), value)) {
            // Written into the stored string, which allocates only when it outgrows its capacity.
            std::string& stored = protocol->variables.text_to_change(variable);
            stored.resize(msdp_sanitized_length(value));
            msdp_write_sanitized(value, stored.data());
        }
    }
}

void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, const char* value)
{
    if (value != nullptr) {
        MSDPSetTable(descriptor, variable, std::string_view(value));
    }
}

void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, std::string_view value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (value.empty()) {
            // An empty value is stored empty, without the markers.
            MSDPSetString(descriptor, variable, value);
        } else if (VARIABLE_NAME_TABLE[variable].is_string) {
            msdp_set_wrapped(protocol->variables, variable, static_cast<char>(MSDP_TABLE_OPEN),
                value, static_cast<char>(MSDP_TABLE_CLOSE));
        }
    }
}

void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, const char* value)
{
    if (value != nullptr) {
        MSDPSetArray(descriptor, variable, std::string_view(value));
    }
}

void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, std::string_view value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (value.empty()) {
            // An empty value is stored empty, without the markers.
            MSDPSetString(descriptor, variable, value);
        } else if (VARIABLE_NAME_TABLE[variable].is_string) {
            msdp_set_wrapped(protocol->variables, variable, static_cast<char>(MSDP_ARRAY_OPEN),
                value, static_cast<char>(MSDP_ARRAY_CLOSE));
        }
    }
}

void MXPSendTag(const descriptor_t* descriptor, const char* tag)
{
    if (tag != nullptr) {
        MXPSendTag(descriptor, std::string_view(tag));
    }
}

void MXPSendTag(const descriptor_t* descriptor, std::string_view tag)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_send
        = protocol != nullptr && tag.size() < static_cast<std::size_t>(MAX_MXP_TAG_LENGTH);
    if (can_send) {
        if (protocol->variables.number(eMSDP_MXP) != 0) {
            char tag_text[MXP_BUFFER_SIZE];
            std::snprintf(tag_text, sizeof(tag_text), "\033[1z%.*s\033[7z\r\n",
                printf_precision(tag), tag.data());
            send_to_client(descriptor, tag_text);
        } else if (protocol->renegotiate) {
            // A client that connects automatically, such as MUSHclient, can miss the first
            // offers, so make them again now that the character has connected, except TTYPE.
            for (int option_index = eNEGOTIATED_TTYPE + 1; option_index < eNEGOTIATED_MAX;
                ++option_index) {
                protocol->option_states[option_index] = false;
                confirm_negotiation(
                    descriptor, static_cast<negotiated_t>(option_index), true, true);
            }

            protocol->renegotiate = false;
            protocol->needs_mxp_version = true;
            negotiate(descriptor);
        }
    }
}

std::string_view ColourRGB(const descriptor_t* descriptor, const char* rgb_code)
{
    if (rgb_code == nullptr) {
        // An empty code is invalid, as a null one is.
        return ColourRGB(descriptor, std::string_view());
    }
    return ColourRGB(descriptor, std::string_view(rgb_code));
}

std::string_view ColourRGB(const descriptor_t* descriptor, std::string_view rgb_code)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool uses_ansi_colour
        = protocol != nullptr && protocol->variables.number(eMSDP_ANSI_COLORS) != 0;
    if (uses_ansi_colour) {
        if (is_valid_colour(rgb_code)) {
            bool background = (ascii_lower(rgb_code[0]) == 'b');
            int red = rgb_code[1] - '0';
            int green = rgb_code[2] - '0';
            int blue = rgb_code[3] - '0';

            if (protocol->variables.number(eMSDP_XTERM_256_COLORS) != 0) {
                return get_xterm_colour(background, red, green, blue);
            } else {
                return get_ansi_colour(background, red, green, blue);
            }
        } else {
            return ANSI_CLEAR;
        }
    } else {
        return "";
    }
}

std::string_view UnicodeGet(int code_point)
{
    static char utf8_text[8];
    char* cursor = utf8_text;

    UnicodeAdd(&cursor, code_point);
    *cursor = '\0';

    // Up to the first NUL, so a code point whose encoding is a NUL byte gives an empty view.
    return std::string_view(utf8_text);
}

void UnicodeAdd(char** out_cursor, int code_point)
{
    if (code_point < 0x80) {
        *(*out_cursor)++ = static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        *(*out_cursor)++ = static_cast<char>(0xC0 | (code_point >> 6));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        *(*out_cursor)++ = static_cast<char>(0xE0 | (code_point >> 12));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point >> 6 & 0x3F));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x200000) {
        *(*out_cursor)++ = static_cast<char>(0xF0 | (code_point >> 18));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point >> 12 & 0x3F));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point >> 6 & 0x3F));
        *(*out_cursor)++ = static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

// Once the client has answered TTYPE, asks for its terminal type if it agreed to TTYPE, and
// offers the other options.
static void negotiate(const descriptor_t* descriptor)
{
    protocol_t* protocol = descriptor->pProtocol;

    if (protocol->negotiated) {
        static constexpr char ttype_request[]
            = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND, TELNET_IAC, TELNET_SE, '\0' };

        if (protocol->ttype_enabled) {
            send_to_client(descriptor, ttype_request);
        }

        confirm_negotiation(descriptor, eNEGOTIATED_NAWS, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_CHARSET, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_MSDP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_MSSP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_ATCP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_MSP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_MXP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_MCCP, true, true);
        confirm_negotiation(descriptor, eNEGOTIATED_EOR, true, true);
    }
}

// Which side of the connection performs a telnet option.
enum class option_side {
    server, // The client answers DO or DONT: ECHO, MSDP, MSSP, MCCP, MSP, EOR, SGA.
    client, // The client answers WILL or WONT: TTYPE, NAWS, CHARSET, ATCP.
};

// What the client's command means for an option performed by one side.
enum class option_answer {
    accept, // DO for a server option, WILL for a client option.
    decline, // DONT for a server option, WONT for a client option.
    refuse, // WILL for a server option or DO for a client option: an offer from the wrong side.
    none, // WONT for a server option, DONT for a client option, or another command.
};

// What command means for an option performed by side.
static option_answer answer_of(char command, option_side side)
{
    char accept_command = TELNET_WILL;
    char decline_command = TELNET_WONT;
    char wrong_side_command = TELNET_DO;
    if (side == option_side::server) {
        accept_command = TELNET_DO;
        decline_command = TELNET_DONT;
        wrong_side_command = TELNET_WILL;
    }

    if (command == accept_command) {
        return option_answer::accept;
    }

    if (command == decline_command) {
        return option_answer::decline;
    }

    if (command == wrong_side_command) {
        return option_answer::refuse;
    }
    return option_answer::none;
}

// Answers command for an option performed by side, whose state is negotiated and whose flag is
// enabled, and returns what command meant. Confirms an accept, confirms a decline and clears
// enabled, and refuses an offer from the wrong side with DONT or WONT. Setting enabled on an
// accept is left to the caller.
static option_answer answer_option(const descriptor_t* descriptor, char command, char option,
    option_side side, negotiated_t negotiated, bool& enabled)
{
    const option_answer answer = answer_of(command, side);
    switch (answer) {
    case option_answer::accept:
        confirm_negotiation(descriptor, negotiated, true, true);
        break;
    case option_answer::decline:
        confirm_negotiation(descriptor, negotiated, false, enabled);
        enabled = false;
        break;
    case option_answer::refuse:
        if (side == option_side::server) {
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        } else {
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;
    case option_answer::none:
        break;
    }
    return answer;
}

// answer_option for an option that needs nothing more: an accept sets enabled.
static option_answer answer_plain_option(const descriptor_t* descriptor, char command, char option,
    option_side side, negotiated_t negotiated, bool& enabled)
{
    const option_answer answer
        = answer_option(descriptor, command, option, side, negotiated, enabled);
    if (answer == option_answer::accept) {
        enabled = true;
    }
    return answer;
}

// Offers the other options the first time the client answers TTYPE, and marks the connection
// for renegotiation, which a NAWS answer cancels.
static void start_negotiation_once(const descriptor_t* descriptor)
{
    protocol_t* protocol = descriptor->pProtocol;
    if (!protocol->negotiated) {
        protocol->negotiated = true;
        negotiate(descriptor);
        protocol->renegotiate = true;
    }
}

// Answers command for TTYPE, which the client performs.
static void answer_ttype(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_option(descriptor, command, option, option_side::client,
        eNEGOTIATED_TTYPE, protocol->ttype_enabled);
    if (answer == option_answer::accept) {
        protocol->ttype_enabled = true;
        start_negotiation_once(descriptor);
    } else if (answer == option_answer::decline) {
        // A WONT still shows the client answers negotiation, so offer the other options;
        // negotiate does not ask for the terminal type.
        start_negotiation_once(descriptor);
    }
}

// Answers command for NAWS, which the client performs.
static void answer_naws(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_plain_option(descriptor, command, option,
        option_side::client, eNEGOTIATED_NAWS, protocol->naws_enabled);
    if (answer == option_answer::accept || answer == option_answer::decline) {
        // The client answers negotiation, so it needs no renegotiation.
        protocol->renegotiate = false;
    }
}

// Answers command for CHARSET, which the client performs, offering ISO-8859-1 on the first
// accept.
static void answer_charset(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_option(descriptor, command, option, option_side::client,
        eNEGOTIATED_CHARSET, protocol->charset_enabled);
    if (answer == option_answer::accept && !protocol->charset_enabled) {
        // Offer ISO-8859-1, because every byte the server sends is latin-1; it never encodes
        // UTF-8.
        static constexpr char charset_latin1[] = { TELNET_IAC, TELNET_SB, TELOPT_CHARSET, 1, ' ',
            'I', 'S', 'O', '-', '8', '8', '5', '9', '-', '1', TELNET_IAC, TELNET_SE, '\0' };
        send_to_client(descriptor, charset_latin1);
        protocol->charset_enabled = true;
    }
}

// Answers command for MSDP, which the server performs, sending SERVER_ID when MSDP comes on.
static void answer_msdp(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_option(descriptor, command, option, option_side::server,
        eNEGOTIATED_MSDP, protocol->msdp_enabled);
    if (answer == option_answer::accept) {
        // Stored on every DO, for a later SEND SERVER_ID: msdp_enabled starts true, so the check
        // below passes only after a DONT.
        MSDPSetString(descriptor, eMSDP_SERVER_ID, MUD_NAME);

        if (!protocol->msdp_enabled) {
            protocol->msdp_enabled = true;

            // MSDPSendPair, unlike MSDPSend, works before login.
            MSDPSendPair(descriptor, "SERVER_ID", MUD_NAME);
        }
    }
}

// Answers command for MSSP, which the server performs, sending the status message on the first
// accept.
static void answer_mssp(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_option(descriptor, command, option, option_side::server,
        eNEGOTIATED_MSSP, protocol->mssp_enabled);
    if (answer == option_answer::accept && !protocol->mssp_enabled) {
        send_mssp(descriptor);
        protocol->mssp_enabled = true;
    }
}

// Answers command for ATCP, which the client performs, sending SERVER_ID when ATCP comes on.
static void answer_atcp(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;
    const option_answer answer = answer_option(descriptor, command, option, option_side::client,
        eNEGOTIATED_ATCP, protocol->atcp_enabled);

    // ATCP carries the MSDP variables only for a client without MSDP.
    if (answer == option_answer::accept && !protocol->msdp_enabled && !protocol->atcp_enabled) {
        protocol->atcp_enabled = true;

        // Sent now, before login, and stored for a later SEND SERVER_ID.
        MSDPSendPair(descriptor, "SERVER_ID", MUD_NAME);
        MSDPSetString(descriptor, eMSDP_SERVER_ID, MUD_NAME);
    }
}

// Turns MXP on: tells the client, locks secure mode and asks for the client's MXP version when
// renegotiation wants it.
static void start_mxp(const descriptor_t* descriptor)
{
    protocol_t* protocol = descriptor->pProtocol;
    static constexpr char mxp_enable[]
        = { TELNET_IAC, TELNET_SB, TELOPT_MXP, TELNET_IAC, TELNET_SE, '\0' };
    send_to_client(descriptor, mxp_enable);

    // Lock secure mode, in which the client accepts MXP tags.
    send_to_client(descriptor, "\033[7z");
    protocol->mxp_enabled = true;
    protocol->variables.stored_number(eMSDP_MXP) = 1;

    if (protocol->needs_mxp_version) {
        MXPSendTag(descriptor, "<VERSION>");
    }
}

// Answers command for MXP, which either side may perform: WILL answers the server's DO MXP
// (eNEGOTIATED_MXP) and DO its WILL MXP (eNEGOTIATED_MXP2). Nothing is refused.
static void answer_mxp(const descriptor_t* descriptor, char command)
{
    protocol_t* protocol = descriptor->pProtocol;

    if (command == TELNET_WILL || command == TELNET_DO) {
        negotiated_t offer = eNEGOTIATED_MXP2;
        if (command == TELNET_WILL) {
            offer = eNEGOTIATED_MXP;
        }
        confirm_negotiation(descriptor, offer, true, true);

        if (!protocol->mxp_enabled) {
            start_mxp(descriptor);
        }
    } else if (command == TELNET_WONT) {
        confirm_negotiation(descriptor, eNEGOTIATED_MXP, false, protocol->mxp_enabled);

        if (!protocol->mxp_enabled) {
            // The MXP standard does not say whether the server sends DO MXP or WILL MXP, and
            // clients differ, so a WONT to DO MXP brings an offer of WILL MXP.
            confirm_negotiation(descriptor, eNEGOTIATED_MXP2, true, true);
        } else {
            // A WONT once MXP is on switches it off.
            protocol->mxp_enabled = false;
        }
    } else if (command == TELNET_DONT) {
        confirm_negotiation(descriptor, eNEGOTIATED_MXP2, false, protocol->mxp_enabled);
        protocol->mxp_enabled = false;
    }
}

// Refuses a WILL or DO for an option the server does not know; other commands need nothing.
static void refuse_unknown_option(const descriptor_t* descriptor, char command, char option)
{
    if (command == TELNET_WILL) {
        send_negotiation_sequence(descriptor, TELNET_DONT, option);
    } else if (command == TELNET_DO) {
        send_negotiation_sequence(descriptor, TELNET_WONT, option);
    }
}

// Answers the client's WILL, WONT, DO or DONT for option and records the result. A WILL or DO
// that would have the wrong side perform an option, such as DO TTYPE, is refused, as is any WILL
// or DO for an unknown option.
static void perform_handshake(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;

    switch (option) {
    case static_cast<char>(TELOPT_TTYPE):
        answer_ttype(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_ECHO):
        answer_plain_option(descriptor, command, option, option_side::server, eNEGOTIATED_ECHO,
            protocol->echo_enabled);
        break;
    case static_cast<char>(TELOPT_NAWS):
        answer_naws(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_CHARSET):
        answer_charset(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_MSDP):
        answer_msdp(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_MSSP):
        answer_mssp(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_MCCP):
        answer_plain_option(descriptor, command, option, option_side::server, eNEGOTIATED_MCCP,
            protocol->mccp_enabled);
        break;
    case static_cast<char>(TELOPT_MSP):
        answer_plain_option(descriptor, command, option, option_side::server, eNEGOTIATED_MSP,
            protocol->msp_enabled);
        break;
    case static_cast<char>(TELOPT_MXP):
        answer_mxp(descriptor, command);
        break;
    case static_cast<char>(TELOPT_ATCP):
        answer_atcp(descriptor, command, option);
        break;
    case static_cast<char>(TELOPT_EOR):
        answer_plain_option(descriptor, command, option, option_side::server, eNEGOTIATED_EOR,
            protocol->eor_enabled);
        break;
    case static_cast<char>(TELOPT_SGA):
        // A DO means the client wants no IAC GA after prompts.
        answer_plain_option(descriptor, command, option, option_side::server, eNEGOTIATED_SGA,
            protocol->sga_enabled);
        break;
    default:
        refuse_unknown_option(descriptor, command, option);
        break;
    }
}

// The most characters of a client's TTYPE name the server keeps.
constexpr int MAX_CLIENT_NAME_LENGTH = 64;

// Copies the printable ASCII name after a TTYPE subnegotiation's IS byte into out_name, cut to
// MAX_CLIENT_NAME_LENGTH characters and NUL-terminated, and returns its length. Other bytes are
// skipped.
static int read_ttype_name(const char* data, char (&out_name)[MAX_CLIENT_NAME_LENGTH + 1])
{
    int name_length = 0;
    for (int input_position = 1;
        data[input_position] != '\0' && name_length < MAX_CLIENT_NAME_LENGTH; ++input_position) {
        const unsigned char client_byte = static_cast<unsigned char>(data[input_position]);
        if (client_byte >= FIRST_PRINTABLE_ASCII && client_byte <= LAST_PRINTABLE_ASCII) {
            out_name[name_length++] = data[input_position];
        }
    }
    out_name[name_length] = '\0';
    return name_length;
}

// Stores client_name, name_length characters, as CLIENT_ID if it is the first TTYPE, and
// returns whether asking for more TTYPEs must stop.
static bool record_first_client_name(
    protocol_t* protocol, const char* client_name, int name_length)
{
    std::string& client_id = protocol->variables.stored_text(eMSDP_CLIENT_ID);
    if (client_id != "Unknown") {
        return false;
    }

    client_id.assign(client_name, static_cast<std::size_t>(name_length));

    // Cycling TTYPE locks up Windows telnet, whose first name is "ANSI"; no client worth cycling
    // sends that first.
    return std::strcmp(client_name, "ANSI") == 0;
}

// Asks for the next TTYPE until a name repeats: an RFC 1091 client repeats its last name to end
// the list, and another may start the list again from the client name. For a name that does not
// repeat, records it as the last TTYPE, records 256-colour support when the text after its first
// '-' is "-256color", and sends the request unless stop_cyclic_ttype is set.
static void request_next_ttype(
    const descriptor_t* descriptor, const char* client_name, bool stop_cyclic_ttype)
{
    protocol_t* protocol = descriptor->pProtocol;
    const std::string_view client_id = protocol->variables.text(eMSDP_CLIENT_ID);
    const bool has_last_ttype = protocol->last_ttype.has_value();
    const bool repeats_last_ttype = has_last_ttype && *protocol->last_ttype == client_name;
    const bool repeats_client_name
        = has_last_ttype && !repeats_last_ttype && client_id == client_name;
    if (repeats_last_ttype || repeats_client_name) {
        return;
    }

    static constexpr char ttype_request[]
        = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND, TELNET_IAC, TELNET_SE, '\0' };
    const char* type_suffix = std::strstr(client_name, "-");

    // Assigned into the stored string once there is one, reusing its buffer.
    protocol->last_ttype = client_name;

    const bool names_xterm_256
        = type_suffix != nullptr && equals_ignoring_case(type_suffix, "-256color");
    if (names_xterm_256) {
        // TinTin++, WinTin++ and BlowTorch show 256-colour support this way.
        enable_xterm_256(protocol);
    }

    if (!stop_cyclic_ttype) {
        send_to_client(descriptor, ttype_request);
    }
}

// Records the colours and character set an MTTS bit field after mtts_prefix_length characters
// of client_name claims.
static void apply_mtts_capabilities(
    protocol_t* protocol, const char* client_name, std::size_t mtts_prefix_length)
{
    // The MTTS capability bits the server reads.
    constexpr int mtts_ansi = 1;
    constexpr int mtts_utf_8 = 4;
    constexpr int mtts_xterm_256 = 8;
    const int mtts_bits = std::atoi(client_name + mtts_prefix_length);
    if ((mtts_bits & mtts_ansi) != 0) {
        protocol->variables.stored_number(eMSDP_ANSI_COLORS) = 1;
    }

    if ((mtts_bits & mtts_utf_8) != 0) {
        protocol->variables.stored_number(eMSDP_UTF_8) = 1;
    }

    if ((mtts_bits & mtts_xterm_256) != 0) {
        enable_xterm_256(protocol);
    }
}

// Splits a client_name made of a name_length-character name, a one-character separator and a
// version into CLIENT_ID and CLIENT_VERSION, cutting client_name to the name, and returns
// whether it had a version. A client_name with no version is left alone.
static bool split_client_version(protocol_t* protocol, char* client_name, std::size_t name_length)
{
    constexpr std::size_t separator_length = 1;
    const std::size_t version_start = name_length + separator_length;
    const std::size_t client_length = std::strlen(client_name);
    if (client_length <= version_start) {
        return false;
    }

    client_name[name_length] = '\0';
    protocol->variables.stored_text(eMSDP_CLIENT_ID).assign(client_name, name_length);
    protocol->variables.stored_text(eMSDP_CLIENT_VERSION).assign(client_name + version_start);
    return true;
}

// Whether client_name is a client some of whose versions support XTerm 256 colours.
static bool sometimes_supports_xterm_256(const char* client_name)
{
    static constexpr const char* client_names[] = { "CMUD", "ATLANTIS", "KILDCLIENT", "TINYFUGUE" };
    for (const char* candidate : client_names) {
        if (equals_ignoring_case(client_name, candidate)) {
            return true;
        }
    }
    return false;
}

// Records the colour support a TTYPE's client_name shows, and for Mudlet and DecafMUD the name
// and version it carries, which may cut client_name to the name.
static void apply_client_name_capabilities(protocol_t* protocol, char* client_name)
{
    static constexpr char mtts_prefix[] = "MTTS ";
    static constexpr char mudlet_name[] = "Mudlet";
    static constexpr char decafmud_name[] = "DecafMUD";

    if (starts_with_ignoring_case(mtts_prefix, client_name)) {
        constexpr std::size_t mtts_prefix_length = std::char_traits<char>::length(mtts_prefix);
        apply_mtts_capabilities(protocol, client_name, mtts_prefix_length);
    } else if (starts_with_ignoring_case(mudlet_name, client_name)) {
        // Mudlet beta 15 and later support 256 colours, but every Mudlet before 1.1 reports
        // version 1.0.
        protocol->xterm_256_support = eSOMETIMES;

        constexpr std::size_t mudlet_name_length = std::char_traits<char>::length(mudlet_name);
        if (split_client_version(protocol, client_name, mudlet_name_length)) {
            // Mudlet 1.1 and later support 256 colours.
            const std::string_view client_version
                = protocol->variables.text(eMSDP_CLIENT_VERSION);
            const bool supports_xterm_256 = client_version.compare("1.1") >= 0;
            if (supports_xterm_256) {
                enable_xterm_256(protocol);
            }
        }
    } else if (equals_ignoring_case(client_name, "EMACS-RINZAI")) {
        enable_xterm_256(protocol);
    } else if (starts_with_ignoring_case(decafmud_name, client_name)) {
        enable_xterm_256(protocol);

        constexpr std::size_t decafmud_name_length
            = std::char_traits<char>::length(decafmud_name);
        split_client_version(protocol, client_name, decafmud_name_length);
    } else if (sometimes_supports_xterm_256(client_name)) {
        protocol->xterm_256_support = eSOMETIMES;
    } else if (equals_ignoring_case(client_name, "ZMUD")) {
        protocol->xterm_256_support = eNO;
    }
}

// Acts on one TTYPE subnegotiation's data: records the client name it carries, asks for the
// next one, and records what the name shows the client supports.
static void answer_ttype_subnegotiation(const descriptor_t* descriptor, const char* data)
{
    protocol_t* protocol = descriptor->pProtocol;
    char client_name[MAX_CLIENT_NAME_LENGTH + 1];
    const int name_length = read_ttype_name(data, client_name);
    const bool stop_cyclic_ttype = record_first_client_name(protocol, client_name, name_length);
    request_next_ttype(descriptor, client_name, stop_cyclic_ttype);
    apply_client_name_capabilities(protocol, client_name);
}

// Records the window size a NAWS subnegotiation's data carries: the width, then the height, each
// as two bytes, high byte first.
static void read_window_size(protocol_t* protocol, const char* data)
{
    protocol->screen_width = static_cast<unsigned char>(data[0]);
    protocol->screen_width <<= 8;
    protocol->screen_width += static_cast<unsigned char>(data[1]);

    protocol->screen_height = static_cast<unsigned char>(data[2]);
    protocol->screen_height <<= 8;
    protocol->screen_height += static_cast<unsigned char>(data[3]);
}

// Acts on one complete subnegotiation for option. data is its NUL-terminated payload, size bytes
// long.
static void perform_subnegotiation(
    const descriptor_t* descriptor, char option, const char* data, int size)
{
    protocol_t* protocol = descriptor->pProtocol;

    switch (option) {
    case static_cast<char>(TELOPT_TTYPE):
        if (protocol->ttype_enabled) {
            answer_ttype_subnegotiation(descriptor, data);
        }
        break;

    case static_cast<char>(TELOPT_NAWS):
        if (protocol->naws_enabled) {
            read_window_size(protocol, data);
        }
        break;

    case static_cast<char>(TELOPT_CHARSET):
        // ISO-8859-1 is the only charset offered, and the server sends latin-1 whatever the
        // client answers, so the answer changes nothing.
        break;

    case static_cast<char>(TELOPT_MSDP):
        if (protocol->msdp_enabled) {
            parse_msdp(descriptor, data);
        }
        break;

    case static_cast<char>(TELOPT_ATCP):
        if (protocol->atcp_enabled) {
            parse_atcp(descriptor, data);
        }
        break;

    default:
        break;
    }
}

static void send_negotiation_sequence(const descriptor_t* descriptor, char command, char option)
{
    char sequence[4];

    sequence[0] = TELNET_IAC;
    sequence[1] = command;
    sequence[2] = option;
    sequence[3] = '\0';

    send_to_client(descriptor, sequence);
}

// Records will_do as option's state and returns whether the state changed. When it changed and
// send_reply is set, sends the offer or answer: DO or DONT for an option the client performs,
// WILL or WONT for one the server performs. MCCP is never sent.
static bool confirm_negotiation(
    const descriptor_t* descriptor, negotiated_t option, bool will_do, bool send_reply)
{
    bool changed = false;

    if (option >= eNEGOTIATED_TTYPE && option < eNEGOTIATED_MAX) {
        if (descriptor->pProtocol->option_states[option] != will_do) {
            descriptor->pProtocol->option_states[option] = will_do;

            changed = true;

            if (send_reply) {
                // DO and DONT answer for an option the client performs; WILL and WONT for one
                // the server performs.
                char client_option_reply = TELNET_DONT;
                char server_option_reply = TELNET_WONT;
                if (will_do) {
                    client_option_reply = TELNET_DO;
                    server_option_reply = TELNET_WILL;
                }

                switch (option) {
                case eNEGOTIATED_TTYPE:
                    send_negotiation_sequence(descriptor, client_option_reply, TELOPT_TTYPE);
                    break;
                case eNEGOTIATED_ECHO:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_ECHO);
                    break;
                case eNEGOTIATED_NAWS:
                    send_negotiation_sequence(descriptor, client_option_reply, TELOPT_NAWS);
                    break;
                case eNEGOTIATED_CHARSET:
                    send_negotiation_sequence(descriptor, client_option_reply, TELOPT_CHARSET);
                    break;
                case eNEGOTIATED_MSDP:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_MSDP);
                    break;
                case eNEGOTIATED_MSSP:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_MSSP);
                    break;
                case eNEGOTIATED_ATCP:
                    send_negotiation_sequence(
                        descriptor, client_option_reply, static_cast<char>(TELOPT_ATCP));
                    break;
                case eNEGOTIATED_MSP:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_MSP);
                    break;
                case eNEGOTIATED_MXP:
                    send_negotiation_sequence(descriptor, client_option_reply, TELOPT_MXP);
                    break;
                case eNEGOTIATED_MXP2:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_MXP);
                    break;
                case eNEGOTIATED_MCCP:
                    break;
                case eNEGOTIATED_EOR:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_EOR);
                    break;
                case eNEGOTIATED_SGA:
                    send_negotiation_sequence(descriptor, server_option_reply, TELOPT_SGA);
                    break;
                default:
                    changed = false;
                    break;
                }
            }
        }
    }

    return changed;
}

// Passes each value in an MSDP payload to execute_msdp_pair with the variable name before it.
// Names and values are cut to MAX_MSDP_SIZE characters.
static void parse_msdp(const descriptor_t* descriptor, const char* data)
{
    char pair_parts[MSDP_PAIR_PARTS][MAX_MSDP_SIZE + 1] = { { '\0' }, { '\0' } };
    char* write_position = nullptr;
    char* part_start = nullptr;

    while (*data != '\0') {
        switch (*data) {
        case MSDP_VAR:
        case MSDP_VAL:
            part_start = pair_parts[*data - 1];
            write_position = part_start;
            ++data;
            break;
        default:
            if (write_position != nullptr && write_position - part_start < MAX_MSDP_SIZE) {
                *write_position++ = *data;
                *write_position = '\0';
            }

            ++data;
            if (*data != '\0') {
                continue;
            }
        }

        execute_msdp_pair(descriptor, pair_parts[MSDP_VAR - 1], pair_parts[MSDP_VAL - 1]);
        pair_parts[MSDP_VAL - 1][0] = '\0';
    }
}

// Whether list_name asks for the list of variables a client can SEND or REPORT.
static bool names_sendable_variables(const char* list_name)
{
    if (equals_ignoring_case(list_name, "SENDABLE_VARIABLES")) {
        return true;
    }
    return equals_ignoring_case(list_name, "REPORTABLE_VARIABLES");
}

// Whether first and second are equal, ignoring ASCII case; usable at compile time.
static constexpr bool equals_ignoring_ascii_case(std::string_view first, std::string_view second)
{
    if (first.size() != second.size()) {
        return false;
    }

    for (std::size_t character_index = 0; character_index < first.size(); ++character_index) {
        if (ascii_lower(first[character_index]) != ascii_lower(second[character_index])) {
            return false;
        }
    }
    return true;
}

// Whether no two variable names in VARIABLE_NAME_TABLE are equal, ignoring case.
static constexpr bool variable_names_differ_ignoring_case()
{
    for (int first_index = eMSDP_NONE + 1; first_index < eMSDP_MAX; ++first_index) {
        for (int second_index = first_index + 1; second_index < eMSDP_MAX; ++second_index) {
            const std::string_view first_name = VARIABLE_NAME_TABLE[first_index].name;
            const std::string_view second_name = VARIABLE_NAME_TABLE[second_index].name;
            if (equals_ignoring_ascii_case(first_name, second_name)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(variable_names_differ_ignoring_case(),
    "Two MSDP variable names differ only in case, so find_variable finds only the first.");

// The variable whose name is name, ignoring case, or eMSDP_NONE.
static variable_t find_variable(const char* name)
{
    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        if (equals_ignoring_case(name, VARIABLE_NAME_TABLE[variable_index].name)) {
            return static_cast<variable_t>(variable_index);
        }
    }
    return eMSDP_NONE;
}

// Carries out SEND: sends the variable named variable_name, if there is one.
static void send_variable_named(const descriptor_t* descriptor, const char* variable_name)
{
    const variable_t variable = find_variable(variable_name);
    if (variable != eMSDP_NONE) {
        MSDPSend(descriptor, variable);
    }
}

// Carries out REPORT: marks the variable named variable_name, if there is one, reported and due
// to be sent.
static void report_variable_named(const descriptor_t* descriptor, const char* variable_name)
{
    const variable_t variable = find_variable(variable_name);
    if (variable != eMSDP_NONE) {
        descriptor->pProtocol->variables.report(variable);
    }
}

// Carries out UNREPORT: stops reporting the variable named variable_name, if there is one.
static void unreport_variable_named(const descriptor_t* descriptor, const char* variable_name)
{
    const variable_t variable = find_variable(variable_name);
    if (variable != eMSDP_NONE) {
        descriptor->pProtocol->variables.unreport(variable);
    }
}

// Carries out RESET: stops reporting every reported variable when list_name is
// REPORTABLE_VARIABLES, REPORTED_VARIABLES or VARIABLES.
static void reset_reported_variables(const descriptor_t* descriptor, const char* list_name)
{
    const bool resets_variables = equals_ignoring_case(list_name, "REPORTABLE_VARIABLES")
        || equals_ignoring_case(list_name, "REPORTED_VARIABLES")
        || equals_ignoring_case(list_name, "VARIABLES");
    if (!resets_variables) {
        return;
    }

    descriptor->pProtocol->variables.unreport_all();
}

// Sends, as the list list_name, the space-separated names of the variables for whose index
// includes returns true.
template <typename Includes>
static void send_variable_list(
    const descriptor_t* descriptor, const char* list_name, Includes includes)
{
    char name_list[MAX_OUTPUT_BUFFER];
    std::size_t list_length = 0;

    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        if (includes(variable_index)) {
            list_length = append_list_name(
                name_list, list_length, VARIABLE_NAME_TABLE[variable_index].name);
        }
    }
    name_list[list_length] = '\0';

    MSDPSendList(descriptor, list_name, name_list);
}

// Carries out LIST: sends the list list_name names. An unknown list sends nothing.
static void send_msdp_list(const descriptor_t* descriptor, const char* list_name)
{
    const protocol_t* protocol = descriptor->pProtocol;

    if (equals_ignoring_case(list_name, "COMMANDS")) {
        static constexpr char name_list[] = "LIST REPORT RESET SEND UNREPORT";
        MSDPSendList(descriptor, "COMMANDS", name_list);
    } else if (equals_ignoring_case(list_name, "LISTS")) {
        static constexpr char name_list[]
            = "COMMANDS LISTS CONFIGURABLE_VARIABLES REPORTABLE_VARIABLES "
              "REPORTED_VARIABLES SENDABLE_VARIABLES GUI_VARIABLES";
        MSDPSendList(descriptor, "LISTS", name_list);
    } else if (names_sendable_variables(list_name)) {
        send_variable_list(descriptor, list_name, [](int variable_index) -> bool {
            return !VARIABLE_NAME_TABLE[variable_index].gui;
        });
    } else if (equals_ignoring_case(list_name, "REPORTED_VARIABLES")) {
        send_variable_list(descriptor, list_name, [protocol](int variable_index) -> bool {
            return protocol->variables.is_reported(static_cast<variable_t>(variable_index));
        });
    } else if (equals_ignoring_case(list_name, "CONFIGURABLE_VARIABLES")) {
        send_variable_list(descriptor, "CONFIGURABLE_VARIABLES", [](int variable_index) -> bool {
            return VARIABLE_NAME_TABLE[variable_index].configurable;
        });
    } else if (equals_ignoring_case(list_name, "GUI_VARIABLES")) {
        send_variable_list(descriptor, list_name, [](int variable_index) -> bool {
            return VARIABLE_NAME_TABLE[variable_index].gui;
        });
    }
}

// Stores the printable bytes of value, cut to description's maximum, as variable's text when
// there are at least its minimum of them and the variable may still be written.
static void set_configurable_text(msdp_variables_t& variables, variable_t variable,
    const variable_name_t& description, const char* value)
{
    // A write-once variable, such as the client name, is set only while it is "Unknown": a
    // client may name itself but not replace a name already found.
    std::string& stored = variables.stored_text(variable);
    const bool can_write = !description.write_once || stored == "Unknown";
    if (!can_write) {
        return;
    }

    char stored_text[MAX_CONFIGURABLE_STRING_LENGTH + 1];
    int stored_length = 0;
    for (; stored_length < description.maximum && *value != '\0'; ++value) {
        const unsigned char value_byte = static_cast<unsigned char>(*value);
        if (value_byte >= FIRST_PRINTABLE_ASCII && value_byte <= LAST_PRINTABLE_ASCII) {
            stored_text[stored_length++] = *value;
        }
    }
    stored_text[stored_length++] = '\0';

    if (stored_length > description.minimum) {
        // stored_length counts the NUL.
        const std::size_t text_length = static_cast<std::size_t>(stored_length) - 1;
        stored.assign(stored_text, text_length);
    }
}

// Stores value, after any leading spaces, as variable's number when it is all digits and within
// description's minimum and maximum.
static void set_configurable_number(msdp_variables_t& variables, variable_t variable,
    const variable_name_t& description, const char* value)
{
    while (*value == ' ') {
        ++value;
    }

    const bool is_number = *value != '\0' && is_all_digits(value);
    if (!is_number) {
        return;
    }

    const int number = std::atoi(value);
    const bool in_range = number >= description.minimum && number <= description.maximum;
    if (in_range) {
        variables.stored_number(variable) = number;
    }
}

// Sets the configurable variable named variable_name, if there is one, from value.
static void set_configurable_variable(
    const descriptor_t* descriptor, const char* variable_name, const char* value)
{
    const variable_t variable = find_variable(variable_name);
    if (variable == eMSDP_NONE || !VARIABLE_NAME_TABLE[variable].configurable) {
        return;
    }

    const variable_name_t& description = VARIABLE_NAME_TABLE[variable];
    msdp_variables_t& variables = descriptor->pProtocol->variables;
    if (description.is_string) {
        set_configurable_text(variables, variable, description, value);
    } else {
        set_configurable_number(variables, variable, description, value);
    }
}

// Carries out a client's MSDP command (SEND, REPORT, RESET, UNREPORT or LIST) or sets a
// configurable variable. An empty name or value does nothing.
static void execute_msdp_pair(
    const descriptor_t* descriptor, const char* name, const char* value)
{
    if (name[0] == '\0' || value[0] == '\0') {
        return;
    }

    if (equals_ignoring_case(name, "SEND")) {
        send_variable_named(descriptor, value);
    } else if (equals_ignoring_case(name, "REPORT")) {
        report_variable_named(descriptor, value);
    } else if (equals_ignoring_case(name, "RESET")) {
        reset_reported_variables(descriptor, value);
    } else if (equals_ignoring_case(name, "UNREPORT")) {
        unreport_variable_named(descriptor, value);
    } else if (equals_ignoring_case(name, "LIST")) {
        send_msdp_list(descriptor, value);
    } else {
        // Otherwise name may be a configurable variable.
        set_configurable_variable(descriptor, name, value);
    }
}

// parse_msdp for an ATCP payload, where '@' starts the name and a space the value.
static void parse_atcp(const descriptor_t* descriptor, const char* data)
{
    char pair_parts[MSDP_PAIR_PARTS][MAX_MSDP_SIZE + 1] = { { '\0' }, { '\0' } };
    char* write_position = nullptr;
    char* part_start = nullptr;

    while (*data != '\0') {
        switch (*data) {
        case '@':
            part_start = pair_parts[0];
            write_position = part_start;
            data++;
            break;
        case ' ':
            part_start = pair_parts[1];
            write_position = part_start;
            data++;
            break;
        default:
            if (write_position != nullptr && write_position - part_start < MAX_MSDP_SIZE) {
                *write_position++ = *data;
                *write_position = '\0';
            }

            ++data;
            if (*data != '\0') {
                continue;
            }
        }

        execute_msdp_pair(descriptor, pair_parts[MSDP_VAR - 1], pair_parts[MSDP_VAL - 1]);
        pair_parts[MSDP_VAL - 1][0] = '\0';
    }
}

// Sends the MSSP status message.
static void send_mssp(const descriptor_t* descriptor)
{
    // IAC SB MSSP, and the room kept for IAC SE and the NUL.
    constexpr int mssp_header_length = 3;
    constexpr int mssp_trailer_room = 4;
    constexpr int pair_buffer_size = 128;
    char mssp_text[MAX_MSSP_BUFFER];
    char pair_text[pair_buffer_size];
    int mssp_length = mssp_header_length;

    // Names and values follow the MSSP specification, http://tintin.sourceforge.net/mssp/;
    // crawlers reject the data when one is misspelt.
    static constexpr MSSP_t mssp_table[] = {
        // Required
        { "NAME", MUD_NAME },
        { "PLAYERS", "0" },
        { "UPTIME", "0" },

        // Generic
        { "CRAWL DELAY", "-1" },
    };

    std::snprintf(mssp_text, sizeof(mssp_text), "%c%c%c", IAC, SB, TELOPT_MSSP);

    for (const MSSP_t& entry : mssp_table) {
        int pair_length;

        std::snprintf(pair_text, sizeof(pair_text), "%c%.*s%c%.*s", MSSP_VAR,
            printf_precision(entry.name), entry.name.data(), MSSP_VAL,
            printf_precision(entry.value), entry.value.data());

        // A pair that does not fit is left out.
        pair_length = std::strlen(pair_text);
        if (pair_length + mssp_length < MAX_MSSP_BUFFER - mssp_trailer_room) {
            std::memcpy(
                mssp_text + mssp_length, pair_text, static_cast<std::size_t>(pair_length) + 1);
            mssp_length += pair_length;
        }
    }

    std::snprintf(mssp_text + mssp_length,
        sizeof(mssp_text) - static_cast<std::size_t>(mssp_length), "%c%c", IAC, SE);

    send_to_client(descriptor, mssp_text);
}

// The value after tag, such as "VERSION=", in text: letters, digits and dots, perhaps quoted. It
// is in a static buffer that the next call overwrites. Returns nullptr when tag is absent, or
// when its value runs to the end of text or reaches 60 characters.
static char* find_mxp_tag_value(const char* tag, const char* text)
{
    constexpr int value_buffer_size = 64;
    constexpr int max_value_length = 60;
    static char tag_value[value_buffer_size];
    const char* cursor = std::strstr(text, tag);

    if (cursor != nullptr) {
        const char* text_end = text + std::strlen(text);

        cursor += std::strlen(tag);

        if (cursor < text_end) {
            int value_length = 0;

            if (*cursor == '\"') {
                cursor++;
            }

            for (; cursor < text_end && value_length < max_value_length; ++cursor) {
                char letter = *cursor;
                if (letter == '.' || is_ascii_digit(letter) || is_ascii_alpha(letter)) {
                    tag_value[value_length++] = letter;
                } else {
                    tag_value[value_length] = '\0';
                    return tag_value;
                }
            }
        }
    }

    return nullptr;
}

// The ANSI colour nearest to the red, green and blue digits, each 0 to 5: the background colour
// when background is set, otherwise the bold or normal foreground colour.
static const char* get_ansi_colour(bool background, int red, int green, int blue)
{
    const char* background_colour = nullptr;
    const char* bold_colour = nullptr;
    const char* dark_colour = nullptr;
    bool is_bright = false;

    if (red == green && red == blue && red < 2) {
        background_colour = ANSI_BACKGROUND_BLACK;
        bold_colour = ANSI_BOLD_BLACK;
        dark_colour = ANSI_DARK_BLACK;
        is_bright = red >= 1;
    } else if (red == green && red == blue) {
        background_colour = ANSI_BACKGROUND_WHITE;
        bold_colour = ANSI_BOLD_WHITE;
        dark_colour = ANSI_DARK_WHITE;
        is_bright = red >= 4;
    } else if (red > green && red > blue) {
        background_colour = ANSI_BACKGROUND_RED;
        bold_colour = ANSI_BOLD_RED;
        dark_colour = ANSI_DARK_RED;
        is_bright = red >= 3;
    } else if (red == green && red > blue) {
        background_colour = ANSI_BACKGROUND_YELLOW;
        bold_colour = ANSI_BOLD_YELLOW;
        dark_colour = ANSI_DARK_YELLOW;
        is_bright = red >= 3;
    } else if (red == blue && red > green) {
        background_colour = ANSI_BACKGROUND_MAGENTA;
        bold_colour = ANSI_BOLD_MAGENTA;
        dark_colour = ANSI_DARK_MAGENTA;
        is_bright = red >= 3;
    } else if (green > blue) {
        background_colour = ANSI_BACKGROUND_GREEN;
        bold_colour = ANSI_BOLD_GREEN;
        dark_colour = ANSI_DARK_GREEN;
        is_bright = green >= 3;
    } else if (green == blue) {
        background_colour = ANSI_BACKGROUND_CYAN;
        bold_colour = ANSI_BOLD_CYAN;
        dark_colour = ANSI_DARK_CYAN;
        is_bright = green >= 3;
    } else {
        // Blue is the largest.
        background_colour = ANSI_BACKGROUND_BLUE;
        bold_colour = ANSI_BOLD_BLUE;
        dark_colour = ANSI_DARK_BLUE;
        is_bright = blue >= 3;
    }

    if (background) {
        return background_colour;
    }
    if (is_bright) {
        return bold_colour;
    }
    return dark_colour;
}

// The xterm 256-colour escape code for the red, green and blue digits, each 0 to 5, in a static
// buffer that the next call overwrites.
static const char* get_xterm_colour(bool background, int red, int green, int blue)
{
    static char escape_code[16];
    int xterm_colour_index = 16 + (red * 36) + (green * 6) + blue;
    std::snprintf(escape_code, sizeof(escape_code), "\033[%c8;5;%c%c%cm",
        '3' + background, // 3 sets the foreground, 4 the background.
        '0' + (xterm_colour_index / 100), // The palette index's hundreds,
        '0' + ((xterm_colour_index % 100) / 10), // tens
        '0' + (xterm_colour_index % 10)); // and units.
    return escape_code;
}

// Whether rgb_code starts with a colour code: 'F' or 'B' in either case, then three digits from
// 0 to 5. Characters after the fourth are ignored.
static bool is_valid_colour(std::string_view rgb_code)
{
    if (rgb_code.size() < static_cast<std::size_t>(RGB_COLOUR_CODE_LENGTH)) {
        return false;
    }

    if (ascii_lower(rgb_code[0]) != 'f' && ascii_lower(rgb_code[0]) != 'b') {
        return false;
    }

    for (int digit_index = 1; digit_index < RGB_COLOUR_CODE_LENGTH; ++digit_index) {
        if (rgb_code[digit_index] < '0' || rgb_code[digit_index] > '5') {
            return false;
        }
    }

    return true;
}

// Whether first and second are equal, ignoring case.
static bool equals_ignoring_case(std::string_view first, std::string_view second)
{
    if (first.size() != second.size()) {
        return false;
    }
    return std::equal(first.begin(), first.end(), second.begin(),
        [](char first_character, char second_character) -> bool {
            return ascii_lower(first_character) == ascii_lower(second_character);
        });
}

// Whether text starts with prefix, ignoring case. The prefix comes first.
static bool starts_with_ignoring_case(const char* prefix, const char* text)
{
    while (*prefix != '\0' && ascii_lower(*prefix) == ascii_lower(*text)) {
        ++prefix;
        ++text;
    }
    return (*prefix == '\0');
}

// Whether every character of text is a decimal digit; true for an empty string.
static bool is_all_digits(const char* text)
{
    while (*text != '\0' && is_ascii_digit(*text)) {
        ++text;
    }
    return (*text == '\0');
}
