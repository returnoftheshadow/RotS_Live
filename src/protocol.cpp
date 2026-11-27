/******************************************************************************
 Protocol snippet by KaVir.  Released into the public domain in February 2011.

 In case this is not legally possible:

 The copyright holder grants any entity the right to use this work for any
 purpose, without any conditions, unless such conditions are required by law.
 ******************************************************************************/

/******************************************************************************
 This snippet was originally designed to be codebase independent, but has been
 modified slightly so that it runs out-of-the-box on Merc derivatives.  To use
 it for other codebases, just change the code in the "Diku/Merc" section below.
 ******************************************************************************/

/******************************************************************************
 Header files.
 ******************************************************************************/

#include <alloca.h>
#include <arpa/telnet.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>

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

// An MSDP or ATCP pair has two parts, the variable and the value, indexed by MSDP_VAR - 1 and
// MSDP_VAL - 1.
constexpr int MSDP_PAIR_PARTS = 2;
static_assert(MSDP_VAR - 1 < MSDP_PAIR_PARTS && MSDP_VAL - 1 < MSDP_PAIR_PARTS,
    "An MSDP pair part's index is outside the parsed pair.");

/******************************************************************************
 The following section is for Diku/Merc derivatives.  Replace as needed.
 ******************************************************************************/

#include "comm.h"

static void send_to_client(const descriptor_t* descriptor, const char* text)
{
    if (descriptor != nullptr) {
        if (descriptor->pProtocol->WriteOOB > 0) {
            descriptor->pProtocol->WriteOOB = 2;
        }
    }
    write_to_descriptor(descriptor->descriptor, text);
}

static void report_bug(const char* text) { vmudlog(NRM, text); }

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

/******************************************************************************
 MSDP file-scope variables.
 ******************************************************************************/

/* These are for the GUI_VARIABLES, my unofficial extension of MSDP.  They're
 * intended for clients that wish to offer a generic GUI - not as nice as a
 * custom GUI, admittedly, but still better than a plain terminal window.
 *
 * These are per-player so that they can be customised for different characters
 * (eg changing 'mana' to 'blood' for vampires).  You could even allow players
 * to customise the buttons and gauges themselves if you wish.
 */
static const char GUI_BUTTON_1[] = "\005\002Help\002help\006";
static const char GUI_BUTTON_2[] = "\005\002Look\002look\006";
static const char GUI_BUTTON_3[] = "\005\002Score\002score\006";
static const char GUI_BUTTON_4[] = "\005\002Equipment\002equipment\006";
static const char GUI_BUTTON_5[] = "\005\002Inventory\002inventory\006";

static const char GUI_GAUGE_1[] = "\005\002Health\002red\002HEALTH\002HEALTH_MAX\006";
static const char GUI_GAUGE_2[] = "\005\002Mana\002blue\002MANA\002MANA_MAX\006";
static const char GUI_GAUGE_3[] = "\005\002Movement\002green\002MOVEMENT\002MOVEMENT_MAX\006";
static const char GUI_GAUGE_4[] = "\005\002Exp TNL\002yellow\002EXPERIENCE\002EXPERIENCE_MAX\006";
static const char GUI_GAUGE_5[]
    = "\005\002Opponent\002darkred\002OPPONENT_HEALTH\002OPPONENT_HEALTH_MAX\006";

/******************************************************************************
 MSDP variable table.
 ******************************************************************************/

/* Macros for readability, but you can remove them if you don't like them */
#define NUMBER_READ_ONLY(variable, name) \
    { name, nullptr, variable, 0, -1, 0, false, false, false, false }
#define NUMBER_READ_ONLY_SET_TO(variable, name, default_value) \
    { name, nullptr, variable, 0, -1, default_value, false, false, false, false }
#define STRING_READ_ONLY(variable, name) \
    { name, nullptr, variable, 0, -1, 0, true, false, false, false }
#define BOOLEAN_SET_TO(variable, name, default_value) \
    { name, nullptr, variable, 0, 1, default_value, false, true, false, false }
#define STRING_WITH_LENGTH_OF(variable, name, min_length, max_length) \
    { name, nullptr, variable, min_length, max_length, 0, true, true, false, false }
#define STRING_WRITE_ONCE(variable, name, min_length, max_length) \
    { name, nullptr, variable, min_length, max_length, 0, true, true, true, false }
#define STRING_GUI(variable, name, default_text) \
    { name, default_text, variable, 0, -1, 0, true, false, false, true }

