/* color.cc */

#include <ctype.h>
#include <cstdio>
#include <limits>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "utils.h"

namespace {

    constexpr size_t kColorRenderBufferCount = 8;
    constexpr size_t kColorRenderBufferSize = 96;

    const char* ansi_background_sequence(int color_index)
    {
        static const char* background_sequences[] = {
            "\x1B[49m",
            "\x1B[41m",
            "\x1B[42m",
            "\x1B[43m",
            "\x1B[44m",
            "\x1B[45m",
            "\x1B[46m",
            "\x1B[47m",
            "\x1B[01m\x1B[41m",
            "\x1B[01m\x1B[42m",
            "\x1B[01m\x1B[43m",
            "\x1B[01m\x1B[44m",
            "\x1B[01m\x1B[45m",
            "\x1B[01m\x1B[46m",
            "\x1B[01m\x1B[47m",
        };

        if (color_index < 0 || color_index >= 15)
            return "\x1B[49m";
        return background_sequences[color_index];
    }

    void sync_color_slot_foreground_from_ansi(struct char_prof_data* profs, int col)
    {
        if (profs == nullptr || col < 0 || col >= MAX_COLOR_FIELDS)
            return;

        profs->color_settings[col].foreground.mode = COLOR_VALUE_ANSI16;
        profs->color_settings[col].foreground.ansi = static_cast<unsigned char>(profs->colors[col]);
    }

    void append_escape(char* buffer, size_t buffer_size, size_t* length, const char* escape_sequence)
    {
        if (buffer == nullptr || buffer_size == 0 || length == nullptr || escape_sequence == nullptr)
            return;

        const size_t remaining = (*length < buffer_size) ? (buffer_size - *length) : 0;
        if (remaining == 0)
            return;

        const int written = snprintf(buffer + *length, remaining, "%s", escape_sequence);
        if (written <= 0)
            return;

        *length += static_cast<size_t>(written);
        if (*length >= buffer_size)
            *length = buffer_size - 1;
    }

    void append_truecolor_escape(char* buffer, size_t buffer_size, size_t* length, bool foreground, const color_value_data& value)
    {
        if (buffer == nullptr || buffer_size == 0 || length == nullptr)
            return;

        const size_t remaining = (*length < buffer_size) ? (buffer_size - *length) : 0;
        if (remaining == 0)
            return;

        const int written = snprintf(buffer + *length, remaining, "\x1B[%d;2;%u;%u;%um",
            foreground ? 38 : 48,
            static_cast<unsigned int>(value.red),
            static_cast<unsigned int>(value.green),
            static_cast<unsigned int>(value.blue));
        if (written <= 0)
            return;

        *length += static_cast<size_t>(written);
        if (*length >= buffer_size)
            *length = buffer_size - 1;
    }

    bool has_non_default_background(const color_slot_data& slot)
    {
        return slot.background.mode != COLOR_VALUE_DEFAULT;
    }

    bool parse_integer_token(const char* token, int* value)
    {
        if (token == nullptr || value == nullptr || *token == '\0')
            return false;

        char* end = nullptr;
        const long parsed = strtol(token, &end, 10);
        if (end == token || *end != '\0')
            return false;
        *value = static_cast<int>(parsed);
        return true;
    }

    bool parse_rgb_triplet(char* arguments, int* red, int* green, int* blue)
    {
        if (arguments == nullptr || red == nullptr || green == nullptr || blue == nullptr)
            return false;

        char first[MAX_INPUT_LENGTH];
        char remainder[MAX_INPUT_LENGTH];
        char second[MAX_INPUT_LENGTH];
        char final_token[MAX_INPUT_LENGTH];
        half_chop(arguments, first, remainder);
        half_chop(remainder, second, final_token);
        if (!parse_integer_token(first, red) || !parse_integer_token(second, green) || !parse_integer_token(final_token, blue))
            return false;
        return true;
    }

    bool is_valid_rgb_channel(int value)
    {
        return value >= 0 && value <= 255;
    }

    int parse_hex_channel(char high, char low)
    {
        auto decode = [](char value) -> int {
            if (value >= '0' && value <= '9')
                return value - '0';
            value = static_cast<char>(tolower(value));
            if (value >= 'a' && value <= 'f')
                return 10 + (value - 'a');
            return -1;
        };

        const int high_value = decode(high);
        const int low_value = decode(low);
        if (high_value < 0 || low_value < 0)
            return -1;
        return (high_value << 4) | low_value;
    }

