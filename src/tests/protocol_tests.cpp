#include "../char_utils.h"
#include "../game_time_text.h"
#include "../interpre.h"
#include "../limits.h"
#include "../protocol.h"
#include "../structs.h"
#include "../utils.h"

#include <arpa/telnet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "ScopedGameClock.h"
#include "scoped_exit_test_rooms.h"
#include "test_cleanup.h"

#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

extern descriptor_data* descriptor_list;
extern int top_of_world;
extern room_data world;
extern weather_data weather_info;
extern char* pc_races[];
extern char* pc_star_types[];
extern char* sector_types[];
extern int average_mob_life;

void clear_char(struct char_data* ch, int mode);
void msdp_update();
void msdp_room_update(const char_data* ch);
void do_gen_tog(
    char_data* character, char* argument, waiting_type* wait_list, int command, int subcommand);
void another_hour(int mode);
int get_percent_absorb(const char_data* character);

namespace {

// Frees what these tests give a character: the name and short description they copy with strdup,
// and the arrays clear_char allocates.
void release_msdp_character(char_data* character)
{
    free(character->player.name);
    character->player.name = nullptr;
    free(character->player.short_descr);
    character->player.short_descr = nullptr;
    release_cleared_character(character);
}

// Releases a test's own character with release_msdp_character when the scope ends.
class MsdpCharacterRelease {
public:
    explicit MsdpCharacterRelease(char_data* character)
        : m_character(character)
    {
    }

    ~MsdpCharacterRelease() { release_msdp_character(m_character); }

    MsdpCharacterRelease(const MsdpCharacterRelease&) = delete;
    MsdpCharacterRelease& operator=(const MsdpCharacterRelease&) = delete;

private:
    // The character released on destruction; the test owns it.
    char_data* m_character;
};

class ProtocolDescriptor {
public:
    ProtocolDescriptor()
    {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, m_sockets) != 0) {
            ADD_FAILURE() << "Expected socketpair to create a local protocol output capture.";
            return;
        }
        const int flags = fcntl(m_sockets[1], F_GETFL, 0);
        if (flags < 0 || fcntl(m_sockets[1], F_SETFL, flags | O_NONBLOCK) != 0) {
            ADD_FAILURE() << "Expected protocol output capture socket to become nonblocking.";
            close(m_sockets[0]);
            close(m_sockets[1]);
            m_sockets[0] = -1;
            m_sockets[1] = -1;
            return;
        }
        descriptor.descriptor = m_sockets[0];
        descriptor.pProtocol = ProtocolCreate();
        descriptor.character = &character;
        SET_BIT(character.specials2.pref, PRF_MSDP);
    }

    ~ProtocolDescriptor()
    {
        release_test_descriptor(&descriptor);
        release_msdp_character(&character);
        if (m_sockets[0] >= 0)
            close(m_sockets[0]);
        if (m_sockets[1] >= 0)
            close(m_sockets[1]);
    }

    std::string read_output()
    {
        std::string output;
        if (m_sockets[1] < 0)
            return output;

        char buffer[4096];
        while (true) {
            const ssize_t bytes_read = recv(m_sockets[1], buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                output.append(buffer, static_cast<size_t>(bytes_read));
                continue;
            }
            break;
        }
        return output;
    }

    descriptor_data descriptor {};
    char_data character {};

private:
    int m_sockets[2] = { -1, -1 };
};

descriptor_data make_protocol_input_descriptor()
{
    descriptor_data descriptor {};
    descriptor.pProtocol = ProtocolCreate();
    return descriptor;
}

class ScopedDescriptorList {
public:
    ScopedDescriptorList()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorList()
    {
        descriptor_list = m_previous_descriptor_list;
    }

private:
    descriptor_data* m_previous_descriptor_list;
};

class ScopedMSDPTestRoom {
public:
    ScopedMSDPTestRoom()
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);

        m_top_of_world = top_of_world;
        top_of_world = 0;
        room_data& room = world[0];
        m_number = room.number;
        m_name = room.name;
        m_description = room.description;
        m_room_flags = room.room_flags;
        m_sector_type = room.sector_type;
        m_light = room.light;

        room.number = 3001;
        room.name = const_cast<char*>("MSDP Test Room");
        room.description = const_cast<char*>("A room used for MSDP update tests.\n\r");
        room.room_flags = INDOORS;
        room.sector_type = SECT_INSIDE;
        room.light = 0;
    }

    ~ScopedMSDPTestRoom()
    {
        top_of_world = m_top_of_world;
        room_data& room = world[0];
        room.number = m_number;
        room.name = m_name;
        room.description = m_description;
        room.room_flags = m_room_flags;
        room.sector_type = m_sector_type;
        room.light = m_light;
    }

private:
    int m_top_of_world = 0;
    int m_number = 0;
    char* m_name = nullptr;
    char* m_description = nullptr;
    long m_room_flags = 0;
    int m_sector_type = 0;
    byte m_light = 0;
};

class ScopedSectorWeather {
public:
    ScopedSectorWeather(int sector, int sky)
        : m_sector(sector)
        , m_sky(weather_info.sky[sector])
    {
        weather_info.sky[sector] = sky;
    }

    ~ScopedSectorWeather()
    {
        weather_info.sky[m_sector] = m_sky;
    }

private:
    int m_sector = 0;
    int m_sky = 0;
};

void initialize_msdp_player(char_data* character, const char* name, int room = 0)
{
    clear_char(character, MOB_VOID);
    character->player.name = strdup(name);
    character->player.level = 10;
    character->player.race = RACE_HUMAN;
    character->in_room = room;
    character->abilities.hit = 150;
    character->tmpabilities.hit = 125;
    character->abilities.mana = 110;
    character->tmpabilities.mana = 90;
    character->abilities.move = 95;
    character->tmpabilities.move = 80;
    character->abilities.str = 16;
    character->tmpabilities.str = 14;
    character->abilities.intel = 15;
    character->tmpabilities.intel = 13;
    character->abilities.wil = 12;
    character->tmpabilities.wil = 11;
    character->abilities.dex = 17;
    character->tmpabilities.dex = 15;
    character->abilities.con = 18;
    character->tmpabilities.con = 16;
    character->abilities.lea = 9;
    character->tmpabilities.lea = 8;
    character->points.exp = xp_to_level(character->player.level) + 1234;
    character->points.gold = 543;
    character->points.spirit = 7;
    character->points.OB = 12;
    character->points.parry = 9;
    character->points.dodge = 6;
    character->points.ENE_regen = 45;
    character->points.willpower = 44;
    character->points.spell_pen = 5;
    character->points.spell_power = 6;
    character->specials2.alignment = 250;
    character->specials2.wimp_level = 33;
    character->specials2.saving_throw = -8;
    character->specials2.perception = 66;
    character->specials.position = POSITION_STANDING;
    character->specials.tactics = TACTICS_NORMAL;
    character->profs->prof_level[PROF_MAGE] = 4;
    character->profs->prof_level[PROF_CLERIC] = 3;
    character->profs->prof_level[PROF_RANGER] = 2;
    character->profs->prof_level[PROF_WARRIOR] = 5;
    SET_BIT(character->specials2.pref, PRF_MSDP);
}

void disable_all_msdp_reports(protocol_t* protocol)
{
    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        protocol->variables.set_reported(static_cast<variable_t>(i), false);
        protocol->variables.mark_clean(static_cast<variable_t>(i));
    }
}

void enable_msdp_reports(protocol_t* protocol, const std::vector<variable_t>& variables)
{
    disable_all_msdp_reports(protocol);
    for (variable_t variable : variables)
        protocol->variables.set_reported(variable, true);
}

void seed_msdp_numbers(descriptor_data* descriptor, const std::vector<variable_t>& variables, int value)
{
    for (variable_t variable : variables)
        MSDPSetNumber(descriptor, variable, value);
}

std::string msdp_packet(const std::string& payload)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_MSDP));
    packet += payload;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

std::string msdp_pair_payload(const std::string& variable, const std::string& value)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += variable;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += value;
    return payload;
}

std::string telnet_sequence(unsigned char command, unsigned char option)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(command));
    packet.push_back(static_cast<char>(option));
    return packet;
}

std::string ttype_send_sequence()
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_TTYPE));
    packet.push_back(static_cast<char>(SEND));
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

std::string expected_msdp_pair(const std::string& variable, const std::string& value)
{
    return msdp_packet(msdp_pair_payload(variable, value));
}

std::string expected_msdp_array_pair(const std::string& variable, const std::vector<std::string>& values)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += variable;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    for (const std::string& value : values) {
        payload.push_back(static_cast<char>(MSDP_VAL));
        payload += value;
    }
    payload.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    return msdp_packet(payload);
}

std::string msdp_table_value(const std::string& table_payload)
{
    std::string value;
    value.push_back(static_cast<char>(MSDP_TABLE_OPEN));
    value += table_payload;
    value.push_back(static_cast<char>(MSDP_TABLE_CLOSE));
    return value;
}

std::string expected_msdp_table_pair(const std::string& variable, const std::string& table_payload)
{
    return expected_msdp_pair(variable, msdp_table_value(table_payload));
}

std::string expected_atcp_pair(const std::string& variable, const std::string& value)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_ATCP));
    packet += "MSDP.";
    packet += variable;
    packet.push_back(' ');
    packet += value;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

void feed_msdp_command(descriptor_data* descriptor, const std::string& variable, const std::string& value)
{
    std::string packet = msdp_packet(msdp_pair_payload(variable, value));
    char output[MAX_INPUT_LENGTH] = { '\0' };
    ProtocolInput(descriptor, packet.data(), static_cast<int>(packet.size()), output);
    EXPECT_STREQ(output, "");
}

void feed_protocol_input(descriptor_data* descriptor, const std::string& input, char* output)
{
    ProtocolInput(descriptor, const_cast<char*>(input.data()), static_cast<int>(input.size()), output);
}

bool output_contains_array_value(const std::string& output, const std::string& value)
{
    std::string marker;
    marker.push_back(static_cast<char>(MSDP_VAL));
    marker += value;
    const std::string terminal_marker = marker + static_cast<char>(MSDP_ARRAY_CLOSE);
    return output.find(marker + static_cast<char>(MSDP_VAL)) != std::string::npos
        || output.find(terminal_marker) != std::string::npos;
}

// The start of the MSDP frame that sets variable to an array whose first element is first_value,
// up to the separator before the second element.
std::string expected_msdp_array_start(const std::string& variable, const std::string& first_value)
{
    std::string start;
    start.push_back(static_cast<char>(IAC));
    start.push_back(static_cast<char>(SB));
    start.push_back(static_cast<char>(TELOPT_MSDP));
    start.push_back(static_cast<char>(MSDP_VAR));
    start += variable;
    start.push_back(static_cast<char>(MSDP_VAL));
    start.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    start.push_back(static_cast<char>(MSDP_VAL));
    start += first_value;
    start.push_back(static_cast<char>(MSDP_VAL));
    return start;
}

// Returns whether an MSDP array in output holds an empty element: two separators in a row, or a
// separator just before the array closes.
bool output_contains_empty_array_element(const std::string& output)
{
    const std::string separators = { static_cast<char>(MSDP_VAL), static_cast<char>(MSDP_VAL) };
    const std::string empty_last = { static_cast<char>(MSDP_VAL), static_cast<char>(MSDP_ARRAY_CLOSE) };
    return output.find(separators) != std::string::npos || output.find(empty_last) != std::string::npos;
}

void append_expected_table_value(std::string& payload, const std::string& name, const std::string& value)
{
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += name;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += value;
}

void append_expected_table_value(std::string& payload, const std::string& name, int value)
{
    append_expected_table_value(payload, name, std::to_string(value));
}

std::string expected_group_member_table(
    const std::string& name, int health, int mana, int movement)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_TABLE_OPEN));
    append_expected_table_value(payload, "NAME", name);
    append_expected_table_value(payload, "HEALTH", health);
    append_expected_table_value(payload, "MANA", mana);
    append_expected_table_value(payload, "MOVEMENT", movement);
    payload.push_back(static_cast<char>(MSDP_TABLE_CLOSE));
    return payload;
}

std::string expected_group_table(const std::vector<std::string>& member_tables)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += "MEMBERS";
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    for (const std::string& member_table : member_tables) {
        payload.push_back(static_cast<char>(MSDP_VAL));
        payload += member_table;
    }
    payload.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    return payload;
}

TEST(ProtocolInput, IgnoresLoneIacByteOnFreshDescriptor)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC) };

    ProtocolInput(&descriptor, input, 1, output);

    EXPECT_STREQ(output, "");

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, IgnoresTruncatedHandshakeVerbWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC), static_cast<char>(DO) };

    ProtocolInput(&descriptor, input, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 2);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, CompletesSplitHandshakeAcrossCallsWithoutLeakingTelnetBytes)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char partial[] = { static_cast<char>(IAC), static_cast<char>(WILL) };
    char option[] = { static_cast<char>(TELOPT_TTYPE) };

    ProtocolInput(&descriptor, partial, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 2);

    output[0] = '\0';
    ProtocolInput(&descriptor, option, 1, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 0);
    EXPECT_TRUE(descriptor.pProtocol->ttype_enabled);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersTruncatedMxpSequenceWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string input = "\x1b[1";

    ProtocolInput(&descriptor, input.data(), static_cast<int>(input.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, static_cast<int>(input.size()));

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersLoneEscapeByteUntilMxpPrefixContinues)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string first = "\x1b";
    std::string second = "[1";

    ProtocolInput(&descriptor, first.data(), static_cast<int>(first.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, static_cast<int>(first.size()));

    output[0] = '\0';
    ProtocolInput(&descriptor, second.data(), static_cast<int>(second.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 3);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(MSDPProtocol, SanitizesStringValuesForJsonLikeConsumers)
{
    EXPECT_EQ(MSDPSanitizeValue("plain"), "plain");
    EXPECT_EQ(MSDPSanitizeValue("quote\"slash\\\n\r\t\001"),
        "quote\\\"slash\\\\\\n\\r\\t\\u0001");
    EXPECT_EQ(MSDPSanitizeValue(nullptr), "");
}

TEST(MSDPProtocol, AppendSanitizedAddsTheEscapedValueAfterExistingTextAndNothingForNull)
{
    std::string text = "NAME=";

    MSDPAppendSanitized(text, "orc \"guard\"\n");
    EXPECT_EQ(text, "NAME=orc \\\"guard\\\"\\n");

    MSDPAppendSanitized(text, nullptr);
    EXPECT_EQ(text, "NAME=orc \\\"guard\\\"\\n");
}

TEST(MSDPProtocol, ProtocolCreateInitializesExpectedDefaults)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_TRUE(protocol->msdp_enabled);
    EXPECT_FALSE(protocol->negotiated);
    EXPECT_FALSE(protocol->renegotiate);
    EXPECT_FALSE(protocol->ttype_enabled);
    EXPECT_FALSE(protocol->echo_enabled);
    EXPECT_FALSE(protocol->naws_enabled);
    EXPECT_FALSE(protocol->charset_enabled);
    EXPECT_FALSE(protocol->mssp_enabled);
    EXPECT_FALSE(protocol->atcp_enabled);
    EXPECT_FALSE(protocol->msp_enabled);
    EXPECT_FALSE(protocol->mxp_enabled);
    EXPECT_FALSE(protocol->mccp_enabled);
    EXPECT_FALSE(protocol->eor_enabled);
    EXPECT_FALSE(protocol->sga_enabled);
    EXPECT_EQ(protocol->xterm_256_support, eUNKNOWN);
    EXPECT_EQ(protocol->screen_width, 0);
    EXPECT_EQ(protocol->screen_height, 0);
    EXPECT_EQ(protocol->mxp_version, "Unknown");
    for (int i = eNEGOTIATED_TTYPE; i < eNEGOTIATED_MAX; ++i)
        EXPECT_FALSE(protocol->option_states[i]) << "negotiated enum " << i;
    EXPECT_EQ(protocol->pending_input_length, 0);
    EXPECT_EQ(protocol->subnegotiation_input_length, 0);

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i)
        EXPECT_TRUE(protocol->variables.is_reported(static_cast<variable_t>(i)))
            << "MSDP enum " << i;

    /* Server-reported variables start dirty so the first update after login
       transmits them even when their value is still the default. */
    const std::vector<variable_t> primed = { eMSDP_CHARACTER_NAME, eMSDP_HEALTH, eMSDP_LEVEL,
        eMSDP_SPELL_PEN, eMSDP_SPELL_POWER, eMSDP_CARRIED_WEIGHT, eMSDP_MAGE_LEVEL,
        eMSDP_SPECIALIZATION, eMSDP_ROOM_NAME };
    for (variable_t variable : primed)
        EXPECT_TRUE(protocol->variables.is_dirty(variable)) << "MSDP enum " << variable;

    /* Client-owned configuration and GUI template variables are not primed. */
    const std::vector<variable_t> not_primed = { eMSDP_CLIENT_ID, eMSDP_ANSI_COLORS,
        eMSDP_XTERM_256_COLORS, eMSDP_UTF_8, eMSDP_SOUND, eMSDP_MXP, eMSDP_BUTTON_1,
        eMSDP_GAUGE_1 };
    for (variable_t variable : not_primed)
        EXPECT_FALSE(protocol->variables.is_dirty(variable)) << "MSDP enum " << variable;

    EXPECT_EQ(protocol->variables.number(eMSDP_SNIPPET_VERSION), 8);
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "Unknown");
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
    EXPECT_EQ(protocol->variables.text(eMSDP_PLUGIN_ID), "Unknown");
    EXPECT_EQ(protocol->variables.number(eMSDP_ANSI_COLORS), 1);
    EXPECT_EQ(protocol->variables.number(eMSDP_XTERM_256_COLORS), 0);
    EXPECT_EQ(protocol->variables.number(eMSDP_UTF_8), 0);
    EXPECT_EQ(protocol->variables.number(eMSDP_SOUND), 0);
    EXPECT_EQ(protocol->variables.number(eMSDP_MXP), 0);
    EXPECT_EQ(protocol->variables.text(eMSDP_BUTTON_1), "\005\002Help\002help\006");
    EXPECT_EQ(protocol->variables.text(eMSDP_GAUGE_1),
        "\005\002Health\002red\002HEALTH\002HEALTH_MAX\006");
}

TEST(MSDPProtocol, ScoreButtonSendsScore)
{
    ProtocolDescriptor context;

    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_BUTTON_3),
        "\005\002Score\002score\006");
}