static constexpr variable_name_t VARIABLE_NAME_TABLE[eMSDP_MAX + 1] = {
    /* General */
    STRING_READ_ONLY(eMSDP_CHARACTER_NAME, "CHARACTER_NAME"),
    STRING_READ_ONLY(eMSDP_SERVER_ID, "SERVER_ID"),
    NUMBER_READ_ONLY(eMSDP_SERVER_TIME, "SERVER_TIME"),
    NUMBER_READ_ONLY_SET_TO(eMSDP_SNIPPET_VERSION, "SNIPPET_VERSION", SNIPPET_VERSION),

    /* Character */
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
    NUMBER_READ_ONLY(eMDSP_SPELL_SAVE, "SPELL_SAVE"),
    NUMBER_READ_ONLY(eMDSP_SPELL_PEN, "SPELL_PEN"),
    NUMBER_READ_ONLY(eMDSP_SPELL_POWER, "SPELL_POWER"),
    NUMBER_READ_ONLY(eMDSP_ARMOUR_ABS, "ARMOUR_ABS"),
    NUMBER_READ_ONLY(eMDSP_OFFENSIVE_BONUS, "OFFENSIVE_BONUS"),
    NUMBER_READ_ONLY(eMDSP_PARRY, "PARRY"),
    NUMBER_READ_ONLY(eMDSP_DODGE, "DODGE"),
    NUMBER_READ_ONLY(eMDSP_ATTACK_SPEED, "ATTACK_SPEED"),
    STRING_READ_ONLY(eMDSP_TACTIC, "TACTIC"),
    STRING_READ_ONLY(eMDSP_SPECIALIZATION, "SPECIALIZATION"),
    NUMBER_READ_ONLY(eMDSP_PERCEPTION, "PERCEPTION"),
    NUMBER_READ_ONLY(eMDSP_WILLPOWER, "WILLPOWER"),
    NUMBER_READ_ONLY(eMDSP_SKILL_ENCUMBRANCE, "SKILL_ENCUMBRANCE"),
    NUMBER_READ_ONLY(eMDSP_MOVEMENT_ENCUMBRANCE, "MOVEMENT_ENCUMBRANCE"),
    NUMBER_READ_ONLY(eMDSP_CARRIED_WEIGHT, "CARRIED_WEIGHT"),
    NUMBER_READ_ONLY(eMDSP_WARRIOR_LEVEL, "WARRIOR_LEVEL"),
    NUMBER_READ_ONLY(eMDSP_WARRIOR_LEVEL_MAX, "WARRIOR_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMDSP_RANGER_LEVEL, "RANGER_LEVEL"),
    NUMBER_READ_ONLY(eMDSP_RANGER_LEVEL_MAX, "RANGER_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMDSP_MYSTIC_LEVEL, "MYSTIC_LEVEL"),
    NUMBER_READ_ONLY(eMDSP_MYSTIC_LEVEL_MAX, "MYSTIC_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMDSP_MAGE_LEVEL, "MAGE_LEVEL"),
    NUMBER_READ_ONLY(eMDSP_MAGE_LEVEL_MAX, "MAGE_LEVEL_MAX"),
    NUMBER_READ_ONLY(eMDSP_HEALTH_REGENERATION, "HEALTH_REGENERATION"),
    NUMBER_READ_ONLY(eMDSP_STAMINA_REGENERATION, "STAMINA_REGENERATION"),
    NUMBER_READ_ONLY(eMDSP_MOVEMENT_REGENERATION, "MOVEMENT_REGENERATION"),

    /* Combat */
    NUMBER_READ_ONLY(eMSDP_OPPONENT_HEALTH, "OPPONENT_HEALTH"),
    NUMBER_READ_ONLY(eMSDP_OPPONENT_HEALTH_MAX, "OPPONENT_HEALTH_MAX"),
    STRING_READ_ONLY(eMSDP_OPPONENT_LEVEL, "OPPONENT_LEVEL"),
    STRING_READ_ONLY(eMSDP_OPPONENT_NAME, "OPPONENT_NAME"),
    STRING_READ_ONLY(eMSDP_OPPONENT_AGE, "OPPONENT_AGE"),

    /* World */
    STRING_READ_ONLY(eMSDP_AREA_NAME, "AREA_NAME"),
    STRING_READ_ONLY(eMSDP_ROOM, "ROOM"),
    STRING_READ_ONLY(eMSDP_ROOM_EXITS, "ROOM_EXITS"),
    STRING_READ_ONLY(eMSDP_ROOM_NAME, "ROOM_NAME"),
    NUMBER_READ_ONLY(eMSDP_ROOM_VNUM, "ROOM_VNUM"),
    STRING_READ_ONLY(eMSDP_WORLD_TIME, "WORLD_TIME"),
    STRING_READ_ONLY(eMDSP_WEATHER, "WEATHER"),

    /* Configurable variables */
    STRING_WRITE_ONCE(eMSDP_CLIENT_ID, "CLIENT_ID", 1, 40),
    STRING_WRITE_ONCE(eMSDP_CLIENT_VERSION, "CLIENT_VERSION", 1, 40),
    STRING_WITH_LENGTH_OF(eMSDP_PLUGIN_ID, "PLUGIN_ID", 1, 40),
    BOOLEAN_SET_TO(eMSDP_ANSI_COLORS, "ANSI_COLORS", 1),
    BOOLEAN_SET_TO(eMSDP_XTERM_256_COLORS, "XTERM_256_COLORS", 0),
    BOOLEAN_SET_TO(eMSDP_UTF_8, "UTF_8", 0),
    BOOLEAN_SET_TO(eMSDP_SOUND, "SOUND", 0),
    BOOLEAN_SET_TO(eMSDP_MXP, "MXP", 0),

    /* GUI variables */
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

    /* Group information */
    STRING_READ_ONLY(eMSDP_GROUP, "GROUP"),

    NUMBER_READ_ONLY(eMSDP_MAX, "") /* This must always be last. */
};

// The most characters a client may store in a configurable string variable.
constexpr int MAX_CONFIGURABLE_STRING_LENGTH = 40;

// The largest Max of any configurable string variable in VARIABLE_NAME_TABLE.
static constexpr int longest_configurable_string()
{
    int longest = 0;
    for (const variable_name_t& entry : VARIABLE_NAME_TABLE) {
        if (entry.bConfigurable && entry.bString && entry.Max > longest) {
            longest = entry.Max;
        }
    }
    return longest;
}

static_assert(longest_configurable_string() <= MAX_CONFIGURABLE_STRING_LENGTH,
    "A configurable string variable is longer than execute_msdp_pair's buffer.");

// The length of a space-separated list of every name in VARIABLE_NAME_TABLE, with its NUL.
static constexpr std::size_t all_variable_names_length()
{
    std::size_t length = 0;
    for (int index = eMSDP_NONE + 1; index < eMSDP_MAX; ++index) {
        length += std::char_traits<char>::length(VARIABLE_NAME_TABLE[index].pName) + 1;
    }
    return length;
}

static_assert(all_variable_names_length() <= MAX_OUTPUT_BUFFER,
    "execute_msdp_pair's variable lists do not fit in MAX_OUTPUT_BUFFER.");

// Appends name to the space-separated list of list_length characters in out_list, which has room,
// and returns the list's new length. Writes no NUL.
static std::size_t append_list_name(char* out_list, std::size_t list_length, const char* name)
{
    if (list_length != 0) {
        out_list[list_length] = ' ';
        ++list_length;
    }
    const std::size_t name_length = std::strlen(name);
    std::memcpy(out_list + list_length, name, name_length);
    return list_length + name_length;
}

/******************************************************************************
 Local function prototypes.
 ******************************************************************************/

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
static bool is_valid_colour(const char* rgb_code);

static bool equals_ignoring_case(const char* first, const char* second);
static bool starts_with_ignoring_case(const char* prefix, const char* text);
static bool is_all_digits(const char* text);
[[nodiscard]] static char* copy_string(const char* value);
[[nodiscard]] static char* copy_string(std::string_view value);
[[nodiscard]] static char* create_string(std::size_t length);
static std::size_t msdp_sanitized_length(std::string_view value);
static char* msdp_write_sanitized(std::string_view value, char* out_text);

/******************************************************************************
 ANSI colour codes.
 ******************************************************************************/

static const char ANSI_CLEAR[] = "\033[0;00m"; /* Remove colour */

static const char ANSI_DARK_BLACK[] = "\033[0;30m"; /* Black foreground */
static const char ANSI_DARK_RED[] = "\033[0;31m"; /* Red foreground */
static const char ANSI_DARK_GREEN[] = "\033[0;32m"; /* Green foreground */
static const char ANSI_DARK_YELLOW[] = "\033[0;33m"; /* Yellow foreground */
static const char ANSI_DARK_BLUE[] = "\033[0;34m"; /* Blue foreground */
static const char ANSI_DARK_MAGENTA[] = "\033[0;35m"; /* Magenta foreground */
static const char ANSI_DARK_CYAN[] = "\033[0;36m"; /* Cyan foreground */
static const char ANSI_DARK_WHITE[] = "\033[0;37m"; /* White foreground */

static const char ANSI_BOLD_BLACK[] = "\033[1;30m"; /* Grey foreground */
static const char ANSI_BOLD_RED[] = "\033[1;31m"; /* Bright red foreground */
static const char ANSI_BOLD_GREEN[] = "\033[1;32m"; /* Bright green foreground */
static const char ANSI_BOLD_YELLOW[] = "\033[1;33m"; /* Bright yellow foreground */
static const char ANSI_BOLD_BLUE[] = "\033[1;34m"; /* Bright blue foreground */
static const char ANSI_BOLD_MAGENTA[] = "\033[1;35m"; /* Bright magenta foreground */
static const char ANSI_BOLD_CYAN[] = "\033[1;36m"; /* Bright cyan foreground */
static const char ANSI_BOLD_WHITE[] = "\033[1;37m"; /* Bright white foreground */

static const char ANSI_BACKGROUND_BLACK[] = "\033[1;40m"; /* Black background */
static const char ANSI_BACKGROUND_RED[] = "\033[1;41m"; /* Red background */
static const char ANSI_BACKGROUND_GREEN[] = "\033[1;42m"; /* Green background */
static const char ANSI_BACKGROUND_YELLOW[] = "\033[1;43m"; /* Yellow background */
static const char ANSI_BACKGROUND_BLUE[] = "\033[1;44m"; /* Blue background */
static const char ANSI_BACKGROUND_MAGENTA[] = "\033[1;45m"; /* Magenta background */
static const char ANSI_BACKGROUND_CYAN[] = "\033[1;46m"; /* Cyan background */
static const char ANSI_BACKGROUND_WHITE[] = "\033[1;47m"; /* White background */

/******************************************************************************
 Protocol global functions.
 ******************************************************************************/

protocol_t* ProtocolCreate()
{
    protocol_t* protocol;

    /* Called the first time we enter - make sure the table is correct */
    static bool table_checked = false;
    if (!table_checked) {
        table_checked = true;
        for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
            if (VARIABLE_NAME_TABLE[variable_index].Variable != variable_index) {
                report_bug("MSDP: Variable table does not match the enums in the header.\n");
                break;
            }
        }
    }

    protocol = new protocol_t;
    protocol->WriteOOB = 0;
    for (int option_index = eNEGOTIATED_TTYPE; option_index < eNEGOTIATED_MAX; ++option_index) {
        protocol->Negotiated[option_index] = false;
    }
    protocol->bIACMode = false;
    protocol->bNegotiated = false;
    protocol->bRenegotiate = false;
    protocol->bNeedMXPVersion = false;
    protocol->bBlockMXP = false;
    protocol->PendingInputLength = 0;
    std::memset(protocol->PendingInput, 0, sizeof(protocol->PendingInput));
    protocol->IacInputLength = 0;
    std::memset(protocol->IacInput, 0, sizeof(protocol->IacInput));
    protocol->bTTYPE = false;
    protocol->bECHO = false;
    protocol->bNAWS = false;
    protocol->bCHARSET = false;
    protocol->bMSDP = true;
    protocol->bMSSP = false;
    protocol->bATCP = false;
    protocol->bMSP = false;
    protocol->bMXP = false;
    protocol->bMCCP = false;
    protocol->bEOR = false;
    protocol->bSGA = false;
    protocol->b256Support = eUNKNOWN;
    protocol->ScreenWidth = 0;
    protocol->ScreenHeight = 0;
    protocol->pMXPVersion = copy_string("Unknown");
    protocol->pLastTTYPE = nullptr;
    protocol->pVariables = new MSDP_t*[eMSDP_MAX];

    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        protocol->pVariables[variable_index] = new MSDP_t;
        protocol->pVariables[variable_index]->bReport = true;
        /* Prime server-reported variables so the first update after login sends
           them even when their value is still the default -- otherwise a stat
           that is legitimately 0 (spell pen/power, carried weight, an unused
           profession level) is never transmitted and looks unsupported to the
           client. Client-owned config and GUI template variables are excluded. */
        protocol->pVariables[variable_index]->bDirty
            = !VARIABLE_NAME_TABLE[variable_index].bConfigurable
            && !VARIABLE_NAME_TABLE[variable_index].bGUI;
        protocol->pVariables[variable_index]->ValueInt = 0;
        protocol->pVariables[variable_index]->pValueString = nullptr;

        if (VARIABLE_NAME_TABLE[variable_index].bString) {
            if (VARIABLE_NAME_TABLE[variable_index].pDefault != nullptr) {
                protocol->pVariables[variable_index]->pValueString
                    = copy_string(VARIABLE_NAME_TABLE[variable_index].pDefault);
            } else if (VARIABLE_NAME_TABLE[variable_index].bConfigurable) {
                protocol->pVariables[variable_index]->pValueString = copy_string("Unknown");
            } else {
                /* Use an empty string */
                protocol->pVariables[variable_index]->pValueString = copy_string("");
            }
        } else if (VARIABLE_NAME_TABLE[variable_index].Default != 0) {
            protocol->pVariables[variable_index]->ValueInt
                = VARIABLE_NAME_TABLE[variable_index].Default;
        }
    }

    return protocol;
}

void ProtocolDestroy(protocol_t* protocol)
{
    // ProtocolCreate makes the protocol, its variable table and each variable with new; only
    // the strings come from copy_string's malloc.
    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        std::free(protocol->pVariables[variable_index]->pValueString);
        delete protocol->pVariables[variable_index];
    }

    delete[] protocol->pVariables;
    std::free(protocol->pLastTTYPE);
    std::free(protocol->pMXPVersion);
    delete protocol;
}

