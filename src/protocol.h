/******************************************************************************
 Protocol snippet by KaVir.  Released into the public domain in February 2011.

 In case this is not legally possible:

 The copyright holder grants any entity the right to use this work for any
 purpose, without any conditions, unless such conditions are required by law.
 ******************************************************************************/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <string_view>

inline constexpr char MUD_NAME[] = "Return of the Shadow";

struct descriptor_data;
using descriptor_t = descriptor_data;

/******************************************************************************
 Symbolic constants.
 ******************************************************************************/

inline constexpr int SNIPPET_VERSION = 8; /* Helpful for debugging */

inline constexpr int MAX_PROTOCOL_BUFFER = 2048;
inline constexpr int MAX_VARIABLE_LENGTH = 4096;
inline constexpr int MAX_OUTPUT_BUFFER = 8192;
inline constexpr int MAX_MSSP_BUFFER = 4096;

inline constexpr int SEND = 1;
inline constexpr int ACCEPTED = 2;

inline constexpr int TELOPT_CHARSET = 42;
inline constexpr int TELOPT_MSDP = 69;
inline constexpr int TELOPT_MSSP = 70;
inline constexpr int TELOPT_MCCP = 86; /* This is MCCP version 2 */
inline constexpr int TELOPT_MSP = 90;
inline constexpr int TELOPT_MXP = 91;
inline constexpr int TELOPT_ATCP = 200;

inline constexpr int MSDP_VAR = 1;
inline constexpr int MSDP_VAL = 2;
inline constexpr int MSDP_TABLE_OPEN = 3;
inline constexpr int MSDP_TABLE_CLOSE = 4;
inline constexpr int MSDP_ARRAY_OPEN = 5;
inline constexpr int MSDP_ARRAY_CLOSE = 6;
inline constexpr int MAX_MSDP_SIZE = 100;

inline constexpr int MSSP_VAR = 1;
inline constexpr int MSSP_VAL = 2;

/******************************************************************************
 Types.
 ******************************************************************************/

enum negotiated_t {
    eNEGOTIATED_TTYPE,
    eNEGOTIATED_ECHO,
    eNEGOTIATED_NAWS,
    eNEGOTIATED_CHARSET,
    eNEGOTIATED_MSDP,
    eNEGOTIATED_MSSP,
    eNEGOTIATED_ATCP,
    eNEGOTIATED_MSP,
    eNEGOTIATED_MXP,
    eNEGOTIATED_MXP2,
    eNEGOTIATED_MCCP,
    eNEGOTIATED_EOR,
    eNEGOTIATED_SGA,

    eNEGOTIATED_MAX /* This must always be last */
};

enum support_t {
    eUNKNOWN,
    eNO,
    eSOMETIMES,
    eYES
};

enum variable_t {
    eMSDP_NONE = -1, /* This must always be first. */

    /* General */
    eMSDP_CHARACTER_NAME,
    eMSDP_SERVER_ID,
    eMSDP_SERVER_TIME,
    eMSDP_SNIPPET_VERSION,

    /* Character */
    eMSDP_AFFECTS,
    eMSDP_ALIGNMENT,
    eMSDP_EXPERIENCE,
    eMSDP_EXPERIENCE_MAX,
    eMSDP_HEALTH,
    eMSDP_HEALTH_MAX,
    eMSDP_LEVEL,
    eMSDP_RACE,
    eMSDP_CLASS, // TODO: reuse this
    eMSDP_MANA,
    eMSDP_MANA_MAX,
    eMSDP_WIMPY,
    eMSDP_PRACTICE,
    eMSDP_MONEY,
    eMSDP_MOVEMENT,
    eMSDP_MOVEMENT_MAX,
    eMSDP_HITROLL, // TODO: reuse this
    eMSDP_DAMROLL, // TODO: reuse this
    eMSDP_SPIRIT,
    eMSDP_AC, // TODO: reuse this
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

    /* Combat */
    eMSDP_OPPONENT_HEALTH,
    eMSDP_OPPONENT_HEALTH_MAX,
    eMSDP_OPPONENT_LEVEL,
    eMSDP_OPPONENT_NAME,
    eMSDP_OPPONENT_AGE,

    /* World */
    eMSDP_AREA_NAME,
    eMSDP_ROOM,
    eMSDP_ROOM_EXITS,
    eMSDP_ROOM_NAME,
    eMSDP_ROOM_VNUM,
    eMSDP_WORLD_TIME,
    eMSDP_WEATHER,