TEST(MSDPProtocol, TTypeNegotiationRequestsFullProtocolSet)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(WILL, TELOPT_TTYPE), output);

    std::string negotiation = context.read_output();
    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->negotiated);
    EXPECT_TRUE(context.descriptor.pProtocol->renegotiate);
    EXPECT_TRUE(context.descriptor.pProtocol->ttype_enabled);
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_TTYPE]);
    EXPECT_EQ(negotiation,
        telnet_sequence(DO, TELOPT_TTYPE)
            + ttype_send_sequence()
            + telnet_sequence(DO, TELOPT_NAWS)
            + telnet_sequence(DO, TELOPT_CHARSET)
            + telnet_sequence(WILL, TELOPT_MSDP)
            + telnet_sequence(WILL, TELOPT_MSSP)
            + telnet_sequence(DO, TELOPT_ATCP)
            + telnet_sequence(WILL, TELOPT_MSP)
            + telnet_sequence(DO, TELOPT_MXP)
            + telnet_sequence(WILL, TELOPT_EOR));
}

TEST(MSDPProtocol, NegotiatesAndRejectsMSDPSupport)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_MSDP]);
    EXPECT_TRUE(context.descriptor.pProtocol->msdp_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WILL, TELOPT_MSDP));

    feed_protocol_input(&context.descriptor, telnet_sequence(DONT, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->option_states[eNEGOTIATED_MSDP]);
    EXPECT_FALSE(context.descriptor.pProtocol->msdp_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WONT, TELOPT_MSDP));

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_MSDP]);
    EXPECT_TRUE(context.descriptor.pProtocol->msdp_enabled);
    EXPECT_EQ(context.read_output(),
        telnet_sequence(WILL, TELOPT_MSDP) + expected_msdp_pair("SERVER_ID", MUD_NAME));
}

// The EOR and SGA tests below read replies the protocol code writes straight to the descriptor's
// socket, so they use ProtocolDescriptor's POSIX socketpair.
TEST(MSDPProtocol, AcceptsOfferedEndOfRecordWithoutAnotherOffer)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    feed_protocol_input(&context.descriptor, telnet_sequence(WILL, TELOPT_TTYPE), output);
    context.read_output();

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_EOR), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_EOR]);
    EXPECT_TRUE(context.descriptor.pProtocol->eor_enabled);
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, NegotiatesAndRejectsEndOfRecord)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_EOR), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_EOR]);
    EXPECT_TRUE(context.descriptor.pProtocol->eor_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WILL, TELOPT_EOR));

    feed_protocol_input(&context.descriptor, telnet_sequence(DONT, TELOPT_EOR), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->option_states[eNEGOTIATED_EOR]);
    EXPECT_FALSE(context.descriptor.pProtocol->eor_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WONT, TELOPT_EOR));
}

TEST(MSDPProtocol, RefusesAClientOfferingEndOfRecord)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(WILL, TELOPT_EOR), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->eor_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(DONT, TELOPT_EOR));
}

TEST(MSDPProtocol, SuppressesAndRestoresGoAhead)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_SGA), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_SGA]);
    EXPECT_TRUE(context.descriptor.pProtocol->sga_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WILL, TELOPT_SGA));

    feed_protocol_input(&context.descriptor, telnet_sequence(DONT, TELOPT_SGA), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->option_states[eNEGOTIATED_SGA]);
    EXPECT_FALSE(context.descriptor.pProtocol->sga_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(WONT, TELOPT_SGA));
}

TEST(MSDPProtocol, RefusesAClientOfferingToSuppressItsGoAhead)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(WILL, TELOPT_SGA), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->sga_enabled);
    EXPECT_EQ(context.read_output(), telnet_sequence(DONT, TELOPT_SGA));
}

TEST(MSDPProtocol, SetNumberMarksDirtyOnlyWhenValueChanges)
{
    ProtocolDescriptor context;

    /* HEALTH is primed dirty by ProtocolCreate; clear it to test change detection. */
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    context.descriptor.pProtocol->variables.mark_clean(eMSDP_HEALTH);

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 42);
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_HEALTH), 42);

    context.descriptor.pProtocol->variables.mark_clean(eMSDP_HEALTH);
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 42);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, SetStringSanitizesAndMarksDirtyOnlyWhenValueChanges)
{
    ProtocolDescriptor context;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "A \"B\"\n");

    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CHARACTER_NAME),
        "A \\\"B\\\"\\n");

    context.descriptor.pProtocol->variables.mark_clean(eMSDP_CHARACTER_NAME);
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "A \"B\"\n");
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
}

// A descriptor with MSDP state and no socket, for tests that read only what the setters store.
class MsdpSetterDescriptor {
public:
    MsdpSetterDescriptor()
        : m_release(&descriptor)
    {
        descriptor.pProtocol = ProtocolCreate();
    }

    // The stored MSDP variables of the descriptor.
    msdp_variables_t& variables() const { return descriptor.pProtocol->variables; }

    // The descriptor the setters are given.
    descriptor_data descriptor {};

private:
    // Gives back the descriptor's protocol state when the test ends.
    TestDescriptorRelease m_release;
};

// The bytes MSDPSetTable stores for payload, or MSDPSetArray when given the array markers.
std::string msdp_wrapped_value(unsigned char open_marker, std::string_view payload,
    unsigned char close_marker)
{
    std::string value;
    value.push_back(static_cast<char>(open_marker));
    value += payload;
    value.push_back(static_cast<char>(close_marker));
    return value;
}

// A one-pair NAME table payload for room_name; tables store it unescaped, so any quote or
// backslash in it stays raw.
std::string msdp_test_table_payload(std::string_view room_name)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += "NAME";
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += room_name;
    return payload;
}

TEST(MSDPProtocol, SetStringKeepsAnUnchangedValueInPlace)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();

    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, "The Common Room of the Prancing Pony");
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_NAME));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME), "The Common Room of the Prancing Pony");

    msdp_variables.mark_clean(eMSDP_ROOM_NAME);
    const char* stored = msdp_variables.text(eMSDP_ROOM_NAME).data();
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, "The Common Room of the Prancing Pony");
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM_NAME));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME).data(), stored)
        << "An unchanged value keeps its stored string.";
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME), "The Common Room of the Prancing Pony");
}

TEST(MSDPProtocol, SetStringReplacesAValueThatDiffersAnywhere)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    const char* const values[] = {
        "The Common Room of the Prancing Pony",
        "The Common Room of the Prancing Pon", // a prefix of the stored value
        "The Common Room of the Prancing Pony", // the stored value is a prefix of this
        "the Common Room of the Prancing Pony", // the first byte differs
        "The Common Room of the Prancing Ponies",
    };

    for (const char* value : values) {
        msdp_variables.mark_clean(eMSDP_ROOM_NAME);
        MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, value);
        EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_NAME)) << value;
        EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME), value);
    }
}

TEST(MSDPProtocol, SetStringComparesTheEscapedFormOfAValue)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    const char* const raw = "say \"hi\" \\ \n\r\t\001\037 caf\xc3\xa9 \x7f";
    const char* const escaped = "say \\\"hi\\\" \\\\ \\n\\r\\t\\u0001\\u001f caf\xc3\xa9 \x7f";

    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, raw);
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_NAME));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME), escaped);

    msdp_variables.mark_clean(eMSDP_ROOM_NAME);
    const char* stored = msdp_variables.text(eMSDP_ROOM_NAME).data();
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, raw);
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM_NAME));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME).data(), stored);

    // The stored text itself, set as a value, escapes again and so differs.
    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, escaped);
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_NAME));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME),
        "say \\\\\\\"hi\\\\\\\" \\\\\\\\ \\\\n\\\\r\\\\t\\\\u0001\\\\u001f caf\xc3\xa9 \x7f");

    const struct {
        const char* value;
        const char* stored;
    } control_cases[] = {
        { "a\001", "a\\u0001" },
        { "a\002", "a\\u0002" },
        { "a\\u0002", "a\\\\u0002" },
        { "a\n", "a\\n" },
        { "a\\n", "a\\\\n" },
    };
    for (const auto& control_case : control_cases) {
        msdp_variables.mark_clean(eMSDP_ROOM_NAME);
        MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, control_case.value);
        EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_NAME)) << control_case.stored;
        EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_NAME), control_case.stored);
    }
}

TEST(MSDPProtocol, SetStringStoresAndKeepsAnEmptyValue)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();

    MSDPSetString(
        &context.descriptor, eMSDP_OPPONENT_AGE, "An orc has just arrived to this place.");
    msdp_variables.mark_clean(eMSDP_OPPONENT_AGE);
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_AGE, "");
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_OPPONENT_AGE));
    EXPECT_EQ(msdp_variables.text(eMSDP_OPPONENT_AGE), "");

    msdp_variables.mark_clean(eMSDP_OPPONENT_AGE);
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_AGE, "");
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_OPPONENT_AGE));
    EXPECT_EQ(msdp_variables.text(eMSDP_OPPONENT_AGE), "");
}

TEST(MSDPProtocol, SetStringTakesAValueThatIsNotNulTerminated)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    constexpr std::string_view weather_message = "Clouds \"race\" by.\n\r";
    const std::string_view without_break = weather_message.substr(0, weather_message.size() - 2);

    MSDPSetString(&context.descriptor, eMSDP_WEATHER, without_break);
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_WEATHER));
    EXPECT_EQ(msdp_variables.text(eMSDP_WEATHER), "Clouds \\\"race\\\" by.");

    msdp_variables.mark_clean(eMSDP_WEATHER);
    const char* stored = msdp_variables.text(eMSDP_WEATHER).data();
    MSDPSetString(&context.descriptor, eMSDP_WEATHER, without_break);
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_WEATHER));
    EXPECT_EQ(msdp_variables.text(eMSDP_WEATHER).data(), stored);

    MSDPSetString(&context.descriptor, eMSDP_WEATHER, "Clouds \"race\" by.");
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_WEATHER))
        << "The view and the same NUL-terminated text compare alike.";
}

TEST(MSDPProtocol, SetTableWrapsThePayloadAndKeepsAnUnchangedTable)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    const std::string payload = msdp_test_table_payload("The \"Pony\" \\ Inn");

    MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM),
        msdp_wrapped_value(MSDP_TABLE_OPEN, payload, MSDP_TABLE_CLOSE));

    msdp_variables.mark_clean(eMSDP_ROOM);
    const char* stored = msdp_variables.text(eMSDP_ROOM).data();
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM).data(), stored);

    const std::string payloads[] = {
        msdp_test_table_payload("The \"Pony\" \\ In"),
        msdp_test_table_payload("The \"Pony\" \\ Inn"),
        msdp_test_table_payload("The \"Pony\" \\ Inns"),
        msdp_test_table_payload("the \"Pony\" \\ Inn"),
    };
    for (const std::string& changed : payloads) {
        msdp_variables.mark_clean(eMSDP_ROOM);
        MSDPSetTable(&context.descriptor, eMSDP_ROOM, changed.c_str());
        EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM));
        EXPECT_EQ(msdp_variables.text(eMSDP_ROOM),
            msdp_wrapped_value(MSDP_TABLE_OPEN, changed, MSDP_TABLE_CLOSE));
    }

    // The same payload as an array differs in its markers.
    msdp_variables.mark_clean(eMSDP_ROOM);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM, payloads[3].c_str());
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM),
        msdp_wrapped_value(MSDP_ARRAY_OPEN, payloads[3], MSDP_ARRAY_CLOSE));
}

TEST(MSDPProtocol, SetTableAndArrayStoreAnEmptyValueUnwrapped)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    const std::string payload = msdp_test_table_payload("Bree");

    MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    msdp_variables.mark_clean(eMSDP_ROOM);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, "");
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM), "");
    msdp_variables.mark_clean(eMSDP_ROOM);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, "");
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM));

    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, "\001n");
    msdp_variables.mark_clean(eMSDP_ROOM_EXITS);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, "");
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_EXITS), "");
    msdp_variables.mark_clean(eMSDP_ROOM_EXITS);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, "");
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
}

TEST(MSDPProtocol, SetArrayWrapsThePayloadAndKeepsAnUnchangedArray)
{
    MsdpSetterDescriptor context;
    msdp_variables_t& msdp_variables = context.variables();
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += "north";
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += "east";

    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_EXITS),
        msdp_wrapped_value(MSDP_ARRAY_OPEN, payload, MSDP_ARRAY_CLOSE));

    msdp_variables.mark_clean(eMSDP_ROOM_EXITS);
    const char* stored = msdp_variables.text(eMSDP_ROOM_EXITS).data();
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, payload.c_str());
    EXPECT_FALSE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_EXITS).data(), stored);

    const std::string shorter = payload.substr(0, payload.size() - 1);
    const std::string longer = payload + "s";
    for (const std::string& changed : { shorter, payload, longer }) {
        msdp_variables.mark_clean(eMSDP_ROOM_EXITS);
        MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, changed.c_str());
        EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
        EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_EXITS),
            msdp_wrapped_value(MSDP_ARRAY_OPEN, changed, MSDP_ARRAY_CLOSE));
    }

    // The same payload as a table differs in its markers.
    msdp_variables.mark_clean(eMSDP_ROOM_EXITS);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM_EXITS, longer.c_str());
    EXPECT_TRUE(msdp_variables.is_dirty(eMSDP_ROOM_EXITS));
    EXPECT_EQ(msdp_variables.text(eMSDP_ROOM_EXITS),
        msdp_wrapped_value(MSDP_TABLE_OPEN, longer, MSDP_TABLE_CLOSE));
}

// Reads the wire through ProtocolDescriptor's socket pair: the setters write only through
// write_to_descriptor, which takes a socket, so no portable capture can see the sent bytes.
TEST(MSDPProtocol, SendsTheStoredBytesOfEscapedStringsAndTables)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    disable_all_msdp_reports(protocol);

    MSDPSetString(&context.descriptor, eMSDP_ROOM_NAME, "The \"Pony\"\t\001");
    MSDPSend(&context.descriptor, eMSDP_ROOM_NAME);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("ROOM_NAME", "The \\\"Pony\\\"\\t\\u0001"));

    const std::string payload = msdp_test_table_payload("The \"Pony\" \\ Inn");
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, payload.c_str());
    MSDPSend(&context.descriptor, eMSDP_ROOM);
    EXPECT_EQ(context.read_output(), expected_msdp_table_pair("ROOM", payload));
}

TEST(MSDPProtocol, SendsStringAndNumberVariablesAsMSDPPackets)
{
    ProtocolDescriptor context;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("CHARACTER_NAME", "Aragorn"));

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "87"));
}

TEST(MSDPProtocol, DoesNotSendMSDPWhenPlayerPreferenceIsDisabled)
{
    ProtocolDescriptor context;
    REMOVE_BIT(context.character.specials2.pref, PRF_MSDP);

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, SendHelpersIgnoreMissingDescriptorProtocolOrCharacter)
{
    MSDPSend(nullptr, eMSDP_HEALTH);
    MSDPFlush(nullptr, eMSDP_HEALTH);
    MSDPSendPair(nullptr, "HEALTH", "1");
    MSDPSendList(nullptr, "COMMANDS", "LIST REPORT");

    descriptor_data descriptor_without_protocol {};
    MSDPSend(&descriptor_without_protocol, eMSDP_HEALTH);
    MSDPFlush(&descriptor_without_protocol, eMSDP_HEALTH);
    MSDPSendPair(&descriptor_without_protocol, "HEALTH", "1");
    MSDPSendList(&descriptor_without_protocol, "COMMANDS", "LIST REPORT");

    ProtocolDescriptor context;
    context.descriptor.character = nullptr;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);
    feed_msdp_command(&context.descriptor, "SEND", "HEALTH");
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, SendsATCPFallbackWhenMSDPIsUnavailable)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->msdp_enabled = false;
    context.descriptor.pProtocol->atcp_enabled = true;

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), expected_atcp_pair("HEALTH", "87"));
}

TEST(MSDPProtocol, SendsListsTablesAndArraysWithMSDPMarkers)
{
    ProtocolDescriptor context;

    MSDPSendList(&context.descriptor, "COMMANDS", "LIST REPORT RESET");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("COMMANDS", { "LIST", "REPORT", "RESET" }));

    std::string table_payload;
    table_payload.push_back(static_cast<char>(MSDP_VAR));
    table_payload += "VNUM";
    table_payload.push_back(static_cast<char>(MSDP_VAL));
    table_payload += "3001";
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, table_payload.c_str());
    MSDPSend(&context.descriptor, eMSDP_ROOM);

    EXPECT_EQ(context.read_output(), expected_msdp_table_pair("ROOM", table_payload));

    std::string array_payload;
    array_payload.push_back(static_cast<char>(MSDP_VAL));
    array_payload += "n";
    array_payload.push_back(static_cast<char>(MSDP_VAL));
    array_payload += "e";
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, array_payload.c_str());
    MSDPSend(&context.descriptor, eMSDP_ROOM_EXITS);

    std::string expected_array;
    expected_array.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    expected_array += array_payload;
    expected_array.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    EXPECT_EQ(context.read_output(), expected_msdp_pair("ROOM_EXITS", expected_array));
}

TEST(MSDPProtocol, SendPairAndListRejectOversizedPayloads)
{
    ProtocolDescriptor context;
    std::string huge_value(MAX_VARIABLE_LENGTH, 'X');

    MSDPSendPair(&context.descriptor, "HEALTH", huge_value.c_str());
    MSDPSendList(&context.descriptor, "COMMANDS", huge_value.c_str());

    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair: MSDPSendPair and MSDPSendList write
// only through write_to_descriptor, which takes a socket, so no portable capture can see them.
TEST(MSDPProtocol, SendPairAndListEscapeTheirValuesForMSDPAndATCP)
{
    ProtocolDescriptor context;

    MSDPSendPair(&context.descriptor, "PLAY_SOUND", "say \"hi\"\n");
    EXPECT_EQ(context.read_output(), expected_msdp_pair("PLAY_SOUND", "say \\\"hi\\\"\\n"));

    MSDPSendList(&context.descriptor, "COMMANDS", "LIST \"REPORT\"");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("COMMANDS", { "LIST", "\\\"REPORT\\\"" }));

    context.descriptor.pProtocol->msdp_enabled = false;
    context.descriptor.pProtocol->atcp_enabled = true;

    MSDPSendPair(&context.descriptor, "PLAY_SOUND", "say \"hi\"\n");
    EXPECT_EQ(context.read_output(), expected_atcp_pair("PLAY_SOUND", "say \\\"hi\\\"\\n"));

    MSDPSendList(&context.descriptor, "COMMANDS", "LIST \"REPORT\"");
    EXPECT_EQ(context.read_output(), expected_atcp_pair("COMMANDS", "LIST \\\"REPORT\\\""));
}

