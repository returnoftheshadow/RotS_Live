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

static void Write(const descriptor_t* apDescriptor, const char* apData)
{
    if (apDescriptor != nullptr) {
        if (apDescriptor->pProtocol->WriteOOB > 0) {
            apDescriptor->pProtocol->WriteOOB = 2;
        }
    }
    write_to_descriptor(apDescriptor->descriptor, apData);
}

static void ReportBug(const char* apText) { vmudlog(NRM, apText); }

static void InfoMessage(const descriptor_t* apDescriptor, const char* apData)
{
    Write(apDescriptor, "\t[F210][\toINFO\t[F210]]\tn ");
    Write(apDescriptor, apData);
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
static const char s_Button1[] = "\005\002Help\002help\006";
static const char s_Button2[] = "\005\002Look\002look\006";
static const char s_Button3[] = "\005\002Score\002score\006";
static const char s_Button4[] = "\005\002Equipment\002equipment\006";
static const char s_Button5[] = "\005\002Inventory\002inventory\006";

static const char s_Gauge1[] = "\005\002Health\002red\002HEALTH\002HEALTH_MAX\006";
static const char s_Gauge2[] = "\005\002Mana\002blue\002MANA\002MANA_MAX\006";
static const char s_Gauge3[] = "\005\002Movement\002green\002MOVEMENT\002MOVEMENT_MAX\006";
static const char s_Gauge4[] = "\005\002Exp TNL\002yellow\002EXPERIENCE\002EXPERIENCE_MAX\006";
static const char s_Gauge5[]
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

static constexpr variable_name_t VariableNameTable[eMSDP_MAX + 1] = {
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
    STRING_GUI(eMSDP_BUTTON_1, "BUTTON_1", s_Button1),
    STRING_GUI(eMSDP_BUTTON_2, "BUTTON_2", s_Button2),
    STRING_GUI(eMSDP_BUTTON_3, "BUTTON_3", s_Button3),
    STRING_GUI(eMSDP_BUTTON_4, "BUTTON_4", s_Button4),
    STRING_GUI(eMSDP_BUTTON_5, "BUTTON_5", s_Button5),
    STRING_GUI(eMSDP_GAUGE_1, "GAUGE_1", s_Gauge1),
    STRING_GUI(eMSDP_GAUGE_2, "GAUGE_2", s_Gauge2),
    STRING_GUI(eMSDP_GAUGE_3, "GAUGE_3", s_Gauge3),
    STRING_GUI(eMSDP_GAUGE_4, "GAUGE_4", s_Gauge4),
    STRING_GUI(eMSDP_GAUGE_5, "GAUGE_5", s_Gauge5),

    /* Group information */
    STRING_READ_ONLY(eMSDP_GROUP, "GROUP"),

    NUMBER_READ_ONLY(eMSDP_MAX, "") /* This must always be last. */
};

// The most characters a client may store in a configurable string variable.
constexpr int MAX_CONFIGURABLE_STRING_LENGTH = 40;

// The largest Max of any configurable string variable in VariableNameTable.
static constexpr int longest_configurable_string()
{
    int longest = 0;
    for (const variable_name_t& entry : VariableNameTable) {
        if (entry.bConfigurable && entry.bString && entry.Max > longest) {
            longest = entry.Max;
        }
    }
    return longest;
}

static_assert(longest_configurable_string() <= MAX_CONFIGURABLE_STRING_LENGTH,
    "A configurable string variable is longer than ExecuteMSDPPair's buffer.");

// The length of a space-separated list of every name in VariableNameTable, with its NUL.
static constexpr std::size_t all_variable_names_length()
{
    std::size_t length = 0;
    for (int index = eMSDP_NONE + 1; index < eMSDP_MAX; ++index) {
        length += std::char_traits<char>::length(VariableNameTable[index].pName) + 1;
    }
    return length;
}

static_assert(all_variable_names_length() <= MAX_OUTPUT_BUFFER,
    "ExecuteMSDPPair's variable lists do not fit in MAX_OUTPUT_BUFFER.");

// Appends name to the space-separated list of list_length characters in out_list, which has room,
// and returns the list's new length. Writes no NUL.
static std::size_t append_list_name(char* out_list, std::size_t list_length, const char* name)
{
    if (list_length != 0) {
        out_list[list_length] = ' ';
        ++list_length;
    }
    const std::size_t name_length = strlen(name);
    memcpy(out_list + list_length, name, name_length);
    return list_length + name_length;
}

/******************************************************************************
 Local function prototypes.
 ******************************************************************************/

static void Negotiate(const descriptor_t* apDescriptor);
static void PerformHandshake(const descriptor_t* apDescriptor, char aCmd, char aProtocol);
static void PerformSubnegotiation(
    const descriptor_t* apDescriptor, char aCmd, const char* apData, int aSize);
static void SendNegotiationSequence(const descriptor_t* apDescriptor, char aCmd, char aProtocol);
static bool ConfirmNegotiation(
    const descriptor_t* apDescriptor, negotiated_t aProtocol, bool abWillDo, bool abSendReply);

static void ParseMSDP(const descriptor_t* apDescriptor, const char* apData);
static void ExecuteMSDPPair(
    const descriptor_t* apDescriptor, const char* apVariable, const char* apValue);

static void ParseATCP(const descriptor_t* apDescriptor, const char* apData);

static void SendMSSP(const descriptor_t* apDescriptor);

static char* GetMxpTag(const char* apTag, const char* apText);

static const char* GetAnsiColour(bool abBackground, int aRed, int aGreen, int aBlue);
static const char* GetRGBColour(bool abBackground, int aRed, int aGreen, int aBlue);
static bool IsValidColour(const char* apArgument);

static bool MatchString(const char* apFirst, const char* apSecond);
static bool PrefixString(const char* apPart, const char* apWhole);
static bool IsNumber(const char* apString);
static char* AllocString(const char* apString);
[[nodiscard]] static char* copy_string(const char* value);
[[nodiscard]] static char* copy_string(std::string_view value);
[[nodiscard]] static char* create_string(std::size_t length);
static std::size_t msdp_sanitized_length(std::string_view value);
static char* msdp_write_sanitized(std::string_view value, char* out_text);

/******************************************************************************
 ANSI colour codes.
 ******************************************************************************/

static const char s_Clean[] = "\033[0;00m"; /* Remove colour */

static const char s_DarkBlack[] = "\033[0;30m"; /* Black foreground */
static const char s_DarkRed[] = "\033[0;31m"; /* Red foreground */
static const char s_DarkGreen[] = "\033[0;32m"; /* Green foreground */
static const char s_DarkYellow[] = "\033[0;33m"; /* Yellow foreground */
static const char s_DarkBlue[] = "\033[0;34m"; /* Blue foreground */
static const char s_DarkMagenta[] = "\033[0;35m"; /* Magenta foreground */
static const char s_DarkCyan[] = "\033[0;36m"; /* Cyan foreground */
static const char s_DarkWhite[] = "\033[0;37m"; /* White foreground */

static const char s_BoldBlack[] = "\033[1;30m"; /* Grey foreground */
static const char s_BoldRed[] = "\033[1;31m"; /* Bright red foreground */
static const char s_BoldGreen[] = "\033[1;32m"; /* Bright green foreground */
static const char s_BoldYellow[] = "\033[1;33m"; /* Bright yellow foreground */
static const char s_BoldBlue[] = "\033[1;34m"; /* Bright blue foreground */
static const char s_BoldMagenta[] = "\033[1;35m"; /* Bright magenta foreground */
static const char s_BoldCyan[] = "\033[1;36m"; /* Bright cyan foreground */
static const char s_BoldWhite[] = "\033[1;37m"; /* Bright white foreground */

static const char s_BackBlack[] = "\033[1;40m"; /* Black background */
static const char s_BackRed[] = "\033[1;41m"; /* Red background */
static const char s_BackGreen[] = "\033[1;42m"; /* Green background */
static const char s_BackYellow[] = "\033[1;43m"; /* Yellow background */
static const char s_BackBlue[] = "\033[1;44m"; /* Blue background */
static const char s_BackMagenta[] = "\033[1;45m"; /* Magenta background */
static const char s_BackCyan[] = "\033[1;46m"; /* Cyan background */
static const char s_BackWhite[] = "\033[1;47m"; /* White background */

/******************************************************************************
 Protocol global functions.
 ******************************************************************************/

protocol_t* ProtocolCreate()
{
    int i; /* Loop counter */
    protocol_t* pProtocol;

    /* Called the first time we enter - make sure the table is correct */
    static bool bInit = false;
    if (!bInit) {
        bInit = true;
        for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
            if (VariableNameTable[i].Variable != i) {
                ReportBug("MSDP: Variable table does not match the enums in the header.\n");
                break;
            }
        }
    }

    pProtocol = new protocol_t;
    pProtocol->WriteOOB = 0;
    for (i = eNEGOTIATED_TTYPE; i < eNEGOTIATED_MAX; ++i) {
        pProtocol->Negotiated[i] = false;
    }
    pProtocol->bIACMode = false;
    pProtocol->bNegotiated = false;
    pProtocol->bRenegotiate = false;
    pProtocol->bNeedMXPVersion = false;
    pProtocol->bBlockMXP = false;
    pProtocol->PendingInputLength = 0;
    memset(pProtocol->PendingInput, 0, sizeof(pProtocol->PendingInput));
    pProtocol->IacInputLength = 0;
    memset(pProtocol->IacInput, 0, sizeof(pProtocol->IacInput));
    pProtocol->bTTYPE = false;
    pProtocol->bECHO = false;
    pProtocol->bNAWS = false;
    pProtocol->bCHARSET = false;
    pProtocol->bMSDP = true;
    pProtocol->bMSSP = false;
    pProtocol->bATCP = false;
    pProtocol->bMSP = false;
    pProtocol->bMXP = false;
    pProtocol->bMCCP = false;
    pProtocol->bEOR = false;
    pProtocol->bSGA = false;
    pProtocol->b256Support = eUNKNOWN;
    pProtocol->ScreenWidth = 0;
    pProtocol->ScreenHeight = 0;
    pProtocol->pMXPVersion = AllocString("Unknown");
    pProtocol->pLastTTYPE = nullptr;
    pProtocol->pVariables = new MSDP_t*[eMSDP_MAX];

    for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        pProtocol->pVariables[i] = new MSDP_t;
        pProtocol->pVariables[i]->bReport = true;
        /* Prime server-reported variables so the first update after login sends
           them even when their value is still the default -- otherwise a stat
           that is legitimately 0 (spell pen/power, carried weight, an unused
           profession level) is never transmitted and looks unsupported to the
           client. Client-owned config and GUI template variables are excluded. */
        pProtocol->pVariables[i]->bDirty
            = !VariableNameTable[i].bConfigurable && !VariableNameTable[i].bGUI;
        pProtocol->pVariables[i]->ValueInt = 0;
        pProtocol->pVariables[i]->pValueString = nullptr;

        if (VariableNameTable[i].bString) {
            if (VariableNameTable[i].pDefault != nullptr) {
                pProtocol->pVariables[i]->pValueString = AllocString(VariableNameTable[i].pDefault);
            } else if (VariableNameTable[i].bConfigurable) {
                pProtocol->pVariables[i]->pValueString = AllocString("Unknown");
            } else {
                /* Use an empty string */
                pProtocol->pVariables[i]->pValueString = AllocString("");
            }
        } else if (VariableNameTable[i].Default != 0) {
            pProtocol->pVariables[i]->ValueInt = VariableNameTable[i].Default;
        }
    }

    return pProtocol;
}