void ProtocolInput(const descriptor_t* descriptor, const char* data, int size, char* out_text)
{
    static char command_text[MAX_PROTOCOL_BUFFER + 1];
    int command_length = 0;
    int position;

    protocol_t* protocol = protocol_of(descriptor);
    const bool has_input = data != nullptr && size > 0;
    if (protocol == nullptr || !has_input || out_text == nullptr) {
        return;
    }

    char* subnegotiation = protocol->IacInput;
    int subnegotiation_length = 0;
    if (protocol->bIACMode) {
        subnegotiation_length = protocol->IacInputLength;
    }

    if (protocol->PendingInputLength > 0) {
        char* combined = static_cast<char*>(alloca(protocol->PendingInputLength + size));
        std::memcpy(combined, protocol->PendingInput, protocol->PendingInputLength);
        std::memcpy(combined + protocol->PendingInputLength, data, size);
        data = combined;
        size += protocol->PendingInputLength;
        protocol->PendingInputLength = 0;
    }

    for (position = 0; position < size; ++position) {
        const bool has_next = (position + 1) < size;
        const bool has_two_more = (position + 2) < size;
        const bool has_three_more = (position + 3) < size;
        const bool is_iac = data[position] == TELNET_IAC;
        const bool is_escape = data[position] == ANSI_ESCAPE;
        const bool is_doubled_iac = is_iac && has_next && data[position + 1] == TELNET_IAC;
        const bool is_mxp_line_tag = is_escape && has_three_more && data[position + 1] == '['
            && std::isdigit(data[position + 2]) && data[position + 3] == 'z';

        /* If we'd overflow the buffer, we just ignore the input */
        const bool command_buffer_full = command_length >= MAX_PROTOCOL_BUFFER;
        const bool subnegotiation_buffer_full = subnegotiation_length >= MAX_PROTOCOL_BUFFER;
        if (command_buffer_full || subnegotiation_buffer_full) {
            report_bug("ProtocolInput: Too much incoming data to store in the buffer.\n");
            if (subnegotiation_buffer_full) {
                protocol->bIACMode = false;
                protocol->IacInputLength = 0;
                protocol->IacInput[0] = '\0';
            }
            return;
        }

        /* IAC IAC is treated as a single value of 255 */
        if (is_doubled_iac) {
            if (protocol->bIACMode) {
                subnegotiation[subnegotiation_length++] = TELNET_IAC;
            } else {
                /* In-band command */
                command_text[command_length++] = TELNET_IAC;
            }
            position++;
        } else if (protocol->bIACMode) {
            const bool ends_subnegotiation = is_iac && has_next && data[position + 1] == TELNET_SE;

            /* End subnegotiation. */
            if (is_iac && !has_next) {
                protocol->PendingInput[0] = data[position];
                protocol->PendingInputLength = 1;
                break;
            } else if (ends_subnegotiation) {
                position++;
                protocol->bIACMode = false;
                subnegotiation[subnegotiation_length] = '\0';
                if (subnegotiation_length >= 2) {
                    perform_subnegotiation(descriptor, subnegotiation[0], &subnegotiation[1],
                        subnegotiation_length - 1);
                }
                subnegotiation_length = 0;
                protocol->IacInputLength = 0;
            } else {
                subnegotiation[subnegotiation_length++] = data[position];
            }
        } else if (is_escape && !has_three_more) {
            const int remaining = size - position;
            const bool may_start_mxp_line_tag = remaining == 1
                || (remaining >= 2 && data[position + 1] == '[')
                || (remaining >= 3 && data[position + 1] == '['
                    && std::isdigit(data[position + 2]));
            if (may_start_mxp_line_tag) {
                std::memcpy(protocol->PendingInput, data + position, remaining);
                protocol->PendingInputLength = remaining;
                break;
            }
        } else if (is_mxp_line_tag) {
            constexpr int line_tag_length = 4;
            char mxp_text[MXP_BUFFER_SIZE];
            char* tag_value = nullptr;
            int tag_length = 0;

            position += line_tag_length; /* Skip to the start of the MXP sequence. */

            while (position < size && data[position] != '>' && tag_length < MAX_MXP_TAG_LENGTH) {
                mxp_text[tag_length++] = data[position++];
            }
            mxp_text[tag_length++] = '>';
            mxp_text[tag_length] = '\0';

            tag_value = find_mxp_tag_value("CLIENT=", mxp_text);
            if (tag_value != nullptr) {
                /* Overwrite the previous client name - this is harder to fake */
                std::free(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                protocol->pVariables[eMSDP_CLIENT_ID]->pValueString = copy_string(tag_value);
            }

            tag_value = find_mxp_tag_value("VERSION=", mxp_text);
            if (tag_value != nullptr) {
                const char* client_name = protocol->pVariables[eMSDP_CLIENT_ID]->pValueString;

                std::free(protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString = copy_string(tag_value);

                if (equals_ignoring_case("MUSHCLIENT", client_name)) {
                    /* MUSHclient 4.02 and later supports 256 colours. */
                    if (std::strcmp(tag_value, "4.02") >= 0) {
                        protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        protocol->b256Support = eYES;
                    } else {
                        /* We know for sure that 256 colours are not supported */
                        protocol->b256Support = eNO;
                    }
                } else if (equals_ignoring_case("CMUD", client_name)) {
                    /* CMUD 3.04 and later supports 256 colours. */
                    if (std::strcmp(tag_value, "3.04") >= 0) {
                        protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        protocol->b256Support = eYES;
                    } else {
                        /* We know for sure that 256 colours are not supported */
                        protocol->b256Support = eNO;
                    }
                } else if (equals_ignoring_case("ATLANTIS", client_name)) {
                    /* Atlantis 0.9.9.0 supports XTerm 256 colours, but it doesn't
                     * yet have MXP.  However MXP is planned, so once it responds
                     * to a <VERSION> tag we'll know we can use 256 colours.
                     */
                    protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    protocol->b256Support = eYES;
                }
            }

            tag_value = find_mxp_tag_value("MXP=", mxp_text);
            if (tag_value != nullptr) {
                std::free(protocol->pMXPVersion);
                protocol->pMXPVersion = copy_string(tag_value);
            }

            if (std::strcmp(protocol->pMXPVersion, "Unknown") != 0) {
                send_to_client(descriptor, "\n");
                std::snprintf(mxp_text, sizeof(mxp_text),
                    "MXP version %s detected and enabled.\r\n", protocol->pMXPVersion);
                send_info_message(descriptor, mxp_text);
            }
        } else {
            /* In-band command */
            if (data[position] == TELNET_IAC) {
                if (!has_next) {
                    protocol->PendingInput[0] = data[position];
                    protocol->PendingInputLength = 1;
                    break;
                }

                switch (data[position + 1]) {
                case TELNET_SB: /* Begin subnegotiation. */
                    position++;
                    protocol->bIACMode = true;
                    break;

                case TELNET_DO: /* Handshake. */
                case TELNET_DONT:
                case TELNET_WILL:
                case TELNET_WONT:
                    if (!has_two_more) {
                        protocol->PendingInput[0] = data[position];
                        protocol->PendingInput[1] = data[position + 1];
                        protocol->PendingInputLength = 2;
                        break;
                    }
                    perform_handshake(descriptor, data[position + 1], data[position + 2]);
                    position += 2;
                    break;

                case TELNET_IAC: /* Two IACs count as one. */
                    command_text[command_length++] = TELNET_IAC;
                    position++;
                    break;

                default: /* Skip it. */
                    position++;
                    break;
                }
                if (protocol->PendingInputLength > 0) {
                    break;
                }
            } else {
                command_text[command_length++] = data[position];
            }
            if (protocol->PendingInputLength > 0) {
                break;
            }
        }

        if (protocol->bIACMode && data[position] == TELNET_IAC && !has_next) {
            protocol->PendingInput[0] = data[position];
            protocol->PendingInputLength = 1;
            break;
        }
    }

    /* Terminate the two buffers */
    subnegotiation[subnegotiation_length] = '\0';
    command_text[command_length] = '\0';
    if (protocol->bIACMode) {
        protocol->IacInputLength = subnegotiation_length;
    } else {
        protocol->IacInputLength = 0;
    }

    /* Copy the input buffer back to the player. */
    std::strcat(out_text, command_text);
}

const char* ProtocolOutput(const descriptor_t* descriptor, const char* text, int* out_length)
{
    static char result[MAX_OUTPUT_BUFFER + 1];
    static constexpr char tab_text[] = "\t";
    static constexpr char msp_trigger[] = "!!";
    static constexpr char mxp_tag_start[] = "\033[1z<";
    static constexpr char mxp_tag_end[] = ">\033[7z";
    static constexpr char link_start[] = "\033[1z<send>\033[7z";
    static constexpr char link_end[] = "\033[1z</send>\033[7z";
    // The most characters a \t[...] code's text may hold, such as a Unicode fallback.
    constexpr int max_bracket_text_length = 7;
    bool reached_end = false;
    bool in_mxp_tag = false;
    bool strips_msp_triggers = false;
    /* Index values */
    int result_length = 0;
    int input_position = 0;

    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr || text == nullptr) {
        return text;
    }

    /* Strip !!SOUND() triggers if they support MSP or are using sound */
    if (protocol->bMSP || protocol->pVariables[eMSDP_SOUND]->ValueInt != 0) {
        strips_msp_triggers = true;
    }

    for (; result_length < MAX_OUTPUT_BUFFER && text[input_position] != '\0' && !reached_end;
        ++input_position) {
        // A length of zero or less means text ends at its NUL.
        const bool past_length = *out_length > 0 && input_position >= *out_length;
        if (past_length) {
            break;
        }

        const bool is_old_msp_trigger = strips_msp_triggers && input_position > 0
            && text[input_position - 1] == '!' && text[input_position] == '!'
            && starts_with_ignoring_case("SOUND(", &text[input_position + 1]);

        if (text[input_position] == '\t') {
            const char* copy_from = nullptr;
            // A Unicode code's ASCII fallback. It lives here, not in its case, because copy_from
            // points into it until the copy below.
            char unicode_fallback[max_bracket_text_length + 1] = { '\0' };

            ++input_position;
            switch (text[input_position]) {
            case '\t': /* Two tabs in a row will display an actual tab */
                copy_from = tab_text;
                break;
            case 'n':
                copy_from = ANSI_CLEAR;
                break;
            case 'r': /* dark red */
                copy_from = ColourRGB(descriptor, "F200");
                break;
            case 'R': /* light red */
                copy_from = ColourRGB(descriptor, "F500");
                break;
            case 'g': /* dark green */
                copy_from = ColourRGB(descriptor, "F020");
                break;
            case 'G': /* light green */
                copy_from = ColourRGB(descriptor, "F050");
                break;
            case 'y': /* dark yellow */
                copy_from = ColourRGB(descriptor, "F220");
                break;
            case 'Y': /* light yellow */
                copy_from = ColourRGB(descriptor, "F550");
                break;
            case 'b': /* dark blue */
                copy_from = ColourRGB(descriptor, "F002");
                break;
            case 'B': /* light blue */
                copy_from = ColourRGB(descriptor, "F005");
                break;
            case 'm': /* dark magenta */
                copy_from = ColourRGB(descriptor, "F202");
                break;
            case 'M': /* light magenta */
                copy_from = ColourRGB(descriptor, "F505");
                break;
            case 'c': /* dark cyan */
                copy_from = ColourRGB(descriptor, "F022");
                break;
            case 'C': /* light cyan */
                copy_from = ColourRGB(descriptor, "F055");
                break;
            case 'w': /* dark white */
                copy_from = ColourRGB(descriptor, "F222");
                break;
            case 'W': /* light white */
                copy_from = ColourRGB(descriptor, "F555");
                break;
            case 'a': /* dark azure */
                copy_from = ColourRGB(descriptor, "F014");
                break;
            case 'A': /* light azure */
                copy_from = ColourRGB(descriptor, "F025");
                break;
            case 'j': /* dark jade */
                copy_from = ColourRGB(descriptor, "F031");
                break;
            case 'J': /* light jade */
                copy_from = ColourRGB(descriptor, "F052");
                break;
            case 'l': /* dark lime */
                copy_from = ColourRGB(descriptor, "F140");
                break;
            case 'L': /* light lime */
                copy_from = ColourRGB(descriptor, "F250");
                break;
            case 'o': /* dark orange */
                copy_from = ColourRGB(descriptor, "F520");
                break;
            case 'O': /* light orange */
                copy_from = ColourRGB(descriptor, "F530");
                break;
            case 'p': /* dark pink */
                copy_from = ColourRGB(descriptor, "F301");
                break;
            case 'P': /* light pink */
                copy_from = ColourRGB(descriptor, "F502");
                break;
            case 't': /* dark tan */
                copy_from = ColourRGB(descriptor, "F210");
                break;
            case 'T': /* light tan */
                copy_from = ColourRGB(descriptor, "F321");
                break;
            case 'v': /* dark violet */
                copy_from = ColourRGB(descriptor, "F104");
                break;
            case 'V': /* light violet */
                copy_from = ColourRGB(descriptor, "F205");
                break;
            case '(': /* MXP link */
                if (!protocol->bBlockMXP && protocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    copy_from = link_start;
                }
                break;
            case ')': /* MXP link */
                if (!protocol->bBlockMXP && protocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    copy_from = link_end;
                }
                protocol->bBlockMXP = false;
                break;
            case '<':
                if (!protocol->bBlockMXP && protocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    copy_from = mxp_tag_start;
                    in_mxp_tag = true;
                } else {
                    /* No MXP support, so just strip it out */
                    while (text[input_position] != '\0' && text[input_position] != '>') {
                        ++input_position;
                    }
                }
                protocol->bBlockMXP = false;
                break;
            case '[':
                ++input_position;
                if (std::tolower(text[input_position]) == 'u') {
                    char bug_text[256];
                    int code_length = 0;
                    int code_point = 0;
                    bool closed = false;
                    bool fits = true;

                    ++input_position;
                    while (std::isdigit(text[input_position])) {
                        code_point *= 10;
                        code_point += (text[input_position]) - '0';
                        ++input_position;
                    }

                    if (text[input_position] == '/') {
                        ++input_position;
                    }

                    while (text[input_position] != '\0' && !closed) {
                        if (text[input_position] == ']') {
                            closed = true;
                        } else if (code_length < max_bracket_text_length) {
                            unicode_fallback[code_length++] = text[input_position++];
                        } else {
                            /* It's too long, so ignore the rest and note the problem */
                            input_position++;
                            fits = false;
                        }
                    }

                    if (!closed) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: Unicode substitute '%s' wasn't terminated with ']'.\n",
                            unicode_fallback);
                        report_bug(bug_text);
                    } else if (!fits) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: Unicode substitute '%s' truncated.  Missing ']'?\n",
                            unicode_fallback);
                        report_bug(bug_text);
                    } else if (protocol->pVariables[eMSDP_UTF_8]->ValueInt != 0) {
                        copy_from = UnicodeGet(code_point);
                    } else {
                        /* Display the substitute string */
                        copy_from = unicode_fallback;
                    }

                    /* Terminate if we've reached the end of the string */
                    reached_end = !closed;
                } else if (std::tolower(text[input_position]) == 'f'
                    || std::tolower(text[input_position]) == 'b') {
                    char code_text[max_bracket_text_length + 1] = { '\0' };
                    char bug_text[256];
                    int code_length = 0;
                    bool closed = false;
                    bool fits = true;

                    /* Copy the 'f' (foreground) or 'b' (background) */
                    code_text[code_length++] = text[input_position++];

                    while (text[input_position] != '\0' && !closed && fits) {
                        if (text[input_position] == ']') {
                            closed = true;
                        } else if (code_length < RGB_COLOUR_CODE_LENGTH) {
                            code_text[code_length++] = text[input_position++];
                        } else {
                            /* It's too long, so drop out - the colour code may still be
                               valid */
                            fits = false;
                        }
                    }

                    const char* layer_name = "back";
                    if (std::tolower(code_text[0]) == 'f') {
                        layer_name = "fore";
                    }

                    if (!closed || !fits) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: RGB %sground colour '%s' wasn't terminated with ']'.\n",
                            layer_name, &code_text[1]);
                        report_bug(bug_text);
                    } else if (!is_valid_colour(code_text)) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: RGB %sground colour '%s' invalid (each digit must be "
                            "in the range 0-5).\n",
                            layer_name, &code_text[1]);
                        report_bug(bug_text);
                    } else {
                        /* Success */
                        copy_from = ColourRGB(descriptor, code_text);
                    }
                } else if (std::tolower(text[input_position]) == 'x') {
                    char code_text[max_bracket_text_length + 1] = { '\0' };
                    char bug_text[256];
                    int code_length = 0;
                    bool closed = false;
                    bool fits = true;

                    ++input_position; /* Skip the 'x' */

                    while (text[input_position] != '\0' && !closed) {
                        if (text[input_position] == ']') {
                            closed = true;
                        } else if (code_length < max_bracket_text_length) {
                            code_text[code_length++] = text[input_position++];
                        } else {
                            /* It's too long, so ignore the rest and note the problem */
                            input_position++;
                            fits = false;
                        }
                    }

                    if (!closed) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: Required MXP version '%s' wasn't terminated with ']'.\n",
                            code_text);
                        report_bug(bug_text);
                    } else if (!fits) {
                        std::snprintf(bug_text, sizeof(bug_text),
                            "BUG: Required MXP version '%s' too long.  Missing ']'?\n", code_text);
                        report_bug(bug_text);
                    } else {
                        const bool mxp_version_unknown
                            = std::strcmp(protocol->pMXPVersion, "Unknown") == 0;
                        const bool mxp_version_too_low = mxp_version_unknown
                            || std::strcmp(protocol->pMXPVersion, code_text) < 0;
                        /* Block the tag unless their version of MXP is high enough */
                        protocol->bBlockMXP = mxp_version_too_low;
                    }

                    /* Terminate if we've reached the end of the string */
                    reached_end = !closed;
                }
                break;
            case '!': /* Used for in-band MSP sound triggers */
                copy_from = msp_trigger;
                break;
            case '\0':
                reached_end = true;
                break;
            default:
                break;
            }

            /* Copy the colour code, if any. */
            if (copy_from != nullptr) {
                while (*copy_from != '\0' && result_length < MAX_OUTPUT_BUFFER) {
                    result[result_length++] = *copy_from++;
                }
            }
        } else if (in_mxp_tag && text[input_position] == '>') {
            const char* copy_from = mxp_tag_end;
            while (*copy_from != '\0' && result_length < MAX_OUTPUT_BUFFER) {
                result[result_length++] = *copy_from++;
            }
            in_mxp_tag = false;
        } else if (is_old_msp_trigger) {
            /* Avoid accidental triggering of old-style MSP triggers */
            result[result_length++] = '?';
        } else {
            /* Just copy the character normally */
            result[result_length++] = text[input_position];
        }
    }

    /* If we'd overflow the buffer, we don't send any output */
    if (result_length >= MAX_OUTPUT_BUFFER) {
        result_length = 0;
        report_bug("ProtocolOutput: Too much outgoing data to store in the buffer.\n");
    }

    /* Terminate the string */
    result[result_length] = '\0';

    /* Store the length */
    if (out_length != nullptr) {
        *out_length = result_length;
    }

    /* Return the string */
    return result;
}