// Reads the wire through ProtocolDescriptor's socket pair, for the reason given at
// SendPairAndListEscapeTheirValuesForMSDPAndATCP.
TEST(MSDPProtocol, SendPairMeasuresItsLimitWithTheEscapedValue)
{
    ProtocolDescriptor context;
    // The framing allowance MSDPSendPair's limit adds to the variable and value lengths.
    constexpr std::size_t packet_overhead = 12;
    const std::string variable = "HEALTH";
    const std::size_t largest_escaped_value
        = MAX_VARIABLE_LENGTH - 1 - variable.size() - packet_overhead;
    // A quote escapes to two bytes, so the raw value is one byte shorter than what is sent.
    const std::string padding(largest_escaped_value - 2, 'X');
    const std::string largest_value = padding + "\"";

    MSDPSendPair(&context.descriptor, variable.c_str(), largest_value.c_str());
    EXPECT_EQ(context.read_output(), expected_msdp_pair(variable, padding + "\\\""));

    const std::string one_too_long = "X" + largest_value;
    MSDPSendPair(&context.descriptor, variable.c_str(), one_too_long.c_str());
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, PublicSetHelpersIgnoreInvalidVariables)
{
    ProtocolDescriptor context;

    /* Clear the priming flag so a dirty CHARACTER_NAME below could only come
       from one of the invalid-variable calls. */
    context.descriptor.pProtocol->variables.mark_clean(eMSDP_CHARACTER_NAME);

    EXPECT_FALSE(MSDPIsValidVariable(eMSDP_NONE));
    EXPECT_FALSE(MSDPIsValidVariable(eMSDP_MAX));

    MSDPSetNumber(&context.descriptor, eMSDP_MAX, 99);
    MSDPSetNumber(&context.descriptor, eMSDP_NONE, 99);
    MSDPSetString(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetString(&context.descriptor, eMSDP_NONE, "ignored");
    MSDPSetTable(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetTable(&context.descriptor, eMSDP_NONE, "ignored");
    MSDPSetArray(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetArray(&context.descriptor, eMSDP_NONE, "ignored");

    EXPECT_EQ(context.read_output(), "");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CHARACTER_NAME), "");
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
}

TEST(MSDPProtocol, UpdateFlushesOnlyDirtyReportedVariables)
{
    ProtocolDescriptor context;

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i)
        context.descriptor.pProtocol->variables.set_reported(static_cast<variable_t>(i), false);

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, true);
    context.descriptor.pProtocol->variables.set_reported(eMSDP_MANA, true);
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 45);
    MSDPSetNumber(&context.descriptor, eMSDP_MANA, 12);
    MSDPSetNumber(&context.descriptor, eMSDP_MOVEMENT, 77);

    MSDPUpdate(&context.descriptor);

    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("HEALTH", "45") + expected_msdp_pair("MANA", "12"));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_MANA));
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_MOVEMENT));
}

TEST(MSDPProtocol, FlushSendsOnlyTheRequestedDirtyReportedVariable)
{
    ProtocolDescriptor context;

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 45);
    MSDPSetNumber(&context.descriptor, eMSDP_MANA, 12);

    MSDPFlush(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "45"));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_MANA));
}

TEST(MSDPProtocol, ParsesReportUnreportResetAndSendCommands)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    feed_msdp_command(&context.descriptor, "REPORT", "HEALTH");
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));

    context.read_output();
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 75);
    feed_msdp_command(&context.descriptor, "SEND", "HEALTH");
    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "75"));

    feed_msdp_command(&context.descriptor, "UNREPORT", "HEALTH");
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));

    const char* reset_targets[] = { "REPORTED_VARIABLES", "REPORTABLE_VARIABLES", "VARIABLES" };
    for (const char* reset_target : reset_targets) {
        context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, true);
        context.descriptor.pProtocol->variables.set_reported(eMSDP_MANA, true);
        context.descriptor.pProtocol->variables.mark_dirty(eMSDP_HEALTH);
        context.descriptor.pProtocol->variables.mark_dirty(eMSDP_MANA);
        feed_msdp_command(&context.descriptor, "RESET", reset_target);
        EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH))
            << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_MANA))
            << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH))
            << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_MANA)) << reset_target;
    }
}

TEST(MSDPProtocol, ListsCommandsAndReportedVariables)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "LIST", "COMMANDS");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("COMMANDS", { "LIST", "REPORT", "RESET", "SEND", "UNREPORT" }));

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i)
        context.descriptor.pProtocol->variables.set_reported(static_cast<variable_t>(i), false);
    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, true);
    context.descriptor.pProtocol->variables.set_reported(eMSDP_MANA, true);

    feed_msdp_command(&context.descriptor, "LIST", "REPORTED_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("REPORTED_VARIABLES", { "HEALTH", "MANA" }));
}

TEST(MSDPProtocol, ListsReportableAndSendableVariablesWithoutAnEmptyName)
{
    ProtocolDescriptor context;

    for (const std::string list_name : { "REPORTABLE_VARIABLES", "SENDABLE_VARIABLES" }) {
        feed_msdp_command(&context.descriptor, "LIST", list_name);
        const std::string list_output = context.read_output();
        const std::string expected_start = expected_msdp_array_start(list_name, "CHARACTER_NAME");

        const std::string list_start = list_output.substr(0, expected_start.size());
        EXPECT_EQ(list_start, expected_start) << list_output;
        EXPECT_FALSE(output_contains_empty_array_element(list_output)) << list_output;
    }
}

TEST(MSDPProtocol, ListsConfigurableAndGuiVariables)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "LIST", "LISTS");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("LISTS",
            { "COMMANDS", "LISTS", "CONFIGURABLE_VARIABLES", "REPORTABLE_VARIABLES",
                "REPORTED_VARIABLES", "SENDABLE_VARIABLES", "GUI_VARIABLES" }));

    feed_msdp_command(&context.descriptor, "LIST", "SENDABLE_VARIABLES");
    std::string sendable_output = context.read_output();
    EXPECT_TRUE(output_contains_array_value(sendable_output, "CHARACTER_NAME")) << sendable_output;
    EXPECT_TRUE(output_contains_array_value(sendable_output, "HEALTH")) << sendable_output;
    EXPECT_TRUE(output_contains_array_value(sendable_output, "GROUP")) << sendable_output;
    EXPECT_FALSE(output_contains_array_value(sendable_output, "BUTTON_1")) << sendable_output;

    feed_msdp_command(&context.descriptor, "LIST", "REPORTABLE_VARIABLES");
    std::string reportable_output = context.read_output();
    EXPECT_TRUE(output_contains_array_value(reportable_output, "CHARACTER_NAME")) << reportable_output;
    EXPECT_TRUE(output_contains_array_value(reportable_output, "HEALTH")) << reportable_output;
    EXPECT_TRUE(output_contains_array_value(reportable_output, "GROUP")) << reportable_output;
    EXPECT_FALSE(output_contains_array_value(reportable_output, "BUTTON_1")) << reportable_output;

    feed_msdp_command(&context.descriptor, "LIST", "CONFIGURABLE_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("CONFIGURABLE_VARIABLES",
            { "CLIENT_ID", "CLIENT_VERSION", "PLUGIN_ID", "ANSI_COLORS",
                "XTERM_256_COLORS", "UTF_8", "SOUND", "MXP" }));

    feed_msdp_command(&context.descriptor, "LIST", "GUI_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("GUI_VARIABLES",
            { "BUTTON_1", "BUTTON_2", "BUTTON_3", "BUTTON_4", "BUTTON_5",
                "GAUGE_1", "GAUGE_2", "GAUGE_3", "GAUGE_4", "GAUGE_5" }));
}

TEST(MSDPProtocol, ConfigurableVariablesValidateRangeLengthAndWriteOnceRules)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "1");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "abc");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "-1");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "2");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    std::string hostile_plugin_id;
    hostile_plugin_id.push_back(static_cast<char>(0xff));
    hostile_plugin_id += "\007RotS";
    feed_msdp_command(&context.descriptor, "PLUGIN_ID", hostile_plugin_id);
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_PLUGIN_ID), "RotS");
    feed_msdp_command(&context.descriptor, "PLUGIN_ID", "\007");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_PLUGIN_ID), "RotS");

    feed_msdp_command(&context.descriptor, "PLUGIN_ID", std::string(48, 'A'));
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_PLUGIN_ID),
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");

    feed_msdp_command(&context.descriptor, "CLIENT_ID", "Mudlet");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "Mudlet");
    feed_msdp_command(&context.descriptor, "CLIENT_ID", "TinTin");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "Mudlet");
}

TEST(MSDPProtocol, UnknownCommandsAndVariablesDoNotChangeKnownState)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    context.descriptor.pProtocol->variables.mark_clean(eMSDP_HEALTH);
    feed_msdp_command(&context.descriptor, "REPORT", "NOT_A_VARIABLE");
    feed_msdp_command(&context.descriptor, "NOT_A_COMMAND", "HEALTH");

    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, IgnoresMalformedAndOversizedSubnegotiationInput)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    feed_msdp_command(&context.descriptor, "REPORT", "");
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));

    std::string oversized_value = "HEALTH";
    oversized_value.append(120, 'X');
    feed_msdp_command(&context.descriptor, "REPORT", oversized_value);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));

    std::string truncated = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));
    truncated.resize(truncated.size() - 2);
    feed_protocol_input(&context.descriptor, truncated, output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_EQ(context.read_output(), "");

    std::string terminator;
    terminator.push_back(static_cast<char>(IAC));
    terminator.push_back(static_cast<char>(SE));
    feed_protocol_input(&context.descriptor, terminator, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
}

TEST(MSDPProtocol, IgnoresMalformedMarkerOrderWithoutUsingStalePairs)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);

    std::string value_without_variable;
    value_without_variable.push_back(static_cast<char>(MSDP_VAL));
    value_without_variable += "HEALTH";
    char output[MAX_INPUT_LENGTH] = { '\0' };
    feed_protocol_input(&context.descriptor, msdp_packet(value_without_variable), output);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));

    output[0] = '\0';
    std::string repeated_variable;
    repeated_variable.push_back(static_cast<char>(MSDP_VAR));
    repeated_variable += "REPORT";
    repeated_variable.push_back(static_cast<char>(MSDP_VAR));
    repeated_variable += "UNREPORT";
    repeated_variable.push_back(static_cast<char>(MSDP_VAL));
    repeated_variable += "HEALTH";
    feed_protocol_input(&context.descriptor, msdp_packet(repeated_variable), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, CompletesSplitMSDPSubnegotiationAcrossProtocolInputCalls)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string packet = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    feed_protocol_input(&context.descriptor, packet.substr(0, 5), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));

    feed_protocol_input(&context.descriptor, packet.substr(5), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, CompletesMSDPSubnegotiationSplitBetweenIacAndSe)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string packet = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));

    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    feed_protocol_input(&context.descriptor, packet.substr(0, packet.size() - 1), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_EQ(context.descriptor.pProtocol->pending_input_length, 1);
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));

    output[0] = '\0';
    feed_protocol_input(&context.descriptor, packet.substr(packet.size() - 1), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_EQ(context.descriptor.pProtocol->pending_input_length, 0);
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, RecoversAfterOversizedUnterminatedSubnegotiation)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string oversized;
    oversized.push_back(static_cast<char>(IAC));
    oversized.push_back(static_cast<char>(SB));
    oversized.push_back(static_cast<char>(TELOPT_MSDP));
    oversized.append(MAX_PROTOCOL_BUFFER, 'X');

    feed_protocol_input(&context.descriptor, oversized, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_EQ(context.descriptor.pProtocol->subnegotiation_input_length, 0);

    std::string terminator;
    terminator.push_back(static_cast<char>(IAC));
    terminator.push_back(static_cast<char>(SE));
    output[0] = '\0';
    feed_protocol_input(&context.descriptor, terminator, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
    EXPECT_EQ(context.descriptor.pProtocol->subnegotiation_input_length, 0);

    output[0] = '\0';
    feed_protocol_input(&context.descriptor, "look", output);

    EXPECT_STREQ(output, "look");
}

TEST(MSDPProtocol, MsdpUpdateSkipsInvalidDescriptorsWithoutStoppingList)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;

    descriptor_data no_character {};
    ProtocolDescriptor npc_context;
    ProtocolDescriptor nowhere_context;
    ProtocolDescriptor valid_context;
    descriptor_data missing_protocol {};
    char_data missing_protocol_character {};
    MsdpCharacterRelease missing_protocol_character_release(&missing_protocol_character);

    clear_char(&missing_protocol_character, MOB_VOID);
    missing_protocol_character.player.name = strdup("NoProtocol");
    missing_protocol_character.in_room = 0;
    missing_protocol.character = &missing_protocol_character;

    initialize_msdp_player(&npc_context.character, "IgnoredNpc");
    SET_BIT(npc_context.character.specials2.act, MOB_ISNPC);
    npc_context.character.player.short_descr = strdup("ignored npc");

    initialize_msdp_player(&nowhere_context.character, "Nowhere");
    nowhere_context.character.in_room = NOWHERE;

    initialize_msdp_player(&valid_context.character, "Updated");
    enable_msdp_reports(valid_context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME, eMSDP_HEALTH });

    no_character.next = &npc_context.descriptor;
    npc_context.descriptor.next = &missing_protocol;
    missing_protocol.next = &nowhere_context.descriptor;
    nowhere_context.descriptor.next = &valid_context.descriptor;
    descriptor_list = &no_character;

    msdp_update();

    EXPECT_EQ(valid_context.descriptor.pProtocol->variables.text(eMSDP_CHARACTER_NAME),
        "Updated");
    EXPECT_EQ(valid_context.descriptor.pProtocol->variables.number(eMSDP_HEALTH), 125);
    EXPECT_EQ(valid_context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "Updated") + expected_msdp_pair("HEALTH", "125"));
    EXPECT_EQ(npc_context.read_output(), "");
    EXPECT_EQ(nowhere_context.read_output(), "");
    EXPECT_FALSE(valid_context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
    EXPECT_FALSE(valid_context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, MsdpUpdateSkipsOutOfRangeRoomsWithoutStoppingList)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor negative_room_context;
    ProtocolDescriptor high_room_context;
    ProtocolDescriptor valid_context;

    initialize_msdp_player(&negative_room_context.character, "NegativeRoom", -2);
    initialize_msdp_player(&high_room_context.character, "HighRoom", top_of_world + 1);
    initialize_msdp_player(&valid_context.character, "StillUpdated");

    enable_msdp_reports(negative_room_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMSDP_WEATHER });
    enable_msdp_reports(high_room_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMSDP_WEATHER });
    enable_msdp_reports(valid_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM });

    MSDPSetString(&negative_room_context.descriptor, eMSDP_ROOM_NAME, "stale negative room");
    MSDPSetString(&negative_room_context.descriptor, eMSDP_WEATHER, "stale negative weather");
    MSDPSetString(&high_room_context.descriptor, eMSDP_ROOM_NAME, "stale high room");
    MSDPSetString(&high_room_context.descriptor, eMSDP_WEATHER, "stale high weather");

    negative_room_context.descriptor.next = &high_room_context.descriptor;
    high_room_context.descriptor.next = &valid_context.descriptor;
    descriptor_list = &negative_room_context.descriptor;

    msdp_update();

    EXPECT_EQ(negative_room_context.read_output(), "");
    EXPECT_EQ(high_room_context.read_output(), "");
    EXPECT_EQ(negative_room_context.descriptor.pProtocol->variables.text(eMSDP_ROOM_NAME),
        "stale negative room");
    EXPECT_EQ(negative_room_context.descriptor.pProtocol->variables.text(eMSDP_WEATHER),
        "stale negative weather");
    EXPECT_EQ(negative_room_context.descriptor.pProtocol->variables.text(eMSDP_CHARACTER_NAME),
        "");
    EXPECT_EQ(high_room_context.descriptor.pProtocol->variables.text(eMSDP_ROOM_NAME),
        "stale high room");
    EXPECT_EQ(high_room_context.descriptor.pProtocol->variables.text(eMSDP_WEATHER),
        "stale high weather");
    EXPECT_EQ(high_room_context.descriptor.pProtocol->variables.text(eMSDP_CHARACTER_NAME),
        "");
    EXPECT_EQ(valid_context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "StillUpdated")
            + expected_msdp_pair("ROOM_NAME", "MSDP Test Room")
            + expected_msdp_pair("ROOM_VNUM", "3001"));
}

TEST(MSDPProtocol, MsdpUpdateEmitsMinimalPlayerState)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_LEVEL, eMSDP_HEALTH, eMSDP_HEALTH_MAX,
            eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM, eMSDP_RACE, eMSDP_TACTIC, eMSDP_SPIRIT });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.text(eMSDP_CHARACTER_NAME), "Aragorn");
    EXPECT_EQ(protocol->variables.number(eMSDP_LEVEL), 10);
    EXPECT_EQ(protocol->variables.number(eMSDP_HEALTH), 125);
    EXPECT_EQ(protocol->variables.number(eMSDP_HEALTH_MAX), 150);
    EXPECT_EQ(protocol->variables.text(eMSDP_ROOM_NAME), "MSDP Test Room");
    EXPECT_EQ(protocol->variables.number(eMSDP_ROOM_VNUM), 3001);
    EXPECT_EQ(protocol->variables.text(eMSDP_RACE), pc_races[RACE_HUMAN]);
    EXPECT_EQ(protocol->variables.text(eMSDP_TACTIC), "normal");
    EXPECT_EQ(protocol->variables.number(eMSDP_SPIRIT), 7);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "Aragorn") + expected_msdp_pair("HEALTH", "125")
            + expected_msdp_pair("HEALTH_MAX", "150") + expected_msdp_pair("LEVEL", "10")
            + expected_msdp_pair("RACE", pc_races[RACE_HUMAN])
            + expected_msdp_pair("SPIRIT", "7") + expected_msdp_pair("TACTIC", "normal")
            + expected_msdp_pair("ROOM_NAME", "MSDP Test Room")
            + expected_msdp_pair("ROOM_VNUM", "3001"));
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_ROOM_NAME));
}