void ProtocolDestroy(protocol_t* apProtocol)
{
    int i; /* Loop counter */

    // ProtocolCreate makes the protocol, its variable table and each variable with new; only
    // the strings come from AllocString's malloc.
    for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        free(apProtocol->pVariables[i]->pValueString);
        delete apProtocol->pVariables[i];
    }

    delete[] apProtocol->pVariables;
    free(apProtocol->pLastTTYPE);
    free(apProtocol->pMXPVersion);
    delete apProtocol;
}

void ProtocolInput(const descriptor_t* apDescriptor, const char* apData, int aSize, char* apOut)
{
    static char CmdBuf[MAX_PROTOCOL_BUFFER + 1];
    int CmdIndex = 0;
    int Index;

    protocol_t* pProtocol = protocol_of(apDescriptor);
    const bool has_input = apData != nullptr && aSize > 0;
    if (pProtocol == nullptr || !has_input || apOut == nullptr) {
        return;
    }

    char* IacBuf = pProtocol->IacInput;
    int IacIndex = 0;
    if (pProtocol->bIACMode) {
        IacIndex = pProtocol->IacInputLength;
    }

    if (pProtocol->PendingInputLength > 0) {
        char* combined = static_cast<char*>(alloca(pProtocol->PendingInputLength + aSize));
        memcpy(combined, pProtocol->PendingInput, pProtocol->PendingInputLength);
        memcpy(combined + pProtocol->PendingInputLength, apData, aSize);
        apData = combined;
        aSize += pProtocol->PendingInputLength;
        pProtocol->PendingInputLength = 0;
    }

    for (Index = 0; Index < aSize; ++Index) {
        const bool has_next = (Index + 1) < aSize;
        const bool has_two_more = (Index + 2) < aSize;
        const bool has_three_more = (Index + 3) < aSize;
        const bool is_iac = apData[Index] == TELNET_IAC;
        const bool is_escape = apData[Index] == ANSI_ESCAPE;
        const bool is_doubled_iac = is_iac && has_next && apData[Index + 1] == TELNET_IAC;
        const bool is_mxp_line_tag = is_escape && has_three_more && apData[Index + 1] == '['
            && isdigit(apData[Index + 2]) && apData[Index + 3] == 'z';

        /* If we'd overflow the buffer, we just ignore the input */
        const bool command_buffer_full = CmdIndex >= MAX_PROTOCOL_BUFFER;
        const bool subnegotiation_buffer_full = IacIndex >= MAX_PROTOCOL_BUFFER;
        if (command_buffer_full || subnegotiation_buffer_full) {
            ReportBug("ProtocolInput: Too much incoming data to store in the buffer.\n");
            if (subnegotiation_buffer_full) {
                pProtocol->bIACMode = false;
                pProtocol->IacInputLength = 0;
                pProtocol->IacInput[0] = '\0';
            }
            return;
        }

        /* IAC IAC is treated as a single value of 255 */
        if (is_doubled_iac) {
            if (pProtocol->bIACMode) {
                IacBuf[IacIndex++] = TELNET_IAC;
            } else {
                /* In-band command */
                CmdBuf[CmdIndex++] = TELNET_IAC;
            }
            Index++;
        } else if (pProtocol->bIACMode) {
            const bool ends_subnegotiation = is_iac && has_next && apData[Index + 1] == TELNET_SE;

            /* End subnegotiation. */
            if (is_iac && !has_next) {
                pProtocol->PendingInput[0] = apData[Index];
                pProtocol->PendingInputLength = 1;
                break;
            } else if (ends_subnegotiation) {
                Index++;
                pProtocol->bIACMode = false;
                IacBuf[IacIndex] = '\0';
                if (IacIndex >= 2) {
                    PerformSubnegotiation(apDescriptor, IacBuf[0], &IacBuf[1], IacIndex - 1);
                }
                IacIndex = 0;
                pProtocol->IacInputLength = 0;
            } else {
                IacBuf[IacIndex++] = apData[Index];
            }
        } else if (is_escape && !has_three_more) {
            const int remaining = aSize - Index;
            const bool may_start_mxp_line_tag = remaining == 1
                || (remaining >= 2 && apData[Index + 1] == '[')
                || (remaining >= 3 && apData[Index + 1] == '[' && isdigit(apData[Index + 2]));
            if (may_start_mxp_line_tag) {
                memcpy(pProtocol->PendingInput, apData + Index, remaining);
                pProtocol->PendingInputLength = remaining;
                break;
            }
        } else if (is_mxp_line_tag) {
            constexpr int line_tag_length = 4;
            char MXPBuffer[MXP_BUFFER_SIZE];
            char* pMXPTag = nullptr;
            int i = 0; /* Loop counter */

            Index += line_tag_length; /* Skip to the start of the MXP sequence. */

            while (Index < aSize && apData[Index] != '>' && i < MAX_MXP_TAG_LENGTH) {
                MXPBuffer[i++] = apData[Index++];
            }
            MXPBuffer[i++] = '>';
            MXPBuffer[i] = '\0';

            pMXPTag = GetMxpTag("CLIENT=", MXPBuffer);
            if (pMXPTag != nullptr) {
                /* Overwrite the previous client name - this is harder to fake */
                free(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString = AllocString(pMXPTag);
            }

            pMXPTag = GetMxpTag("VERSION=", MXPBuffer);
            if (pMXPTag != nullptr) {
                const char* pClientName = pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString;

                free(pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString = AllocString(pMXPTag);

                if (MatchString("MUSHCLIENT", pClientName)) {
                    /* MUSHclient 4.02 and later supports 256 colours. */
                    if (strcmp(pMXPTag, "4.02") >= 0) {
                        pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        pProtocol->b256Support = eYES;
                    } else {
                        /* We know for sure that 256 colours are not supported */
                        pProtocol->b256Support = eNO;
                    }
                } else if (MatchString("CMUD", pClientName)) {
                    /* CMUD 3.04 and later supports 256 colours. */
                    if (strcmp(pMXPTag, "3.04") >= 0) {
                        pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        pProtocol->b256Support = eYES;
                    } else {
                        /* We know for sure that 256 colours are not supported */
                        pProtocol->b256Support = eNO;
                    }
                } else if (MatchString("ATLANTIS", pClientName)) {
                    /* Atlantis 0.9.9.0 supports XTerm 256 colours, but it doesn't
                     * yet have MXP.  However MXP is planned, so once it responds
                     * to a <VERSION> tag we'll know we can use 256 colours.
                     */
                    pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    pProtocol->b256Support = eYES;
                }
            }

            pMXPTag = GetMxpTag("MXP=", MXPBuffer);
            if (pMXPTag != nullptr) {
                free(pProtocol->pMXPVersion);
                pProtocol->pMXPVersion = AllocString(pMXPTag);
            }

            if (strcmp(pProtocol->pMXPVersion, "Unknown") != 0) {
                Write(apDescriptor, "\n");
                snprintf(MXPBuffer, sizeof(MXPBuffer), "MXP version %s detected and enabled.\r\n",
                    pProtocol->pMXPVersion);
                InfoMessage(apDescriptor, MXPBuffer);
            }
        } else {
            /* In-band command */
            if (apData[Index] == TELNET_IAC) {
                if (!has_next) {
                    pProtocol->PendingInput[0] = apData[Index];
                    pProtocol->PendingInputLength = 1;
                    break;
                }

                switch (apData[Index + 1]) {
                case TELNET_SB: /* Begin subnegotiation. */
                    Index++;
                    pProtocol->bIACMode = true;
                    break;

                case TELNET_DO: /* Handshake. */
                case TELNET_DONT:
                case TELNET_WILL:
                case TELNET_WONT:
                    if (!has_two_more) {
                        pProtocol->PendingInput[0] = apData[Index];
                        pProtocol->PendingInput[1] = apData[Index + 1];
                        pProtocol->PendingInputLength = 2;
                        break;
                    }
                    PerformHandshake(apDescriptor, apData[Index + 1], apData[Index + 2]);
                    Index += 2;
                    break;

                case TELNET_IAC: /* Two IACs count as one. */
                    CmdBuf[CmdIndex++] = TELNET_IAC;
                    Index++;
                    break;

                default: /* Skip it. */
                    Index++;
                    break;
                }
                if (pProtocol->PendingInputLength > 0) {
                    break;
                }
            } else {
                CmdBuf[CmdIndex++] = apData[Index];
            }
            if (pProtocol->PendingInputLength > 0) {
                break;
            }
        }

        if (pProtocol->bIACMode && apData[Index] == TELNET_IAC && !has_next) {
            pProtocol->PendingInput[0] = apData[Index];
            pProtocol->PendingInputLength = 1;
            break;
        }
    }

    /* Terminate the two buffers */
    IacBuf[IacIndex] = '\0';
    CmdBuf[CmdIndex] = '\0';
    if (pProtocol->bIACMode) {
        pProtocol->IacInputLength = IacIndex;
    } else {
        pProtocol->IacInputLength = 0;
    }

    /* Copy the input buffer back to the player. */
    strcat(apOut, CmdBuf);
}

const char* ProtocolOutput(const descriptor_t* apDescriptor, const char* apData, int* apLength)
{
    static char Result[MAX_OUTPUT_BUFFER + 1];
    static constexpr char Tab[] = "\t";
    static constexpr char MSP[] = "!!";
    static constexpr char MXPStart[] = "\033[1z<";
    static constexpr char MXPStop[] = ">\033[7z";
    static constexpr char LinkStart[] = "\033[1z<send>\033[7z";
    static constexpr char LinkStop[] = "\033[1z</send>\033[7z";
    // The most characters a \t[...] code's text may hold, such as a Unicode fallback.
    constexpr int max_bracket_text_length = 7;
    bool bTerminate = false;
    bool bUseMXP = false;
    bool bUseMSP = false;
    /* Index values */
    int i = 0;
    int j = 0;

    protocol_t* pProtocol = protocol_of(apDescriptor);
    if (pProtocol == nullptr || apData == nullptr) {
        return apData;
    }

    /* Strip !!SOUND() triggers if they support MSP or are using sound */
    if (pProtocol->bMSP || pProtocol->pVariables[eMSDP_SOUND]->ValueInt != 0) {
        bUseMSP = true;
    }

    for (; i < MAX_OUTPUT_BUFFER && apData[j] != '\0' && !bTerminate; ++j) {
        // A length of zero or less means apData ends at its NUL.
        const bool past_length = *apLength > 0 && j >= *apLength;
        if (past_length) {
            break;
        }

        const bool is_old_msp_trigger = bUseMSP && j > 0 && apData[j - 1] == '!' && apData[j] == '!'
            && PrefixString("SOUND(", &apData[j + 1]);

        if (apData[j] == '\t') {
            const char* pCopyFrom = nullptr;
            // A Unicode code's ASCII fallback. It lives here, not in its case, because pCopyFrom
            // points into it until the copy below.
            char UnicodeFallback[max_bracket_text_length + 1] = { '\0' };

            ++j;
            switch (apData[j]) {
            case '\t': /* Two tabs in a row will display an actual tab */
                pCopyFrom = Tab;
                break;
            case 'n':
                pCopyFrom = s_Clean;
                break;
            case 'r': /* dark red */
                pCopyFrom = ColourRGB(apDescriptor, "F200");
                break;
            case 'R': /* light red */
                pCopyFrom = ColourRGB(apDescriptor, "F500");
                break;
            case 'g': /* dark green */
                pCopyFrom = ColourRGB(apDescriptor, "F020");
                break;
            case 'G': /* light green */
                pCopyFrom = ColourRGB(apDescriptor, "F050");
                break;
            case 'y': /* dark yellow */
                pCopyFrom = ColourRGB(apDescriptor, "F220");
                break;
            case 'Y': /* light yellow */
                pCopyFrom = ColourRGB(apDescriptor, "F550");
                break;
            case 'b': /* dark blue */
                pCopyFrom = ColourRGB(apDescriptor, "F002");
                break;
            case 'B': /* light blue */
                pCopyFrom = ColourRGB(apDescriptor, "F005");
                break;
            case 'm': /* dark magenta */
                pCopyFrom = ColourRGB(apDescriptor, "F202");
                break;
            case 'M': /* light magenta */
                pCopyFrom = ColourRGB(apDescriptor, "F505");
                break;
            case 'c': /* dark cyan */
                pCopyFrom = ColourRGB(apDescriptor, "F022");
                break;
            case 'C': /* light cyan */
                pCopyFrom = ColourRGB(apDescriptor, "F055");
                break;
            case 'w': /* dark white */
                pCopyFrom = ColourRGB(apDescriptor, "F222");
                break;
            case 'W': /* light white */
                pCopyFrom = ColourRGB(apDescriptor, "F555");
                break;
            case 'a': /* dark azure */
                pCopyFrom = ColourRGB(apDescriptor, "F014");
                break;
            case 'A': /* light azure */
                pCopyFrom = ColourRGB(apDescriptor, "F025");
                break;
            case 'j': /* dark jade */
                pCopyFrom = ColourRGB(apDescriptor, "F031");
                break;
            case 'J': /* light jade */
                pCopyFrom = ColourRGB(apDescriptor, "F052");
                break;
            case 'l': /* dark lime */
                pCopyFrom = ColourRGB(apDescriptor, "F140");
                break;
            case 'L': /* light lime */
                pCopyFrom = ColourRGB(apDescriptor, "F250");
                break;
            case 'o': /* dark orange */
                pCopyFrom = ColourRGB(apDescriptor, "F520");
                break;
            case 'O': /* light orange */
                pCopyFrom = ColourRGB(apDescriptor, "F530");
                break;
            case 'p': /* dark pink */
                pCopyFrom = ColourRGB(apDescriptor, "F301");
                break;
            case 'P': /* light pink */
                pCopyFrom = ColourRGB(apDescriptor, "F502");
                break;
            case 't': /* dark tan */
                pCopyFrom = ColourRGB(apDescriptor, "F210");
                break;
            case 'T': /* light tan */
                pCopyFrom = ColourRGB(apDescriptor, "F321");
                break;
            case 'v': /* dark violet */
                pCopyFrom = ColourRGB(apDescriptor, "F104");
                break;
            case 'V': /* light violet */
                pCopyFrom = ColourRGB(apDescriptor, "F205");
                break;
            case '(': /* MXP link */
                if (!pProtocol->bBlockMXP && pProtocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    pCopyFrom = LinkStart;
                }
                break;
            case ')': /* MXP link */
                if (!pProtocol->bBlockMXP && pProtocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    pCopyFrom = LinkStop;
                }
                pProtocol->bBlockMXP = false;
                break;
            case '<':
                if (!pProtocol->bBlockMXP && pProtocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
                    pCopyFrom = MXPStart;
                    bUseMXP = true;
                } else {
                    /* No MXP support, so just strip it out */
                    while (apData[j] != '\0' && apData[j] != '>') {
                        ++j;
                    }
                }
                pProtocol->bBlockMXP = false;
                break;
            case '[':
                ++j;
                if (tolower(apData[j]) == 'u') {
                    char BugString[256];
                    int Index = 0;
                    int Number = 0;
                    bool bDone = false;
                    bool bValid = true;

                    ++j;
                    while (isdigit(apData[j])) {
                        Number *= 10;
                        Number += (apData[j]) - '0';
                        ++j;
                    }

                    if (apData[j] == '/') {
                        ++j;
                    }

                    while (apData[j] != '\0' && !bDone) {
                        if (apData[j] == ']') {
                            bDone = true;
                        } else if (Index < max_bracket_text_length) {
                            UnicodeFallback[Index++] = apData[j++];
                        } else {
                            /* It's too long, so ignore the rest and note the problem */
                            j++;
                            bValid = false;
                        }
                    }

                    if (!bDone) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: Unicode substitute '%s' wasn't terminated with ']'.\n",
                            UnicodeFallback);
                        ReportBug(BugString);
                    } else if (!bValid) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: Unicode substitute '%s' truncated.  Missing ']'?\n",
                            UnicodeFallback);
                        ReportBug(BugString);
                    } else if (pProtocol->pVariables[eMSDP_UTF_8]->ValueInt != 0) {
                        pCopyFrom = UnicodeGet(Number);
                    } else {
                        /* Display the substitute string */
                        pCopyFrom = UnicodeFallback;
                    }

                    /* Terminate if we've reached the end of the string */
                    bTerminate = !bDone;
                } else if (tolower(apData[j]) == 'f' || tolower(apData[j]) == 'b') {
                    char Buffer[max_bracket_text_length + 1] = { '\0' };
                    char BugString[256];
                    int Index = 0;
                    bool bDone = false;
                    bool bValid = true;

                    /* Copy the 'f' (foreground) or 'b' (background) */
                    Buffer[Index++] = apData[j++];

                    while (apData[j] != '\0' && !bDone && bValid) {
                        if (apData[j] == ']') {
                            bDone = true;
                        } else if (Index < RGB_COLOUR_CODE_LENGTH) {
                            Buffer[Index++] = apData[j++];
                        } else {
                            /* It's too long, so drop out - the colour code may still be
                               valid */
                            bValid = false;
                        }
                    }

                    const char* layer_name = "back";
                    if (tolower(Buffer[0]) == 'f') {
                        layer_name = "fore";
                    }

                    if (!bDone || !bValid) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: RGB %sground colour '%s' wasn't terminated with ']'.\n",
                            layer_name, &Buffer[1]);
                        ReportBug(BugString);
                    } else if (!IsValidColour(Buffer)) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: RGB %sground colour '%s' invalid (each digit must be "
                            "in the range 0-5).\n",
                            layer_name, &Buffer[1]);
                        ReportBug(BugString);
                    } else {
                        /* Success */
                        pCopyFrom = ColourRGB(apDescriptor, Buffer);
                    }
                } else if (tolower(apData[j]) == 'x') {
                    char Buffer[max_bracket_text_length + 1] = { '\0' };
                    char BugString[256];
                    int Index = 0;
                    bool bDone = false;
                    bool bValid = true;

                    ++j; /* Skip the 'x' */

                    while (apData[j] != '\0' && !bDone) {
                        if (apData[j] == ']') {
                            bDone = true;
                        } else if (Index < max_bracket_text_length) {
                            Buffer[Index++] = apData[j++];
                        } else {
                            /* It's too long, so ignore the rest and note the problem */
                            j++;
                            bValid = false;
                        }
                    }

                    if (!bDone) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: Required MXP version '%s' wasn't terminated with ']'.\n",
                            Buffer);
                        ReportBug(BugString);
                    } else if (!bValid) {
                        snprintf(BugString, sizeof(BugString),
                            "BUG: Required MXP version '%s' too long.  Missing ']'?\n", Buffer);
                        ReportBug(BugString);
                    } else {
                        const bool mxp_version_unknown
                            = strcmp(pProtocol->pMXPVersion, "Unknown") == 0;
                        const bool mxp_version_too_low = mxp_version_unknown
                            || strcmp(pProtocol->pMXPVersion, Buffer) < 0;
                        /* Block the tag unless their version of MXP is high enough */
                        pProtocol->bBlockMXP = mxp_version_too_low;
                    }

                    /* Terminate if we've reached the end of the string */
                    bTerminate = !bDone;
                }
                break;
            case '!': /* Used for in-band MSP sound triggers */
                pCopyFrom = MSP;
                break;
            case '\0':
                bTerminate = true;
                break;
            default:
                break;
            }

            /* Copy the colour code, if any. */
            if (pCopyFrom != nullptr) {
                while (*pCopyFrom != '\0' && i < MAX_OUTPUT_BUFFER) {
                    Result[i++] = *pCopyFrom++;
                }
            }
        } else if (bUseMXP && apData[j] == '>') {
            const char* pCopyFrom = MXPStop;
            while (*pCopyFrom != '\0' && i < MAX_OUTPUT_BUFFER) {
                Result[i++] = *pCopyFrom++;
            }
            bUseMXP = false;
        } else if (is_old_msp_trigger) {
            /* Avoid accidental triggering of old-style MSP triggers */
            Result[i++] = '?';
        } else {
            /* Just copy the character normally */
            Result[i++] = apData[j];
        }
    }

    /* If we'd overflow the buffer, we don't send any output */
    if (i >= MAX_OUTPUT_BUFFER) {
        i = 0;
        ReportBug("ProtocolOutput: Too much outgoing data to store in the buffer.\n");
    }

    /* Terminate the string */
    Result[i] = '\0';

    /* Store the length */
    if (apLength != nullptr) {
        *apLength = i;
    }

    /* Return the string */
    return Result;
}