    bool parse_hex_triplet(const char* token, int* red, int* green, int* blue)
    {
        if (token == nullptr || red == nullptr || green == nullptr || blue == nullptr)
            return false;

        const char* value = token;
        if (*value == '#')
            ++value;
        if (strlen(value) != 6)
            return false;

        const int parsed_red = parse_hex_channel(value[0], value[1]);
        const int parsed_green = parse_hex_channel(value[2], value[3]);
        const int parsed_blue = parse_hex_channel(value[4], value[5]);
        if (parsed_red < 0 || parsed_green < 0 || parsed_blue < 0)
            return false;

        *red = parsed_red;
        *green = parsed_green;
        *blue = parsed_blue;
        return true;
    }

    void describe_color_value(const color_value_data& value, int fallback_ansi, char* buffer, size_t buffer_size)
    {
        if (buffer == nullptr || buffer_size == 0)
            return;

        if (value.mode == COLOR_VALUE_TRUECOLOR) {
            snprintf(buffer, buffer_size, "#%02X%02X%02X",
                static_cast<unsigned int>(value.red),
                static_cast<unsigned int>(value.green),
                static_cast<unsigned int>(value.blue));
            return;
        }

        if (value.mode == COLOR_VALUE_ANSI16) {
            snprintf(buffer, buffer_size, "ansi %s", color_color[value.ansi]);
            return;
        }

        if (fallback_ansi != CNRM)
            snprintf(buffer, buffer_size, "ansi %s", color_color[fallback_ansi]);
        else
            snprintf(buffer, buffer_size, "default");
    }

    void show_extended_color_usage(struct char_data* ch)
    {
        send_to_char("Usage:\n\r", ch);
        send_to_char("  color <slot> <ansi colour>\n\r", ch);
        send_to_char("  color <slot> fg ansi <ansi colour>\n\r", ch);
        send_to_char("  color <slot> fg rgb <red> <green> <blue>\n\r", ch);
        send_to_char("  color <slot> fg hex #RRGGBB\n\r", ch);
        send_to_char("  color <slot> fg default\n\r", ch);
        send_to_char("  color <slot> bg ansi <ansi colour>\n\r", ch);
        send_to_char("  color <slot> bg rgb <red> <green> <blue>\n\r", ch);
        send_to_char("  color <slot> bg hex #RRGGBB\n\r", ch);
        send_to_char("  color <slot> bg default\n\r", ch);
    }

    void set_ansi_background(struct char_data* ch, int col, int value)
    {
        if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
            return;

        ch->profs->color_settings[col].background.mode = COLOR_VALUE_ANSI16;
        ch->profs->color_settings[col].background.ansi = static_cast<unsigned char>(value);
        ch->profs->color_settings[col].background.red = 0;
        ch->profs->color_settings[col].background.green = 0;
        ch->profs->color_settings[col].background.blue = 0;
    }

} // namespace

const char* color_fields[] = {
    "narrate",
    "chat",
    "yell",
    "tell",
    "say",
    "roomname",
    "hit",
    "damage",
    "character",
    "object",
    "enemy",
    "description",
    "group",
    "magic",
    "weather",
    "off",
    "on",
    "default",
    "\n",
};

int num_of_color_fields = sizeof(color_fields) / sizeof(color_fields[0]);
static constexpr int kNumConfigurableColorFields = 15;
static constexpr int kColorCommandOff = kNumConfigurableColorFields;
static constexpr int kColorCommandOn = kNumConfigurableColorFields + 1;
static constexpr int kColorCommandDefault = kNumConfigurableColorFields + 2;

static void show_color_slot_summary(struct char_data* ch, int slot)
{
    if (ch == nullptr || ch->profs == nullptr || slot < 0 || slot >= MAX_COLOR_FIELDS)
        return;

    char foreground[64];
    char background[64];
    describe_color_value(ch->profs->color_settings[slot].foreground, ch->profs->colors[slot], foreground, sizeof(foreground));
    describe_color_value(ch->profs->color_settings[slot].background, CNRM, background, sizeof(background));
    snprintf(buf, sizeof(buf), "%11s: fg %s bg %s\n\r", color_fields[slot], foreground, background);
    send_to_char(buf, ch);
}