TEST(MSDPProtocol, MsdpUpdateEmitsZeroValuedStatsOnFirstUpdate)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    /* A character with no spell-pen/power gear and nothing carried. Without the
       priming in ProtocolCreate these variables never reach the client at all,
       because MSDPSetNumber only marks a variable dirty when its value changes
       and they are already 0. */
    initialize_msdp_player(&context.character, "Aragorn");
    context.character.points.spell_pen = 0;
    context.character.points.spell_power = 0;
    context.character.specials.carry_weight = 0;
    descriptor_list = &context.descriptor;

    msdp_update();

    const std::string output = context.read_output();
    EXPECT_NE(output.find(expected_msdp_pair("SPELL_PEN", "0")), std::string::npos);
    EXPECT_NE(output.find(expected_msdp_pair("SPELL_POWER", "0")), std::string::npos);
    EXPECT_NE(output.find(expected_msdp_pair("CARRIED_WEIGHT", "0")), std::string::npos);

    /* Client-owned configuration variables stay out of the primed burst. */
    EXPECT_EQ(output.find(expected_msdp_pair("ANSI_COLORS", "1")), std::string::npos);
    EXPECT_EQ(output.find(expected_msdp_pair("SOUND", "0")), std::string::npos);

    /* Priming is a one-shot: an unchanged value is not resent. */
    msdp_update();
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, MsdpUpdateEmitsCarriedWeightProfessionLevelsAndSpecialization)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    context.character.specials.carry_weight = 2550;
    context.character.profs->specialization = game_types::PS_Archery;
    /* square_root[n] is 100*sqrt(n), so these give coefficients of 1000, 700,
       400 and 500 -- i.e. max profession levels of 30, 21, 12 and 15. */
    context.character.profs->prof_coof[PROF_WARRIOR] = 100;
    context.character.profs->prof_coof[PROF_RANGER] = 49;
    context.character.profs->prof_coof[PROF_CLERIC] = 16;
    context.character.profs->prof_coof[PROF_MAGE] = 25;

    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_CARRIED_WEIGHT, eMSDP_SPECIALIZATION, eMSDP_WARRIOR_LEVEL,
            eMSDP_WARRIOR_LEVEL_MAX, eMSDP_RANGER_LEVEL, eMSDP_RANGER_LEVEL_MAX,
            eMSDP_MYSTIC_LEVEL, eMSDP_MYSTIC_LEVEL_MAX, eMSDP_MAGE_LEVEL,
            eMSDP_MAGE_LEVEL_MAX });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.number(eMSDP_CARRIED_WEIGHT), 2550);
    EXPECT_EQ(protocol->variables.text(eMSDP_SPECIALIZATION), "archery");
    EXPECT_EQ(protocol->variables.number(eMSDP_WARRIOR_LEVEL), 5);
    EXPECT_EQ(protocol->variables.number(eMSDP_WARRIOR_LEVEL_MAX), 30);
    EXPECT_EQ(protocol->variables.number(eMSDP_RANGER_LEVEL), 2);
    EXPECT_EQ(protocol->variables.number(eMSDP_RANGER_LEVEL_MAX), 21);
    EXPECT_EQ(protocol->variables.number(eMSDP_MYSTIC_LEVEL), 3);
    EXPECT_EQ(protocol->variables.number(eMSDP_MYSTIC_LEVEL_MAX), 12);
    EXPECT_EQ(protocol->variables.number(eMSDP_MAGE_LEVEL), 4);
    EXPECT_EQ(protocol->variables.number(eMSDP_MAGE_LEVEL_MAX), 15);
}

TEST(MSDPProtocol, MsdpUpdateClampsNegativeProfessionMaximumForUrukWithoutMagePoints)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    /* GET_PROF_COOF applies a flat -100 mage penalty to Uruks with no floor at
       zero, so an Uruk that spent no points on mage has a negative coefficient. */
    initialize_msdp_player(&context.character, "Uglook");
    context.character.player.race = RACE_URUK;
    context.character.profs->prof_coof[PROF_MAGE] = 0;
    context.character.profs->prof_coof[PROF_WARRIOR] = 100;
    enable_msdp_reports(
        context.descriptor.pProtocol, { eMSDP_MAGE_LEVEL_MAX, eMSDP_WARRIOR_LEVEL_MAX });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.number(eMSDP_MAGE_LEVEL_MAX), 0);
    EXPECT_EQ(protocol->variables.number(eMSDP_WARRIOR_LEVEL_MAX), 30);
}

TEST(MSDPProtocol, MsdpUpdateReportsNoSpecializationAsNothing)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    context.character.profs->specialization = game_types::PS_None;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_SPECIALIZATION });
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(
        context.descriptor.pProtocol->variables.text(eMSDP_SPECIALIZATION), "nothing");
}

TEST(MSDPProtocol, MsdpUpdateEmitsBroadCharacterStats)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    const std::vector<variable_t> variables = {
        eMSDP_ALIGNMENT, eMSDP_EXPERIENCE, eMSDP_EXPERIENCE_MAX, eMSDP_MANA,
        eMSDP_MANA_MAX, eMSDP_WIMPY, eMSDP_MONEY, eMSDP_MOVEMENT, eMSDP_MOVEMENT_MAX,
        eMSDP_STR, eMSDP_INT, eMSDP_WILL, eMSDP_DEX, eMSDP_CON, eMSDP_LEA,
        eMSDP_STR_PERM, eMSDP_INT_PERM, eMSDP_WIL_PERM, eMSDP_DEX_PERM,
        eMSDP_CON_PERM, eMSDP_LEA_PERM, eMSDP_SPELL_SAVE, eMSDP_SPELL_PEN,
        eMSDP_SPELL_POWER, eMSDP_ARMOUR_ABS, eMSDP_OFFENSIVE_BONUS, eMSDP_PARRY,
        eMSDP_DODGE, eMSDP_ATTACK_SPEED, eMSDP_PERCEPTION, eMSDP_WILLPOWER,
        eMSDP_SKILL_ENCUMBRANCE, eMSDP_MOVEMENT_ENCUMBRANCE, eMSDP_HEALTH_REGENERATION,
        eMSDP_STAMINA_REGENERATION, eMSDP_MOVEMENT_REGENERATION
    };

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, variables);
    seed_msdp_numbers(&context.descriptor, variables, -999);
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    const int next_level_xp = xp_to_level(GET_LEVEL(&context.character) + 1);
    const int current_level_xp = xp_to_level(GET_LEVEL(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_ALIGNMENT), 250);
    EXPECT_EQ(protocol->variables.number(eMSDP_EXPERIENCE_MAX),
        next_level_xp - current_level_xp);
    EXPECT_EQ(protocol->variables.number(eMSDP_EXPERIENCE),
        next_level_xp - GET_EXP(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_MANA), 90);
    EXPECT_EQ(protocol->variables.number(eMSDP_MANA_MAX), 110);
    EXPECT_EQ(protocol->variables.number(eMSDP_MOVEMENT), 80);
    EXPECT_EQ(protocol->variables.number(eMSDP_MOVEMENT_MAX), 95);
    EXPECT_EQ(protocol->variables.number(eMSDP_MONEY), 543);
    EXPECT_EQ(protocol->variables.number(eMSDP_STR), 14);
    EXPECT_EQ(protocol->variables.number(eMSDP_INT), 13);
    EXPECT_EQ(protocol->variables.number(eMSDP_WILL), 11);
    EXPECT_EQ(protocol->variables.number(eMSDP_DEX), 15);
    EXPECT_EQ(protocol->variables.number(eMSDP_CON), 16);
    EXPECT_EQ(protocol->variables.number(eMSDP_LEA), 8);
    EXPECT_EQ(protocol->variables.number(eMSDP_STR_PERM), 16);
    EXPECT_EQ(protocol->variables.number(eMSDP_INT_PERM), 15);
    EXPECT_EQ(protocol->variables.number(eMSDP_WIL_PERM), 12);
    EXPECT_EQ(protocol->variables.number(eMSDP_DEX_PERM), 17);
    EXPECT_EQ(protocol->variables.number(eMSDP_CON_PERM), 18);
    EXPECT_EQ(protocol->variables.number(eMSDP_LEA_PERM), 9);
    EXPECT_EQ(protocol->variables.number(eMSDP_WIMPY), 33);
    EXPECT_EQ(protocol->variables.number(eMSDP_SPELL_SAVE), -8);
    EXPECT_EQ(protocol->variables.number(eMSDP_SPELL_PEN), 5);
    EXPECT_EQ(protocol->variables.number(eMSDP_SPELL_POWER), 6);
    EXPECT_EQ(protocol->variables.number(eMSDP_ARMOUR_ABS),
        get_percent_absorb(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_OFFENSIVE_BONUS),
        get_real_OB(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_PARRY), get_real_parry(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_DODGE), get_real_dodge(&context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_ATTACK_SPEED),
        utils::get_energy_regen(context.character) / 5);
    EXPECT_EQ(protocol->variables.number(eMSDP_PERCEPTION), 66);
    EXPECT_EQ(protocol->variables.number(eMSDP_WILLPOWER), 44);
    EXPECT_EQ(protocol->variables.number(eMSDP_SKILL_ENCUMBRANCE),
        utils::get_encumbrance(context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_MOVEMENT_ENCUMBRANCE),
        utils::get_leg_encumbrance(context.character));
    EXPECT_EQ(protocol->variables.number(eMSDP_HEALTH_REGENERATION),
        static_cast<int>(hit_gain(&context.character)));
    EXPECT_EQ(protocol->variables.number(eMSDP_STAMINA_REGENERATION),
        static_cast<int>(mana_gain(&context.character)));
    EXPECT_EQ(protocol->variables.number(eMSDP_MOVEMENT_REGENERATION),
        static_cast<int>(move_gain(&context.character)));

    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("ALIGNMENT", "250")
            + expected_msdp_pair("EXPERIENCE",
                std::to_string(next_level_xp - GET_EXP(&context.character)))
            + expected_msdp_pair("EXPERIENCE_MAX",
                std::to_string(next_level_xp - current_level_xp))
            + expected_msdp_pair("MANA", "90") + expected_msdp_pair("MANA_MAX", "110")
            + expected_msdp_pair("WIMPY", "33") + expected_msdp_pair("MONEY", "543")
            + expected_msdp_pair("MOVEMENT", "80")
            + expected_msdp_pair("MOVEMENT_MAX", "95") + expected_msdp_pair("STR", "14")
            + expected_msdp_pair("INT", "13") + expected_msdp_pair("WILL", "11")
            + expected_msdp_pair("DEX", "15") + expected_msdp_pair("CON", "16")
            + expected_msdp_pair("LEA", "8") + expected_msdp_pair("STR_PERM", "16")
            + expected_msdp_pair("INT_PERM", "15") + expected_msdp_pair("WIL_PERM", "12")
            + expected_msdp_pair("DEX_PERM", "17") + expected_msdp_pair("CON_PERM", "18")
            + expected_msdp_pair("LEA_PERM", "9") + expected_msdp_pair("SPELL_SAVE", "-8")
            + expected_msdp_pair("SPELL_PEN", "5") + expected_msdp_pair("SPELL_POWER", "6")
            + expected_msdp_pair("ARMOUR_ABS",
                std::to_string(get_percent_absorb(&context.character)))
            + expected_msdp_pair("OFFENSIVE_BONUS", std::to_string(get_real_OB(&context.character)))
            + expected_msdp_pair("PARRY", std::to_string(get_real_parry(&context.character)))
            + expected_msdp_pair("DODGE", std::to_string(get_real_dodge(&context.character)))
            + expected_msdp_pair("ATTACK_SPEED",
                std::to_string(utils::get_energy_regen(context.character) / 5))
            + expected_msdp_pair("PERCEPTION", "66") + expected_msdp_pair("WILLPOWER", "44")
            + expected_msdp_pair("SKILL_ENCUMBRANCE",
                std::to_string(utils::get_encumbrance(context.character)))
            + expected_msdp_pair("MOVEMENT_ENCUMBRANCE",
                std::to_string(utils::get_leg_encumbrance(context.character)))
            + expected_msdp_pair("HEALTH_REGENERATION",
                std::to_string(static_cast<int>(hit_gain(&context.character))))
            + expected_msdp_pair("STAMINA_REGENERATION",
                std::to_string(static_cast<int>(mana_gain(&context.character))))
            + expected_msdp_pair("MOVEMENT_REGENERATION",
                std::to_string(static_cast<int>(move_gain(&context.character)))));
    for (variable_t variable : variables)
        EXPECT_FALSE(protocol->variables.is_dirty(variable));
}

TEST(MSDPProtocol, MsdpUpdateEmitsIndoorAndOutdoorWeather)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor indoor_context;
    ProtocolDescriptor outdoor_context;

    initialize_msdp_player(&indoor_context.character, "Indoor");
    enable_msdp_reports(indoor_context.descriptor.pProtocol, { eMSDP_WEATHER });
    descriptor_list = &indoor_context.descriptor;

    msdp_update();

    EXPECT_EQ(indoor_context.descriptor.pProtocol->variables.text(eMSDP_WEATHER),
        "You can have no feeling about the weather here.");
    EXPECT_EQ(indoor_context.read_output(),
        expected_msdp_pair("WEATHER", "You can have no feeling about the weather here."));
    EXPECT_FALSE(indoor_context.descriptor.pProtocol->variables.is_dirty(eMSDP_WEATHER));

    world[0].room_flags = 0;
    world[0].sector_type = SECT_FIELD;
    ScopedSectorWeather field_weather(SECT_FIELD, SKY_CLOUDLESS);
    initialize_msdp_player(&outdoor_context.character, "Outdoor");
    enable_msdp_reports(outdoor_context.descriptor.pProtocol, { eMSDP_WEATHER });
    descriptor_list = &outdoor_context.descriptor;

    msdp_update();

    EXPECT_EQ(outdoor_context.descriptor.pProtocol->variables.text(eMSDP_WEATHER),
        "Above the fields, not a cloud can be seen in the sky.");
    EXPECT_EQ(outdoor_context.read_output(),
        expected_msdp_pair("WEATHER", "Above the fields, not a cloud can be seen in the sky."));
    EXPECT_FALSE(outdoor_context.descriptor.pProtocol->variables.is_dirty(eMSDP_WEATHER));
}

TEST(MSDPProtocol, MsdpUpdateClearsGroupWhenUngrouped)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_GROUP });
    MSDPSetTable(&context.descriptor, eMSDP_GROUP,
        expected_group_table({ expected_group_member_table("stale member", 75, 60, 50) }).c_str());
    descriptor_list = &context.descriptor;

    msdp_update();

    const std::string expected_group = expected_group_table({});
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_GROUP),
        msdp_table_value(expected_group).c_str());
    EXPECT_EQ(context.read_output(), expected_msdp_table_pair("GROUP", expected_group));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_GROUP));
}

TEST(MSDPProtocol, MsdpUpdateGivesEachPlayerOnlyTheirOwnGroup)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    // Neither player has MSDP turned on, so the update stores each value without sending it.
    MsdpSetterDescriptor grouped_context;
    MsdpSetterDescriptor ungrouped_context;
    char_data grouped_player {};
    MsdpCharacterRelease grouped_player_release(&grouped_player);
    char_data group_member {};
    MsdpCharacterRelease group_member_release(&group_member);
    char_data ungrouped_player {};
    MsdpCharacterRelease ungrouped_player_release(&ungrouped_player);

    initialize_msdp_player(&grouped_player, "Aragorn");
    initialize_msdp_player(&group_member, "Boromir");
    initialize_msdp_player(&ungrouped_player, "Faramir");
    group_data group(&grouped_player);
    group.add_member(&group_member);

    grouped_context.descriptor.character = &grouped_player;
    ungrouped_context.descriptor.character = &ungrouped_player;
    grouped_context.descriptor.next = &ungrouped_context.descriptor;
    descriptor_list = &grouped_context.descriptor;

    msdp_update();

    // The health, mana and movement percentages that initialize_msdp_player's stats give.
    const std::string expected_group = expected_group_table({
        expected_group_member_table("Aragorn", 83, 81, 84),
        expected_group_member_table("Boromir", 83, 81, 84),
    });
    EXPECT_EQ(grouped_context.variables().text(eMSDP_GROUP),
        msdp_table_value(expected_group).c_str());
    EXPECT_EQ(ungrouped_context.variables().text(eMSDP_GROUP),
        msdp_table_value(expected_group_table({})).c_str());
}

TEST(MSDPProtocol, MsdpUpdateEmitsAllGroupMembersWithPercentages)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data player_member {};
    MsdpCharacterRelease player_member_release(&player_member);
    char_data npc_member {};
    MsdpCharacterRelease npc_member_release(&npc_member);

    initialize_msdp_player(&context.character, "Aragorn");
    initialize_msdp_player(&player_member, "Boromir");
    clear_char(&npc_member, MOB_ISNPC);
    SET_BIT(npc_member.specials2.act, MOB_ISNPC);
    npc_member.player.short_descr = strdup("orc \"guard\"\n");

    context.character.abilities.hit = 200;
    context.character.tmpabilities.hit = 150;
    context.character.abilities.mana = 100;
    context.character.tmpabilities.mana = 50;
    context.character.abilities.move = 80;
    context.character.tmpabilities.move = 20;

    player_member.abilities.hit = 90;
    player_member.tmpabilities.hit = 45;
    player_member.abilities.mana = 120;
    player_member.tmpabilities.mana = 90;
    player_member.abilities.move = 100;
    player_member.tmpabilities.move = 100;

    npc_member.abilities.hit = 40;
    npc_member.tmpabilities.hit = 10;
    npc_member.abilities.mana = 0;
    npc_member.tmpabilities.mana = 10;
    npc_member.abilities.move = 20;
    npc_member.tmpabilities.move = -5;

    group_data group(&context.character);
    group.add_member(&player_member);
    group.add_member(&npc_member);

    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_GROUP });
    descriptor_list = &context.descriptor;

    msdp_update();

    const std::string expected_group = expected_group_table({
        expected_group_member_table("Aragorn", 75, 50, 25),
        expected_group_member_table("Boromir", 50, 75, 100),
        expected_group_member_table("orc \\\"guard\\\"\\n", 25, 0, 0),
    });
    EXPECT_EQ(context.read_output(), expected_msdp_table_pair("GROUP", expected_group));
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_GROUP),
        msdp_table_value(expected_group).c_str());
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_GROUP));

    msdp_update();

    EXPECT_EQ(context.read_output(), "");

    player_member.tmpabilities.mana = 30;
    msdp_update();

    const std::string updated_group = expected_group_table({
        expected_group_member_table("Aragorn", 75, 50, 25),
        expected_group_member_table("Boromir", 50, 25, 100),
        expected_group_member_table("orc \\\"guard\\\"\\n", 25, 0, 0),
    });
    EXPECT_EQ(context.read_output(), expected_msdp_table_pair("GROUP", updated_group));
}

TEST(MSDPProtocol, MsdpUpdateClearsOpponentFieldsWhenNotFighting)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL, eMSDP_OPPONENT_AGE });
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH, 88);
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_NAME, "stale opponent");
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_LEVEL, "44");
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_AGE, "Stale has been here for a little while.");
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.number(eMSDP_OPPONENT_HEALTH), 0);
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_NAME), "");
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_LEVEL), "");
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_AGE), "");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "0") + expected_msdp_pair("OPPONENT_LEVEL", "")
            + expected_msdp_pair("OPPONENT_NAME", "") + expected_msdp_pair("OPPONENT_AGE", ""));
}

TEST(MSDPProtocol, MsdpUpdatePublishesNothingForDescriptorAtCharacterMenu)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM });

    /* A descriptor parked at the character-selection menu holds a character that
       has been loaded but not entered, and load_character() (objsave.cpp) leaves
       the player file's load_room *vnum* in in_room at that stage. Indexing
       world[] with it would publish a real but completely unrelated room. */
    context.descriptor.connected = CON_SLCT;
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.read_output(), "");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_ROOM_NAME), "");
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_ROOM_VNUM), 0);
}

TEST(MSDPProtocol, MsdpUpdatePublishesOnceDescriptorIsPlaying)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_ROOM_VNUM });
    context.descriptor.connected = CON_PLYNG;
    descriptor_list = &context.descriptor;

    msdp_update();

    /* Same descriptor, now in the game: the real room is published normally. */
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_ROOM_VNUM),
        world[context.character.in_room].number);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("ROOM_VNUM", std::to_string(world[context.character.in_room].number)));
}