/* Some clients (such as GMud) don't properly handle negotiation, and simply
 * display every printable character to the screen.  However TTYPE isn't a
 * printable character, so we negotiate for it first, and only negotiate for
 * other protocols if the client responds with IAC WILL TTYPE or IAC WONT
 * TTYPE.  Thanks go to Donky on MudBytes for the suggestion.
 */
void ProtocolNegotiate(const descriptor_t* apDescriptor)
{
    ConfirmNegotiation(apDescriptor, eNEGOTIATED_TTYPE, true, true);
}

/******************************************************************************
 MSDP global functions.
 ******************************************************************************/

void MSDPUpdate(const descriptor_t* apDescriptor)
{
    int i; /* Loop counter */

    protocol_t* pProtocol = protocol_of(apDescriptor);
    if (pProtocol == nullptr) {
        return;
    }

    for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        if (pProtocol->pVariables[i]->bReport) {
            if (pProtocol->pVariables[i]->bDirty) {
                if (MSDPSend(apDescriptor, static_cast<variable_t>(i))) {
                    pProtocol->pVariables[i]->bDirty = false;
                }
            }
        }
    }
}

void MSDPFlush(const descriptor_t* apDescriptor, variable_t aMSDP)
{
    if (aMSDP > eMSDP_NONE && aMSDP < eMSDP_MAX) {
        protocol_t* pProtocol = protocol_of(apDescriptor);

        if (pProtocol != nullptr && pProtocol->pVariables[aMSDP]->bReport) {
            if (pProtocol->pVariables[aMSDP]->bDirty) {
                if (MSDPSend(apDescriptor, aMSDP)) {
                    pProtocol->pVariables[aMSDP]->bDirty = false;
                }
            }
        }
    }
}