const char* color_color[] = {
    "normal",
    "red",
    "green",
    "yellow",
    "blue",
    "magenta",
    "cyan",
    "white",
    "bright red",
    "bright green",
    "bright yellow",
    "bright blue",
    "bright magenta",
    "bright cyan",
    "bright white",
    "\n"
};

int num_of_colors = sizeof(color_color) / sizeof(color_color[0]);

char* color_sequence[] = {
    "\x1B[0m",
    "\x1B[31m",
    "\x1B[32m",
    "\x1B[33m",
    "\x1B[34m",
    "\x1B[35m",
    "\x1B[36m",
    "\x1B[37m",
    "\x1B[01m\x1B[31m",
    "\x1B[01m\x1B[32m",
    "\x1B[01m\x1B[33m",
    "\x1B[01m\x1B[34m",
    "\x1B[01m\x1B[35m",
    "\x1B[01m\x1B[36m",
    "\x1B[01m\x1B[37m",
    ""
};

void convert_old_colormask(struct char_file_u* ch)
{
    int i;

    if (!ch->profs.color_mask)
        i = 0;
    else
        for (i = 0; i < 10; ++i)
            ch->profs.colors[i] = ch->profs.color_mask >> (i * 3) & 7;

    for (i = 0; i < MAX_COLOR_FIELDS; ++i) {
        if (ch->profs.color_settings[i].foreground.mode == COLOR_VALUE_DEFAULT)
            sync_color_slot_foreground_from_ansi(&ch->profs, i);
    }
}

char get_colornum(struct char_data* ch, int col)
{
    if (!ch)
        return 0;

    if (!ch->profs)
        return 0;

    return ch->profs->colors[col];
}

void set_colornum(struct char_data* ch, int col, int value)
{
    if (!ch || !ch->profs)
        return;

    ch->profs->colors[col] = value;
    sync_color_slot_foreground_from_ansi(ch->profs, col);
}

int nearest_ansi_color(int red, int green, int blue)
{
    struct AnsiColor {
        int red;
        int green;
        int blue;
    };

    static const AnsiColor ansi_palette[] = {
        { 0, 0, 0 },
        { 170, 0, 0 },
        { 0, 170, 0 },
        { 170, 85, 0 },
        { 0, 0, 170 },
        { 170, 0, 170 },
        { 0, 170, 170 },
        { 170, 170, 170 },
        { 255, 85, 85 },
        { 85, 255, 85 },
        { 255, 255, 85 },
        { 85, 85, 255 },
        { 255, 85, 255 },
        { 85, 255, 255 },
        { 255, 255, 255 },
    };

    int best_index = CNRM;
    long best_distance = std::numeric_limits<long>::max();
    for (int index = 0; index < num_of_colors - 1; ++index) {
        const long red_distance = red - ansi_palette[index].red;
        const long green_distance = green - ansi_palette[index].green;
        const long blue_distance = blue - ansi_palette[index].blue;
        const long distance = red_distance * red_distance + green_distance * green_distance + blue_distance * blue_distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }

    return best_index;
}

void set_truecolor_foreground(struct char_data* ch, int col, int red, int green, int blue)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    color_slot_data& slot = ch->profs->color_settings[col];
    slot.foreground.mode = COLOR_VALUE_TRUECOLOR;
    slot.foreground.red = static_cast<unsigned char>(red);
    slot.foreground.green = static_cast<unsigned char>(green);
    slot.foreground.blue = static_cast<unsigned char>(blue);
    slot.foreground.ansi = static_cast<unsigned char>(nearest_ansi_color(red, green, blue));
    ch->profs->colors[col] = static_cast<char>(slot.foreground.ansi);
}

void set_truecolor_background(struct char_data* ch, int col, int red, int green, int blue)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    color_slot_data& slot = ch->profs->color_settings[col];
    slot.background.mode = COLOR_VALUE_TRUECOLOR;
    slot.background.red = static_cast<unsigned char>(red);
    slot.background.green = static_cast<unsigned char>(green);
    slot.background.blue = static_cast<unsigned char>(blue);
    slot.background.ansi = static_cast<unsigned char>(nearest_ansi_color(red, green, blue));
}