TEST(MSDPProtocol, RoomUpdateOnEnteringGameSendsNoPlaceholderCharacterValues)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    /* The enter-game and reconnect paths call msdp_room_update() on a fresh protocol
       before msdp_update() has computed any character value. */
    initialize_msdp_player(&context.character, "Aragorn");
    context.character.points.spell_pen = 0;
    context.character.desc = &context.descriptor;
    context.descriptor.connected = CON_PLYNG;
    descriptor_list = &context.descriptor;

    msdp_room_update(&context.character);

    const std::string room_output = context.read_output();
    EXPECT_NE(room_output.find(expected_msdp_pair("ROOM_NAME", "MSDP Test Room")), std::string::npos);
    EXPECT_NE(room_output.find(expected_msdp_pair("ROOM_VNUM", "3001")), std::string::npos);
    EXPECT_EQ(room_output.find(expected_msdp_pair("CHARACTER_NAME", "")), std::string::npos);
    for (const char* variable_name : { "LEVEL", "HEALTH", "STR", "WILL", "WIL_PERM", "WILLPOWER",
             "WARRIOR_LEVEL", "WARRIOR_LEVEL_MAX", "MAGE_LEVEL" }) {
        EXPECT_EQ(room_output.find(expected_msdp_pair(variable_name, "0")), std::string::npos)
            << variable_name << " was sent as 0 before its real value was known.";
    }

    msdp_update();

    const std::string update_output = context.read_output();
    EXPECT_NE(update_output.find(expected_msdp_pair("CHARACTER_NAME", "Aragorn")), std::string::npos);
    EXPECT_NE(update_output.find(expected_msdp_pair("LEVEL", "10")), std::string::npos);
    EXPECT_NE(update_output.find(expected_msdp_pair("WARRIOR_LEVEL", "5")), std::string::npos);
    EXPECT_NE(update_output.find(expected_msdp_pair("SPELL_PEN", "0")), std::string::npos)
        << "A value that really is 0 must still reach the client once.";
}

namespace {

// VAR name VAL value, one entry of an MSDP table.
std::string msdp_table_entry(const std::string& name, const std::string& value)
{
    return msdp_pair_payload(name, value);
}

// An MSDP array value holding values.
std::string msdp_array_value(const std::vector<std::string>& values)
{
    std::string array;
    array.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    for (const std::string& value : values) {
        array.push_back(static_cast<char>(MSDP_VAL));
        array += value;
    }
    array.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    return array;
}

// The name ScopedExitTestRooms gives its hall, which needs no escaping.
constexpr const char* HALL_NAME = "Exit Test Hall";

// The ROOM table ScopedExitTestRooms' hall is sent, with exits_value as its EXITS entry and
// sent_name as its NAME.
std::string hall_room_table(const std::string& exits_value, const std::string& sent_name = HALL_NAME)
{
    return msdp_table_entry("VNUM", std::to_string(ScopedExitTestRooms::HALL_VNUM))
        + msdp_table_entry("NAME", sent_name) + msdp_table_entry("EXITS", exits_value)
        + msdp_table_entry("TERRAIN", sector_types[SECT_INSIDE]);
}

// The vnum ScopedExitTestRooms gives the room its hall's exit in direction leads to.
std::string destination_vnum(int direction)
{
    const int destination = ScopedExitTestRooms::destination(direction);
    return std::to_string(ScopedExitTestRooms::vnum_of(destination));
}

// The directions of the hall's exits a mortal sees: the open, closed, broken and opened hidden
// ones.
constexpr int MORTAL_HALL_EXITS[] = { NORTH, EAST, UP, DOWN };

// MSDP's direction letters, north to down, as the server sends them.
constexpr std::string_view MSDP_LETTERS = "neswud";

// MSDP's letter for direction.
std::string msdp_letter(int direction)
{
    return std::string(1, MSDP_LETTERS[direction]);
}

// The vnums of the hall's exits a mortal sees, in direction order.
std::vector<std::string> mortal_hall_exit_vnums()
{
    std::vector<std::string> vnums;
    vnums.reserve(std::size(MORTAL_HALL_EXITS));
    for (int direction : MORTAL_HALL_EXITS) {
        vnums.push_back(destination_vnum(direction));
    }
    return vnums;
}

// The EXITS table of the hall's exits a mortal sees.
std::string mortal_hall_exits_table()
{
    std::string table;
    for (int direction : MORTAL_HALL_EXITS) {
        table += msdp_table_entry(msdp_letter(direction), destination_vnum(direction));
    }
    return msdp_table_value(table);
}

// A player standing in ScopedExitTestRooms' hall whose connection reports the four room
// variables.
class MsdpRoomUpdateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        initialize_msdp_player(&m_context.character, "Mapper", ScopedExitTestRooms::HALL);
        m_context.character.desc = &m_context.descriptor;
        m_context.descriptor.connected = CON_PLYNG;
        enable_msdp_reports(m_context.descriptor.pProtocol,
            { eMSDP_ROOM, eMSDP_ROOM_EXITS, eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM });
    }

    // The frames after the exits: the ROOM table with exits_value, then ROOM_NAME and ROOM_VNUM,
    // with sent_name as the room's name in both.
    static std::string expected_room_frames(
        const std::string& exits_value, const std::string& sent_name = HALL_NAME)
    {
        return expected_msdp_table_pair("ROOM", hall_room_table(exits_value, sent_name))
            + expected_msdp_pair("ROOM_NAME", sent_name)
            + expected_msdp_pair("ROOM_VNUM", std::to_string(ScopedExitTestRooms::HALL_VNUM));
    }

    // Empties descriptor_list for the test and restores it after.
    ScopedDescriptorList m_descriptor_list_scope;
    // The hall and its exits.
    ScopedExitTestRooms m_rooms;
    // The player and the connection whose output the tests read.
    ProtocolDescriptor m_context;
};

} // namespace

TEST_F(MsdpRoomUpdateTest, SendsTheExitsTheRoomLineShowsAsATableOfDirectionToRoomNumber)
{
    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(),
        expected_msdp_array_pair("ROOM_EXITS", { "n", "e", "u", "d" })
            + expected_room_frames(mortal_hall_exits_table()));
}

TEST_F(MsdpRoomUpdateTest, WithLegacyExitsSetTheExitsAreAnArrayOfRoomNumbersInRoomExitsOrder)
{
    SET_BIT(PRF_FLAGS(&m_context.character), PRF_LEGACY_EXITS);

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(),
        expected_msdp_array_pair("ROOM_EXITS", { "n", "e", "u", "d" })
            + expected_room_frames(msdp_array_value(mortal_hall_exit_vnums())));
}

TEST_F(MsdpRoomUpdateTest, AGodIsSentTheHiddenDoorsAndWindowsItSees)
{
    m_context.character.player.level = LEVEL_GOD;

    msdp_room_update(&m_context.character);

    std::string exits_table;
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        exits_table += msdp_table_entry(msdp_letter(direction), destination_vnum(direction));
    }
    EXPECT_EQ(m_context.read_output(),
        expected_msdp_array_pair("ROOM_EXITS", { "n", "e", "s", "w", "u", "d" })
            + expected_room_frames(msdp_table_value(exits_table)));
}

TEST_F(MsdpRoomUpdateTest, InTheDarkTheRoomNameAndNumberAreSentWithNoExits)
{
    room_data& hall = world[ScopedExitTestRooms::HALL];
    hall.light = 0;
    hall.room_flags = INDOORS | DARK;

    msdp_room_update(&m_context.character);

    // ROOM_EXITS starts empty, so an empty list is no change and is not sent.
    EXPECT_EQ(m_context.read_output(), expected_room_frames(msdp_table_value("")));
}

TEST_F(MsdpRoomUpdateTest, GoingBlindEmptiesTheExitsAndLeavesTheNameAndNumberAsSent)
{
    msdp_room_update(&m_context.character);
    m_context.read_output();
    SET_BIT(m_context.character.specials.affected_by, AFF_BLIND);

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(),
        expected_msdp_pair("ROOM_EXITS", "")
            + expected_msdp_table_pair("ROOM", hall_room_table(msdp_table_value(""))));
}

TEST_F(MsdpRoomUpdateTest, AsleepThePlayerIsSentNoExits)
{
    m_context.character.specials.position = POSITION_SLEEPING;

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(), expected_room_frames(msdp_table_value("")));
}

TEST_F(MsdpRoomUpdateTest, AnExitToRealRoomZeroIsSent)
{
    for (int direction = NORTH; direction <= DOWN; ++direction) {
        m_rooms.remove_exit(direction);
    }
    m_rooms.set_exit(NORTH, 0, 0);
    m_rooms.set_exit(EAST, 0, NOWHERE);

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(),
        expected_msdp_array_pair("ROOM_EXITS", { "n" })
            + expected_room_frames(
                msdp_table_value(msdp_table_entry("n", std::to_string(world[0].number)))));
}

TEST_F(MsdpRoomUpdateTest, SettingLegacyExitsResendsTheRoomAtOnce)
{
    m_context.descriptor.output = m_context.descriptor.small_outbuf;
    m_context.descriptor.small_outbuf[0] = '\0';
    m_context.descriptor.bufspace = SMALL_BUFSIZE - 1;
    msdp_room_update(&m_context.character);
    m_context.read_output();

    char on_argument[] = "on";
    do_gen_tog(&m_context.character, on_argument, nullptr, 0, SCMD_LEGACY_EXITS);

    EXPECT_TRUE(PRF_FLAGGED(&m_context.character, PRF_LEGACY_EXITS));
    EXPECT_EQ(m_context.read_output(),
        expected_msdp_table_pair(
            "ROOM", hall_room_table(msdp_array_value(mortal_hall_exit_vnums()))));
    const std::string sent_text(m_context.descriptor.output);
    EXPECT_NE(sent_text.find("list of room numbers"), std::string::npos);
}

TEST_F(MsdpRoomUpdateTest, EscapesQuotesAndBackslashesInTheRoomName)
{
    // ScopedExitTestRooms restores the hall's own name when the test ends.
    char quoted_name[] = "The \"Pony\" \\ Inn";
    world[ScopedExitTestRooms::HALL].name = quoted_name;
    const std::string escaped_name = "The \\\"Pony\\\" \\\\ Inn";

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(),
        expected_msdp_array_pair("ROOM_EXITS", { "n", "e", "u", "d" })
            + expected_room_frames(mortal_hall_exits_table(), escaped_name));
}

TEST_F(MsdpRoomUpdateTest, SendsNothingForAnNpc)
{
    SET_BIT(m_context.character.specials2.act, MOB_ISNPC);
    m_context.character.player.short_descr = strdup("mapping npc");

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(), "");
}

TEST_F(MsdpRoomUpdateTest, SendsNothingWithoutAConnection)
{
    m_context.character.desc = nullptr;

    msdp_room_update(&m_context.character);

    EXPECT_EQ(m_context.read_output(), "");
}

TEST_F(MsdpRoomUpdateTest, SendsNothingWhenTheConnectionHasNoProtocolState)
{
    descriptor_data no_protocol {};
    no_protocol.descriptor = m_context.descriptor.descriptor;
    no_protocol.character = &m_context.character;
    m_context.character.desc = &no_protocol;

    msdp_room_update(&m_context.character);

    m_context.character.desc = &m_context.descriptor;
    EXPECT_EQ(m_context.read_output(), "");
}

TEST_F(MsdpRoomUpdateTest, SendsNothingForARoomOutsideTheWorld)
{
    for (int out_of_range_room : { -1, top_of_world + 1 }) {
        m_context.character.in_room = out_of_range_room;

        msdp_room_update(&m_context.character);

        EXPECT_EQ(m_context.read_output(), "") << "in_room " << out_of_range_room;
    }
    m_context.character.in_room = ScopedExitTestRooms::HALL;
}

TEST(MSDPProtocol, MsdpUpdateSendsWorldTimeInTheFirstUpdateAfterEnteringTheGame)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ScopedGameClock clock_scope;
    ProtocolDescriptor context;

    time_info.hours = 23;
    game_time_text::refresh();
    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_WORLD_TIME });
    context.descriptor.connected = CON_PLYNG;
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.read_output(), expected_msdp_pair("WORLD_TIME", "It is about 11:00 PM"));
}

TEST(MSDPProtocol, MsdpUpdateSendsWorldTimeAgainOnlyWhenTheHourChanges)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ScopedGameClock clock_scope;
    ProtocolDescriptor context;

    /* Moon 0 rises at hour 1 and sets at 13, so the 22:00 to 23:00 step sends no moon text. */
    time_info.hours = 22;
    time_info.moon = 0;
    game_time_text::refresh();
    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_WORLD_TIME });
    context.descriptor.connected = CON_PLYNG;
    descriptor_list = &context.descriptor;

    msdp_update();
    EXPECT_EQ(context.read_output(), expected_msdp_pair("WORLD_TIME", "It is about 10:00 PM"));

    msdp_update();
    EXPECT_EQ(context.read_output(), "") << "An unchanged hour must not be sent again.";

    another_hour(0);
    msdp_update();
    EXPECT_EQ(context.read_output(), expected_msdp_pair("WORLD_TIME", "It is about 11:00 PM"));
}

TEST(MSDPProtocol, AnotherHourSendsNothingToADescriptorAtTheCharacterMenu)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ScopedGameClock clock_scope;
    ProtocolDescriptor context;

    time_info.hours = 22;
    time_info.moon = 0;
    game_time_text::refresh();
    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_WORLD_TIME });
    context.descriptor.connected = CON_SLCT;
    descriptor_list = &context.descriptor;

    another_hour(0);

    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, MsdpUpdateEmitsNpcOpponentDetails)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a snarling orc");
    opponent.player.level = 12;
    const WorldClock::time_point now = WorldClock::now();
    utils::set_mob_age_in_ticks(opponent, 0, now);
    opponent.abilities.hit = 200;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL, eMSDP_OPPONENT_AGE });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.number(eMSDP_OPPONENT_HEALTH), 25);
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_NAME), "a snarling orc");
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_LEVEL), "12");
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_AGE),
        "A snarling orc has just arrived to this place.");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25") + expected_msdp_pair("OPPONENT_LEVEL", "12")
            + expected_msdp_pair("OPPONENT_NAME", "a snarling orc")
            + expected_msdp_pair("OPPONENT_AGE", "A snarling orc has just arrived to this place."));
}

TEST(MSDPProtocol, MsdpUpdateUsesDiagnoseAgeBandsForNpcOpponent)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a snarling orc");
    opponent.abilities.hit = 200;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_OPPONENT_AGE });
    descriptor_list = &context.descriptor;

    const struct {
        int ticks;
        const char* expected;
    } cases[] = {
        { average_mob_life / 4, "A snarling orc has arrived but recently." },
        { average_mob_life * 3 / 4, "A snarling orc has been here for a little while." },
        { average_mob_life, "A snarling orc has been here for quite a while." },
        { average_mob_life * 3 / 2, "A snarling orc has been here for a long time already." },
        { average_mob_life * 2, "A snarling orc has been here for a very long time." },
    };
    for (const auto& c : cases) {
        const WorldClock::time_point now = WorldClock::now();
        utils::set_mob_age_in_ticks(opponent, c.ticks, now);
        msdp_update();
        EXPECT_EQ(
            context.descriptor.pProtocol->variables.text(eMSDP_OPPONENT_AGE), c.expected);
        EXPECT_EQ(context.read_output(), expected_msdp_pair("OPPONENT_AGE", c.expected));
    }
}

TEST(MSDPProtocol, MsdpUpdateHidesAgeOfOrcFriendPetOpponent)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    SET_BIT(opponent.specials2.act, MOB_ISNPC | MOB_ORC_FRIEND | MOB_PET);
    opponent.player.short_descr = strdup("a tame warg");
    opponent.abilities.hit = 200;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_OPPONENT_AGE });
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_AGE, "stale");
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_OPPONENT_AGE), "");
    EXPECT_EQ(context.read_output(), expected_msdp_pair("OPPONENT_AGE", ""));
}

TEST(MSDPProtocol, MsdpUpdateHandlesOpponentWithInvalidMaxHealth)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a wounded orc");
    opponent.player.level = 12;
    opponent.abilities.hit = 0;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_OPPONENT_HEALTH });
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH, 88);
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_OPPONENT_HEALTH), 0);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("OPPONENT_HEALTH", "0"));
}

TEST(MSDPProtocol, MsdpUpdateMasksPlayerOpponentDetails)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    initialize_msdp_player(&opponent, "Boromir");
    opponent.player.level = 18;
    opponent.player.race = RACE_HUMAN;
    opponent.abilities.hit = 120;
    opponent.tmpabilities.hit = 30;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL, eMSDP_OPPONENT_AGE });
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_AGE, "stale");
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->variables.number(eMSDP_OPPONENT_HEALTH), 25);
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_NAME),
        pc_star_types[RACE_HUMAN]);
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_LEVEL), "???");
    EXPECT_EQ(protocol->variables.text(eMSDP_OPPONENT_AGE), "");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25") + expected_msdp_pair("OPPONENT_LEVEL", "???")
            + expected_msdp_pair("OPPONENT_NAME", pc_star_types[RACE_HUMAN])
            + expected_msdp_pair("OPPONENT_AGE", ""));
}

TEST(MSDPProtocol, MsdpUpdateSetsOpponentHealthMaxTo100WhileFightingNpc)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a snarling orc");
    opponent.abilities.hit = 200;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_HEALTH_MAX });
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_OPPONENT_HEALTH_MAX), 100);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25")
            + expected_msdp_pair("OPPONENT_HEALTH_MAX", "100"));
}

TEST(MSDPProtocol, MsdpUpdateSetsOpponentHealthMaxTo100WhileFightingPlayer)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};
    MsdpCharacterRelease opponent_release(&opponent);

    initialize_msdp_player(&context.character, "Aragorn");
    initialize_msdp_player(&opponent, "Boromir");
    opponent.player.race = RACE_HUMAN;
    opponent.abilities.hit = 120;
    opponent.tmpabilities.hit = 30;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_HEALTH_MAX });
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_OPPONENT_HEALTH_MAX), 100);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25")
            + expected_msdp_pair("OPPONENT_HEALTH_MAX", "100"));
}

TEST(MSDPProtocol, MsdpUpdateSetsOpponentHealthMaxTo0WhenNotFighting)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_HEALTH_MAX });
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH, 40);
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH_MAX, 100);
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_OPPONENT_HEALTH_MAX), 0);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "0")
            + expected_msdp_pair("OPPONENT_HEALTH_MAX", "0"));
}

// --- Regression tests for the "dirty flag dropped while MSDP is off" bug -------------------------
// MSDP is off by default for new characters; a client may enable it after login. Values dirtied
// while MSDP was off (notably login-time constants like CHARACTER_NAME) must NOT be marked clean
// without actually being sent, or they are lost for the session.

TEST(MSDPProtocol, MsdpUpdateKeepsVariableDirtyWhenMsdpIsOff)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    REMOVE_BIT(context.character.specials2.pref, PRF_MSDP);
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME });
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    ASSERT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));

    MSDPUpdate(&context.descriptor);

    EXPECT_EQ(context.read_output(), "");
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
}