void MSDPMarkAllReportedDirty(const descriptor_t* apDescriptor)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);
    if (pProtocol == nullptr) {
        return;
    }

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        if (pProtocol->pVariables[i]->bReport) {
            pProtocol->pVariables[i]->bDirty = true;
        }
    }
}

bool MSDPSend(const descriptor_t* apDescriptor, variable_t aMSDP)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };
    protocol_t* pProtocol = protocol_of(apDescriptor);

    if (pProtocol == nullptr || apDescriptor->character == nullptr) {
        return false;
    }
    const bool wants_msdp = PRF_FLAGGED(apDescriptor->character, PRF_MSDP);
    if (!wants_msdp) {
        return false;
    }

    if (aMSDP > eMSDP_NONE && aMSDP < eMSDP_MAX) {
        if (VariableNameTable[aMSDP].bString) {
            /* Should really be replaced with a dynamic buffer */
            const std::size_t name_length = strlen(VariableNameTable[aMSDP].pName);
            const std::size_t value_length = strlen(pProtocol->pVariables[aMSDP]->pValueString);
            const std::size_t RequiredBuffer = name_length + value_length + MSDP_FRAMING_LENGTH;

            if (RequiredBuffer >= static_cast<std::size_t>(MAX_VARIABLE_LENGTH)) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer),
                    "MSDPSend: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n",
                    VariableNameTable[aMSDP].pName, RequiredBuffer, MAX_VARIABLE_LENGTH);
                ReportBug(MSDPBuffer);
                MSDPBuffer[0] = '\0';
            } else if (pProtocol->bMSDP) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%c%c%s%c%s%c%c", IAC, SB, TELOPT_MSDP,
                    MSDP_VAR, VariableNameTable[aMSDP].pName, MSDP_VAL,
                    pProtocol->pVariables[aMSDP]->pValueString, IAC, SE);
            } else if (pProtocol->bATCP) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%cMSDP.%s %s%c%c", IAC, SB,
                    TELOPT_ATCP, VariableNameTable[aMSDP].pName,
                    pProtocol->pVariables[aMSDP]->pValueString, IAC, SE);
            }
        } else {
            /* It's an integer, not a string */
            if (pProtocol->bMSDP) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%c%c%s%c%d%c%c", IAC, SB, TELOPT_MSDP,
                    MSDP_VAR, VariableNameTable[aMSDP].pName, MSDP_VAL,
                    pProtocol->pVariables[aMSDP]->ValueInt, IAC, SE);
            } else if (pProtocol->bATCP) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%cMSDP.%s %d%c%c", IAC, SB,
                    TELOPT_ATCP, VariableNameTable[aMSDP].pName,
                    pProtocol->pVariables[aMSDP]->ValueInt, IAC, SE);
            }
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (MSDPBuffer[0] != '\0') {
            Write(apDescriptor, MSDPBuffer);
            return true;
        }
    }
    return false;
}

void MSDPSendPair(const descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != nullptr && apValue != nullptr) {
        protocol_t* pProtocol = protocol_of(apDescriptor);
        if (pProtocol == nullptr) {
            return;
        }

        const std::string_view value(apValue);
        const std::size_t value_length = msdp_sanitized_length(value);

        /* Should really be replaced with a dynamic buffer */
        // Kept as std::size_t so no value is long enough to wrap around the limit check.
        constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
        const std::size_t name_length = strlen(apVariable);
        const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

        if (required_length >= buffer_limit) {
            if (required_length - value_length < buffer_limit) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer),
                    "MSDPSendPair: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n", apVariable,
                    required_length, MAX_VARIABLE_LENGTH);
            } else {
                /* The variable name itself is too long */
                snprintf(MSDPBuffer, sizeof(MSDPBuffer),
                    "MSDPSendPair: Variable name has a length of %zu bytes (exceeds "
                    "MAX_VARIABLE_LENGTH of %d).\n",
                    required_length, MAX_VARIABLE_LENGTH);
            }

            ReportBug(MSDPBuffer);
            MSDPBuffer[0] = '\0';
        } else if (pProtocol->bMSDP) {
            const int prefix_length = snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%c%c%s%c", IAC,
                SB, TELOPT_MSDP, MSDP_VAR, apVariable, MSDP_VAL);
            char* const value_end = msdp_write_sanitized(value, MSDPBuffer + prefix_length);
            const std::size_t room_left
                = sizeof(MSDPBuffer) - static_cast<std::size_t>(value_end - MSDPBuffer);
            snprintf(value_end, room_left, "%c%c", IAC, SE);
        } else if (pProtocol->bATCP) {
            const int prefix_length = snprintf(
                MSDPBuffer, sizeof(MSDPBuffer), "%c%c%cMSDP.%s ", IAC, SB, TELOPT_ATCP, apVariable);
            char* const value_end = msdp_write_sanitized(value, MSDPBuffer + prefix_length);
            const std::size_t room_left
                = sizeof(MSDPBuffer) - static_cast<std::size_t>(value_end - MSDPBuffer);
            snprintf(value_end, room_left, "%c%c", IAC, SE);
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (MSDPBuffer[0] != '\0') {
            Write(apDescriptor, MSDPBuffer);
        }
    }
}

void MSDPSendList(const descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    char MSDPBuffer[MAX_VARIABLE_LENGTH + 1] = { '\0' };

    if (apVariable != nullptr && apValue != nullptr) {
        protocol_t* pProtocol = protocol_of(apDescriptor);
        if (pProtocol == nullptr) {
            return;
        }

        const std::string_view value(apValue);
        const std::size_t value_length = msdp_sanitized_length(value);

        /* Should really be replaced with a dynamic buffer */
        // Kept as std::size_t so no value is long enough to wrap around the limit check.
        constexpr std::size_t buffer_limit = MAX_VARIABLE_LENGTH;
        const std::size_t name_length = strlen(apVariable);
        const std::size_t required_length = name_length + value_length + MSDP_FRAMING_LENGTH;

        if (required_length >= buffer_limit) {
            if (required_length - value_length < buffer_limit) {
                snprintf(MSDPBuffer, sizeof(MSDPBuffer),
                    "MSDPSendList: %s %zu bytes (exceeds MAX_VARIABLE_LENGTH of %d).\n", apVariable,
                    required_length, MAX_VARIABLE_LENGTH);
            } else {
                /* The variable name itself is too long */
                snprintf(MSDPBuffer, sizeof(MSDPBuffer),
                    "MSDPSendList: Variable name has a length of %zu bytes (exceeds "
                    "MAX_VARIABLE_LENGTH of %d).\n",
                    required_length, MAX_VARIABLE_LENGTH);
            }

            ReportBug(MSDPBuffer);
            MSDPBuffer[0] = '\0';
        } else if (pProtocol->bMSDP) {
            int i; /* Loop counter */
            const int prefix_length = snprintf(MSDPBuffer, sizeof(MSDPBuffer), "%c%c%c%c%s%c%c%c",
                IAC, SB, TELOPT_MSDP, MSDP_VAR, apVariable, MSDP_VAL, MSDP_ARRAY_OPEN, MSDP_VAL);
            char* const value_end = msdp_write_sanitized(value, MSDPBuffer + prefix_length);
            const std::size_t room_left
                = sizeof(MSDPBuffer) - static_cast<std::size_t>(value_end - MSDPBuffer);
            snprintf(value_end, room_left, "%c%c%c", MSDP_ARRAY_CLOSE, IAC, SE);

            /* Convert the spaces to MSDP_VAL */
            for (i = 0; MSDPBuffer[i] != '\0'; ++i) {
                if (MSDPBuffer[i] == ' ') {
                    MSDPBuffer[i] = MSDP_VAL;
                }
            }
        } else if (pProtocol->bATCP) {
            const int prefix_length = snprintf(
                MSDPBuffer, sizeof(MSDPBuffer), "%c%c%cMSDP.%s ", IAC, SB, TELOPT_ATCP, apVariable);
            char* const value_end = msdp_write_sanitized(value, MSDPBuffer + prefix_length);
            const std::size_t room_left
                = sizeof(MSDPBuffer) - static_cast<std::size_t>(value_end - MSDPBuffer);
            snprintf(value_end, room_left, "%c%c", IAC, SE);
        }

        /* Just in case someone calls this function without checking MSDP/ATCP */
        if (MSDPBuffer[0] != '\0') {
            Write(apDescriptor, MSDPBuffer);
        }
    }
}

void MSDPSetNumber(const descriptor_t* apDescriptor, variable_t aMSDP, int aValue)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool can_set = pProtocol != nullptr && MSDPIsValidVariable(aMSDP);
    if (can_set) {
        if (!VariableNameTable[aMSDP].bString) {
            if (pProtocol->pVariables[aMSDP]->ValueInt != aValue) {
                pProtocol->pVariables[aMSDP]->ValueInt = aValue;
                pProtocol->pVariables[aMSDP]->bDirty = true;
            }
        }
    }
}

