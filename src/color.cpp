/* color.cc */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "utils.h"

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
    "off",
    "on",
    "default",
    "\n",
};

int num_of_color_fields = sizeof(color_fields) / sizeof(color_fields[0]);

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
        return;

    for (i = 0; i < 10; ++i)
        ch->profs.colors[i] = ch->profs.color_mask >> (i * 3) & 7;
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
}

ACMD(do_color)
{
    int tmp, num, col;

    half_chop(argument, buf, arg);

    if (!*buf) {
        /* so we report the colors currently set */
        if (!PRF_FLAGGED(ch, PRF_COLOR)) {
            send_to_char("Your colours are turned off.\n\r", ch);
            return;
        }

        send_to_char("Your colours are:\n\r", ch);
        for (tmp = 0; tmp < num_of_color_fields - 4; tmp++) {
            snprintf(buf, sizeof(buf), "%11s: %s\n\r",
                color_fields[tmp], color_color[(int)get_colornum(ch, tmp)]);
            send_to_char(buf, ch);
        }
        return;
    }

    num = old_search_block(buf, 0, strlen(buf), color_fields, FALSE) - 1;

    if (num < 0) {
        send_to_char("Possible arguments are:\n\r", ch);
        buf[0] = 0;
        for (tmp = 0; tmp < num_of_color_fields; tmp++) {
            std::size_t length = strlen(buf);
            if (length >= sizeof(buf) - 1) {
                break;
            }

            std::snprintf(buf + length, sizeof(buf) - length, " %s", color_fields[tmp]);
        }
        strcat(buf, "\n\r");
        send_to_char(buf, ch);
        return;
    }

    if (num == 15) {
        set_colors_default(ch);
        send_to_char("Ok, you'll use the default colour set.\r\n", ch);
        return;
    } else if (num == 14) {
        SET_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned on.\n\r", ch);
        return;
    } else if (num == 13) {
        REMOVE_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned off.\n\r", ch);
        return;
    }

    col = old_search_block(arg, 0, strlen(arg), color_color, TRUE) - 1;

    if (col < 0) {
        send_to_char("Possible colours are:", ch);
        buf[0] = 0;
        for (tmp = 0; tmp < 8; tmp++) {
            std::size_t length = strlen(buf);
            if (length >= sizeof(buf) - 1) {
                break;
            }

            std::snprintf(buf + length, sizeof(buf) - length, " %s", color_color[tmp]);
        }
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