TEST(MSDPProtocol, MsdpUpdateFlushesValueDirtiedWhileMsdpWasOff)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    REMOVE_BIT(context.character.specials2.pref, PRF_MSDP);
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME });
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPUpdate(&context.descriptor);
    context.read_output();

    SET_BIT(context.character.specials2.pref, PRF_MSDP);
    MSDPUpdate(&context.descriptor);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("CHARACTER_NAME", "Aragorn"));
}

TEST(MSDPProtocol, MsdpFlushKeepsVariableDirtyWhenMsdpIsOff)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    REMOVE_BIT(context.character.specials2.pref, PRF_MSDP);
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME });
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");

    MSDPFlush(&context.descriptor, eMSDP_CHARACTER_NAME);

    EXPECT_EQ(context.read_output(), "");
    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
}

TEST(MSDPProtocol, MarkAllReportedDirtyForcesResendOnNextUpdate)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME });
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPUpdate(&context.descriptor);
    context.read_output();
    ASSERT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_CHARACTER_NAME));

    MSDPMarkAllReportedDirty(&context.descriptor);
    MSDPUpdate(&context.descriptor);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("CHARACTER_NAME", "Aragorn"));
}

// The telnet IS code that starts a client's TTYPE reply.
constexpr char TTYPE_IS = 0;

// An option number no telnet handler in the protocol knows.
constexpr unsigned char UNKNOWN_TELNET_OPTION = 99;

// Frames payload as a telnet subnegotiation of option.
std::string telnet_subnegotiation(unsigned char option, std::string_view payload)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(option));
    packet += payload;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

// The client's TTYPE reply naming terminal_type.
std::string ttype_reply(std::string_view terminal_type)
{
    std::string payload(1, TTYPE_IS);
    payload += terminal_type;
    return telnet_subnegotiation(TELOPT_TTYPE, payload);
}

// The client's NAWS report, each byte as it travels; a 255 byte must already be doubled.
std::string naws_report(std::string_view size_bytes)
{
    return telnet_subnegotiation(TELOPT_NAWS, size_bytes);
}

// Feeds input to the protocol as one read and returns the bytes the server wrote back. The input
// is all negotiation, so no player text may come out of it. input stays a std::string for
// feed_protocol_input.
std::string reply_to(ProtocolDescriptor& context, const std::string& input)
{
    char output[MAX_INPUT_LENGTH] = { '\0' };
    feed_protocol_input(&context.descriptor, input, output);
    EXPECT_STREQ(output, "") << "negotiation input reached the player's text";
    return context.read_output();
}

// The offers protocol.cpp's negotiate makes for every protocol after TTYPE, in the order it makes
// them.
std::string expected_protocol_offers()
{
    return telnet_sequence(DO, TELOPT_NAWS) + telnet_sequence(DO, TELOPT_CHARSET)
        + telnet_sequence(WILL, TELOPT_MSDP) + telnet_sequence(WILL, TELOPT_MSSP)
        + telnet_sequence(DO, TELOPT_ATCP) + telnet_sequence(WILL, TELOPT_MSP)
        + telnet_sequence(DO, TELOPT_MXP) + telnet_sequence(WILL, TELOPT_EOR);
}

// The CHARSET REQUEST the server sends once a client agrees to CHARSET.
std::string expected_charset_request()
{
    std::string payload(1, static_cast<char>(SEND));
    payload += " ISO-8859-1";
    return telnet_subnegotiation(TELOPT_CHARSET, payload);
}

// The empty MXP subnegotiation and the secure-line escape that switch MXP on.
std::string expected_mxp_start()
{
    return telnet_subnegotiation(TELOPT_MXP, "") + "\033[7z";
}

// The bytes MXPSendTag sends for tag.
std::string expected_mxp_tag(std::string_view tag)
{
    std::string sent = "\033[1z";
    sent += tag;
    sent += "\033[7z\r\n";
    return sent;
}

// The announcement the server writes when it learns the client's MXP version.
std::string expected_mxp_announcement(std::string_view version)
{
    std::string announcement = "\n\t[F210][\toINFO\t[F210]]\tn MXP version ";
    announcement += version;
    announcement += " detected and enabled.\r\n";
    return announcement;
}

// The MXP <VERSION> reply a client sends, holding attributes.
std::string mxp_version_reply(std::string_view attributes)
{
    std::string reply = "\033[1z<VERSION ";
    reply += attributes;
    reply += ">";
    return reply;
}

// One MSSP variable and its value.
struct MsspVariable {
    // The MSSP variable name.
    std::string name;
    // The value sent for it.
    std::string value;
};

// The MSSP table the server sends. Nothing in the server sets the player count or the boot time,
// so both are always 0.
std::string expected_mssp_packet()
{
    const std::vector<MsspVariable> variables
        = { { "NAME", MUD_NAME }, { "PLAYERS", "0" }, { "UPTIME", "0" }, { "CRAWL DELAY", "-1" } };
    std::string payload;
    for (const MsspVariable& variable : variables) {
        payload.push_back(static_cast<char>(MSSP_VAR));
        payload += variable.name;
        payload.push_back(static_cast<char>(MSSP_VAL));
        payload += variable.value;
    }
    return telnet_subnegotiation(TELOPT_MSSP, payload);
}