bool MSDPIsValidVariable(variable_t aMSDP)
{
    return aMSDP > eMSDP_NONE && aMSDP < eMSDP_MAX;
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
        if (strncmp(stored, escape, escape_length) != 0) {
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
    if (strncmp(stored + 1, payload.data(), payload.size()) != 0) {
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
        ReportBug("msdp_set_wrapped: out of memory; the MSDP value is unchanged.\n");
        return false;
    }
    wrapped[0] = open_marker;
    memcpy(wrapped + 1, payload.data(), payload.size());
    wrapped[payload.size() + 1] = close_marker;
    wrapped[payload.size() + 2] = '\0';

    free(variable.pValueString);
    variable.pValueString = wrapped;
    variable.bDirty = true;
    return true;
}

void MSDPAppendSanitized(std::string& out_text, const char* apValue)
{
    if (apValue == nullptr) {
        return;
    }
    const std::string_view value(apValue);
    const std::size_t old_size = out_text.size();
    const std::size_t length = msdp_sanitized_length(value);
    out_text.resize(old_size + length);
    msdp_write_sanitized(value, out_text.data() + old_size);
}

std::string MSDPSanitizeValue(const char* apValue)
{
    std::string sanitized;
    MSDPAppendSanitized(sanitized, apValue);
    return sanitized;
}

void MSDPSetString(const descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue)
{
    if (apValue != nullptr) {
        MSDPSetString(apDescriptor, aMSDP, std::string_view(apValue));
    }
}

void MSDPSetString(const descriptor_t* apDescriptor, variable_t aMSDP, std::string_view aValue)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool can_set = pProtocol != nullptr && MSDPIsValidVariable(aMSDP);
    if (can_set && VariableNameTable[aMSDP].bString) {
        MSDP_t* variable = pProtocol->pVariables[aMSDP];
        if (!equals_sanitized(variable->pValueString, aValue)) {
            char* const sanitized = msdp_copy_sanitized(aValue);
            if (sanitized == nullptr) {
                ReportBug("MSDPSetString: out of memory; the MSDP value is unchanged.\n");
                return;
            }
            free(variable->pValueString);
            variable->pValueString = sanitized;
            variable->bDirty = true;
        }
    }
}

void MSDPSetTable(const descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool can_set = pProtocol != nullptr && apValue != nullptr && MSDPIsValidVariable(aMSDP);
    if (can_set) {
        if (*apValue == '\0') {
            /* It's easier to call MSDPSetString if the value is empty */
            MSDPSetString(apDescriptor, aMSDP, apValue);
        } else if (VariableNameTable[aMSDP].bString) {
            msdp_set_wrapped(*pProtocol->pVariables[aMSDP], static_cast<char>(MSDP_TABLE_OPEN),
                apValue, static_cast<char>(MSDP_TABLE_CLOSE));
        }
    }
}

void MSDPSetArray(const descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool can_set = pProtocol != nullptr && apValue != nullptr && MSDPIsValidVariable(aMSDP);
    if (can_set) {
        if (*apValue == '\0') {
            /* It's easier to call MSDPSetString if the value is empty */
            MSDPSetString(apDescriptor, aMSDP, apValue);
        } else if (VariableNameTable[aMSDP].bString) {
            msdp_set_wrapped(*pProtocol->pVariables[aMSDP], static_cast<char>(MSDP_ARRAY_OPEN),
                apValue, static_cast<char>(MSDP_ARRAY_CLOSE));
        }
    }
}

/******************************************************************************
 MXP global functions.
 ******************************************************************************/

void MXPSendTag(const descriptor_t* apDescriptor, const char* apTag)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool can_send = pProtocol != nullptr && apTag != nullptr
        && strlen(apTag) < static_cast<std::size_t>(MAX_MXP_TAG_LENGTH);
    if (can_send) {
        if (pProtocol->pVariables[eMSDP_MXP]->ValueInt != 0) {
            char MXPBuffer[MXP_BUFFER_SIZE];
            snprintf(MXPBuffer, sizeof(MXPBuffer), "\033[1z%s\033[7z\r\n", apTag);
            Write(apDescriptor, MXPBuffer);
        } else if (pProtocol->bRenegotiate) {
            /* Tijer pointed out that when MUSHclient autoconnects, it fails
             * to complete the negotiation.  This workaround will attempt to
             * renegotiate after the character has connected.
             */

            int i; /* Renegotiate everything except TTYPE */
            for (i = eNEGOTIATED_TTYPE + 1; i < eNEGOTIATED_MAX; ++i) {
                pProtocol->Negotiated[i] = false;
                ConfirmNegotiation(apDescriptor, static_cast<negotiated_t>(i), true, true);
            }

            pProtocol->bRenegotiate = false;
            pProtocol->bNeedMXPVersion = true;
            Negotiate(apDescriptor);
        }
    }
}

/******************************************************************************
 Colour global functions.
 ******************************************************************************/

const char* ColourRGB(const descriptor_t* apDescriptor, const char* apRGB)
{
    protocol_t* pProtocol = protocol_of(apDescriptor);

    const bool uses_ansi_colour
        = pProtocol != nullptr && pProtocol->pVariables[eMSDP_ANSI_COLORS]->ValueInt != 0;
    if (uses_ansi_colour) {
        if (IsValidColour(apRGB)) {
            bool bBackground = (tolower(apRGB[0]) == 'b');
            int Red = apRGB[1] - '0';
            int Green = apRGB[2] - '0';
            int Blue = apRGB[3] - '0';

            if (pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt != 0) {
                return GetRGBColour(bBackground, Red, Green, Blue);
            } else {
                /* Use regular ANSI colour */
                return GetAnsiColour(bBackground, Red, Green, Blue);
            }
        } else {
            /* Invalid colour - use this to clear any existing colour. */
            return s_Clean;
        }
    } else {
        /* Don't send any colour, not even clear */
        return "";
    }
}

/******************************************************************************
 UTF-8 global functions.
 ******************************************************************************/

char* UnicodeGet(int aValue)
{
    static char Buffer[8];
    char* pString = Buffer;

    UnicodeAdd(&pString, aValue);
    *pString = '\0';

    return Buffer;
}

void UnicodeAdd(char** apString, int aValue)
{
    if (aValue < 0x80) {
        *(*apString)++ = static_cast<char>(aValue);
    } else if (aValue < 0x800) {
        *(*apString)++ = static_cast<char>(0xC0 | (aValue >> 6));
        *(*apString)++ = static_cast<char>(0x80 | (aValue & 0x3F));
    } else if (aValue < 0x10000) {
        *(*apString)++ = static_cast<char>(0xE0 | (aValue >> 12));
        *(*apString)++ = static_cast<char>(0x80 | (aValue >> 6 & 0x3F));
        *(*apString)++ = static_cast<char>(0x80 | (aValue & 0x3F));
    } else if (aValue < 0x200000) {
        *(*apString)++ = static_cast<char>(0xF0 | (aValue >> 18));
        *(*apString)++ = static_cast<char>(0x80 | (aValue >> 12 & 0x3F));
        *(*apString)++ = static_cast<char>(0x80 | (aValue >> 6 & 0x3F));
        *(*apString)++ = static_cast<char>(0x80 | (aValue & 0x3F));
    }
}

/******************************************************************************
 Local negotiation functions.
 ******************************************************************************/

static void Negotiate(const descriptor_t* apDescriptor)
{
    protocol_t* pProtocol = apDescriptor->pProtocol;

    if (pProtocol->bNegotiated) {
        static constexpr char RequestTTYPE[]
            = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND, TELNET_IAC, TELNET_SE, '\0' };

        /* Request the client type if TTYPE is supported. */
        if (pProtocol->bTTYPE) {
            Write(apDescriptor, RequestTTYPE);
        }

        /* Check for other protocols. */
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_NAWS, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_CHARSET, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSDP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSSP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_ATCP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_MCCP, true, true);
        ConfirmNegotiation(apDescriptor, eNEGOTIATED_EOR, true, true);
    }
}