void clear_color_background(struct char_data* ch, int col)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    ch->profs->color_settings[col].background = color_value_data {};
}

const char* get_color_sequence(struct char_data* ch, int col)
{
    static char render_buffers[kColorRenderBufferCount][kColorRenderBufferSize];
    static size_t next_render_buffer = 0;

    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return "";

    const color_slot_data& slot = ch->profs->color_settings[col];
    const color_value_data& foreground = slot.foreground;
    const color_value_data& background = slot.background;

    const bool use_foreground = foreground.mode != COLOR_VALUE_DEFAULT || ch->profs->colors[col] != CNRM;
    const bool use_background = has_non_default_background(slot);
    if (!use_foreground && !use_background)
        return "";

    char* buffer = render_buffers[next_render_buffer];
    next_render_buffer = (next_render_buffer + 1) % kColorRenderBufferCount;
    buffer[0] = '\0';
    size_t length = 0;

    if (foreground.mode == COLOR_VALUE_TRUECOLOR) {
        append_truecolor_escape(buffer, kColorRenderBufferSize, &length, true, foreground);
    } else {
        const int ansi_index = (foreground.mode == COLOR_VALUE_ANSI16)
            ? static_cast<int>(foreground.ansi)
            : static_cast<int>(ch->profs->colors[col]);
        if (ansi_index != CNRM)
            append_escape(buffer, kColorRenderBufferSize, &length, color_sequence[ansi_index]);
    }

    if (background.mode == COLOR_VALUE_TRUECOLOR)
        append_truecolor_escape(buffer, kColorRenderBufferSize, &length, false, background);
    else if (background.mode == COLOR_VALUE_ANSI16)
        append_escape(buffer, kColorRenderBufferSize, &length, ansi_background_sequence(background.ansi));

    return buffer;
}

/*
 * Give 'ch' the set of RotS default colors.
 */
void set_colors_default(struct char_data* ch)
{
    SET_BIT(PRF_FLAGS(ch), PRF_COLOR);
    set_colornum(ch, COLOR_NARR, CYEL);
    set_colornum(ch, COLOR_CHAT, CMAG);
    set_colornum(ch, COLOR_YELL, CRED);
    set_colornum(ch, COLOR_TELL, CGRN);
    set_colornum(ch, COLOR_SAY, CCYN);
    set_colornum(ch, COLOR_ROOM, CYEL);
    set_colornum(ch, COLOR_HIT, CCYN);
    set_colornum(ch, COLOR_DAMG, CRED);
    set_colornum(ch, COLOR_CHAR, CGRN);
    set_colornum(ch, COLOR_OBJ, CCYN);
    set_colornum(ch, COLOR_ENMY, CBWHT);
    set_colornum(ch, COLOR_DESC, CGRN);
    set_colornum(ch, COLOR_GTELL, CGRN);
    set_colornum(ch, COLOR_MAGIC, CBMAG);
    set_colornum(ch, COLOR_WEATHER, CBCYN);
}