TEST(ProtocolInput, IgnoresAMissingDescriptorProtocolDataOutputOrLength)
{
    char output[MAX_INPUT_LENGTH] = "kept";
    const char text[] = "look";
    descriptor_data descriptor_without_protocol {};
    descriptor_data descriptor = make_protocol_input_descriptor();
    char lone_iac[] = { static_cast<char>(IAC) };
    ProtocolInput(&descriptor, lone_iac, 1, output);
    ASSERT_EQ(descriptor.pProtocol->pending_input_length, 1);

    ProtocolInput(nullptr, text, 4, output);
    ProtocolInput(&descriptor_without_protocol, text, 4, output);
    ProtocolInput(&descriptor, nullptr, 4, output);
    ProtocolInput(&descriptor, text, 4, nullptr);
    ProtocolInput(&descriptor, text, 0, output);
    ProtocolInput(&descriptor, text, -1, output);

    EXPECT_STREQ(output, "kept");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 1) << "an ignored call keeps the pending IAC";

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, AppendsPlayerTextToWhatTheOutputAlreadyHolds)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = "say ";

    feed_protocol_input(&descriptor, "hello", output);

    EXPECT_STREQ(output, "say hello");

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, TurnsADoubledIacIntoOneByteOfPlayerText)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    const std::string input
        = std::string("a") + static_cast<char>(IAC) + static_cast<char>(IAC) + "b";

    feed_protocol_input(&descriptor, input, output);

    EXPECT_EQ(std::string(output), std::string("a") + static_cast<char>(IAC) + "b");

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, SkipsTelnetCommandsThatAreNotNegotiation)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    const std::string input = std::string("a") + static_cast<char>(IAC) + static_cast<char>(NOP)
        + "b" + static_cast<char>(IAC) + static_cast<char>(GA) + "c" + static_cast<char>(IAC)
        + static_cast<char>(SE) + "d";

    feed_protocol_input(&descriptor, input, output);

    EXPECT_STREQ(output, "abcd");
    EXPECT_FALSE(descriptor.pProtocol->in_subnegotiation);
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 0);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, PassesAReadThatExactlyFillsTheCommandBuffer)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    std::vector<char> output(MAX_PROTOCOL_BUFFER + 1, '\0');
    const std::string full_read(MAX_PROTOCOL_BUFFER, 'a');

    feed_protocol_input(&descriptor, full_read, output.data());

    EXPECT_EQ(std::string(output.data()), full_read);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, DropsAndLogsAWholeReadLongerThanTheCommandBuffer)
{
    ScopedDescriptorList descriptor_list_scope;
    descriptor_data descriptor = make_protocol_input_descriptor();
    std::vector<char> output(MAX_PROTOCOL_BUFFER + 2, '\0');
    const std::string long_read(MAX_PROTOCOL_BUFFER + 1, 'a');

    testing::internal::CaptureStderr();
    feed_protocol_input(&descriptor, long_read, output.data());
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_STREQ(output.data(), "") << "none of the read reaches the player, not even its start";
    EXPECT_NE(logged.find("ProtocolInput: Too much incoming data to store in the buffer."),
        std::string::npos)
        << logged;
    EXPECT_FALSE(descriptor.pProtocol->in_subnegotiation);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, KeepsAnEscapeMidInputButDropsOneNearTheEndWithoutABracket)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&descriptor, "\033[0mlook", output);
    EXPECT_STREQ(output, "\033[0mlook");

    output[0] = '\0';
    feed_protocol_input(&descriptor, "\033Olook", output);
    EXPECT_STREQ(output, "\033Olook");

    output[0] = '\0';
    feed_protocol_input(&descriptor, "look\033x", output);
    EXPECT_STREQ(output, "lookx");

    output[0] = '\0';
    feed_protocol_input(&descriptor, "\033ab", output);
    EXPECT_STREQ(output, "ab");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 0);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersAnEscapeAndBracketEvenWhenNoDigitFollows)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&descriptor, "\033[a", output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 3);

    feed_protocol_input(&descriptor, "bc", output);

    EXPECT_STREQ(output, "\033[abc");
    EXPECT_EQ(descriptor.pProtocol->pending_input_length, 0);

    ProtocolDestroy(descriptor.pProtocol);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolInput, KeepsALoneIacInsideASubnegotiationAsData)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->naws_enabled = true;
    const std::string size_bytes = std::string(1, static_cast<char>(IAC)) + "P" + '\0' + '\030';

    EXPECT_EQ(reply_to(context, naws_report(size_bytes)), "");

    EXPECT_EQ(context.descriptor.pProtocol->screen_width, 0xff * 256 + 'P');
    EXPECT_EQ(context.descriptor.pProtocol->screen_height, 24);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolInput, IgnoresASubnegotiationWithNoDataAfterItsOption)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->naws_enabled = true;
    context.descriptor.pProtocol->screen_width = 7;
    context.descriptor.pProtocol->screen_height = 9;

    EXPECT_EQ(reply_to(context, telnet_subnegotiation(TELOPT_NAWS, "")), "");
    const std::string empty_subnegotiation = { static_cast<char>(IAC), static_cast<char>(SB),
        static_cast<char>(IAC), static_cast<char>(SE) };
    EXPECT_EQ(reply_to(context, empty_subnegotiation), "");

    EXPECT_EQ(context.descriptor.pProtocol->screen_width, 7);
    EXPECT_EQ(context.descriptor.pProtocol->screen_height, 9);
    EXPECT_FALSE(context.descriptor.pProtocol->in_subnegotiation);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, ProtocolNegotiateOffersTerminalTypeOnce)
{
    ProtocolDescriptor context;

    ProtocolNegotiate(&context.descriptor);
    EXPECT_EQ(context.read_output(), telnet_sequence(DO, TELOPT_TTYPE));
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_TTYPE]);

    ProtocolNegotiate(&context.descriptor);
    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, TerminalTypeRefusalOffersEveryOtherProtocolWithoutAskingForTheType)
{
    ProtocolDescriptor context;
    ProtocolNegotiate(&context.descriptor);
    context.read_output();

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_TTYPE)), expected_protocol_offers());

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_FALSE(protocol->ttype_enabled);
    EXPECT_FALSE(protocol->option_states[eNEGOTIATED_TTYPE]);
    EXPECT_TRUE(protocol->negotiated);
    EXPECT_TRUE(protocol->renegotiate);
    EXPECT_TRUE(protocol->option_states[eNEGOTIATED_MCCP]) << "MCCP is recorded as offered unsent";

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_TTYPE)), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, TerminalTypeIsWithdrawnRefusedAndAgreedAgainWithoutRenegotiating)
{
    ProtocolDescriptor context;
    reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE));

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_TTYPE)),
        telnet_sequence(DONT, TELOPT_TTYPE));
    EXPECT_FALSE(context.descriptor.pProtocol->ttype_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_TTYPE)),
        telnet_sequence(WONT, TELOPT_TTYPE));
    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_TTYPE)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE)),
        telnet_sequence(DO, TELOPT_TTYPE));
    EXPECT_TRUE(context.descriptor.pProtocol->ttype_enabled);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, EchoIsAgreedWithdrawnAndRefused)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_ECHO)), telnet_sequence(WILL, TELOPT_ECHO));
    EXPECT_TRUE(context.descriptor.pProtocol->echo_enabled);
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_ECHO]);
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_ECHO)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_ECHO)), telnet_sequence(WONT, TELOPT_ECHO));
    EXPECT_FALSE(context.descriptor.pProtocol->echo_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_ECHO)), telnet_sequence(DONT, TELOPT_ECHO));
    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_ECHO)), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, WindowSizeIsAgreedWithdrawnAndRefusedAndEitherAnswerCancelsRenegotiation)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    protocol->renegotiate = true;
    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_NAWS)), telnet_sequence(DO, TELOPT_NAWS));
    EXPECT_TRUE(protocol->naws_enabled);
    EXPECT_FALSE(protocol->renegotiate);

    protocol->renegotiate = true;
    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_NAWS)), telnet_sequence(DONT, TELOPT_NAWS));
    EXPECT_FALSE(protocol->naws_enabled);
    EXPECT_FALSE(protocol->renegotiate);

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_NAWS)), telnet_sequence(WONT, TELOPT_NAWS));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, CharsetAgreementRequestsLatin1UntilTheClientWithdraws)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_CHARSET)),
        telnet_sequence(DO, TELOPT_CHARSET) + expected_charset_request());
    EXPECT_TRUE(context.descriptor.pProtocol->charset_enabled);
    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_CHARSET)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_CHARSET)),
        telnet_sequence(DONT, TELOPT_CHARSET));
    EXPECT_FALSE(context.descriptor.pProtocol->charset_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_CHARSET)),
        telnet_sequence(DO, TELOPT_CHARSET) + expected_charset_request());
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_CHARSET)),
        telnet_sequence(WONT, TELOPT_CHARSET));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, CharsetAgreementAfterTheServerOfferRequestsLatin1Alone)
{
    ProtocolDescriptor context;
    reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE));

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_CHARSET)), expected_charset_request());
    EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_UTF_8), 0);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, MsdpOfferedByTheClientIsRefused)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MSDP)), telnet_sequence(DONT, TELOPT_MSDP));
    EXPECT_TRUE(context.descriptor.pProtocol->msdp_enabled);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMssp, RequestSendsTheStatusTableAgainEachTimeItIsReenabled)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MSSP)),
        telnet_sequence(WILL, TELOPT_MSSP) + expected_mssp_packet());
    EXPECT_TRUE(context.descriptor.pProtocol->mssp_enabled);
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MSSP)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_MSSP)), telnet_sequence(WONT, TELOPT_MSSP));
    EXPECT_FALSE(context.descriptor.pProtocol->mssp_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MSSP)),
        telnet_sequence(WILL, TELOPT_MSSP) + expected_mssp_packet());
    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MSSP)), telnet_sequence(DONT, TELOPT_MSSP));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMssp, StatusTableAfterTheServerOfferIsSentWithoutAnotherAgreement)
{
    ProtocolDescriptor context;
    reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE));

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MSSP)), expected_mssp_packet());
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, CompressionIsAgreedAndWithdrawnSilentlyBecauseMccpIsNotBuiltIn)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MCCP)), "");
    EXPECT_TRUE(protocol->mccp_enabled);
    EXPECT_TRUE(protocol->option_states[eNEGOTIATED_MCCP]);
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MCCP)), "");
    EXPECT_TRUE(protocol->mccp_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_MCCP)), "");
    EXPECT_FALSE(protocol->mccp_enabled);
    EXPECT_FALSE(protocol->option_states[eNEGOTIATED_MCCP]);
    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_MCCP)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MCCP)), telnet_sequence(DONT, TELOPT_MCCP));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, SoundIsAgreedWithdrawnAndRefused)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MSP)), telnet_sequence(WILL, TELOPT_MSP));
    EXPECT_TRUE(context.descriptor.pProtocol->msp_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_MSP)), telnet_sequence(WONT, TELOPT_MSP));
    EXPECT_FALSE(context.descriptor.pProtocol->msp_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MSP)), telnet_sequence(DONT, TELOPT_MSP));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, MxpOfferedByTheClientIsAcceptedAndStartsTheSecureLine)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MXP)),
        telnet_sequence(DO, TELOPT_MXP) + expected_mxp_start());
    EXPECT_TRUE(protocol->mxp_enabled);
    EXPECT_TRUE(protocol->option_states[eNEGOTIATED_MXP]);
    EXPECT_EQ(protocol->variables.number(eMSDP_MXP), 1);

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MXP)), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, MxpRequestedByTheClientIsAnsweredWithWill)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MXP)),
        telnet_sequence(WILL, TELOPT_MXP) + expected_mxp_start());
    EXPECT_TRUE(context.descriptor.pProtocol->mxp_enabled);
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_MXP2]);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, MxpRefusalBeforeItStartsIsRetriedWithWill)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_MXP)), telnet_sequence(WILL, TELOPT_MXP));
    EXPECT_FALSE(context.descriptor.pProtocol->mxp_enabled);
    EXPECT_TRUE(context.descriptor.pProtocol->option_states[eNEGOTIATED_MXP2]);

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_MXP)), expected_mxp_start());
    EXPECT_TRUE(context.descriptor.pProtocol->mxp_enabled);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, MxpSwitchedOffLeavesTheMsdpMxpFlagSetSoTagsAreStillSent)
{
    ProtocolDescriptor offered_by_client;
    reply_to(offered_by_client, telnet_sequence(WILL, TELOPT_MXP));

    EXPECT_EQ(reply_to(offered_by_client, telnet_sequence(WONT, TELOPT_MXP)),
        telnet_sequence(DONT, TELOPT_MXP));
    EXPECT_FALSE(offered_by_client.descriptor.pProtocol->mxp_enabled);
    EXPECT_EQ(offered_by_client.descriptor.pProtocol->variables.number(eMSDP_MXP), 1);
    MXPSendTag(&offered_by_client.descriptor, "<B>");
    EXPECT_EQ(offered_by_client.read_output(), expected_mxp_tag("<B>"));

    ProtocolDescriptor requested_by_client;
    reply_to(requested_by_client, telnet_sequence(DO, TELOPT_MXP));

    EXPECT_EQ(reply_to(requested_by_client, telnet_sequence(DONT, TELOPT_MXP)),
        telnet_sequence(WONT, TELOPT_MXP));
    EXPECT_FALSE(requested_by_client.descriptor.pProtocol->mxp_enabled);
    EXPECT_EQ(requested_by_client.descriptor.pProtocol->variables.number(eMSDP_MXP), 1);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, AtcpIsAgreedButOnlyUsedWhileMsdpIsOff)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_ATCP)), telnet_sequence(DO, TELOPT_ATCP));
    EXPECT_FALSE(protocol->atcp_enabled);
    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_ATCP)), "")
        << "an unused ATCP is withdrawn without a reply";
    EXPECT_FALSE(protocol->option_states[eNEGOTIATED_ATCP]);

    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, TELOPT_MSDP)), "");
    EXPECT_FALSE(protocol->msdp_enabled);

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_ATCP)),
        telnet_sequence(DO, TELOPT_ATCP) + expected_atcp_pair("SERVER_ID", MUD_NAME));
    EXPECT_TRUE(protocol->atcp_enabled);
    EXPECT_EQ(protocol->variables.text(eMSDP_SERVER_ID), MUD_NAME);
    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_ATCP)), "");

    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, TELOPT_ATCP)), telnet_sequence(DONT, TELOPT_ATCP));
    EXPECT_FALSE(protocol->atcp_enabled);
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_ATCP)), telnet_sequence(WONT, TELOPT_ATCP));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, RefusalsOfWhatWasNeverAgreedGetNoReply)
{
    ProtocolDescriptor context;
    const std::vector<std::string> refusals = { telnet_sequence(DONT, TELOPT_TTYPE),
        telnet_sequence(DONT, TELOPT_ECHO), telnet_sequence(WONT, TELOPT_ECHO),
        telnet_sequence(WONT, TELOPT_NAWS), telnet_sequence(DONT, TELOPT_NAWS),
        telnet_sequence(WONT, TELOPT_CHARSET), telnet_sequence(DONT, TELOPT_CHARSET),
        telnet_sequence(WONT, TELOPT_MSDP), telnet_sequence(DONT, TELOPT_MSSP),
        telnet_sequence(WONT, TELOPT_MSSP), telnet_sequence(WONT, TELOPT_MCCP),
        telnet_sequence(DONT, TELOPT_MSP), telnet_sequence(WONT, TELOPT_MSP),
        telnet_sequence(DONT, TELOPT_MXP), telnet_sequence(DONT, TELOPT_ATCP),
        telnet_sequence(WONT, TELOPT_ATCP), telnet_sequence(DONT, TELOPT_EOR),
        telnet_sequence(WONT, TELOPT_EOR), telnet_sequence(DONT, TELOPT_SGA),
        telnet_sequence(WONT, TELOPT_SGA) };

    for (std::size_t refusal_index = 0; refusal_index < refusals.size(); ++refusal_index) {
        EXPECT_EQ(reply_to(context, refusals[refusal_index]), "") << "refusal " << refusal_index;
    }

    for (int negotiated = eNEGOTIATED_TTYPE; negotiated < eNEGOTIATED_MAX; ++negotiated) {
        EXPECT_FALSE(context.descriptor.pProtocol->option_states[negotiated]) << "negotiated enum " << negotiated;
    }
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, UnknownOptionsAreRefusedAndTheirRefusalsIgnored)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, UNKNOWN_TELNET_OPTION)),
        telnet_sequence(DONT, UNKNOWN_TELNET_OPTION));
    EXPECT_EQ(reply_to(context, telnet_sequence(DO, UNKNOWN_TELNET_OPTION)),
        telnet_sequence(WONT, UNKNOWN_TELNET_OPTION));
    EXPECT_EQ(reply_to(context, telnet_sequence(WONT, UNKNOWN_TELNET_OPTION)), "");
    EXPECT_EQ(reply_to(context, telnet_sequence(DONT, UNKNOWN_TELNET_OPTION)), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolHandshake, RefusalOfTheBinaryOptionLosesItsOptionByte)
{
    ProtocolDescriptor context;
    const std::string refuse_will = { static_cast<char>(IAC), static_cast<char>(WONT) };
    const std::string refuse_do = { static_cast<char>(IAC), static_cast<char>(DONT) };

    EXPECT_EQ(reply_to(context, telnet_sequence(DO, TELOPT_BINARY)), refuse_will);
    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_BINARY)), refuse_do);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, TerminalTypeIsIgnoredUntilTheClientAgreesToSendIt)
{
    ProtocolDescriptor context;

    EXPECT_EQ(reply_to(context, ttype_reply("Mudlet 4.17.2")), "");

    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "Unknown");
    EXPECT_FALSE(context.descriptor.pProtocol->last_ttype.has_value());
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, FirstTerminalTypeNamesTheClientAndEachNewTypeAsksForTheNext)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->ttype_enabled = true;

    EXPECT_EQ(reply_to(context, ttype_reply("TinTin++")), ttype_send_sequence());
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "TinTin++");
    EXPECT_EQ(protocol->last_ttype, "TinTin++");
    EXPECT_EQ(protocol->xterm_256_support, eUNKNOWN);

    EXPECT_EQ(reply_to(context, ttype_reply("XTERM-256COLOR")), ttype_send_sequence());
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "TinTin++");
    EXPECT_EQ(protocol->last_ttype, "XTERM-256COLOR");
    EXPECT_EQ(protocol->xterm_256_support, eYES);
    EXPECT_EQ(protocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    EXPECT_EQ(reply_to(context, ttype_reply("MTTS 2825")), ttype_send_sequence());
    EXPECT_EQ(protocol->last_ttype, "MTTS 2825");

    EXPECT_EQ(reply_to(context, ttype_reply("MTTS 2825")), "") << "a repeated type ends the cycle";
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, TerminalTypeCycleEndsWhenItReturnsToTheClientName)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->ttype_enabled = true;

    reply_to(context, ttype_reply("TinTin++"));
    reply_to(context, ttype_reply("XTERM"));

    EXPECT_EQ(reply_to(context, ttype_reply("TinTin++")), "");
    EXPECT_EQ(context.descriptor.pProtocol->last_ttype, "XTERM");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, ClientNameSetEarlierIsKeptWhileTheTypesStillCycle)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->ttype_enabled = true;
    feed_msdp_command(&context.descriptor, "CLIENT_ID", "MyClient");

    EXPECT_EQ(reply_to(context, ttype_reply("XTERM")), ttype_send_sequence());
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "MyClient");

    EXPECT_EQ(reply_to(context, ttype_reply("MyClient")), "");
    EXPECT_EQ(protocol->last_ttype, "XTERM");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, AnsiAsTheFirstTerminalTypeAsksForNoOther)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->ttype_enabled = true;

    EXPECT_EQ(reply_to(context, ttype_reply("ANSI")), "");
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "ANSI");
    EXPECT_EQ(protocol->last_ttype, "ANSI");

    EXPECT_EQ(reply_to(context, ttype_reply("VT100")), ttype_send_sequence())
        << "only an ANSI first reply stops the cycle";
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, TerminalTypeKeepsTheFirst64PrintableBytes)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->ttype_enabled = true;
    std::string terminal_type = "\001Tin\177";
    terminal_type.push_back(static_cast<char>(0x80));
    terminal_type += "Tin";
    terminal_type.append(70, 'A');

    EXPECT_EQ(reply_to(context, ttype_reply(terminal_type)), ttype_send_sequence());

    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID),
        "TinTin" + std::string(58, 'A'));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, EmptyTerminalTypeBecomesAnEmptyClientName)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->ttype_enabled = true;

    EXPECT_EQ(reply_to(context, ttype_reply("")), ttype_send_sequence());

    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "");
    EXPECT_EQ(context.descriptor.pProtocol->last_ttype, "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, MudletTypeIsSplitIntoClientAndVersionAndItsVersionDecides256Colours)
{
    ProtocolDescriptor current_mudlet;
    current_mudlet.descriptor.pProtocol->ttype_enabled = true;
    reply_to(current_mudlet, ttype_reply("Mudlet 4.17.2"));
    protocol_t* current_protocol = current_mudlet.descriptor.pProtocol;

    EXPECT_EQ(current_protocol->variables.text(eMSDP_CLIENT_ID), "Mudlet");
    EXPECT_EQ(current_protocol->variables.text(eMSDP_CLIENT_VERSION), "4.17.2");
    EXPECT_EQ(current_protocol->last_ttype, "Mudlet 4.17.2");
    EXPECT_EQ(current_protocol->xterm_256_support, eYES);
    EXPECT_EQ(current_protocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    ProtocolDescriptor old_mudlet;
    old_mudlet.descriptor.pProtocol->ttype_enabled = true;
    reply_to(old_mudlet, ttype_reply("Mudlet 1.0"));
    protocol_t* old_protocol = old_mudlet.descriptor.pProtocol;

    EXPECT_EQ(old_protocol->variables.text(eMSDP_CLIENT_VERSION), "1.0");
    EXPECT_EQ(old_protocol->xterm_256_support, eSOMETIMES);
    EXPECT_EQ(old_protocol->variables.number(eMSDP_XTERM_256_COLORS), 0);

    ProtocolDescriptor unversioned_mudlet;
    unversioned_mudlet.descriptor.pProtocol->ttype_enabled = true;
    reply_to(unversioned_mudlet, ttype_reply("Mudlet"));
    protocol_t* unversioned_protocol = unversioned_mudlet.descriptor.pProtocol;

    EXPECT_EQ(unversioned_protocol->variables.text(eMSDP_CLIENT_ID), "Mudlet");
    EXPECT_EQ(unversioned_protocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
    EXPECT_EQ(unversioned_protocol->xterm_256_support, eSOMETIMES);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, DecafMudTypeIsSplitIntoClientAndVersionAndAlwaysHas256Colours)
{
    ProtocolDescriptor versioned;
    versioned.descriptor.pProtocol->ttype_enabled = true;
    reply_to(versioned, ttype_reply("DecafMUD 0.9.0"));

    EXPECT_EQ(versioned.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "DecafMUD");
    EXPECT_EQ(versioned.descriptor.pProtocol->variables.text(eMSDP_CLIENT_VERSION), "0.9.0");
    EXPECT_EQ(versioned.descriptor.pProtocol->xterm_256_support, eYES);
    EXPECT_EQ(versioned.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    ProtocolDescriptor unversioned;
    unversioned.descriptor.pProtocol->ttype_enabled = true;
    reply_to(unversioned, ttype_reply("DecafMUD"));

    EXPECT_EQ(unversioned.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "DecafMUD");
    EXPECT_EQ(unversioned.descriptor.pProtocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
    EXPECT_EQ(unversioned.descriptor.pProtocol->xterm_256_support, eYES);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, MttsBitsTurnOnAnsiUtf8And256ColoursAndLeaveTheClientVersion)
{
    ProtocolDescriptor all_bits;
    protocol_t* all_bits_protocol = all_bits.descriptor.pProtocol;
    all_bits_protocol->ttype_enabled = true;
    all_bits_protocol->variables.stored_number(eMSDP_ANSI_COLORS) = 0;
    reply_to(all_bits, ttype_reply("MTTS 13"));

    EXPECT_EQ(all_bits_protocol->variables.text(eMSDP_CLIENT_ID), "MTTS 13");
    EXPECT_EQ(all_bits_protocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
    EXPECT_EQ(all_bits_protocol->variables.number(eMSDP_ANSI_COLORS), 1);
    EXPECT_EQ(all_bits_protocol->variables.number(eMSDP_UTF_8), 1);
    EXPECT_EQ(all_bits_protocol->variables.number(eMSDP_XTERM_256_COLORS), 1);
    EXPECT_EQ(all_bits_protocol->xterm_256_support, eYES);

    ProtocolDescriptor no_bits;
    protocol_t* no_bits_protocol = no_bits.descriptor.pProtocol;
    no_bits_protocol->ttype_enabled = true;
    no_bits_protocol->variables.stored_number(eMSDP_ANSI_COLORS) = 0;
    reply_to(no_bits, ttype_reply("MTTS 2"));

    EXPECT_EQ(no_bits_protocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
    EXPECT_EQ(no_bits_protocol->variables.number(eMSDP_ANSI_COLORS), 0);
    EXPECT_EQ(no_bits_protocol->variables.number(eMSDP_UTF_8), 0);
    EXPECT_EQ(no_bits_protocol->variables.number(eMSDP_XTERM_256_COLORS), 0);
    EXPECT_EQ(no_bits_protocol->xterm_256_support, eUNKNOWN);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, OnlyA256colorSuffixAfterTheFirstDashMeans256Colours)
{
    ProtocolDescriptor suffixed;
    suffixed.descriptor.pProtocol->ttype_enabled = true;
    reply_to(suffixed, ttype_reply("xterm-256color"));

    EXPECT_EQ(suffixed.descriptor.pProtocol->xterm_256_support, eYES);
    EXPECT_EQ(suffixed.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 1);

    ProtocolDescriptor second_dash;
    second_dash.descriptor.pProtocol->ttype_enabled = true;
    reply_to(second_dash, ttype_reply("rxvt-unicode-256color"));

    EXPECT_EQ(second_dash.descriptor.pProtocol->xterm_256_support, eUNKNOWN);
    EXPECT_EQ(second_dash.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS), 0);
}

// A client name and the 256-colour support the server records for it.
struct KnownClient {
    // The client's first TTYPE reply.
    std::string terminal_type;
    // The support recorded for it.
    support_t support;
    // The XTERM_256_COLORS value recorded for it.
    int xterm_256_colours;
};

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, KnownClientNamesRecordTheir256ColourSupport)
{
    const std::vector<KnownClient> clients = { { "EMACS-RINZAI", eYES, 1 }, { "cmud", eSOMETIMES, 0 },
        { "ATLANTIS", eSOMETIMES, 0 }, { "KildClient", eSOMETIMES, 0 }, { "TinyFugue", eSOMETIMES, 0 },
        { "zMUD", eNO, 0 }, { "TinTin++", eUNKNOWN, 0 } };

    for (const KnownClient& client : clients) {
        ProtocolDescriptor context;
        context.descriptor.pProtocol->ttype_enabled = true;
        reply_to(context, ttype_reply(client.terminal_type));

        EXPECT_EQ(context.descriptor.pProtocol->xterm_256_support, client.support) << client.terminal_type;
        EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS),
            client.xterm_256_colours)
            << client.terminal_type;
    }
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, WindowSizeIsStoredOnlyAfterTheClientAgreedToSendIt)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    const std::string eighty_by_twenty_four = { '\0', 'P', '\0', '\030' };

    reply_to(context, naws_report(eighty_by_twenty_four));
    EXPECT_EQ(protocol->screen_width, 0);
    EXPECT_EQ(protocol->screen_height, 0);

    protocol->naws_enabled = true;
    reply_to(context, naws_report(eighty_by_twenty_four));
    EXPECT_EQ(protocol->screen_width, 80);
    EXPECT_EQ(protocol->screen_height, 24);

    const std::string doubled_iacs(8, static_cast<char>(IAC));
    reply_to(context, naws_report(doubled_iacs));
    EXPECT_EQ(protocol->screen_width, 65535);
    EXPECT_EQ(protocol->screen_height, 65535);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, ShortWindowSizeReadsLeftoverBytesOfAnEarlierSubnegotiation)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->naws_enabled = true;

    reply_to(context, telnet_subnegotiation(UNKNOWN_TELNET_OPTION, "ABCDE"));
    const std::string width_only = { '\0', 'd' };
    reply_to(context, naws_report(width_only));

    EXPECT_EQ(context.descriptor.pProtocol->screen_width, 100);
    EXPECT_EQ(context.descriptor.pProtocol->screen_height, 'D') << "the height is the stale fifth byte";
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, CharsetReplyAndUnknownSubnegotiationsChangeNothing)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->charset_enabled = true;
    const std::string accepted = std::string(1, static_cast<char>(ACCEPTED)) + "ISO-8859-1";

    EXPECT_EQ(reply_to(context, telnet_subnegotiation(TELOPT_CHARSET, accepted)), "");
    EXPECT_EQ(reply_to(context, telnet_subnegotiation(UNKNOWN_TELNET_OPTION, "data")), "");

    EXPECT_EQ(protocol->variables.number(eMSDP_UTF_8), 0);
    EXPECT_TRUE(protocol->charset_enabled);
    EXPECT_FALSE(protocol->in_subnegotiation);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolSubnegotiation, MsdpCommandsAreIgnoredWhileMsdpIsOff)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->msdp_enabled = false;
    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 40);

    EXPECT_EQ(reply_to(context, msdp_packet(msdp_pair_payload("REPORT", "HEALTH"))), "");
    EXPECT_EQ(reply_to(context, msdp_packet(msdp_pair_payload("SEND", "HEALTH"))), "");

    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolAtcp, CommandsAreIgnoredWhileAtcpIsOff)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->variables.set_reported(eMSDP_HEALTH, false);

    reply_to(context, telnet_subnegotiation(TELOPT_ATCP, "@REPORT HEALTH"));

    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_reported(eMSDP_HEALTH));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolAtcp, EachAtNameStartsACommandAndEachSpaceStartsAValue)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->msdp_enabled = false;
    protocol->atcp_enabled = true;
    disable_all_msdp_reports(protocol);

    reply_to(context, telnet_subnegotiation(TELOPT_ATCP, "@REPORT HEALTH MANA"));

    EXPECT_TRUE(protocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(protocol->variables.is_reported(eMSDP_MANA));

    reply_to(context, telnet_subnegotiation(TELOPT_ATCP, "@UNREPORT HEALTH@REPORT MOVEMENT"));

    EXPECT_FALSE(protocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(protocol->variables.is_reported(eMSDP_MANA));
    EXPECT_TRUE(protocol->variables.is_reported(eMSDP_MOVEMENT));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolAtcp, IgnoresTextBeforeTheFirstAtAndAValueLongerThanTheMsdpSizeLimit)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->msdp_enabled = false;
    protocol->atcp_enabled = true;
    disable_all_msdp_reports(protocol);

    reply_to(context, telnet_subnegotiation(TELOPT_ATCP, "REPORT HEALTH"));
    EXPECT_FALSE(protocol->variables.is_reported(eMSDP_HEALTH));

    const std::string long_value = "@REPORT HEALTH" + std::string(MAX_MSDP_SIZE, 'X');
    reply_to(context, telnet_subnegotiation(TELOPT_ATCP, long_value));
    EXPECT_FALSE(protocol->variables.is_reported(eMSDP_HEALTH));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolAtcp, SendAnswersInAtcpForm)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->msdp_enabled = false;
    context.descriptor.pProtocol->atcp_enabled = true;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);

    EXPECT_EQ(reply_to(context, telnet_subnegotiation(TELOPT_ATCP, "@SEND HEALTH")),
        expected_atcp_pair("HEALTH", "87"));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, VersionReplyRecordsTheClientAndAnnouncesTheMxpVersion)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_EQ(reply_to(context, mxp_version_reply("MXP=1.0 CLIENT=MUSHclient VERSION=4.94")),
        expected_mxp_announcement("1.0"));

    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "MUSHclient");
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_VERSION), "4.94");
    EXPECT_EQ(protocol->mxp_version, "1.0");
    EXPECT_EQ(protocol->xterm_256_support, eYES);
    EXPECT_EQ(protocol->variables.number(eMSDP_XTERM_256_COLORS), 1);
}