static void PerformHandshake(const descriptor_t* apDescriptor, char aCmd, char aProtocol)
{
    protocol_t* pProtocol = apDescriptor->pProtocol;

    switch (aProtocol) {
    case static_cast<char>(TELOPT_TTYPE):
        if (aCmd == TELNET_WILL) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_TTYPE, true, true);
            pProtocol->bTTYPE = true;

            if (!pProtocol->bNegotiated) {
                /* Negotiate for the remaining protocols. */
                pProtocol->bNegotiated = true;
                Negotiate(apDescriptor);

                /* We may need to renegotiate if they don't reply */
                pProtocol->bRenegotiate = true;
            }
        } else if (aCmd == TELNET_WONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_TTYPE, false, pProtocol->bTTYPE);
            pProtocol->bTTYPE = false;

            if (!pProtocol->bNegotiated) {
                /* Still negotiate, as this client obviously knows how to
                 * correctly respond to negotiation attempts - but we don't
                 * ask for TTYPE, as it doesn't support it.
                 */
                pProtocol->bNegotiated = true;
                Negotiate(apDescriptor);

                /* We may need to renegotiate if they don't reply */
                pProtocol->bRenegotiate = true;
            }
        } else if (aCmd == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_WONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_ECHO):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_ECHO, true, true);
            pProtocol->bECHO = true;
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_ECHO, false, pProtocol->bECHO);
            pProtocol->bECHO = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_NAWS):
        if (aCmd == TELNET_WILL) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_NAWS, true, true);
            pProtocol->bNAWS = true;

            /* Renegotiation workaround won't be necessary. */
            pProtocol->bRenegotiate = false;
        } else if (aCmd == TELNET_WONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_NAWS, false, pProtocol->bNAWS);
            pProtocol->bNAWS = false;

            /* Renegotiation workaround won't be necessary. */
            pProtocol->bRenegotiate = false;
        } else if (aCmd == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_WONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_CHARSET):
        if (aCmd == TELNET_WILL) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_CHARSET, true, true);
            if (!pProtocol->bCHARSET) {
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
                Write(apDescriptor, charset_latin1);
                pProtocol->bCHARSET = true;
            }
        } else if (aCmd == TELNET_WONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_CHARSET, false, pProtocol->bCHARSET);
            pProtocol->bCHARSET = false;
        } else if (aCmd == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_WONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_MSDP):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSDP, true, true);

            /* Unlike its sibling flags (bMSSP/bATCP/bMSP/bMXP/bMCCP, all false by
               default), bMSDP defaults to true at connection creation -- so the
               "!pProtocol->bMSDP" check below can essentially never be true on a
               normal DO/WILL exchange, making the block it guards dead in practice.
               MSDPSetString() only populates SERVER_ID's own backing storage (no
               wire traffic, no login/preference guard, safe to call on every DO),
               so do that unconditionally here -- otherwise a client-issued
               "SEND SERVER_ID" later (read via MSDPSend(), which reads that
               storage) returns an empty string instead of the mud's name. */
            MSDPSetString(apDescriptor, eMSDP_SERVER_ID, MUD_NAME);

            if (!pProtocol->bMSDP) {
                pProtocol->bMSDP = true;

                /* Identify the mud to the client immediately. This fires during
                   telnet negotiation, before login, so it can't go through
                   MSDPSend() (which requires a logged-in character with PRF_MSDP
                   set) -- MSDPSendPair() sends it immediately with no such guard. */
                MSDPSendPair(apDescriptor, "SERVER_ID", MUD_NAME);
            }
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSDP, false, pProtocol->bMSDP);
            pProtocol->bMSDP = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_MSSP):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSSP, true, true);

            if (!pProtocol->bMSSP) {
                SendMSSP(apDescriptor);
                pProtocol->bMSSP = true;
            }
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSSP, false, pProtocol->bMSSP);
            pProtocol->bMSSP = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_MCCP):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MCCP, true, true);

            if (!pProtocol->bMCCP) {
                pProtocol->bMCCP = true;
            }
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MCCP, false, pProtocol->bMCCP);

            if (pProtocol->bMCCP) {
                pProtocol->bMCCP = false;
            }
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_MSP):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSP, true, true);
            pProtocol->bMSP = true;
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MSP, false, pProtocol->bMSP);
            pProtocol->bMSP = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_MXP):
        if (aCmd == TELNET_WILL || aCmd == TELNET_DO) {
            if (aCmd == TELNET_WILL) {
                ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP, true, true);
            } else {
                /* aCmd == TELNET_DO */
                ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP2, true, true);
            }

            if (!pProtocol->bMXP) {
                /* Enable MXP. */
                static constexpr char EnableMXP[]
                    = { TELNET_IAC, TELNET_SB, TELOPT_MXP, TELNET_IAC, TELNET_SE, '\0' };
                Write(apDescriptor, EnableMXP);

                /* Create a secure channel, and note that MXP is active. */
                Write(apDescriptor, "\033[7z");
                pProtocol->bMXP = true;
                pProtocol->pVariables[eMSDP_MXP]->ValueInt = 1;

                if (pProtocol->bNeedMXPVersion) {
                    MXPSendTag(apDescriptor, "<VERSION>");
                }
            }
        } else if (aCmd == TELNET_WONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP, false, pProtocol->bMXP);

            if (!pProtocol->bMXP) {
                /* The MXP standard doesn't actually specify whether you should
                 * negotiate with IAC DO MXP or IAC WILL MXP.  As a result, some
                 * clients support one, some the other, and some support both.
                 *
                 * Therefore we first try IAC DO MXP, and if the client replies
                 * with WONT, we try again (here) with IAC WILL MXP.
                 */
                ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP2, true, true);
            } else {
                /* The client is actually asking us to switch MXP off. */
                pProtocol->bMXP = false;
            }
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_MXP2, false, pProtocol->bMXP);
            pProtocol->bMXP = false;
        }
        break;

    case static_cast<char>(TELOPT_ATCP):
        if (aCmd == TELNET_WILL) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_ATCP, true, true);

            /* If we don't support MSDP, fake it with ATCP */
            if (!pProtocol->bMSDP && !pProtocol->bATCP) {
                pProtocol->bATCP = true;

                /* Identify the mud to the client. This fires during telnet negotiation,
                   before login, so it can't go through MSDPSend() (which requires a
                   logged-in character with PRF_MSDP set) -- MSDPSendPair() sends it
                   immediately with no such guard, exactly as before. But MSDPSendPair()
                   never touches the variable's own backing storage, so without this,
                   a later client-issued "SEND SERVER_ID" (which does go through
                   MSDPSend(), reading that storage) would return an empty string.
                   MSDPSetString() has no login/preference guard either, so store the
                   value too. */
                MSDPSendPair(apDescriptor, "SERVER_ID", MUD_NAME);
                MSDPSetString(apDescriptor, eMSDP_SERVER_ID, MUD_NAME);
            }
        } else if (aCmd == TELNET_WONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_ATCP, false, pProtocol->bATCP);
            pProtocol->bATCP = false;
        } else if (aCmd == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_WONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_EOR):
        if (aCmd == TELNET_DO) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_EOR, true, true);
            pProtocol->bEOR = true;
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_EOR, false, pProtocol->bEOR);
            pProtocol->bEOR = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    case static_cast<char>(TELOPT_SGA):
        if (aCmd == TELNET_DO) {
            /* The client wants no IAC GA after prompts. */
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_SGA, true, true);
            pProtocol->bSGA = true;
        } else if (aCmd == TELNET_DONT) {
            ConfirmNegotiation(apDescriptor, eNEGOTIATED_SGA, false, pProtocol->bSGA);
            pProtocol->bSGA = false;
        } else if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        }
        break;

    default:
        if (aCmd == TELNET_WILL) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_DONT, aProtocol);
        } else if (aCmd == TELNET_DO) {
            /* Invalid negotiation, send a rejection */
            SendNegotiationSequence(apDescriptor, TELNET_WONT, aProtocol);
        }
        break;
    }
}

// Whether client_name is a client some of whose versions support XTerm 256 colours.
static bool sometimes_supports_xterm_256(const char* client_name)
{
    static constexpr const char* client_names[] = { "CMUD", "ATLANTIS", "KILDCLIENT", "TINYFUGUE" };
    for (const char* candidate : client_names) {
        if (MatchString(client_name, candidate)) {
            return true;
        }
    }
    return false;
}

static void PerformSubnegotiation(
    const descriptor_t* apDescriptor, char aCmd, const char* apData, int aSize)
{
    protocol_t* pProtocol = apDescriptor->pProtocol;

    switch (aCmd) {
    case static_cast<char>(TELOPT_TTYPE):
        if (pProtocol->bTTYPE) {
            /* Store the client name. */
            constexpr int MaxClientLength = 64;
            char pClientName[MaxClientLength + 1];
            int i = 0;
            int j = 1;
            bool bStopCyclicTTYPE = false;

            for (; apData[j] != '\0' && i < MaxClientLength; ++j) {
                const unsigned char client_byte = static_cast<unsigned char>(apData[j]);
                if (client_byte >= FIRST_PRINTABLE_ASCII && client_byte <= LAST_PRINTABLE_ASCII) {
                    pClientName[i++] = apData[j];
                }
            }
            pClientName[i] = '\0';

            /* Store the first TTYPE as the client name */
            if (strcmp(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString, "Unknown") == 0) {
                free(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString = AllocString(pClientName);

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
                if (strcmp(pClientName, "ANSI") == 0) {
                    bStopCyclicTTYPE = true;
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
            const bool has_last_ttype = pProtocol->pLastTTYPE != nullptr;
            const bool repeats_last_ttype
                = has_last_ttype && strcmp(pProtocol->pLastTTYPE, pClientName) == 0;
            const bool repeats_client_name = has_last_ttype && !repeats_last_ttype
                && strcmp(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString, pClientName) == 0;
            if (!repeats_last_ttype && !repeats_client_name) {
                static constexpr char RequestTTYPE[] = { TELNET_IAC, TELNET_SB, TELOPT_TTYPE, SEND,
                    TELNET_IAC, TELNET_SE, '\0' };
                const char* pStartPos = strstr(pClientName, "-");

                /* Store the TTYPE */
                free(pProtocol->pLastTTYPE);
                pProtocol->pLastTTYPE = AllocString(pClientName);

                /* Look for 256 colour support */
                const bool names_xterm_256
                    = pStartPos != nullptr && MatchString(pStartPos, "-256color");
                if (names_xterm_256) {
                    /* This is currently the only way to detect support for 256
                     * colours in TinTin++, WinTin++ and BlowTorch.
                     */
                    pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    pProtocol->b256Support = eYES;
                }

                /* Request another TTYPE */
                if (!bStopCyclicTTYPE) {
                    Write(apDescriptor, RequestTTYPE);
                }
            }

            // A client's name and the separator before the version it may append.
            static constexpr char mtts_prefix[] = "MTTS ";
            static constexpr char mudlet_name[] = "Mudlet";
            static constexpr char decafmud_name[] = "DecafMUD";
            constexpr std::size_t separator_length = 1;

            if (PrefixString(mtts_prefix, pClientName)) {
                // The MTTS capability bits the server reads.
                constexpr int mtts_ansi = 1;
                constexpr int mtts_utf_8 = 4;
                constexpr int mtts_xterm_256 = 8;
                constexpr std::size_t mtts_prefix_length
                    = std::char_traits<char>::length(mtts_prefix);
                pProtocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt
                    = atoi(pClientName + mtts_prefix_length);

                if ((pProtocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_ansi) != 0) {
                    pProtocol->pVariables[eMSDP_ANSI_COLORS]->ValueInt = 1;
                }
                if ((pProtocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_utf_8) != 0) {
                    pProtocol->pVariables[eMSDP_UTF_8]->ValueInt = 1;
                }
                if ((pProtocol->pVariables[eMSDP_CLIENT_VERSION]->ValueInt & mtts_xterm_256) != 0) {
                    pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                    pProtocol->b256Support = eYES;
                }

            } else if (PrefixString(mudlet_name, pClientName)) {
                /* Mudlet beta 15 and later supports 256 colours, but we can't
                 * identify it from the mud - everything prior to 1.1 claims
                 * to be version 1.0, so we just don't know.
                 */
                pProtocol->b256Support = eSOMETIMES;

                constexpr std::size_t mudlet_name_length
                    = std::char_traits<char>::length(mudlet_name);
                constexpr std::size_t version_start = mudlet_name_length + separator_length;
                const std::size_t client_length = strlen(pClientName);
                if (client_length > version_start) {
                    pClientName[mudlet_name_length] = '\0';
                    free(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                    pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString = AllocString(pClientName);
                    free(pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                    pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString
                        = AllocString(pClientName + version_start);

                    /* Mudlet 1.1 and later supports 256 colours. */
                    const char* client_version
                        = pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString;
                    const bool supports_xterm_256 = strcmp(client_version, "1.1") >= 0;
                    if (supports_xterm_256) {
                        pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                        pProtocol->b256Support = eYES;
                    }
                }
            } else if (MatchString(pClientName, "EMACS-RINZAI")) {
                /* We know for certain that this client has support */
                pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                pProtocol->b256Support = eYES;
            } else if (PrefixString(decafmud_name, pClientName)) {
                /* We know for certain that this client has support */
                pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt = 1;
                pProtocol->b256Support = eYES;

                constexpr std::size_t decafmud_name_length
                    = std::char_traits<char>::length(decafmud_name);
                constexpr std::size_t version_start = decafmud_name_length + separator_length;
                const std::size_t client_length = strlen(pClientName);
                if (client_length > version_start) {
                    pClientName[decafmud_name_length] = '\0';
                    free(pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString);
                    pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString = AllocString(pClientName);
                    free(pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString);
                    pProtocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString
                        = AllocString(pClientName + version_start);
                }
            } else if (sometimes_supports_xterm_256(pClientName)) {
                /* We know that some versions of this client have support */
                pProtocol->b256Support = eSOMETIMES;
            } else if (MatchString(pClientName, "ZMUD")) {
                /* We know for certain that this client does not have support */
                pProtocol->b256Support = eNO;
            }
        }
        break;

    case static_cast<char>(TELOPT_NAWS):
        if (pProtocol->bNAWS) {
            /* Store the new width. */
            pProtocol->ScreenWidth = static_cast<unsigned char>(apData[0]);
            pProtocol->ScreenWidth <<= 8;
            pProtocol->ScreenWidth += static_cast<unsigned char>(apData[1]);

            /* Store the new height. */
            pProtocol->ScreenHeight = static_cast<unsigned char>(apData[2]);
            pProtocol->ScreenHeight <<= 8;
            pProtocol->ScreenHeight += static_cast<unsigned char>(apData[3]);
        }
        break;

    case static_cast<char>(TELOPT_CHARSET):
        /* Nothing to record. We only ever offer one charset, ISO-8859-1 (see the
         * REQUEST we send in PerformHandshake), and we emit latin-1 either way --
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
        if (pProtocol->bMSDP) {
            ParseMSDP(apDescriptor, apData);
        }
        break;

    case static_cast<char>(TELOPT_ATCP):
        if (pProtocol->bATCP) {
            ParseATCP(apDescriptor, apData);
        }
        break;

    default: /* Unknown subnegotiation, so we simply ignore it. */
        break;
    }
}

static void SendNegotiationSequence(const descriptor_t* apDescriptor, char aCmd, char aProtocol)
{
    char NegotiateSequence[4];

    NegotiateSequence[0] = TELNET_IAC;
    NegotiateSequence[1] = aCmd;
    NegotiateSequence[2] = aProtocol;
    NegotiateSequence[3] = '\0';

    Write(apDescriptor, NegotiateSequence);
}

static bool ConfirmNegotiation(
    const descriptor_t* apDescriptor, negotiated_t aProtocol, bool abWillDo, bool abSendReply)
{
    bool bResult = false;

    if (aProtocol >= eNEGOTIATED_TTYPE && aProtocol < eNEGOTIATED_MAX) {
        /* Only negotiate if the state has changed. */
        if (apDescriptor->pProtocol->Negotiated[aProtocol] != abWillDo) {
            /* Store the new state. */
            apDescriptor->pProtocol->Negotiated[aProtocol] = abWillDo;

            bResult = true;

            if (abSendReply) {
                // DO and DONT answer for an option the client performs; WILL and WONT for one
                // the server performs.
                char client_option_reply = TELNET_DONT;
                char server_option_reply = TELNET_WONT;
                if (abWillDo) {
                    client_option_reply = TELNET_DO;
                    server_option_reply = TELNET_WILL;
                }

                switch (aProtocol) {
                case eNEGOTIATED_TTYPE:
                    SendNegotiationSequence(apDescriptor, client_option_reply, TELOPT_TTYPE);
                    break;
                case eNEGOTIATED_ECHO:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_ECHO);
                    break;
                case eNEGOTIATED_NAWS:
                    SendNegotiationSequence(apDescriptor, client_option_reply, TELOPT_NAWS);
                    break;
                case eNEGOTIATED_CHARSET:
                    SendNegotiationSequence(apDescriptor, client_option_reply, TELOPT_CHARSET);
                    break;
                case eNEGOTIATED_MSDP:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_MSDP);
                    break;
                case eNEGOTIATED_MSSP:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_MSSP);
                    break;
                case eNEGOTIATED_ATCP:
                    SendNegotiationSequence(
                        apDescriptor, client_option_reply, static_cast<char>(TELOPT_ATCP));
                    break;
                case eNEGOTIATED_MSP:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_MSP);
                    break;
                case eNEGOTIATED_MXP:
                    SendNegotiationSequence(apDescriptor, client_option_reply, TELOPT_MXP);
                    break;
                case eNEGOTIATED_MXP2:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_MXP);
                    break;
                case eNEGOTIATED_MCCP:
                    break;
                case eNEGOTIATED_EOR:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_EOR);
                    break;
                case eNEGOTIATED_SGA:
                    SendNegotiationSequence(apDescriptor, server_option_reply, TELOPT_SGA);
                    break;
                default:
                    bResult = false;
                    break;
                }
            }
        }
    }

    return bResult;
}