ACMD(do_color)
{
    int tmp, num, col;
    char option[MAX_INPUT_LENGTH];
    char remainder[MAX_INPUT_LENGTH];

    half_chop(argument, buf, arg);

    if (!*buf) {
        /* so we report the colors currently set */
        if (!PRF_FLAGGED(ch, PRF_COLOR)) {
            send_to_char("Your colours are turned off.\n\r", ch);
            return;
        }

        send_to_char("Your colours are:\n\r", ch);
        for (tmp = 0; tmp < kNumConfigurableColorFields; tmp++)
            show_color_slot_summary(ch, tmp);
        return;
    }

    num = old_search_block(buf, 0, strlen(buf), color_fields, FALSE) - 1;

    if (num < 0) {
        send_to_char("Possible arguments are:\n\r", ch);
        buf[0] = 0;
        for (tmp = 0; tmp < num_of_color_fields - 1; tmp++)
            sprintf(buf, "%s %s", buf, color_fields[tmp]);
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        show_extended_color_usage(ch);
        return;
    }

    if (num == kColorCommandDefault) {
        set_colors_default(ch);
        send_to_char("Ok, you'll use the default colour set.\r\n", ch);
        return;
    } else if (num == kColorCommandOn) {
        SET_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned on.\n\r", ch);
        return;
    } else if (num == kColorCommandOff) {
        REMOVE_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned off.\n\r", ch);
        return;
    }

    if (!*arg) {
        show_extended_color_usage(ch);
        return;
    }

    half_chop(arg, option, remainder);
    if (!strcasecmp(option, "fg") || !strcasecmp(option, "bg")) {
        const bool foreground = !strcasecmp(option, "fg");
        char mode[MAX_INPUT_LENGTH];
        char value_arguments[MAX_INPUT_LENGTH];
        half_chop(remainder, mode, value_arguments);

        if (!*mode) {
            show_extended_color_usage(ch);
            return;
        }

        if (!strcasecmp(mode, "default")) {
            if (foreground) {
                ch->profs->color_settings[num].foreground = color_value_data {};
                ch->profs->colors[num] = CNRM;
                vsend_to_char(ch, "You set %s foreground to default.\n\r", color_fields[num]);
            } else {
                clear_color_background(ch, num);
                vsend_to_char(ch, "You set %s background to default.\n\r", color_fields[num]);
            }
            return;
        }

        if (!strcasecmp(mode, "ansi")) {
            col = old_search_block(value_arguments, 0, strlen(value_arguments), color_color, TRUE) - 1;
            if (col < 0) {
                send_to_char("Possible colours are:", ch);
                buf[0] = 0;
                for (tmp = 0; tmp < 8; tmp++)
                    sprintf(buf, "%s %s", buf, color_color[tmp]);
                strcat(buf, ".\r\n");
                strcat(buf, "Additionally, you may prefix any of the above colours with 'bright'.\r\n");
                send_to_char(buf, ch);
                return;
            }

            if (foreground) {
                set_colornum(ch, num, col);
                vsend_to_char(ch, "You set %s foreground to %s%s%s.\n\r",
                    color_fields[num], CC_USE(ch, num), color_color[col], CC_NORM(ch));
            } else {
                set_ansi_background(ch, num, col);
                vsend_to_char(ch, "You set %s background to %s.\n\r", color_fields[num], color_color[col]);
            }
            return;
        }

        if (!strcasecmp(mode, "rgb")) {
            int red, green, blue;
            if (!parse_rgb_triplet(value_arguments, &red, &green, &blue)) {
                send_to_char("RGB colours must be provided as three integers.\n\r", ch);
                return;
            }
            if (!is_valid_rgb_channel(red) || !is_valid_rgb_channel(green) || !is_valid_rgb_channel(blue)) {
                send_to_char("RGB values must be between 0 and 255.\n\r", ch);
                return;
            }

            if (foreground) {
                set_truecolor_foreground(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s foreground to #%02X%02X%02X.\n\r", color_fields[num], red, green, blue);
            } else {
                set_truecolor_background(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s background to #%02X%02X%02X.\n\r", color_fields[num], red, green, blue);
            }
            return;
        }

        if (!strcasecmp(mode, "hex")) {
            int red, green, blue;
            if (!parse_hex_triplet(value_arguments, &red, &green, &blue)) {
                send_to_char("Hex colours must look like #RRGGBB.\n\r", ch);
                return;
            }

            if (foreground) {
                set_truecolor_foreground(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s foreground to #%02X%02X%02X.\n\r", color_fields[num], red, green, blue);
            } else {
                set_truecolor_background(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s background to #%02X%02X%02X.\n\r", color_fields[num], red, green, blue);
            }
            return;
        }

        show_extended_color_usage(ch);
        return;
    }

    col = old_search_block(arg, 0, strlen(arg), color_color, TRUE) - 1;

    if (col < 0) {
        send_to_char("Possible colours are:", ch);
        buf[0] = 0;
        for (tmp = 0; tmp < 8; tmp++)
            sprintf(buf, "%s %s", buf, color_color[tmp]);
        strcat(buf, ".\r\n");
        strcat(buf, "Additionally, you may prefix any of the above "
                    "colours with 'bright'.\r\n");
        send_to_char(buf, ch);
        return;
    }

    set_colornum(ch, num, col);

    vsend_to_char(ch, "You colour %s %s%s%s.\n\r",
        color_fields[num],
        CC_USE(ch, num),
        color_color[col],
        CC_NORM(ch));
}