    /* Configuration */
    eMSDP_CLIENT_ID,
    eMSDP_CLIENT_VERSION,
    eMSDP_PLUGIN_ID,
    eMSDP_ANSI_COLORS,
    eMSDP_XTERM_256_COLORS,
    eMSDP_UTF_8,
    eMSDP_SOUND,
    eMSDP_MXP,

    /* GUI variables */
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

    /* Group Information  */
    eMSDP_GROUP,

    eMSDP_MAX /* This must always be last */
};

struct variable_name_t {
    const char* name; /* The string name of this variable */
    const char* default_text; /* The default value for a string */
    variable_t variable; /* The enum type of this variable */
    int minimum; /* The minimum valid value or string length */
    int maximum; /* The maximum valid value or string length */
    int default_number; /* The default value for a number */
    bool is_string; /* Is this variable a string or a number? */
    bool configurable; /* Can it be configured by the client? */
    bool write_once; /* Can only set this variable once */
    bool gui; /* It's a special GUI configuration variable */
};

struct MSDP_t {
    char* value_text; /* The string value of the variable */
    int value_number; /* The numeric value of the variable */
    bool reported; /* Is this variable being reported? */
    bool dirty; /* Does this variable need to be sent again? */
};

struct MSSP_t {
    const char* name; /* The name of the MSSP variable */
    const char* value; /* The value of the MSSP variable */
};

struct protocol_t {
    char* mxp_version; /* The version of MXP supported */
    char* last_ttype; /* Used for the cyclic TTYPE check */
    MSDP_t** variables; /* The MSDP variables */
    int write_oob; /* Used internally to indicate OOB data */
    int pending_input_length; /* Buffered bytes from an incomplete telnet/MXP fragment */
    int subnegotiation_input_length; /* Buffered bytes from an incomplete subnegotiation sequence */
    support_t xterm_256_support; /* The client supports XTerm 256 colors */
    int screen_width; /* The client's screen width */
    int screen_height; /* The client's screen height */
    bool option_states[eNEGOTIATED_MAX];
    bool in_subnegotiation; /* Current mode - deals with broken packets */
    bool negotiated; /* Indicates client successfully negotiated */
    bool renegotiate; /* Workaround for clients that autoconnect */
    bool needs_mxp_version; /* Workaround for clients that autoconnect */
    bool mxp_blocked; /* Used internally based on MXP version */
    char pending_input[4]; /* Prefix buffer for split negotiation sequences */
    /* Subnegotiation bytes split across network reads */
    char subnegotiation_input[MAX_PROTOCOL_BUFFER + 1];
    bool ttype_enabled; /* The client supports TTYPE */
    bool echo_enabled; /* Toggles ECHO on/off */
    bool naws_enabled; /* The client supports NAWS */
    bool charset_enabled; /* The client supports CHARSET */
    bool msdp_enabled; /* The client supports MSDP */
    bool mssp_enabled; /* The client supports MSSP */
    bool atcp_enabled; /* The client supports ATCP */
    bool msp_enabled; /* The client supports MSP */
    bool mxp_enabled; /* The client supports MXP */
    bool mccp_enabled; /* The client supports MCCP */
    bool eor_enabled; /* The client agreed to IAC EOR after each prompt */
    bool sga_enabled; /* The client asked the server to suppress go-ahead */
};

/******************************************************************************
 Protocol functions.
 ******************************************************************************/

/* Function: ProtocolCreate
 *
 * Creates, initialises and returns a structure containing protocol data for a
 * single user.  This should be called when the descriptor is initialised.
 */
protocol_t* ProtocolCreate();

/* Function: ProtocolDestroy
 *
 * Frees the memory allocated by the specified structure.  This should be
 * called just before a descriptor is freed.
 */
void ProtocolDestroy(protocol_t* protocol);

/* Function: ProtocolNegotiate
 *
 * Negatiates with the client to see which protocols the user supports, and
 * stores the results in the user's protocol structure.  Call this when you
 * wish to perform negotiation (but only call it once).  It is usually called
 * either immediately after the user has connected, or just after they have
 * entered the game.
 */
void ProtocolNegotiate(const descriptor_t* descriptor);

/* Function: ProtocolInput
 *
 * Extracts any negotiation sequences from the input buffer, and passes back
 * whatever is left for the mud to parse normally.  Call this after data has
 * been read into the input buffer, before it is used for anything else.
 */
void ProtocolInput(const descriptor_t* descriptor, const char* data, int size, char* out_text);