/******************************************************************************
 Local MSDP functions.
 ******************************************************************************/

static void ParseMSDP(const descriptor_t* apDescriptor, const char* apData)
{
    char Variable[MSDP_PAIR_PARTS][MAX_MSDP_SIZE + 1] = { { '\0' }, { '\0' } };
    char* pPos = nullptr;
    char* pStart = nullptr;

    while (*apData != '\0') {
        switch (*apData) {
        case MSDP_VAR:
        case MSDP_VAL:
            pStart = Variable[*apData - 1];
            pPos = pStart;
            ++apData;
            break;
        default: /* Anything else */
            if (pPos != nullptr && pPos - pStart < MAX_MSDP_SIZE) {
                *pPos++ = *apData;
                *pPos = '\0';
            }

            ++apData;
            if (*apData != '\0') {
                continue;
            }
        }

        ExecuteMSDPPair(apDescriptor, Variable[MSDP_VAR - 1], Variable[MSDP_VAL - 1]);
        Variable[MSDP_VAL - 1][0] = '\0';
    }
}

// Whether list_name asks for the list of variables a client can SEND or REPORT.
static bool names_sendable_variables(const char* list_name)
{
    if (MatchString(list_name, "SENDABLE_VARIABLES")) {
        return true;
    }
    return MatchString(list_name, "REPORTABLE_VARIABLES");
}