/* Some clients (such as GMud) don't properly handle negotiation, and simply
 * display every printable character to the screen.  However TTYPE isn't a
 * printable character, so we negotiate for it first, and only negotiate for
 * other protocols if the client responds with IAC WILL TTYPE or IAC WONT
 * TTYPE.  Thanks go to Donky on MudBytes for the suggestion.
 */
void ProtocolNegotiate(const descriptor_t* descriptor)
{
    confirm_negotiation(descriptor, eNEGOTIATED_TTYPE, true, true);
}

/******************************************************************************
 MSDP global functions.
 ******************************************************************************/

void MSDPUpdate(const descriptor_t* descriptor)
{
    protocol_t* protocol = protocol_of(descriptor);
    if (protocol == nullptr) {
        return;
    }

    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        if (protocol->pVariables[variable_index]->bReport) {
            if (protocol->pVariables[variable_index]->bDirty) {
                if (MSDPSend(descriptor, static_cast<variable_t>(variable_index))) {
                    protocol->pVariables[variable_index]->bDirty = false;
                }
            }
        }
    }
}

void MSDPFlush(const descriptor_t* descriptor, variable_t variable)
{
    if (variable > eMSDP_NONE && variable < eMSDP_MAX) {
        protocol_t* protocol = protocol_of(descriptor);

        if (protocol != nullptr && protocol->pVariables[variable]->bReport) {
            if (protocol->pVariables[variable]->bDirty) {
                if (MSDPSend(descriptor, variable)) {
                    protocol->pVariables[variable]->bDirty = false;
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

    for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX; ++variable_index) {
        if (protocol->pVariables[variable_index]->bReport) {
            protocol->pVariables[variable_index]->bDirty = true;
        }
    }
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
        if (VARIABLE_NAME_TABLE[variable].bString) {
            /* Should really be replaced with a dynamic buffer */
            const std::size_t name_length = std::strlen(VARIABLE_NAME_TABLE[variable].pName);
            const std::size_t value_length
                = std::strlen(protocol->pVariables[variable]->pValueString);
            const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

            if (required_length >= static_cast<std::size_t>(MAX_VARIABLE_LENGTH)) {
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSend: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n",
                    VARIABLE_NAME_TABLE[variable].pName, required_length, MAX_VARIABLE_LENGTH);
                report_bug(msdp_text);
                msdp_text[0] = '\0';
            } else if (protocol->bMSDP) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%s%c%s%c%c", IAC, SB,
                    TELOPT_MSDP, MSDP_VAR, VARIABLE_NAME_TABLE[variable].pName, MSDP_VAL,
                    protocol->pVariables[variable]->pValueString, IAC, SE);
            } else if (protocol->bATCP) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%s %s%c%c", IAC, SB,
                    TELOPT_ATCP, VARIABLE_NAME_TABLE[variable].pName,
                    protocol->pVariables[variable]->pValueString, IAC, SE);
            }
        } else {
            /* It's an integer, not a string */
            if (protocol->bMSDP) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%s%c%d%c%c", IAC, SB,
                    TELOPT_MSDP, MSDP_VAR, VARIABLE_NAME_TABLE[variable].pName, MSDP_VAL,
                    protocol->pVariables[variable]->ValueInt, IAC, SE);
            } else if (protocol->bATCP) {
                std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%s %d%c%c", IAC, SB,
                    TELOPT_ATCP, VARIABLE_NAME_TABLE[variable].pName,
                    protocol->pVariables[variable]->ValueInt, IAC, SE);
            }
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (msdp_text[0] != '\0') {
            send_to_client(descriptor, msdp_text);
            return true;
        }
    }
    return false;
}