// The 256-colour support one MXP <VERSION> reply records.
struct MxpClientVersion {
    // The reply's attributes.
    std::string attributes;
    // The support recorded.
    support_t support;
    // The XTERM_256_COLORS value recorded.
    int xterm_256_colours;
};

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, VersionComparedAsTextDecides256ColoursForMushclientCmudAndAtlantis)
{
    const std::vector<MxpClientVersion> replies = { { "CLIENT=MUSHclient VERSION=4.02", eYES, 1 },
        { "CLIENT=MUSHclient VERSION=4.01", eNO, 0 }, { "CLIENT=MUSHclient VERSION=10.00", eNO, 0 },
        { "CLIENT=CMUD VERSION=3.04", eYES, 1 }, { "CLIENT=CMUD VERSION=3.03", eNO, 0 },
        { "CLIENT=Atlantis VERSION=0.9.9.0", eYES, 1 }, { "CLIENT=zMUD VERSION=7.21", eUNKNOWN, 0 },
        { "CLIENT=CMUD", eUNKNOWN, 0 } };

    for (const MxpClientVersion& reply : replies) {
        ProtocolDescriptor context;
        reply_to(context, mxp_version_reply(reply.attributes));

        EXPECT_EQ(context.descriptor.pProtocol->xterm_256_support, reply.support) << reply.attributes;
        EXPECT_EQ(context.descriptor.pProtocol->variables.number(eMSDP_XTERM_256_COLORS),
            reply.xterm_256_colours)
            << reply.attributes;
    }
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, VersionReplyWithoutAnMxpVersionAnnouncesNothing)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_EQ(reply_to(context, mxp_version_reply("CLIENT=zMUD VERSION=7.21")), "");

    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_ID), "zMUD");
    EXPECT_EQ(protocol->variables.text(eMSDP_CLIENT_VERSION), "7.21");
    EXPECT_EQ(protocol->mxp_version, "Unknown");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, ValuesSkipAnOpeningQuoteAndEndAtTheFirstByteNotALetterDigitOrDot)
{
    ProtocolDescriptor quoted;

    EXPECT_EQ(reply_to(quoted, mxp_version_reply("MXP=\"1.0\" CLIENT=\"TinTin++\" VERSION=\"2.02\"")),
        expected_mxp_announcement("1.0"));
    EXPECT_EQ(quoted.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "TinTin");
    EXPECT_EQ(quoted.descriptor.pProtocol->variables.text(eMSDP_CLIENT_VERSION), "2.02");

    ProtocolDescriptor empty_value;
    reply_to(empty_value, mxp_version_reply("CLIENT= VERSION=\"\""));
    EXPECT_EQ(empty_value.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "");
    EXPECT_EQ(empty_value.descriptor.pProtocol->variables.text(eMSDP_CLIENT_VERSION), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, ValueOfSixtyCharactersIsIgnoredAndOneOfFiftyNineKept)
{
    ProtocolDescriptor longest_kept;
    const std::string fifty_nine(59, 'A');
    reply_to(longest_kept, mxp_version_reply("CLIENT=" + fifty_nine));
    EXPECT_EQ(longest_kept.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID),
        fifty_nine);

    ProtocolDescriptor too_long;
    reply_to(too_long, mxp_version_reply("CLIENT=" + std::string(60, 'A')));
    EXPECT_EQ(too_long.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "Unknown");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, TextAroundTheReplyIsPlayerInputAndEveryLaterSequenceRepeatsTheAnnouncement)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, "say " + mxp_version_reply("MXP=1.0") + "hello", output);

    EXPECT_STREQ(output, "say hello");
    EXPECT_EQ(context.read_output(), expected_mxp_announcement("1.0"));

    output[0] = '\0';
    feed_protocol_input(&context.descriptor, "\033[4z<SUPPORTS>", output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(context.read_output(), expected_mxp_announcement("1.0"));
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, ReplySplitAcrossReadsKeepsTheFirstPartAndPassesTheRestToThePlayer)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, "\033[1z<VERSION MXP=1.0 CLIENT=CMUD", output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(context.read_output(), expected_mxp_announcement("1.0"));
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_ID), "CMUD");

    feed_protocol_input(&context.descriptor, " VERSION=3.04>look", output);

    EXPECT_STREQ(output, " VERSION=3.04>look");
    EXPECT_EQ(context.descriptor.pProtocol->variables.text(eMSDP_CLIENT_VERSION), "Unknown");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, ReplyLongerThanAThousandBytesSwallowsTheNextByteAndPassesTheRest)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, "\033[1z" + std::string(1000, 'A') + "BC>", output);

    EXPECT_STREQ(output, "C>");
    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, SendTagWrapsTagsUnderAThousandBytesInSecureLineEscapes)
{
    descriptor_data descriptor_without_protocol {};
    MXPSendTag(nullptr, "<B>");
    MXPSendTag(&descriptor_without_protocol, "<B>");

    ProtocolDescriptor context;
    context.descriptor.pProtocol->variables.stored_number(eMSDP_MXP) = 1;

    MXPSendTag(&context.descriptor, nullptr);
    EXPECT_EQ(context.read_output(), "");

    const std::string longest_tag = "<" + std::string(997, 'B') + ">";
    MXPSendTag(&context.descriptor, longest_tag.c_str());
    EXPECT_EQ(context.read_output(), expected_mxp_tag(longest_tag));

    const std::string too_long_tag = "<" + std::string(998, 'B') + ">";
    MXPSendTag(&context.descriptor, too_long_tag.c_str());
    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, SendTagWithoutMxpSendsNothingUnlessRenegotiationIsPending)
{
    ProtocolDescriptor context;

    MXPSendTag(&context.descriptor, "<VERSION>");

    EXPECT_EQ(context.read_output(), "");
    EXPECT_FALSE(context.descriptor.pProtocol->needs_mxp_version);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, SendTagWhileRenegotiationIsPendingOffersEverythingAgainIncludingEchoAndSga)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE));
    ASSERT_TRUE(protocol->renegotiate);

    MXPSendTag(&context.descriptor, "<VERSION>");

    EXPECT_EQ(context.read_output(),
        telnet_sequence(WILL, TELOPT_ECHO) + telnet_sequence(DO, TELOPT_NAWS)
            + telnet_sequence(DO, TELOPT_CHARSET) + telnet_sequence(WILL, TELOPT_MSDP)
            + telnet_sequence(WILL, TELOPT_MSSP) + telnet_sequence(DO, TELOPT_ATCP)
            + telnet_sequence(WILL, TELOPT_MSP) + telnet_sequence(DO, TELOPT_MXP)
            + telnet_sequence(WILL, TELOPT_MXP) + telnet_sequence(WILL, TELOPT_EOR)
            + telnet_sequence(WILL, TELOPT_SGA) + ttype_send_sequence());
    EXPECT_FALSE(protocol->renegotiate);
    EXPECT_TRUE(protocol->needs_mxp_version);
    EXPECT_TRUE(protocol->option_states[eNEGOTIATED_ECHO]);
    EXPECT_FALSE(protocol->echo_enabled);
    EXPECT_TRUE(protocol->option_states[eNEGOTIATED_SGA]);

    MXPSendTag(&context.descriptor, "<VERSION>");
    EXPECT_EQ(context.read_output(), "") << "renegotiation happens once";
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(ProtocolMxp, MxpAgreedAfterRenegotiationAsksTheClientForItsVersion)
{
    ProtocolDescriptor context;
    reply_to(context, telnet_sequence(WILL, TELOPT_TTYPE));
    MXPSendTag(&context.descriptor, "<VERSION>");
    context.read_output();

    EXPECT_EQ(reply_to(context, telnet_sequence(WILL, TELOPT_MXP)),
        expected_mxp_start() + expected_mxp_tag("<VERSION>"));
    EXPECT_TRUE(context.descriptor.pProtocol->mxp_enabled);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, SendReturnsWhetherItWroteAndFallsBackToAtcpForStrings)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);

    EXPECT_FALSE(MSDPSend(&context.descriptor, eMSDP_NONE));
    EXPECT_FALSE(MSDPSend(&context.descriptor, eMSDP_MAX));
    EXPECT_TRUE(MSDPSend(&context.descriptor, eMSDP_HEALTH));
    context.read_output();

    protocol->msdp_enabled = false;
    protocol->atcp_enabled = true;
    EXPECT_TRUE(MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME));
    EXPECT_EQ(context.read_output(), expected_atcp_pair("CHARACTER_NAME", "Aragorn"));

    protocol->atcp_enabled = false;
    EXPECT_FALSE(MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME));
    EXPECT_FALSE(MSDPSend(&context.descriptor, eMSDP_HEALTH));
    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, SendLogsAndDropsAStringThatFillsTheBuffer)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;
    // The framing allowance MSDPSend's limit adds to the name and value lengths.
    constexpr std::size_t packet_overhead = 12;
    const std::string name = "CHARACTER_NAME";
    const std::string largest_value(MAX_VARIABLE_LENGTH - 1 - name.size() - packet_overhead, 'A');

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, largest_value.c_str());
    EXPECT_TRUE(MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME));
    EXPECT_EQ(context.read_output(), expected_msdp_pair(name, largest_value));

    const std::string one_too_long = largest_value + "A";
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, one_too_long.c_str());
    testing::internal::CaptureStderr();
    EXPECT_FALSE(MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME));
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(context.read_output(), "");
    EXPECT_NE(logged.find("MSDPSend: CHARACTER_NAME 4096 bytes (exceeds MAX_VARIABLE_LENGTH of 4096)."),
        std::string::npos)
        << logged;
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, SendPairAndListLogAnOverlongNameWithTheWholePacketLength)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;
    const std::string long_name(MAX_VARIABLE_LENGTH - 6, 'V');

    testing::internal::CaptureStderr();
    MSDPSendPair(&context.descriptor, long_name.c_str(), "1");
    MSDPSendList(&context.descriptor, long_name.c_str(), "1");
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(context.read_output(), "");
    EXPECT_NE(logged.find("MSDPSendPair: Variable name has a length of 4103 bytes (exceeds "
                          "MAX_VARIABLE_LENGTH of 4096)."),
        std::string::npos)
        << logged;
    EXPECT_NE(logged.find("MSDPSendList: Variable name has a length of 4103 bytes (exceeds "
                          "MAX_VARIABLE_LENGTH of 4096)."),
        std::string::npos)
        << logged;
}

TEST(MSDPProtocol, SendPairLogsAnOverlongPairThroughAFormatThatReadsPercentSigns)
{
    ScopedDescriptorList descriptor_list_scope;
    ProtocolDescriptor context;
    const std::string huge_value(MAX_VARIABLE_LENGTH, 'X');

    testing::internal::CaptureStderr();
    MSDPSendPair(&context.descriptor, "A%%B", huge_value.c_str());
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_NE(logged.find("MSDPSendPair: A%B 4112 bytes (exceeds MAX_VARIABLE_LENGTH of 4096)."),
        std::string::npos)
        << logged;
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, SendPairAndListSendNothingWithoutANameAValueOrAnOutOfBandProtocol)
{
    ProtocolDescriptor context;

    MSDPSendPair(&context.descriptor, nullptr, "1");
    MSDPSendPair(&context.descriptor, "HEALTH", nullptr);
    MSDPSendList(&context.descriptor, nullptr, "LIST");
    MSDPSendList(&context.descriptor, "COMMANDS", nullptr);
    EXPECT_EQ(context.read_output(), "");

    context.descriptor.pProtocol->msdp_enabled = false;
    MSDPSendPair(&context.descriptor, "HEALTH", "1");
    MSDPSendList(&context.descriptor, "COMMANDS", "LIST REPORT");
    EXPECT_EQ(context.read_output(), "");
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, WriteMarksPendingOutOfBandDataAsWritten)
{
    ProtocolDescriptor context;

    MSDPSendPair(&context.descriptor, "HEALTH", "1");
    EXPECT_EQ(context.descriptor.pProtocol->write_oob, 0);

    context.descriptor.pProtocol->write_oob = 1;
    MSDPSendPair(&context.descriptor, "HEALTH", "2");
    EXPECT_EQ(context.descriptor.pProtocol->write_oob, 2);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "1") + expected_msdp_pair("HEALTH", "2"));
}

TEST(MSDPProtocol, MarkAllReportedDirtyIgnoresAMissingProtocolAndSkipsUnreportedVariables)
{
    descriptor_data descriptor_without_protocol {};
    MSDPMarkAllReportedDirty(nullptr);
    MSDPMarkAllReportedDirty(&descriptor_without_protocol);

    ProtocolDescriptor context;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_HEALTH });

    MSDPMarkAllReportedDirty(&context.descriptor);

    EXPECT_TRUE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_HEALTH));
    EXPECT_FALSE(context.descriptor.pProtocol->variables.is_dirty(eMSDP_MANA));
}

TEST(MSDPProtocol, NumericConfigurableValueMaySkipLeadingSpacesButNotBeOnlySpaces)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    feed_msdp_command(&context.descriptor, "ANSI_COLORS", "  0");
    EXPECT_EQ(protocol->variables.number(eMSDP_ANSI_COLORS), 0);

    feed_msdp_command(&context.descriptor, "ANSI_COLORS", "  1");
    EXPECT_EQ(protocol->variables.number(eMSDP_ANSI_COLORS), 1);

    feed_msdp_command(&context.descriptor, "ANSI_COLORS", "   ");
    EXPECT_EQ(protocol->variables.number(eMSDP_ANSI_COLORS), 1);
}

// Reads the wire through ProtocolDescriptor's socket pair; nothing portable sees the bytes.
TEST(MSDPProtocol, UnknownSendUnreportResetAndListValuesChangeNothing)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    enable_msdp_reports(protocol, { eMSDP_HEALTH });
    protocol->variables.mark_dirty(eMSDP_HEALTH);

    feed_msdp_command(&context.descriptor, "SEND", "NOT_A_VARIABLE");
    feed_msdp_command(&context.descriptor, "UNREPORT", "NOT_A_VARIABLE");
    feed_msdp_command(&context.descriptor, "RESET", "NOT_A_LIST");
    feed_msdp_command(&context.descriptor, "LIST", "NOT_A_LIST");

    EXPECT_EQ(context.read_output(), "");
    EXPECT_TRUE(protocol->variables.is_reported(eMSDP_HEALTH));
    EXPECT_TRUE(protocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, SetStringIgnoresANullValueAMissingProtocolAndNumberVariables)
{
    descriptor_data descriptor_without_protocol {};
    MSDPSetString(nullptr, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPSetString(&descriptor_without_protocol, eMSDP_CHARACTER_NAME, "Aragorn");

    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    protocol->variables.mark_clean(eMSDP_CHARACTER_NAME);
    protocol->variables.mark_clean(eMSDP_HEALTH);
    const char* missing_value = nullptr;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, missing_value);
    MSDPSetString(&context.descriptor, eMSDP_HEALTH, "12");

    EXPECT_EQ(protocol->variables.text(eMSDP_CHARACTER_NAME), "");
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_CHARACTER_NAME));
    EXPECT_TRUE(std::holds_alternative<int>(protocol->variables.value(eMSDP_HEALTH)));
    EXPECT_EQ(protocol->variables.number(eMSDP_HEALTH), 0);
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_HEALTH));
}

TEST(MSDPProtocol, SetTableAndSetArrayIgnoreANullValue)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, "\x01VNUM\x02"
                                                  "1101");
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, "\x02n");
    const std::string room_before = protocol->variables.text(eMSDP_ROOM);
    const std::string exits_before = protocol->variables.text(eMSDP_ROOM_EXITS);
    protocol->variables.mark_clean(eMSDP_ROOM);
    protocol->variables.mark_clean(eMSDP_ROOM_EXITS);
    const char* missing_value = nullptr;

    MSDPSetTable(&context.descriptor, eMSDP_ROOM, missing_value);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, missing_value);

    EXPECT_EQ(protocol->variables.text(eMSDP_ROOM), room_before);
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_ROOM));
    EXPECT_EQ(protocol->variables.text(eMSDP_ROOM_EXITS), exits_before);
    EXPECT_FALSE(protocol->variables.is_dirty(eMSDP_ROOM_EXITS));
}

// The size of a struct with no padding between its members: their sizes added, rounded up to the
// struct's alignment.
constexpr std::size_t unpadded_size(std::size_t members_size, std::size_t alignment)
{
    return (members_size + alignment - 1) / alignment * alignment;
}

TEST(ProtocolLayout, ProtocolHasNoPaddingBetweenFields)
{
    constexpr std::size_t members_size = sizeof(protocol_t::variables)
        + sizeof(protocol_t::mxp_version) + sizeof(protocol_t::last_ttype)
        + sizeof(protocol_t::write_oob) + sizeof(protocol_t::pending_input_length)
        + sizeof(protocol_t::subnegotiation_input_length) + sizeof(protocol_t::xterm_256_support)
        + sizeof(protocol_t::screen_width) + sizeof(protocol_t::screen_height)
        + sizeof(protocol_t::option_states) + sizeof(protocol_t::in_subnegotiation)
        + sizeof(protocol_t::negotiated) + sizeof(protocol_t::renegotiate)
        + sizeof(protocol_t::needs_mxp_version) + sizeof(protocol_t::mxp_blocked)
        + sizeof(protocol_t::pending_input) + sizeof(protocol_t::subnegotiation_input)
        + sizeof(protocol_t::ttype_enabled) + sizeof(protocol_t::echo_enabled) + sizeof(protocol_t::naws_enabled)
        + sizeof(protocol_t::charset_enabled) + sizeof(protocol_t::msdp_enabled) + sizeof(protocol_t::mssp_enabled)
        + sizeof(protocol_t::atcp_enabled) + sizeof(protocol_t::msp_enabled) + sizeof(protocol_t::mxp_enabled)
        + sizeof(protocol_t::mccp_enabled) + sizeof(protocol_t::eor_enabled) + sizeof(protocol_t::sga_enabled);

    EXPECT_EQ(sizeof(protocol_t), unpadded_size(members_size, alignof(protocol_t)));
}

TEST(ProtocolLayout, VariableNameHasNoPaddingBetweenFields)
{
    constexpr std::size_t members_size = sizeof(variable_name_t::name)
        + sizeof(variable_name_t::default_text) + sizeof(variable_name_t::variable)
        + sizeof(variable_name_t::minimum) + sizeof(variable_name_t::maximum)
        + sizeof(variable_name_t::default_number) + sizeof(variable_name_t::is_string)
        + sizeof(variable_name_t::configurable) + sizeof(variable_name_t::write_once)
        + sizeof(variable_name_t::gui);

    EXPECT_EQ(sizeof(variable_name_t), unpadded_size(members_size, alignof(variable_name_t)));
}

TEST(ProtocolLayout, MsspEntryHasNoPaddingBetweenFields)
{
    constexpr std::size_t members_size = sizeof(MSSP_t::name) + sizeof(MSSP_t::value);

    EXPECT_EQ(sizeof(MSSP_t), unpadded_size(members_size, alignof(MSSP_t)));
}

} // namespace