static void ExecuteMSDPPair(
    const descriptor_t* apDescriptor, const char* apVariable, const char* apValue)
{
    if (apVariable[0] != '\0' && apValue[0] != '\0') {
        if (MatchString(apVariable, "SEND")) {
            bool bDone = false;
            int i; /* Loop counter */
            for (i = eMSDP_NONE + 1; i < eMSDP_MAX && !bDone; ++i) {
                if (MatchString(apValue, VariableNameTable[i].pName)) {
                    MSDPSend(apDescriptor, static_cast<variable_t>(i));
                    bDone = true;
                }
            }
        } else if (MatchString(apVariable, "REPORT")) {
            bool bDone = false;
            int i; /* Loop counter */
            for (i = eMSDP_NONE + 1; i < eMSDP_MAX && !bDone; ++i) {
                if (MatchString(apValue, VariableNameTable[i].pName)) {
                    apDescriptor->pProtocol->pVariables[i]->bReport = true;
                    apDescriptor->pProtocol->pVariables[i]->bDirty = true;
                    bDone = true;
                }
            }
        } else if (MatchString(apVariable, "RESET")) {
            const bool resets_variables = MatchString(apValue, "REPORTABLE_VARIABLES")
                || MatchString(apValue, "REPORTED_VARIABLES")
                || MatchString(apValue, "VARIABLES");
            if (resets_variables) {
                int i; /* Loop counter */
                for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                    if (apDescriptor->pProtocol->pVariables[i]->bReport) {
                        apDescriptor->pProtocol->pVariables[i]->bReport = false;
                        apDescriptor->pProtocol->pVariables[i]->bDirty = false;
                    }
                }
            }
        } else if (MatchString(apVariable, "UNREPORT")) {
            bool bDone = false;
            int i; /* Loop counter */
            for (i = eMSDP_NONE + 1; i < eMSDP_MAX && !bDone; ++i) {
                if (MatchString(apValue, VariableNameTable[i].pName)) {
                    apDescriptor->pProtocol->pVariables[i]->bReport = false;
                    apDescriptor->pProtocol->pVariables[i]->bDirty = false;
                    bDone = true;
                }
            }
        } else if (MatchString(apVariable, "LIST")) {
            if (MatchString(apValue, "COMMANDS")) {
                static constexpr char MSDPCommands[] = "LIST REPORT RESET SEND UNREPORT";
                MSDPSendList(apDescriptor, "COMMANDS", MSDPCommands);
            } else if (MatchString(apValue, "LISTS")) {
                static constexpr char MSDPCommands[]
                    = "COMMANDS LISTS CONFIGURABLE_VARIABLES REPORTABLE_VARIABLES "
                      "REPORTED_VARIABLES SENDABLE_VARIABLES GUI_VARIABLES";
                MSDPSendList(apDescriptor, "LISTS", MSDPCommands);
            }
            /* Split this into two if some variables aren't REPORTABLE */
            else if (names_sendable_variables(apValue)) {
                char MSDPCommands[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;
                int i; /* Loop counter */

                for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                    if (!VariableNameTable[i].bGUI) {
                        commands_length = append_list_name(
                            MSDPCommands, commands_length, VariableNameTable[i].pName);
                    }
                }
                MSDPCommands[commands_length] = '\0';

                MSDPSendList(apDescriptor, apValue, MSDPCommands);
            } else if (MatchString(apValue, "REPORTED_VARIABLES")) {
                char MSDPCommands[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;
                int i; /* Loop counter */

                for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                    if (apDescriptor->pProtocol->pVariables[i]->bReport) {
                        commands_length = append_list_name(
                            MSDPCommands, commands_length, VariableNameTable[i].pName);
                    }
                }
                MSDPCommands[commands_length] = '\0';

                MSDPSendList(apDescriptor, apValue, MSDPCommands);
            } else if (MatchString(apValue, "CONFIGURABLE_VARIABLES")) {
                char MSDPCommands[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;
                int i; /* Loop counter */

                for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                    if (VariableNameTable[i].bConfigurable) {
                        commands_length = append_list_name(
                            MSDPCommands, commands_length, VariableNameTable[i].pName);
                    }
                }
                MSDPCommands[commands_length] = '\0';

                MSDPSendList(apDescriptor, "CONFIGURABLE_VARIABLES", MSDPCommands);
            } else if (MatchString(apValue, "GUI_VARIABLES")) {
                char MSDPCommands[MAX_OUTPUT_BUFFER];
                std::size_t commands_length = 0;
                int i; /* Loop counter */

                for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                    if (VariableNameTable[i].bGUI) {
                        commands_length = append_list_name(
                            MSDPCommands, commands_length, VariableNameTable[i].pName);
                    }
                }
                MSDPCommands[commands_length] = '\0';

                MSDPSendList(apDescriptor, apValue, MSDPCommands);
            }
        } else {
            /* Set any configurable variables */
            int i; /* Loop counter */

            for (i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
                if (!VariableNameTable[i].bConfigurable) {
                    continue;
                }
                if (!MatchString(apVariable, VariableNameTable[i].pName)) {
                    continue;
                }

                MSDP_t* const variable = apDescriptor->pProtocol->pVariables[i];
                if (VariableNameTable[i].bString) {
                    /* A write-once variable can only be set if the value
                     * is "Unknown".  This is for things like client name,
                     * where we don't really want the player overwriting a
                     * proper client name with junk - but on the other hand,
                     * its possible a client may choose to use MSDP to
                     * identify itself.
                     */
                    const bool can_write = !VariableNameTable[i].bWriteOnce
                        || strcmp(variable->pValueString, "Unknown") == 0;
                    if (can_write) {
                        /* Store the new value if it's valid */
                        char pBuffer[MAX_CONFIGURABLE_STRING_LENGTH + 1];
                        int j; /* Loop counter */

                        for (j = 0; j < VariableNameTable[i].Max && *apValue != '\0'; ++apValue) {
                            const unsigned char value_byte = static_cast<unsigned char>(*apValue);
                            if (value_byte >= FIRST_PRINTABLE_ASCII
                                && value_byte <= LAST_PRINTABLE_ASCII) {
                                pBuffer[j++] = *apValue;
                            }
                        }
                        pBuffer[j++] = '\0';

                        if (j > VariableNameTable[i].Min) {
                            free(variable->pValueString);
                            variable->pValueString = AllocString(pBuffer);
                        }
                    }
                } else {
                    /* This variable only accepts numeric values */
                    /* Strip any leading spaces */
                    while (*apValue == ' ') {
                        ++apValue;
                    }

                    const bool is_number = *apValue != '\0' && IsNumber(apValue);
                    if (is_number) {
                        int Value = atoi(apValue);
                        const bool in_range = Value >= VariableNameTable[i].Min
                            && Value <= VariableNameTable[i].Max;
                        if (in_range) {
                            variable->ValueInt = Value;
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

static void ParseATCP(const descriptor_t* apDescriptor, const char* apData)
{
    char Variable[MSDP_PAIR_PARTS][MAX_MSDP_SIZE + 1] = { { '\0' }, { '\0' } };
    char* pPos = nullptr;
    char* pStart = nullptr;

    while (*apData != '\0') {
        switch (*apData) {
        case '@':
            pStart = Variable[0];
            pPos = pStart;
            apData++;
            break;
        case ' ':
            pStart = Variable[1];
            pPos = pStart;
            apData++;
            break;
        default: /* Anything else */
            if (pPos != nullptr && pPos - pStart < MAX_MSDP_SIZE) {
                *pPos++ = *apData;
                *pPos = '\0';
            }

            ++apData;
            if (*apData != '\0') {
                continue;
            }
        }

        ExecuteMSDPPair(apDescriptor, Variable[MSDP_VAR - 1], Variable[MSDP_VAL - 1]);
        Variable[MSDP_VAL - 1][0] = '\0';
    }
}

/******************************************************************************
 Local MSSP functions.
 ******************************************************************************/

static void SendMSSP(const descriptor_t* apDescriptor)
{
    // IAC SB MSSP, and the room kept for IAC SE and the NUL.
    constexpr int mssp_header_length = 3;
    constexpr int mssp_trailer_room = 4;
    constexpr int pair_buffer_size = 128;
    char MSSPBuffer[MAX_MSSP_BUFFER];
    char MSSPPair[pair_buffer_size];
    int SizeBuffer = mssp_header_length;
    int i; /* Loop counter */

    /* Before updating the following table, please read the MSSP specification:
     *
     * http://tintin.sourceforge.net/mssp/
     *
     * It's important that you use the correct format and spelling for the MSSP
     * variables, otherwise crawlers may reject the data as invalid.
     */
    static constexpr MSSP_t MSSPTable[] = {
        /* Required */
        { "NAME", MUD_NAME }, /* Change this in protocol.h */
        { "PLAYERS", "0" },
        { "UPTIME", "0" },

        /* Generic */
        { "CRAWL DELAY", "-1" },
        { nullptr, nullptr } /* This must always be last. */
    };

    /* Begin the subnegotiation sequence */
    snprintf(MSSPBuffer, sizeof(MSSPBuffer), "%c%c%c", IAC, SB, TELOPT_MSSP);

    for (i = 0; MSSPTable[i].pName != nullptr; ++i) {
        int SizePair;

        /* Retrieve the next MSSP variable/value pair */
        snprintf(MSSPPair, sizeof(MSSPPair), "%c%s%c%s", MSSP_VAR, MSSPTable[i].pName, MSSP_VAL,
            MSSPTable[i].pValue);

        /* Make sure we don't overflow the buffer */
        SizePair = strlen(MSSPPair);
        if (SizePair + SizeBuffer < MAX_MSSP_BUFFER - mssp_trailer_room) {
            memcpy(MSSPBuffer + SizeBuffer, MSSPPair, static_cast<std::size_t>(SizePair) + 1);
            SizeBuffer += SizePair;
        }
    }

    /* End the subnegotiation sequence */
    snprintf(MSSPBuffer + SizeBuffer, sizeof(MSSPBuffer) - static_cast<std::size_t>(SizeBuffer),
        "%c%c", IAC, SE);

    /* Send the sequence */
    Write(apDescriptor, MSSPBuffer);
}

/******************************************************************************
 Local MXP functions.
 ******************************************************************************/

static char* GetMxpTag(const char* apTag, const char* apText)
{
    constexpr int value_buffer_size = 64;
    constexpr int max_value_length = 60;
    static char MXPBuffer[value_buffer_size];
    const char* pStartPos = strstr(apText, apTag);

    if (pStartPos != nullptr) {
        const char* pEndPos = apText + strlen(apText);

        pStartPos += strlen(apTag); /* Add length of the tag */

        if (pStartPos < pEndPos) {
            int Index = 0;

            /* Some clients use quotes...and some don't. */
            if (*pStartPos == '\"') {
                pStartPos++;
            }

            for (; pStartPos < pEndPos && Index < max_value_length; ++pStartPos) {
                char Letter = *pStartPos;
                if (Letter == '.' || isdigit(Letter) || isalpha(Letter)) {
                    MXPBuffer[Index++] = Letter;
                } else {
                    /* Return the result */
                    MXPBuffer[Index] = '\0';
                    return MXPBuffer;
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

static const char* GetAnsiColour(bool abBackground, int aRed, int aGreen, int aBlue)
{
    const char* background_colour = nullptr;
    const char* bold_colour = nullptr;
    const char* dark_colour = nullptr;
    bool is_bright = false;

    if (aRed == aGreen && aRed == aBlue && aRed < 2) {
        background_colour = s_BackBlack;
        bold_colour = s_BoldBlack;
        dark_colour = s_DarkBlack;
        is_bright = aRed >= 1;
    } else if (aRed == aGreen && aRed == aBlue) {
        background_colour = s_BackWhite;
        bold_colour = s_BoldWhite;
        dark_colour = s_DarkWhite;
        is_bright = aRed >= 4;
    } else if (aRed > aGreen && aRed > aBlue) {
        background_colour = s_BackRed;
        bold_colour = s_BoldRed;
        dark_colour = s_DarkRed;
        is_bright = aRed >= 3;
    } else if (aRed == aGreen && aRed > aBlue) {
        background_colour = s_BackYellow;
        bold_colour = s_BoldYellow;
        dark_colour = s_DarkYellow;
        is_bright = aRed >= 3;
    } else if (aRed == aBlue && aRed > aGreen) {
        background_colour = s_BackMagenta;
        bold_colour = s_BoldMagenta;
        dark_colour = s_DarkMagenta;
        is_bright = aRed >= 3;
    } else if (aGreen > aBlue) {
        background_colour = s_BackGreen;
        bold_colour = s_BoldGreen;
        dark_colour = s_DarkGreen;
        is_bright = aGreen >= 3;
    } else if (aGreen == aBlue) {
        background_colour = s_BackCyan;
        bold_colour = s_BoldCyan;
        dark_colour = s_DarkCyan;
        is_bright = aGreen >= 3;
    } else {
        /* aBlue is the highest */
        background_colour = s_BackBlue;
        bold_colour = s_BoldBlue;
        dark_colour = s_DarkBlue;
        is_bright = aBlue >= 3;
    }

    if (abBackground) {
        return background_colour;
    }
    if (is_bright) {
        return bold_colour;
    }
    return dark_colour;
}

static const char* GetRGBColour(bool abBackground, int aRed, int aGreen, int aBlue)
{
    static char Result[16];
    int ColVal = 16 + (aRed * 36) + (aGreen * 6) + aBlue;
    snprintf(Result, sizeof(Result), "\033[%c8;5;%c%c%cm", '3' + abBackground, /* Background */
        '0' + (ColVal / 100), /* Red        */
        '0' + ((ColVal % 100) / 10), /* Green      */
        '0' + (ColVal % 10)); /* Blue       */
    return Result;
}

static bool IsValidColour(const char* apArgument)
{
    int i; /* Loop counter */

    if (apArgument == nullptr) {
        return false;
    }

    /* The sequence is 4 bytes, but we can ignore anything after it. */
    if (strlen(apArgument) < static_cast<std::size_t>(RGB_COLOUR_CODE_LENGTH)) {
        return false;
    }

    /* The first byte indicates foreground/background. */
    if (tolower(apArgument[0]) != 'f' && tolower(apArgument[0]) != 'b') {
        return false;
    }

    /* The remaining three bytes must each be in the range '0' to '5'. */
    for (i = 1; i < RGB_COLOUR_CODE_LENGTH; ++i) {
        if (apArgument[i] < '0' || apArgument[i] > '5') {
            return false;
        }
    }

    /* It's a valid foreground or background colour */
    return true;
}

/******************************************************************************
 Other local functions.
 ******************************************************************************/

static bool MatchString(const char* apFirst, const char* apSecond)
{
    while (*apFirst != '\0' && tolower(*apFirst) == tolower(*apSecond)) {
        ++apFirst;
        ++apSecond;
    }
    return (*apFirst == '\0' && *apSecond == '\0');
}

static bool PrefixString(const char* apPart, const char* apWhole)
{
    while (*apPart != '\0' && tolower(*apPart) == tolower(*apWhole)) {
        ++apPart;
        ++apWhole;
    }
    return (*apPart == '\0');
}

static bool IsNumber(const char* apString)
{
    while (*apString != '\0' && isdigit(*apString)) {
        ++apString;
    }
    return (*apString == '\0');
}

static char* AllocString(const char* apString)
{
    return copy_string(apString);
}

// Copies the string contained in value into a newly allocated string.
// Returns nullptr if given a nullptr value, or out of memory; either is logged.
// The caller is responsible for freeing this string with free().
[[nodiscard]] static char* copy_string(const char* value)
{
    if (value == nullptr) {
        ReportBug("copy_string: asked to copy a null string.\n");
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
        ReportBug("copy_string: out of memory.\n");
        return nullptr;
    }

    memcpy(result, value.data(), length);
    result[length] = '\0';
    return result;
}

// Allocates a new string that can hold length characters plus the NUL terminator, which the
// caller writes. Returns nullptr out of memory.
// The caller is responsible for freeing this string with free().
[[nodiscard]] static char* create_string(std::size_t length)
{
    return static_cast<char*>(malloc(length + 1));
}