void MSDPSendPair(const descriptor_t* descriptor, const char* name, const char* value)
{
    char msdp_text[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (name != nullptr && value != nullptr) {
        protocol_t* protocol = protocol_of(descriptor);
        if (protocol == nullptr) {
            return;
        }

        const std::string_view value_view(value);
        const std::size_t value_length = msdp_sanitized_length(value_view);

        /* Should really be replaced with a dynamic buffer */
        // Kept as std::size_t so no value is long enough to wrap around the limit check.
        constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
        const std::size_t name_length = std::strlen(name);
        const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

        if (required_length >= buffer_limit) {
            if (required_length - value_length < buffer_limit) {
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSendPair: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n", name,
                    required_length, MAX_VARIABLE_LENGTH);
            } else {
                /* The variable name itself is too long */
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSendPair: Variable name has a length of %zu bytes (exceeds "
                    "MAX_VARIABLE_LENGTH of %d).\n",
                    required_length, MAX_VARIABLE_LENGTH);
            }

            report_bug(msdp_text);
            msdp_text[0] = '\0';
        } else if (protocol->bMSDP) {
            const int prefix_length = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%s%c",
                IAC, SB, TELOPT_MSDP, MSDP_VAR, name, MSDP_VAL);
            char* const value_end = msdp_write_sanitized(value_view, msdp_text + prefix_length);
            const std::size_t room_left
                = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
            std::snprintf(value_end, room_left, "%c%c", IAC, SE);
        } else if (protocol->bATCP) {
            const int prefix_length = std::snprintf(
                msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%s ", IAC, SB, TELOPT_ATCP, name);
            char* const value_end = msdp_write_sanitized(value_view, msdp_text + prefix_length);
            const std::size_t room_left
                = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
            std::snprintf(value_end, room_left, "%c%c", IAC, SE);
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (msdp_text[0] != '\0') {
            send_to_client(descriptor, msdp_text);
        }
    }
}

void MSDPSendList(const descriptor_t* descriptor, const char* name, const char* value)
{
    char msdp_text[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (name != nullptr && value != nullptr) {
        protocol_t* protocol = protocol_of(descriptor);
        if (protocol == nullptr) {
            return;
        }

        const std::string_view value_view(value);
        const std::size_t value_length = msdp_sanitized_length(value_view);

        /* Should really be replaced with a dynamic buffer */
        // Kept as std::size_t so no value is long enough to wrap around the limit check.
        constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
        const std::size_t name_length = std::strlen(name);
        const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

        if (required_length >= buffer_limit) {
            if (required_length - value_length < buffer_limit) {
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSendList: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n", name,
                    required_length, MAX_VARIABLE_LENGTH);
            } else {
                /* The variable name itself is too long */
                std::snprintf(msdp_text, sizeof(msdp_text),
                    "MSDPSendList: Variable name has a length of %zu bytes (exceeds "
                    "MAX_VARIABLE_LENGTH of %d).\n",
                    required_length, MAX_VARIABLE_LENGTH);
            }

            report_bug(msdp_text);
            msdp_text[0] = '\0';
        } else if (protocol->bMSDP) {
            const int prefix_length
                = std::snprintf(msdp_text, sizeof(msdp_text), "%c%c%c%c%s%c%c%c", IAC, SB,
                    TELOPT_MSDP, MSDP_VAR, name, MSDP_VAL, MSDP_ARRAY_OPEN, MSDP_VAL);
            char* const value_end = msdp_write_sanitized(value_view, msdp_text + prefix_length);
            const std::size_t room_left
                = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
            std::snprintf(value_end, room_left, "%c%c%c", MSDP_ARRAY_CLOSE, IAC, SE);

            /* Convert the spaces to MSDP_VAL */
            for (int text_position = 0; msdp_text[text_position] != '\0'; ++text_position) {
                if (msdp_text[text_position] == ' ') {
                    msdp_text[text_position] = MSDP_VAL;
                }
            }
        } else if (protocol->bATCP) {
            const int prefix_length = std::snprintf(
                msdp_text, sizeof(msdp_text), "%c%c%cMSDP.%s ", IAC, SB, TELOPT_ATCP, name);
            char* const value_end = msdp_write_sanitized(value_view, msdp_text + prefix_length);
            const std::size_t room_left
                = sizeof(msdp_text) - static_cast<std::size_t>(value_end - msdp_text);
            std::snprintf(value_end, room_left, "%c%c", IAC, SE);
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (msdp_text[0] != '\0') {
            send_to_client(descriptor, msdp_text);
        }
    }
}

void MSDPSetNumber(const descriptor_t* descriptor, variable_t variable, int value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (!VARIABLE_NAME_TABLE[variable].bString) {
            if (protocol->pVariables[variable]->ValueInt != value) {
                protocol->pVariables[variable]->ValueInt = value;
                protocol->pVariables[variable]->bDirty = true;
            }
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

// The MSDPSanitizeValue text for value in a newly allocated string, or nullptr out of memory. The
// caller frees it with free().
[[nodiscard]] static char* msdp_copy_sanitized(std::string_view value)
{
    const std::size_t length = msdp_sanitized_length(value);
    char* const sanitized = create_string(length);
    if (sanitized == nullptr) {
        return nullptr;
    }
    msdp_write_sanitized(value, sanitized);
    sanitized[length] = '\0';
    return sanitized;
}

// Whether the NUL-terminated stored, which is never null, is exactly the MSDPSanitizeValue text
// for value, compared without building it.
static bool equals_sanitized(const char* stored, std::string_view value)
{
    char escape[MSDP_LONGEST_ESCAPE];
    for (const char character : value) {
        const std::size_t escape_length
            = msdp_escape_byte(static_cast<unsigned char>(character), escape);
        // A shorter stored mismatches at its NUL, so strncmp never reads past it.
        if (std::strncmp(stored, escape, escape_length) != 0) {
            return false;
        }
        stored += escape_length;
    }
    return *stored == '\0';
}

// Whether the NUL-terminated stored is exactly open_marker, then payload, then close_marker.
// payload holds no NUL.
static bool equals_wrapped(
    const char* stored, char open_marker, std::string_view payload, char close_marker)
{
    if (stored[0] != open_marker) {
        return false;
    }
    // A shorter stored mismatches at its NUL, so strncmp never reads past it.
    if (std::strncmp(stored + 1, payload.data(), payload.size()) != 0) {
        return false;
    }
    const char* const after_payload = stored + 1 + payload.size();
    return after_payload[0] == close_marker && after_payload[1] == '\0';
}

// Stores open_marker, payload and close_marker as variable's value and marks it dirty, unless
// that is its value already. Returns whether the value changed; out of memory, it logs and leaves
// the value as it was. payload holds no NUL.
static bool msdp_set_wrapped(
    MSDP_t& variable, char open_marker, std::string_view payload, char close_marker)
{
    if (equals_wrapped(variable.pValueString, open_marker, payload, close_marker)) {
        return false;
    }

    constexpr std::size_t marker_bytes = 2;
    char* const wrapped = create_string(payload.size() + marker_bytes);
    if (wrapped == nullptr) {
        report_bug("msdp_set_wrapped: out of memory; the MSDP value is unchanged.\n");
        return false;
    }
    wrapped[0] = open_marker;
    std::memcpy(wrapped + 1, payload.data(), payload.size());
    wrapped[payload.size() + 1] = close_marker;
    wrapped[payload.size() + 2] = '\0';

    std::free(variable.pValueString);
    variable.pValueString = wrapped;
    variable.bDirty = true;
    return true;
}

void MSDPAppendSanitized(std::string& out_text, const char* value)
{
    if (value == nullptr) {
        return;
    }
    const std::string_view value_view(value);
    const std::size_t old_size = out_text.size();
    const std::size_t length = msdp_sanitized_length(value_view);
    out_text.resize(old_size + length);
    msdp_write_sanitized(value_view, out_text.data() + old_size);
}

std::string MSDPSanitizeValue(const char* value)
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
    if (can_set && VARIABLE_NAME_TABLE[variable].bString) {
        MSDP_t* stored = protocol->pVariables[variable];
        if (!equals_sanitized(stored->pValueString, value)) {
            char* const sanitized = msdp_copy_sanitized(value);
            if (sanitized == nullptr) {
                report_bug("MSDPSetString: out of memory; the MSDP value is unchanged.\n");
                return;
            }
            std::free(stored->pValueString);
            stored->pValueString = sanitized;
            stored->bDirty = true;
        }
    }
}

void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, const char* value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && value != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (*value == '\0') {
            /* It's easier to call MSDPSetString if the value is empty */
            MSDPSetString(descriptor, variable, value);
        } else if (VARIABLE_NAME_TABLE[variable].bString) {
            msdp_set_wrapped(*protocol->pVariables[variable], static_cast<char>(MSDP_TABLE_OPEN),
                value, static_cast<char>(MSDP_TABLE_CLOSE));
        }
    }
}

void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, const char* value)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_set = protocol != nullptr && value != nullptr && MSDPIsValidVariable(variable);
    if (can_set) {
        if (*value == '\0') {
            /* It's easier to call MSDPSetString if the value is empty */
            MSDPSetString(descriptor, variable, value);
        } else if (VARIABLE_NAME_TABLE[variable].bString) {
            msdp_set_wrapped(*protocol->pVariables[variable], static_cast<char>(MSDP_ARRAY_OPEN),
                value, static_cast<char>(MSDP_ARRAY_CLOSE));
        }
    }
}

/******************************************************************************
 MXP global functions.
 ******************************************************************************/

void MXPSendTag(const descriptor_t* descriptor, const char* tag)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool can_send = protocol != nullptr && tag != nullptr
        && std::strlen(tag) < static_cast<std::size_t>(MAX_MXP_TAG_LENGTH);
    if (can_send) {
        if (protocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
            char tag_text[MXP_BUFFER_SIZE];
            std::snprintf(tag_text, sizeof(tag_text), "\033[1z%s\033[7z\r\n", tag);
            send_to_client(descriptor, tag_text);
        } else if (protocol->bRenegotiate) {
            /* Tijer pointed out that when MUSHclient autoconnects, it fails
             * to complete the negotiation.  This workaround will attempt to
             * renegotiate after the character has connected.
             */

            // Renegotiate everything except TTYPE.
            for (int option_index = eNEGOTIATED_TTYPE + 1; option_index < eNEGOTIATED_MAX;
                ++option_index) {
                protocol->Negotiated[option_index] = false;
                confirm_negotiation(
                    descriptor, static_cast<negotiated_t>(option_index), true, true);
            }

            protocol->bRenegotiate = false;
            protocol->bNeedMXPVersion = true;
            negotiate(descriptor);
        }
    }
}

/******************************************************************************
 Colour global functions.
 ******************************************************************************/

const char* ColourRGB(const descriptor_t* descriptor, const char* rgb_code)
{
    protocol_t* protocol = protocol_of(descriptor);

    const bool uses_ansi_colour
        = protocol != nullptr && protocol->pVariables[eMSDP_ANSI_COLORS]->ValueInt != 0;
    if (uses_ansi_colour) {
        if (is_valid_colour(rgb_code)) {
            bool background = (std::tolower(rgb_code[0]) == 'b');
            int red = rgb_code[1] - '0';
            int green = rgb_code[2] - '0';
            int blue = rgb_code[3] - '0';

            if (protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt != 0) {
                return get_xterm_colour(background, red, green, blue);
            } else {
                /* Use regular ANSI colour */
                return get_ansi_colour(background, red, green, blue);
            }
        } else {
            /* Invalid colour - use this to clear any existing colour. */
            return ANSI_CLEAR;
        }
    } else {
        /* Don't send any colour, not even clear */
        return "";
    }
}

/******************************************************************************
 UTF-8 global functions.
 ******************************************************************************/