/* Function: ProtocolOutput
 *
 * This function takes a string, applies colour codes to it, and returns the
 * result.  It should be called just before writing to the output buffer.
 *
 * The special character used to indicate the start of a colour sequence is
 * '\t' (i.e., a tab, or ASCII character 9).  This makes it easy to include
 * in help files (as you can literally press the tab key) as well as strings
 * (where you can use \t instead).  However players can't send tabs (on most
 * muds at least), so this stops them from sending colour codes to each other.
 *
 * The predefined colours are:
 *
 *   n: no colour (switches colour off)
 *
 *   r: dark red                        R: light red
 *   g: dark green                      G: light green
 *   b: dark blue                       B: light blue
 *   y: dark yellow                     Y: light yellow
 *   m: dark magenta                    M: light magenta
 *   c: dark cyan                       C: light cyan
 *   w: dark white                      W: light white
 *
 *   a: dark azure                      A: light azure
 *   j: dark jade                       J: light jade
 *   l: dark lime                       L: light lime
 *   o: dark orange                     O: bright orange
 *   p: dark pink                       P: light pink
 *   t: dark tan                        T: light tan
 *   v: dark violet                     V: light violet
 *
 * So for example "This is \tOorange\tn." will colour the word "orange".  You
 * can add more colours yourself just by updating the switch statement.
 *
 * It's also possible to explicitly specify an RGB value, by including the four
 * character colour sequence (as used by ColourRGB) within square brackets, eg:
 *
 *    This is a \t[F010]very dark green foreground\tn.
 *
 * The square brackets can also be used to send unicode characters, like this:
 *
 *    Boat: \t[U9973/B]
 *    Rook: \t[U9814/C]
 *
 * For example you might use 'B' to represent a boat on your ASCII map, or a 'C'
 * to represent a castle - but players with UTF-8 support would actually see the
 * appropriate unicode characters for a boat or a rook (the chess playing piece).
 *
 * The exact syntax is '\t' (tab), '[', 'U' (indicating unicode), then the decimal
 * number of the unicode character (see http://www.unicode.org/charts), then '/'
 * followed by the ASCII character/s that should be used if the client doesn't
 * support UTF-8.  The ASCII sequence can be up to 7 characters in length, but in
 * most cases you'll only want it to be one or two characters (so that it has the
 * same alignment as the unicode character).
 *
 * Finally, this function also allows you to embed MXP tags.  The easiest and
 * safest way to do this is via the ( and ) bracket options:
 *
 *    From here, you can walk \t(north\t).
 *
 * However it's also possible to include more explicit MSP tags, like this:
 *
 *    The baker offers to sell you a \t<send href="buy pie">pie\t</send>.
 *
 * Note that the MXP tags will automatically be removed if the user doesn't
 * support MXP, but it's very important you remember to close the tags.
 */
const char* ProtocolOutput(const descriptor_t* descriptor, const char* text, int* out_length);

/******************************************************************************
 MSDP functions.
 ******************************************************************************/

/* Function: MSDPUpdate
 *
 * Call this regularly (I'd suggest at least once per second) to flush every
 * dirty MSDP variable that has been requested by the client via REPORT.  This
 * will automatically use ATCP instead if MSDP is not supported by the client.
 */
void MSDPUpdate(const descriptor_t* descriptor);

/* Function: MSDPFlush
 *
 * Works like MSDPUpdate(), except only flushes a specific variable.  The
 * variable will only actually be sent if it's both reported and dirty.
 *
 * Call this function after setting a variable if you want it to be reported
 * immediately, instead of on the next update.
 */
void MSDPFlush(const descriptor_t* descriptor, variable_t variable);

/* Function: MSDPMarkAllReportedDirty
 *
 * Marks every reported MSDP variable dirty so the next MSDPUpdate() re-sends a full
 * snapshot. Call when a client's MSDP channel becomes newly deliverable (e.g. the
 * player enables MSDP after login) so it receives values that never change again.
 */
void MSDPMarkAllReportedDirty(const descriptor_t* descriptor);

/* Function: MSDPSend
 *
 * Send the specified MSDP variable to the player.  You shouldn't ever really
 * need to do this manually, except perhaps when debugging something.  This
 * will automatically use ATCP instead if MSDP is not supported by the client.
 */
bool MSDPSend(const descriptor_t* descriptor, variable_t variable);

/* Function: MSDPSendPair
 *
 * Send the specified strings to the user as an MSDP variable/value pair.  This
 * will automatically use ATCP instead if MSDP is not supported by the client.
 */
void MSDPSendPair(const descriptor_t* descriptor, const char* name, const char* value);

/* Function: MSDPSendList
 *
 * Works like MSDPSendPair, but the value is sent as an MSDP array.
 *
 * value should be a list of values separated by spaces.
 */
void MSDPSendList(const descriptor_t* descriptor, const char* name, const char* value);

/* Function: MSDPSetNumber
 *
 * Call this whenever an MSDP integer variable has changed.  The easiest
 * approach is to send every MSDP variable within an update function (and
 * this is what the snippet does by default), but if the variable is only
 * set in one place you can just move its MDSPSend() call to there.
 *
 * You can also this function for bools, chars, enums, short ints, etc.
 */
void MSDPSetNumber(const descriptor_t* descriptor, variable_t variable, int value);

/* Function: MSDPSetString
 *
 * Call this whenever an MSDP string variable has changed.  The easiest
 * approach is to send every MSDP variable within an update function (and
 * this is what the snippet does by default), but if the variable is only
 * set in one place you can just move its MDSPSend() call to there.
 *
 * The value is passed through MSDPSanitizeValue() first, so it stays a
 * valid JSON string for clients that convert MSDP into JSON. Setting the value
 * already stored allocates nothing. A null value leaves the variable unchanged.
 */
void MSDPSetString(const descriptor_t* descriptor, variable_t variable, const char* value);

// MSDPSetString for a value that need not be NUL-terminated, such as a message without its line
// break. value must hold no NUL.
void MSDPSetString(const descriptor_t* descriptor, variable_t variable, std::string_view value);

/* Function: MSDPSanitizeValue
 *
 * Returns a copy of value with characters that are illegal inside a bare
 * JSON string ('"', '\', and control characters such as \n and \r) replaced
 * by their standard JSON escape sequence. MSDPSetString() applies this
 * automatically; call it directly when hand-building a table/array value
 * (see MSDPSetTable) that embeds freeform text, since those bypass
 * MSDPSetString(). Returns an empty string for a null value.
 */
std::string MSDPSanitizeValue(const char* value);

// Appends the MSDPSanitizeValue text for value to out_text, so a caller building a table in a
// reused string needs no copy of its own. A null value appends nothing.
void MSDPAppendSanitized(std::string& out_text, const char* value);

/* Function: MSDPSetTable
 *
 * Works like MSDPSetString, but the value is sent as an MSDP table.
 *
 * You must add the MSDP_VAR and MSDP_VAL manually, for example:
 *
 * sprintf( Buffer, "%c%s%c%s", (char)MSDP_VAR, Name, (char)MSDP_VAL, Value );
 * MSDPSetTable( d, eMSDP_TEST, Buffer );
 */
void MSDPSetTable(const descriptor_t* descriptor, variable_t variable, const char* value);

/* Function: MSDPSetArray
 *
 * Works like MSDPSetString, but the value is sent as an MSDP array.
 *
 * You must add the MSDP_VAR before each element manually, for example:
 *
 * sprintf( Buffer, "%c%s%c%s", (char)MSDP_VAL, Val1, (char)MSDP_VAL, Val2 );
 * MSDPSetArray( d, eMSDP_TEST, Buffer );
 */
void MSDPSetArray(const descriptor_t* descriptor, variable_t variable, const char* value);

bool MSDPIsValidVariable(variable_t variable);

/******************************************************************************
 MXP functions.
 ******************************************************************************/

/* Function: MXPSendTag
 *
 * Sends the specified tag to the user in a secure line, if MXP is supported.
 * This is mainly useful for the <VERSION> tag.
 */
void MXPSendTag(const descriptor_t* descriptor, const char* tag);

/******************************************************************************
 Colour functions.
 ******************************************************************************/

/* Function: ColourRGB
 *
 * Returns a colour as an escape code, based on the RGB value provided.  The
 * string must be four characters, where the first is either 'f' for foreground
 * or 'b' for background (case insensitive), and the other three characters are
 * numeric digits in the range 0 to 5, representing red, green and blue.
 *
 * For example "F500" returns a red foreground, "B530" an orange background,
 * and so on.  An invalid colour will clear whatever you've set previously.
 *
 * If the user doesn't support XTerm 256 colours, this function will return the
 * best-fit ANSI colour instead.
 *
 * If you wish to embed colours in strings, use ProtocolOutput().
 */
const char* ColourRGB(const descriptor_t* descriptor, const char* rgb_code);

/******************************************************************************
 Unicode (UTF-8 conversion) functions.
 ******************************************************************************/

/* Function: UnicodeGet
 *
 * Returns the UTF-8 sequence for the specified unicode value.
 */
char* UnicodeGet(int code_point);

/* Function: UnicodeAdd
 *
 * Adds the UTF-8 sequence for the specified unicode value onto the end of the
 * string, without adding a NUL character at the end.
 */
void UnicodeAdd(char** out_cursor, int code_point);

#endif /* PROTOCOL_H */