char* UnicodeGet(int code_point)
{
    static char utf8_text[8];
    char* cursor = utf8_text;

    UnicodeAdd(&cursor, code_point);
    *cursor = '\0';

    return utf8_text;
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

/******************************************************************************
 Local negotiation functions.
 ******************************************************************************/

static void negotiate(const descriptor_t* descriptor)
{
    protocol_t* protocol = descriptor->pProtocol;

    if (protocol->bNegotiated) {
        static constexpr char ttype_request[]
            = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND, TELNET_IAC, TELNET_SE, '\0' };

        /* Request the client type if TTYPE is supported. */
        if (protocol->bTTYPE) {
            send_to_client(descriptor, ttype_request);
        }

        /* Check for other protocols. */
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

static void perform_handshake(const descriptor_t* descriptor, char command, char option)
{
    protocol_t* protocol = descriptor->pProtocol;

    switch (option) {
    case static_cast<char>(TELOPT_TTYPE):
        if (command == TELNET_WILL) {
            confirm_negotiation(descriptor, eNEGOTIATED_TTYPE, true, true);
            protocol->bTTYPE = true;

            if (!protocol->bNegotiated) {
                /* Negotiate for the remaining protocols. */
                protocol->bNegotiated = true;
                negotiate(descriptor);

                /* We may need to renegotiate if they don't reply */
                protocol->bRenegotiate = true;
            }
        } else if (command == TELNET_WONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_TTYPE, false, protocol->bTTYPE);
            protocol->bTTYPE = false;

            if (!protocol->bNegotiated) {
                /* Still negotiate, as this client obviously knows how to
                 * correctly respond to negotiation attempts - but we don't
                 * ask for TTYPE, as it doesn't support it.
                 */
                protocol->bNegotiated = true;
                negotiate(descriptor);

                /* We may need to renegotiate if they don't reply */
                protocol->bRenegotiate = true;
            }
        } else if (command == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;

    case static_cast<char>(TELOPT_ECHO):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_ECHO, true, true);
            protocol->bECHO = true;
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_ECHO, false, protocol->bECHO);
            protocol->bECHO = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_NAWS):
        if (command == TELNET_WILL) {
            confirm_negotiation(descriptor, eNEGOTIATED_NAWS, true, true);
            protocol->bNAWS = true;

            /* Renegotiation workaround won't be necessary. */
            protocol->bRenegotiate = false;
        } else if (command == TELNET_WONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_NAWS, false, protocol->bNAWS);
            protocol->bNAWS = false;

            /* Renegotiation workaround won't be necessary. */
            protocol->bRenegotiate = false;
        } else if (command == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;

    case static_cast<char>(TELOPT_CHARSET):
        if (command == TELNET_WILL) {
            confirm_negotiation(descriptor, eNEGOTIATED_CHARSET, true, true);
            if (!protocol->bCHARSET) {
                /* Advertise ISO-8859-1, not UTF-8: that is what we actually put
                   on the wire. Nothing in the output path ever encodes to UTF-8
                   -- ProtocolOutput() (the only function that would, via
                   UnicodeGet()) has no callers anywhere in the server; text goes
                   straight from process_output() to write_to_descriptor() as raw
                   bytes. The one source of non-ASCII output is the Quenya month
                   names in consts.cpp (month_name[0..11], e.g. "N\xE9nim\xEB"),
                   reached through day_to_str() from `time` and the pkill log,
                   and those are literal latin-1 bytes.

                   Claiming UTF-8 here made conforming clients (Mudlet) decode
                   those bytes as UTF-8, where a lone 0xEB is an invalid sequence
                   -- so the month names rendered as replacement characters.

                   Note this is only about what we *emit*. PRF_LATIN1 still
                   controls whether process_output() folds those bytes down to
                   7-bit ASCII via unaccent() for players who asked for that. */
                static constexpr char charset_latin1[] = {
                    TELNET_IAC, TELNET_SB, TELOPT_CHARSET, 1, ' ', 'I', 'S', 'O',
                    '-', '8', '8', '5', '9', '-', '1', TELNET_IAC, TELNET_SE, '\0'
                };
                send_to_client(descriptor, charset_latin1);
                protocol->bCHARSET = true;
            }
        } else if (command == TELNET_WONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_CHARSET, false, protocol->bCHARSET);
            protocol->bCHARSET = false;
        } else if (command == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;

    case static_cast<char>(TELOPT_MSDP):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSDP, true, true);

            /* Unlike its sibling flags (bMSSP/bATCP/bMSP/bMXP/bMCCP, all false by
               default), bMSDP defaults to true at connection creation -- so the
               "!protocol->bMSDP" check below can essentially never be true on a
               normal DO/WILL exchange, making the block it guards dead in practice.
               MSDPSetString() only populates SERVER_ID's own backing storage (no
               wire traffic, no login/preference guard, safe to call on every DO),
               so do that unconditionally here -- otherwise a client-issued
               "SEND SERVER_ID" later (read via MSDPSend(), which reads that
               storage) returns an empty string instead of the mud's name. */
            MSDPSetString(descriptor, eMSDP_SERVER_ID, MUD_NAME);

            if (!protocol->bMSDP) {
                protocol->bMSDP = true;

                /* Identify the mud to the client immediately. This fires during
                   telnet negotiation, before login, so it can't go through
                   MSDPSend() (which requires a logged-in character with PRF_MSDP
                   set) -- MSDPSendPair() sends it immediately with no such guard. */
                MSDPSendPair(descriptor, "SERVER_ID", MUD_NAME);
            }
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSDP, false, protocol->bMSDP);
            protocol->bMSDP = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_MSSP):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSSP, true, true);

            if (!protocol->bMSSP) {
                send_mssp(descriptor);
                protocol->bMSSP = true;
            }
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSSP, false, protocol->bMSSP);
            protocol->bMSSP = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_MCCP):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_MCCP, true, true);

            if (!protocol->bMCCP) {
                protocol->bMCCP = true;
            }
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MCCP, false, protocol->bMCCP);

            if (protocol->bMCCP) {
                protocol->bMCCP = false;
            }
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_MSP):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSP, true, true);
            protocol->bMSP = true;
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MSP, false, protocol->bMSP);
            protocol->bMSP = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_MXP):
        if (command == TELNET_WILL || command == TELNET_DO) {
            if (command == TELNET_WILL) {
                confirm_negotiation(descriptor, eNEGOTIATED_MXP, true, true);
            } else {
                /* command == TELNET_DO */
                confirm_negotiation(descriptor, eNEGOTIATED_MXP2, true, true);
            }

            if (!protocol->bMXP) {
                /* Enable MXP. */
                static constexpr char mxp_enable[]
                    = { TELNET_IAC, TELNET_SB, TELOPT_MXP, TELNET_IAC, TELNET_SE, '\0' };
                send_to_client(descriptor, mxp_enable);

                /* Create a secure channel, and note that MXP is active. */
                send_to_client(descriptor, "\033[7z");
                protocol->bMXP = true;
                protocol->pVariables[eMSDP_MXP]->ValueInt = 1;

                if (protocol->bNeedMXPVersion) {
                    MXPSendTag(descriptor, "<VERSION>");
                }
            }
        } else if (command == TELNET_WONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MXP, false, protocol->bMXP);

            if (!protocol->bMXP) {
                /* The MXP standard doesn't actually specify whether you should
                 * negotiate with IAC DO MXP or IAC WILL MXP.  As a result, some
                 * clients support one, some the other, and some support both.
                 *
                 * Therefore we first try IAC DO MXP, and if the client replies
                 * with WONT, we try again (here) with IAC WILL MXP.
                 */
                confirm_negotiation(descriptor, eNEGOTIATED_MXP2, true, true);
            } else {
                /* The client is actually asking us to switch MXP off. */
                protocol->bMXP = false;
            }
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_MXP2, false, protocol->bMXP);
            protocol->bMXP = false;
        }
        break;

    case static_cast<char>(TELOPT_ATCP):
        if (command == TELNET_WILL) {
            confirm_negotiation(descriptor, eNEGOTIATED_ATCP, true, true);

            /* If we don't support MSDP, fake it with ATCP */
            if (!protocol->bMSDP && !protocol->bATCP) {
                protocol->bATCP = true;

                /* Identify the mud to the client. This fires during telnet negotiation,
                   before login, so it can't go through MSDPSend() (which requires a
                   logged-in character with PRF_MSDP set) -- MSDPSendPair() sends it
                   immediately with no such guard, exactly as before. But MSDPSendPair()
                   never touches the variable's own backing storage, so without this,
                   a later client-issued "SEND SERVER_ID" (which does go through
                   MSDPSend(), reading that storage) would return an empty string.
                   MSDPSetString() has no login/preference guard either, so store the
                   value too. */
                MSDPSendPair(descriptor, "SERVER_ID", MUD_NAME);
                MSDPSetString(descriptor, eMSDP_SERVER_ID, MUD_NAME);
            }
        } else if (command == TELNET_WONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_ATCP, false, protocol->bATCP);
            protocol->bATCP = false;
        } else if (command == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;

    case static_cast<char>(TELOPT_EOR):
        if (command == TELNET_DO) {
            confirm_negotiation(descriptor, eNEGOTIATED_EOR, true, true);
            protocol->bEOR = true;
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_EOR, false, protocol->bEOR);
            protocol->bEOR = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    case static_cast<char>(TELOPT_SGA):
        if (command == TELNET_DO) {
            /* The client wants no IAC GA after prompts. */
            confirm_negotiation(descriptor, eNEGOTIATED_SGA, true, true);
            protocol->bSGA = true;
        } else if (command == TELNET_DONT) {
            confirm_negotiation(descriptor, eNEGOTIATED_SGA, false, protocol->bSGA);
            protocol->bSGA = false;
        } else if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        }
        break;

    default:
        if (command == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_DONT, option);
        } else if (command == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            send_negotiation_sequence(descriptor, TELNET_WONT, option);
        }
        break;
    }
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

static void perform_subnegotiation(
    const descriptor_t* descriptor, char option, const char* data, int size)
{
    protocol_t* protocol = descriptor->pProtocol;

    switch (option) {
    case static_cast<char>(TELOPT_TTYPE):
        if (protocol->bTTYPE) {
            /* Store the client name. */
            constexpr int max_client_length = 64;
            char client_name[max_client_length + 1];
            int name_length = 0;
            bool stop_cyclic_ttype = false;

            for (int input_position = 1;
                data[input_position] != '\0' && name_length < max_client_length; ++input_position) {
                const unsigned char client_byte = static_cast<unsigned char>(data[input_position]);
                if (client_byte >= FIRST_PRINTABLE_ASCII && client_byte <= LAST_PRINTABLE_ASCII) {
                    client_name[name_length++] = data[input_position];
                }
            }
            client_name[name_length] = '\0';

            /* Store the first TTYPE as the client name */
            if (std::strcmp(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString, "Unknown") == 0) {
                std::free(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                protocol->pVariables[eMSDP_CLIENT_ID]->pValueString = copy_string(client_name);

                /* This is a bit nasty, but using cyclic TTYPE on windows telnet
                 * causes it to lock up.  None of the clients we need to cycle
                 * with send ANSI to start with anyway, so we shouldn't have any
                 * conflicts.
                 *
                 * An alternative solution is to use escape sequences to check
                 * for windows telnet prior to negotiation, and this also avoids
                 * the player losing echo, but it has other issues.  Because the
                 * escape codes are technically in-band, even though they'll be
                 * stripped from the display, the newlines will still cause some
                 * scrolling.  Therefore you need to either pause the session
                 * for a few seconds before displaying the login screen, or wait
                 * until the player has entered their name before negotiating.
                 */
                if (std::strcmp(client_name, "ANSI") == 0) {
                    stop_cyclic_ttype = true;
                }
            }

            /* Cycle through the TTYPEs until we get the same result twice, or
             * find ourselves back at the start.
             *
             * If the client follows RFC1091 properly then it will indicate the
             * end of the list by repeating the last response, and then return
             * to the top of the list.  If you're the trusting type, then feel
             * free to remove the second strcmp ;)
             */
            const char* client_id = protocol->pVariables[eMSDP_CLIENT_ID]->pValueString;
            const bool has_last_ttype = protocol->pLastTTYPE != nullptr;
            const bool repeats_last_ttype
                = has_last_ttype && std::strcmp(protocol->pLastTTYPE, client_name) == 0;
            const bool repeats_client_name = has_last_ttype && !repeats_last_ttype
                && std::strcmp(client_id, client_name) == 0;
            if (!repeats_last_ttype && !repeats_client_name) {
                static constexpr char ttype_request[] = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND,
                    TELNET_IAC, TELNET_SE, '\0' };
                const char* type_suffix = std::strstr(client_name, "-");

                /* Store the TTYPE */
                std::free(protocol->pLastTTYPE);
                protocol->pLastTTYPE = copy_string(client_name);

                /* Look for 256 colour support */
                const bool names_xterm_256
                    = type_suffix != nullptr && equals_ignoring_case(type_suffix, "-256color");
                if (names_xterm_256) {
                    /* This is currently the only way to detect support for 256
                     * colours in TinTin++, WinTin++ and BlowTorch.
                     */
                    protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    protocol->b256Support = eYES;
                }

                /* Request another TTYPE */
                if (!stop_cyclic_ttype) {
                    send_to_client(descriptor, ttype_request);
                }
            }

            // A client's name and the separator before the version it may append.
            static constexpr char mtts_prefix[] = "MTTS ";
            static constexpr char mudlet_name[] = "Mudlet";
            static constexpr char decafmud_name[] = "DecafMUD";
            constexpr std::size_t separator_length = 1;

            if (starts_with_ignoring_case(mtts_prefix, client_name)) {
                // The MTTS capability bits the server reads.
                constexpr int mtts_ansi = 1;
                constexpr int mtts_utf_8 = 4;
                constexpr int mtts_xterm_256 = 8;
                constexpr std::size_t mtts_prefix_length
                    = std::char_traits<char>::length(mtts_prefix);
                protocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt
                    = std::atoi(client_name + mtts_prefix_length);

                if ((protocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_ansi) != 0) {
                    protocol->pVariables[eMSDP_ANSI_COLORS]->ValueInt = 1;
                }
                if ((protocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_utf_8) != 0) {
                    protocol->pVariables[eMSDP_UTF_8]->ValueInt = 1;
                }
                if ((protocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_xterm_256) != 0) {
                    protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    protocol->b256Support = eYES;
                }

            } else if (starts_with_ignoring_case(mudlet_name, client_name)) {
                /* Mudlet beta 15 and later supports 256 colours, but we can't
                 * identify it from the mud - everything prior to 1.1 claims
                 * to be version 1.0, so we just don't know.
                 */
                protocol->b256Support = eSOMETIMES;

                constexpr std::size_t mudlet_name_length
                    = std::char_traits<char>::length(mudlet_name);
                constexpr std::size_t version_start = mudlet_name_length + separator_length;
                const std::size_t client_length = std::strlen(client_name);
                if (client_length > version_start) {
                    client_name[mudlet_name_length] = '\0';
                    std::free(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                    protocol->pVariables[eMSDP_CLIENT_ID]->pValueString = copy_string(client_name);
                    std::free(protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                    protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString
                        = copy_string(client_name + version_start);

                    /* Mudlet 1.1 and later supports 256 colours. */
                    const char* client_version
                        = protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString;
                    const bool supports_xterm_256 = std::strcmp(client_version, "1.1") >= 0;
                    if (supports_xterm_256) {
                        protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        protocol->b256Support = eYES;
                    }
                }
            } else if (equals_ignoring_case(client_name, "EMACS-RINZAI")) {
                /* We know for certain that this client has support */
                protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                protocol->b256Support = eYES;
            } else if (starts_with_ignoring_case(decafmud_name, client_name)) {
                /* We know for certain that this client has support */
                protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                protocol->b256Support = eYES;

                constexpr std::size_t decafmud_name_length
                    = std::char_traits<char>::length(decafmud_name);
                constexpr std::size_t version_start = decafmud_name_length + separator_length;
                const std::size_t client_length = std::strlen(client_name);
                if (client_length > version_start) {
                    client_name[decafmud_name_length] = '\0';
                    std::free(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                    protocol->pVariables[eMSDP_CLIENT_ID]->pValueString = copy_string(client_name);
                    std::free(protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                    protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString
                        = copy_string(client_name + version_start);
                }
            } else if (sometimes_supports_xterm_256(client_name)) {
                /* We know that some versions of this client have support */
                protocol->b256Support = eSOMETIMES;
            } else if (equals_ignoring_case(client_name, "ZMUD")) {
                /* We know for certain that this client does not have support */
                protocol->b256Support = eNO;
            }
        }
        break;

    case static_cast<char>(TELOPT_NAWS):
        if (protocol->bNAWS) {
            /* Store the new width. */
            protocol->ScreenWidth = static_cast<unsigned char>(data[0]);
            protocol->ScreenWidth <<= 8;
            protocol->ScreenWidth += static_cast<unsigned char>(data[1]);

            /* Store the new height. */
            protocol->ScreenHeight = static_cast<unsigned char>(data[2]);
            protocol->ScreenHeight <<= 8;
            protocol->ScreenHeight += static_cast<unsigned char>(data[3]);
        }
        break;

    case static_cast<char>(TELOPT_CHARSET):
        /* Nothing to record. We only ever offer one charset, ISO-8859-1 (see the
         * REQUEST we send in perform_handshake), and we emit latin-1 either way --
         * so neither ACCEPTED nor REJECTED changes what we put on the wire. If you
         * ever offer more than one, read the reply here to see which was picked.
         *
         * This deliberately no longer sets eMSDP_UTF_8. It did, back when the
         * REQUEST above claimed UTF-8 -- but that flag means "UTF-8 is in play",
         * and accepting latin-1 is the opposite of that. The server has no UTF-8
         * output path at all (ProtocolOutput(), which would provide one, is
         * uncalled), so asserting it here only misled clients. Genuine client-side
         * UTF-8 capability is still recorded from the MTTS bitfield in the TTYPE
         * handler, which is a statement about the client rather than about this
         * negotiation.
         */
        break;

    case static_cast<char>(TELOPT_MSDP):
        if (protocol->bMSDP) {
            parse_msdp(descriptor, data);
        }
        break;

    case static_cast<char>(TELOPT_ATCP):
        if (protocol->bATCP) {
            parse_atcp(descriptor, data);
        }
        break;

    default: /* Unknown subnegotiation, so we simply ignore it. */
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

static bool confirm_negotiation(
    const descriptor_t* descriptor, negotiated_t option, bool will_do, bool send_reply)
{
    bool changed = false;

    if (option >= eNEGOTIATED_TTYPE && option < eNEGOTIATED_MAX) {
        /* Only negotiate if the state has changed. */
        if (descriptor->pProtocol->Negotiated[option] != will_do) {
            /* Store the new state. */
            descriptor->pProtocol->Negotiated[option] = will_do;

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

/******************************************************************************
 Local MSDP functions.
 ******************************************************************************/

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
        default: /* Anything else */
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

static void execute_msdp_pair(
    const descriptor_t* descriptor, const char* name, const char* value)
{
    if (name[0] != '\0' && value[0] != '\0') {
        if (equals_ignoring_case(name, "SEND")) {
            bool found = false;
            for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX && !found;
                ++variable_index) {
                if (equals_ignoring_case(value, VARIABLE_NAME_TABLE[variable_index].pName)) {
                    MSDPSend(descriptor, static_cast<variable_t>(variable_index));
                    found = true;
                }
            }
        } else if (equals_ignoring_case(name, "REPORT")) {
            bool found = false;
            for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX && !found;
                ++variable_index) {
                if (equals_ignoring_case(value, VARIABLE_NAME_TABLE[variable_index].pName)) {
                    descriptor->pProtocol->pVariables[variable_index]->bReport = true;
                    descriptor->pProtocol->pVariables[variable_index]->bDirty = true;
                    found = true;
                }
            }
        } else if (equals_ignoring_case(name, "RESET")) {
            const bool resets_variables = equals_ignoring_case(value, "REPORTABLE_VARIABLES")
                || equals_ignoring_case(value, "REPORTED_VARIABLES")
                || equals_ignoring_case(value, "VARIABLES");
            if (resets_variables) {
                for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                    ++variable_index) {
                    if (descriptor->pProtocol->pVariables[variable_index]->bReport) {
                        descriptor->pProtocol->pVariables[variable_index]->bReport = false;
                        descriptor->pProtocol->pVariables[variable_index]->bDirty = false;
                    }
                }
            }
        } else if (equals_ignoring_case(name, "UNREPORT")) {
            bool found = false;
            for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX && !found;
                ++variable_index) {
                if (equals_ignoring_case(value, VARIABLE_NAME_TABLE[variable_index].pName)) {
                    descriptor->pProtocol->pVariables[variable_index]->bReport = false;
                    descriptor->pProtocol->pVariables[variable_index]->bDirty = false;
                    found = true;
                }
            }
        } else if (equals_ignoring_case(name, "LIST")) {
            if (equals_ignoring_case(value, "COMMANDS")) {
                static constexpr char name_list[] = "LIST REPORT RESET SEND UNREPORT";
                MSDPSendList(descriptor, "COMMANDS", name_list);
            } else if (equals_ignoring_case(value, "LISTS")) {
                static constexpr char name_list[]
                    = "COMMANDS LISTS CONFIGURABLE_VARIABLES REPORTABLE_VARIABLES "
                      "REPORTED_VARIABLES SENDABLE_VARIABLES GUI_VARIABLES";
                MSDPSendList(descriptor, "LISTS", name_list);
            }
            /* Split this into two if some variables aren't REPORTABLE */
            else if (names_sendable_variables(value)) {
                char name_list[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;

                for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                    ++variable_index) {
                    if (!VARIABLE_NAME_TABLE[variable_index].bGUI) {
                        commands_length = append_list_name(
                            name_list, commands_length, VARIABLE_NAME_TABLE[variable_index].pName);
                    }
                }
                name_list[commands_length] = '\0';

                MSDPSendList(descriptor, value, name_list);
            } else if (equals_ignoring_case(value, "REPORTED_VARIABLES")) {
                char name_list[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;

                for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                    ++variable_index) {
                    if (descriptor->pProtocol->pVariables[variable_index]->bReport) {
                        commands_length = append_list_name(
                            name_list, commands_length, VARIABLE_NAME_TABLE[variable_index].pName);
                    }
                }
                name_list[commands_length] = '\0';

                MSDPSendList(descriptor, value, name_list);
            } else if (equals_ignoring_case(value, "CONFIGURABLE_VARIABLES")) {
                char name_list[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;

                for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                    ++variable_index) {
                    if (VARIABLE_NAME_TABLE[variable_index].bConfigurable) {
                        commands_length = append_list_name(
                            name_list, commands_length, VARIABLE_NAME_TABLE[variable_index].pName);
                    }
                }
                name_list[commands_length] = '\0';

                MSDPSendList(descriptor, "CONFIGURABLE_VARIABLES", name_list);
            } else if (equals_ignoring_case(value, "GUI_VARIABLES")) {
                char name_list[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;

                for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                    ++variable_index) {
                    if (VARIABLE_NAME_TABLE[variable_index].bGUI) {
                        commands_length = append_list_name(
                            name_list, commands_length, VARIABLE_NAME_TABLE[variable_index].pName);
                    }
                }
                name_list[commands_length] = '\0';

                MSDPSendList(descriptor, value, name_list);
            }
        } else {
            /* Set any configurable variables */
            for (int variable_index = eMSDP_NONE + 1; variable_index < eMSDP_MAX;
                ++variable_index) {
                if (!VARIABLE_NAME_TABLE[variable_index].bConfigurable) {
                    continue;
                }
                if (!equals_ignoring_case(name, VARIABLE_NAME_TABLE[variable_index].pName)) {
                    continue;
                }

                MSDP_t* const variable = descriptor->pProtocol->pVariables[variable_index];
                if (VARIABLE_NAME_TABLE[variable_index].bString) {
                    /* A write-once variable can only be set if the value
                     * is "Unknown".  This is for things like client name,
                     * where we don't really want the player overwriting a
                     * proper client name with junk - but on the other hand,
                     * its possible a client may choose to use MSDP to
                     * identify itself.
                     */
                    const bool can_write = !VARIABLE_NAME_TABLE[variable_index].bWriteOnce
                        || std::strcmp(variable->pValueString, "Unknown") == 0;
                    if (can_write) {
                        /* Store the new value if it's valid */
                        char stored_text[MAX_CONFIGURABLE_STRING_LENGTH + 1];
                        int stored_length = 0;
                        const int max_length = VARIABLE_NAME_TABLE[variable_index].Max;
                        for (; stored_length < max_length && *value != '\0'; ++value) {
                            const unsigned char value_byte = static_cast<unsigned char>(*value);
                            if (value_byte >= FIRST_PRINTABLE_ASCII
                                && value_byte <= LAST_PRINTABLE_ASCII) {
                                stored_text[stored_length++] = *value;
                            }
                        }
                        stored_text[stored_length++] = '\0';

                        if (stored_length > VARIABLE_NAME_TABLE[variable_index].Min) {
                            std::free(variable->pValueString);
                            variable->pValueString = copy_string(stored_text);
                        }
                    }
                } else {
                    /* This variable only accepts numeric values */
                    /* Strip any leading spaces */
                    while (*value == ' ') {
                        ++value;
                    }

                    const bool is_number = *value != '\0' && is_all_digits(value);
                    if (is_number) {
                        int number = std::atoi(value);
                        const bool in_range = number >= VARIABLE_NAME_TABLE[variable_index].Min
                            && number <= VARIABLE_NAME_TABLE[variable_index].Max;
                        if (in_range) {
                            variable->ValueInt = number;
                        }
                    }
                }
            }
        }
    }
}

/******************************************************************************
 Local ATCP functions.
 ******************************************************************************/

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
        default: /* Anything else */
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

/******************************************************************************
 Local MSSP functions.
 ******************************************************************************/

static void send_mssp(const descriptor_t* descriptor)
{
    // IAC SB MSSP, and the room kept for IAC SE and the NUL.
    constexpr int mssp_header_length = 3;
    constexpr int mssp_trailer_room = 4;
    constexpr int pair_buffer_size = 128;
    char mssp_text[MAX_MSSP_BUFFER];
    char pair_text[pair_buffer_size];
    int mssp_length = mssp_header_length;

    /* Before updating the following table, please read the MSSP specification:
     *
     * http://tintin.sourceforge.net/mssp/
     *
     * It's important that you use the correct format and spelling for the MSSP
     * variables, otherwise crawlers may reject the data as invalid.
     */
    static constexpr MSSP_t mssp_table[] = {
        /* Required */
        { "NAME", MUD_NAME }, /* Change this in protocol.h */
        { "PLAYERS", "0" },
        { "UPTIME", "0" },

        /* Generic */
        { "CRAWL DELAY", "-1" },
        { nullptr, nullptr } /* This must always be last. */
    };

    /* Begin the subnegotiation sequence */
    std::snprintf(mssp_text, sizeof(mssp_text), "%c%c%c", IAC, SB, TELOPT_MSSP);

    for (int entry_index = 0; mssp_table[entry_index].pName != nullptr; ++entry_index) {
        int pair_length;

        /* Retrieve the next MSSP variable/value pair */
        std::snprintf(pair_text, sizeof(pair_text), "%c%s%c%s", MSSP_VAR,
            mssp_table[entry_index].pName, MSSP_VAL, mssp_table[entry_index].pValue);

        /* Make sure we don't overflow the buffer */
        pair_length = std::strlen(pair_text);
        if (pair_length + mssp_length < MAX_MSSP_BUFFER - mssp_trailer_room) {
            std::memcpy(
                mssp_text + mssp_length, pair_text, static_cast<std::size_t>(pair_length) + 1);
            mssp_length += pair_length;
        }
    }

    /* End the subnegotiation sequence */
    std::snprintf(mssp_text + mssp_length,
        sizeof(mssp_text) - static_cast<std::size_t>(mssp_length), "%c%c", IAC, SE);

    /* Send the sequence */
    send_to_client(descriptor, mssp_text);
}

/******************************************************************************
 Local MXP functions.
 ******************************************************************************/

static char* find_mxp_tag_value(const char* tag, const char* text)
{
    constexpr int value_buffer_size = 64;
    constexpr int max_value_length = 60;
    static char tag_value[value_buffer_size];
    const char* cursor = std::strstr(text, tag);

    if (cursor != nullptr) {
        const char* text_end = text + std::strlen(text);

        cursor += std::strlen(tag); /* Add length of the tag */

        if (cursor < text_end) {
            int value_length = 0;

            /* Some clients use quotes...and some don't. */
            if (*cursor == '\"') {
                cursor++;
            }

            for (; cursor < text_end && value_length < max_value_length; ++cursor) {
                char letter = *cursor;
                if (letter == '.' || std::isdigit(letter) || std::isalpha(letter)) {
                    tag_value[value_length++] = letter;
                } else {
                    /* Return the result */
                    tag_value[value_length] = '\0';
                    return tag_value;
                }
            }
        }
    }

    /* Return nullptr to indicate no tag was found. */
    return nullptr;
}

/******************************************************************************
 Local colour functions.
 ******************************************************************************/

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
        /* blue is the highest */
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

static const char* get_xterm_colour(bool background, int red, int green, int blue)
{
    static char escape_code[16];
    int xterm_colour_index = 16 + (red * 36) + (green * 6) + blue;
    std::snprintf(escape_code, sizeof(escape_code), "\033[%c8;5;%c%c%cm",
        '3' + background, /* Background */
        '0' + (xterm_colour_index / 100), /* Red        */
        '0' + ((xterm_colour_index % 100) / 10), /* Green      */
        '0' + (xterm_colour_index % 10)); /* Blue       */
    return escape_code;
}

static bool is_valid_colour(const char* rgb_code)
{
    if (rgb_code == nullptr) {
        return false;
    }

    /* The sequence is 4 bytes, but we can ignore anything after it. */
    if (std::strlen(rgb_code) < static_cast<std::size_t>(RGB_COLOUR_CODE_LENGTH)) {
        return false;
    }

    /* The first byte indicates foreground/background. */
    if (std::tolower(rgb_code[0]) != 'f' && std::tolower(rgb_code[0]) != 'b') {
        return false;
    }

    /* The remaining three bytes must each be in the range '0' to '5'. */
    for (int digit_index = 1; digit_index < RGB_COLOUR_CODE_LENGTH; ++digit_index) {
        if (rgb_code[digit_index] < '0' || rgb_code[digit_index] > '5') {
            return false;
        }
    }

    /* It's a valid foreground or background colour */
    return true;
}

/******************************************************************************
 Other local functions.
 ******************************************************************************/

static bool equals_ignoring_case(const char* first, const char* second)
{
    while (*first != '\0' && std::tolower(*first) == std::tolower(*second)) {
        ++first;
        ++second;
    }
    return (*first == '\0' && *second == '\0');
}

static bool starts_with_ignoring_case(const char* prefix, const char* text)
{
    while (*prefix != '\0' && std::tolower(*prefix) == std::tolower(*text)) {
        ++prefix;
        ++text;
    }
    return (*prefix == '\0');
}

static bool is_all_digits(const char* text)
{
    while (*text != '\0' && std::isdigit(*text)) {
        ++text;
    }
    return (*text == '\0');
}

// Copies the string contained in value into a newly allocated string.
// Returns nullptr if given a nullptr value, or out of memory; either is logged.
// The caller is responsible for freeing this string with free().
[[nodiscard]] static char* copy_string(const char* value)
{
    if (value == nullptr) {
        report_bug("copy_string: asked to copy a null string.\n");
        return nullptr;
    }

    return copy_string(std::string_view(value));
}

// Copies the string contained in value into a newly allocated, NUL-terminated string. value
// holds no NUL. Returns nullptr out of memory, and logs it.
// The caller is responsible for freeing this string with free().
[[nodiscard]] static char* copy_string(std::string_view value)
{
    const std::size_t length = value.size();
    char* const result = create_string(length);
    if (result == nullptr) {
        report_bug("copy_string: out of memory.\n");
        return nullptr;
    }

    std::memcpy(result, value.data(), length);
    result[length] = '\0';
    return result;
}

// Allocates a new string that can hold length characters plus the NUL terminator, which the
// caller writes. Returns nullptr out of memory.
// The caller is responsible for freeing this string with free().
[[nodiscard]] static char* create_string(std::size_t length)
{
    return static_cast<char*>(std::malloc(length + 1));
}
