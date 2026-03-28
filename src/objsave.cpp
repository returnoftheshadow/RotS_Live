/* ************************************************************************
 *   File: objsave.c                                     Part of CircleMUD *
 *  Usage: loading/saving player objects for rent and crash-save           *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "pkill.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "account_management.h"

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

/* these factors should be unique integers */
#define RENT_FACTOR 1
#define CRYO_FACTOR 4
#define RENT_HALFTIME 300
#define FORCERENT_FACTOR 10

// defines for follower saving
#define FOL_MOUNT 0
#define FOL_ORC_FRIEND 1
#define FOL_TAMED 2
#define FOL_GUARDIAN 3

#define MIN_RANK 1
#define MAX_RANK 10

int cost_per_day(struct obj_data* obj);

extern struct room_data world;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct descriptor_data* descriptor_list;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern int r_mortal_start_room[];
extern int r_bugged_start_room;

/* Extern functions */

ACMD(do_action);
ACMD(do_tell);
SPECIAL(receptionist);
SPECIAL(cryogenicist);

int Crash_alias_load(struct char_data* ch, FILE* fp);
void Crash_follower_load(struct char_data* ch, FILE* fp);
int calc_load_room(struct char_data* ch, int load_result);
int Crash_get_filename(char* orig_name, char* filename);

FILE* fd;
int Crash_is_unrentable(struct obj_data* obj);

namespace {

std::unordered_map<std::string, std::string> g_staged_account_backed_object_bytes;

std::string account_backed_object_stage_key(const char_data* character)
{
    if (character == nullptr || GET_NAME(character) == nullptr || *GET_NAME(character) == '\0')
        return "";

    std::string key = GET_NAME(character);
    for (char& ch : key)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

std::string build_empty_account_backed_object_bytes()
{
    std::string bytes;

    const rent_info rent {};
    const follower_file_elem follower_sentinel { -17, 0, 0, 0, 0, 0, 0 };

    obj_file_elem object_sentinel {};
    object_sentinel.item_number = SENTINEL_ITEM_ID_VALUE;
    object_sentinel.item_number_deprecated = DEPRECATED_ID_VALUE;

    const sh_int board_points[MAX_MAXBOARD] = {};
    const char alias_terminator[20] = {};

    bytes.append(reinterpret_cast<const char*>(&rent), sizeof(rent));
    bytes.append(reinterpret_cast<const char*>(&object_sentinel), sizeof(object_sentinel));
    bytes.append(reinterpret_cast<const char*>(board_points), sizeof(board_points));
    bytes.append(alias_terminator, sizeof(alias_terminator));
    bytes.append(reinterpret_cast<const char*>(&follower_sentinel), sizeof(follower_sentinel));
    return bytes;
}

bool read_crashsave_record(FILE* file, void* buffer, size_t size, size_t count, const char* context)
{
    if (std::fread(buffer, size, count, file) == count)
        return true;

    if (std::ferror(file))
        perror(context);
    else {
        sprintf(buf1, "SYSERR: truncated crashsave data while %s.", context);
        log(buf1);
    }

    return false;
}

bool take_staged_account_backed_object_bytes_for_character(const char_data* character, std::string* bytes)
{
    const std::string stage_key = account_backed_object_stage_key(character);
    if (stage_key.empty())
        return false;

    const auto it = g_staged_account_backed_object_bytes.find(stage_key);
    if (it == g_staged_account_backed_object_bytes.end())
        return false;

    *bytes = it->second.empty() ? build_empty_account_backed_object_bytes() : it->second;
    g_staged_account_backed_object_bytes.erase(it);
    return true;
}

FILE* open_account_backed_object_stream(const char_data* character)
{
    std::string object_bytes;
    if (!take_staged_account_backed_object_bytes_for_character(character, &object_bytes))
        return nullptr;

    FILE* stream = tmpfile();
    if (stream == nullptr) {
        sprintf(buf1, "SYSERR: unable to create temporary object stream for %s: %s", GET_NAME(character), strerror(errno));
        log(buf1);
        return nullptr;
    }

    if (std::fwrite(object_bytes.data(), sizeof(char), object_bytes.size(), stream) != object_bytes.size()) {
        sprintf(buf1, "SYSERR: unable to stage account-backed object data for %s.", GET_NAME(character));
        log(buf1);
        std::fclose(stream);
        return nullptr;
    }

    if (std::fflush(stream) != 0 || std::fseek(stream, 0, SEEK_SET) != 0) {
        sprintf(buf1, "SYSERR: unable to rewind account-backed object stream for %s.", GET_NAME(character));
        log(buf1);
        std::fclose(stream);
        return nullptr;
    }

    return stream;
}

bool read_binary_file_contents(const char* path, std::string* bytes)
{
    if (path == nullptr || bytes == nullptr)
        return false;

    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;

    std::string loaded_bytes;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            loaded_bytes.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            const bool had_error = std::ferror(file) != 0;
            std::fclose(file);
            if (had_error)
                return false;
            break;
        }
    }

    *bytes = std::move(loaded_bytes);
    return true;
}

void refresh_account_backed_object_file(const char_data* character)
{
    if (character == nullptr || IS_NPC(character))
        return;

    char path[MAX_INPUT_LENGTH];
    if (!Crash_get_filename(const_cast<char*>(GET_NAME(character)), path))
        return;

    std::string object_bytes;
    if (!read_binary_file_contents(path, &object_bytes))
        return;

    std::string error_message;
    if (!account::write_linked_character_object_file(".", GET_NAME(character), object_bytes, &error_message) && !error_message.empty()) {
        sprintf(buf1, "SYSERR: failed to refresh account-native object file for %s: %s",
            GET_NAME(character), error_message.c_str());
        log(buf1);
    }
}

} // namespace

void stage_account_backed_object_bytes_for_character(const struct char_data* ch, const char* bytes, size_t length)
{
    const std::string stage_key = account_backed_object_stage_key(ch);
    if (stage_key.empty())
        return;

    if (bytes == nullptr || length == 0) {
        g_staged_account_backed_object_bytes[stage_key] = std::string();
        return;
    }

    g_staged_account_backed_object_bytes[stage_key] = std::string(bytes, length);
}

void clear_account_backed_object_bytes_for_character(const struct char_data* ch)
{
    const std::string stage_key = account_backed_object_stage_key(ch);
    if (stage_key.empty())
        return;

    g_staged_account_backed_object_bytes.erase(stage_key);
}

int Crash_get_filename(char* orig_name, char* filename)
{

    char *ptr, name[30];

    if (!*orig_name)
        return 0;
    strcpy(name, orig_name);
    for (ptr = name; *ptr; ptr++)
        *ptr = tolower(*ptr);

    switch (tolower(*name)) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        sprintf(filename, "plrobjs/A-E/%s.obj", name);
        break;
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        sprintf(filename, "plrobjs/F-J/%s.obj", name);
        break;
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        sprintf(filename, "plrobjs/K-O/%s.obj", name);
        break;
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        sprintf(filename, "plrobjs/P-T/%s.obj", name);
        break;
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        sprintf(filename, "plrobjs/U-Z/%s.obj", name);
        break;
    default:
        sprintf(filename, "plrobjs/ZZZ/%s.obj", name);
        break;
    }
    return 1;
}

FILE* Crash_get_file_by_name(char* name, char* mode)
{
    FILE* fp;

    if (!Crash_get_filename(name, buf))
        return 0;
    if (!(fp = fopen(buf, mode))) {
        log("crashsave: mark0");
        return 0;
    }
    return fp;
}

int Crash_delete_file(char* name)
{

    char filename[50];
    FILE* fl;

    if (!Crash_get_filename(name, filename))
        return 0;
    if (!(fl = fopen(filename, "rb"))) {
        if (errno != ENOENT) { /* if it fails but NOT because of no file */
            sprintf(buf1, "SYSERR: deleting crash file %s (1)", filename);
            perror(buf1);
        }
        return 0;
    }
    fclose(fl);

    if (unlink(filename) < 0) {
        if (errno != ENOENT) { /* if it fails, NOT because of no file */
            sprintf(buf1, "SYSERR: deleting crash file %s (2)", filename);
            perror(buf1);
        }
    }

    return (1);
}

int Crash_delete_crashfile(struct char_data* ch)
{

    char fname[MAX_INPUT_LENGTH];
    struct rent_info rent;
    FILE* fl;

    if (!Crash_get_filename(GET_NAME(ch), fname))
        return 0;
    if (!(fl = fopen(fname, "rb"))) {
        if (errno != ENOENT) { /* if it fails, NOT because of no file */
            sprintf(buf1, "SYSERR: checking for crash file %s (3)", fname);
            perror(buf1);
        }
        return 0;
    }

    if (!feof(fl))
        fread(&rent, sizeof(struct rent_info), 1, fl);
    fclose(fl);
    if (rent.rentcode == RENT_CRASH)
        Crash_delete_file(GET_NAME(ch));
    return 1;
}

int Crash_clean_file(char* name)
{

    char fname[MAX_STRING_LENGTH];
    struct rent_info rent;

    FILE* fl;

    if (!Crash_get_filename(name, fname))
        return 0;
    /*
     * open for write so that permission problems will be flagged now,
     * at boot time.
     */

    if (!(fl = fopen(fname, "r+b"))) {
        if (errno != ENOENT) { /* if it fails, NOT because of no file */
            sprintf(buf1, "SYSERR: OPENING OBJECT FILE %s (4)", fname);
            perror(buf1);
        }
        return 0;
    }
    if (!feof(fl))
        fread(&rent, sizeof(struct rent_info), 1, fl);
    fclose(fl);
    return 0;
}

void update_obj_file(void)
{

    int i;

    for (i = 0; i <= top_of_p_table; i++)
        Crash_clean_file((player_table + i)->name);
    return;
}

extern int generic_scalp;

obj_data* load_scalp(int number);

struct obj_data*

Crash_obj2char(struct char_data* ch, struct obj_file_elem* object)
{
    /*
     * this function loads an object with a virtual number of
     * `object->item_number', and stores it in `obj'.  `obj'
     * is then the object "prototype."  we modify that prototype
     * with what is stored in the obj_file_elem `object'.
     */
    int j;
    struct obj_data* obj;

    if (real_object(object->item_number) > -1) {
        /* somewhat awkward, accounting for scalps... */
        if (object->item_number == generic_scalp) {

            /* player id numbers exceed sh_int size, so we started stashing the id in extra_flags.
             * scalp items don't use them, and load scalp knows where to put it. */
            int head_number = object->value[4];
            if (object->extra_flags != 0) {
                head_number = object->extra_flags;
            }
            obj = load_scalp(head_number);
        } else {
            obj = read_object(object->item_number, VIRT);
            obj->obj_flags.extra_flags = object->extra_flags;
            obj->obj_flags.timer = object->timer;
            obj->obj_flags.bitvector = object->bitvector;
            obj->loaded_by = object->loaded_by;

            /*
             * drink containers and light sources are the only types of items
             * that should be different in the obj_file_elem than our prototypes
             */
            if (obj->obj_flags.type_flag == ITEM_LIGHT || obj->obj_flags.type_flag == ITEM_DRINKCON) {
                obj->obj_flags.value[0] = object->value[0];
                obj->obj_flags.value[1] = object->value[1];
                obj->obj_flags.value[2] = object->value[2];
                obj->obj_flags.value[3] = object->value[3];
                obj->obj_flags.value[4] = object->value[4];
            }

            for (j = 0; j < MAX_OBJ_AFFECT; j++)
                obj->affected[j] = object->affected[j];
        }
        return obj;
    }
    return 0;
}

const char* overflow_str = "The buffer was overflowed, aborting.\r\n";
const size_t overflow_len = strlen(overflow_str) + 1;

void Crash_listrent(struct char_data* ch, char* name)
{
    FILE* fl;
    char buf[MAX_STRING_LENGTH];
    size_t bufpt, max_space;
    struct obj_file_elem object;
    struct obj_data* obj;
    struct rent_info rent;

    /* Get the file by the player's name */
    if (!(fl = Crash_get_file_by_name(name, "rb"))) {
        sprintf(buf, "%s has no rent file.\n\r", name);
        log(buf);
        send_to_char(buf, ch);
        return;
    }

    /* Read in a rent_info struct in binary format */
    if (!feof(fl))
        fread(&rent, sizeof(struct rent_info), 1, fl);

    max_space = MAX_STRING_LENGTH - overflow_len;

    bufpt = snprintf(buf, max_space, "%s rents with:\n\r", name);
    while (!feof(fl)) {
        fread(&object, sizeof(struct obj_file_elem), 1, fl);
        if (ferror(fl)) {
            fclose(fl);
            return;
        }
        if (!feof(fl))

            if (object.item_number_deprecated != DEPRECATED_ID_VALUE) {
                object.item_number = object.item_number_deprecated;
                object.item_number_deprecated = DEPRECATED_ID_VALUE;
            }

        if (real_object(object.item_number) > -1) {
            obj = read_object(object.item_number, VIRT);

            /* If we would overflow the buffer, don't add anymore */

            if (bufpt >= max_space - strlen(obj->short_description) - 1) {
                snprintf(buf + bufpt, overflow_len, overflow_str);
                send_to_char(buf, ch);
                extract_obj(obj);
                fclose(fl);
                return;
            }

            bufpt += snprintf(buf + bufpt, max_space - bufpt, "%s\n\r", obj->short_description);

            extract_obj(obj);
        }
    }
    send_to_char(buf, ch);
    fclose(fl);
}

int Crash_write_rentcode(struct char_data* ch, FILE* fl, struct rent_info* rent)
{
    if (fwrite(rent, sizeof(struct rent_info), 1, fl) < 1) {
        perror("Writing rent code.");
        return 0;
    }
    return 1;
}

//============================================================================
// This function fixes the fact that we add containers to characters before we
// add items to them, so the item is being added to the character at 0 weight.
// This allows characters to get infinite dodge.
//============================================================================
void recalc_worn_weight(char_data* character)
{
    extern sh_int encumb_table[MAX_WEAR];
    extern sh_int leg_encumb_table[MAX_WEAR];

    character->specials.worn_weight = 0;
    character->points.encumb = 0;
    character->specials.encumb_weight = 0;
    character->specials2.leg_encumb = 0;

    for (int item_slot = 0; item_slot < MAX_WEAR; ++item_slot) {
        obj_data* item = character->equipment[item_slot];
        if (item) {
            int item_weight = item->get_weight();
            sh_int encumb_value = encumb_table[item_slot];
            sh_int leg_encumb_value = leg_encumb_table[item_slot];

            character->points.encumb += item->obj_flags.value[2] * encumb_value;
            character->specials2.leg_encumb += item->obj_flags.value[2] * leg_encumb_value;

            if (encumb_value > 0) {
                character->specials.encumb_weight += item_weight * encumb_value;
            } else {
                character->specials.encumb_weight += item_weight / 2;
            }

            character->specials.worn_weight += item_weight;
        }
    }
}

FILE* Crash_load(char_data* character)
{
    FILE* fl;
    struct obj_data* equip_array[11];
    struct obj_data* obj;
    struct obj_file_elem object;
    struct rent_info rent;
    int cost, orig_rent_code, equip_lost;
    int num_of_hours, tmp, equip_counter;
    struct obj_data dummy_sack;

    clear_object(&dummy_sack);

    equip_lost = 0;

    /* zero out our equipment array */
    for (tmp = 0; tmp < 11; tmp++)
        equip_array[tmp] = 0;

    /* ok. is their rent file intact? */
    fl = Crash_get_file_by_name(GET_NAME(character), "r+b");
    if (!fl)
        fl = open_account_backed_object_stream(character);

    if (!fl) {
        send_to_char("*** Your equipment was lost! Please contact an immortal. ***\n\r", character);
        sprintf(buf, "%s entering game with no equipment.", GET_NAME(character));
        GET_ALIAS(character) = 0;
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        character->specials2.load_room = calc_load_room(character, RENT_UNDEF);
        return fl;
    }
    if (!read_crashsave_record(fl, &rent, sizeof(struct rent_info), 1, "reading crash rent data in Crash_load")) {
        std::fclose(fl);
        return nullptr;
    }

    /* ok, we have a file. now we find out how much to charge them */
    cost = (rent.net_cost_per_hour * RENT_HALFTIME * num_of_hours / (RENT_HALFTIME + num_of_hours));

    /* now we're finding out how long we need to charge them */
    if ((rent.rentcode == RENT_RENTED) || (rent.rentcode == RENT_TIMEDOUT) || (rent.rentcode == RENT_FORCED)) {
        num_of_hours = (time(0) - rent.time) / SECS_PER_REAL_HOUR;
        /* RENT FORMULA */
        cost = (rent.net_cost_per_hour * RENT_HALFTIME * num_of_hours / (RENT_HALFTIME + num_of_hours));

        /* extra charging for timeouts and link-renters */
        if ((rent.rentcode == RENT_TIMEDOUT) || (rent.rentcode == RENT_FORCED))
            cost *= FORCERENT_FACTOR;

        cost /= 100; /* TEMPORARY fix for high rent. should be looked into */

        /* can they pay for their rent? */
        if (cost > GET_GOLD(character)) {
            equip_lost = 1;
            sprintf(buf, "%s entering game, rented equipment lost (no $).",
                GET_NAME(character));
            mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        } else {
            equip_lost = 0;
            GET_GOLD(character) = std::max(GET_GOLD(character) - cost, 0);
        }
    }

    switch (orig_rent_code = rent.rentcode) {
    case RENT_RENTED:
        sprintf(buf, "%s un-renting and entering game.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_CRASH:
        sprintf(buf, "%s retrieving crash-saved items and entering game.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_CAMP:
        sprintf(buf, "%s un-camping and entering game.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_FORCED:
    case RENT_TIMEDOUT:
        sprintf(buf, "%s retrieving force-saved items and entering game.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_QUIT:
        sprintf(buf, "%s un-quit and entering game.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    default:
        sprintf(buf, "WARNING: %s entering game with undefined rent code.", GET_NAME(character));
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    }

    equip_counter = 1;
    while (true) {
        if (!read_crashsave_record(fl, &object, sizeof(struct obj_file_elem), 1, "reading crash object data in Crash_load")) {
            std::fclose(fl);
            return nullptr;
        }

        if (object.item_number_deprecated != DEPRECATED_ID_VALUE) {
            object.item_number = object.item_number_deprecated;
            object.item_number_deprecated = DEPRECATED_ID_VALUE;
        }

        if (object.item_number == SENTINEL_ITEM_ID_VALUE) /* the alias marker */
            break;

        if (!equip_lost) {
            if (object.item_number < 0)
                obj = &dummy_sack;
            else
                obj = Crash_obj2char(character, &object);

            if (!obj) {
                sprintf(buf, "LOAD ERROR, equipment lost for %s.", GET_NAME(character));
                log(buf);
                obj = &dummy_sack;
            }

            // We have an object, set the "player touched this" variable to 1.
            obj->touched = 1;

            if (object.wear_pos < 0)
                object.wear_pos = MAX_WEAR;
            if (object.wear_pos < MAX_WEAR) {
                if (obj != &dummy_sack)
                    equip_char(character, obj, object.wear_pos);
                equip_array[0] = obj;
            } else if ((object.wear_pos == MAX_WEAR) || !equip_array[0]) {
                if (obj != &dummy_sack)
                    obj_to_char(obj, character);
                equip_array[0] = obj;
            } else {
                if (obj != &dummy_sack)
                    obj_to_obj(obj, equip_array[object.wear_pos - MAX_WEAR - 1], TRUE);
                equip_array[object.wear_pos - MAX_WEAR] = obj;
            }
        }
    }
    while (dummy_sack.contains) {
        obj = dummy_sack.contains;
        obj_from_obj(obj);
        obj_to_char(obj, character);
    }
    if (equip_lost) {
        send_to_char("\n\rYou could not afford your rent.\n\r"
                     "Your possesions have been sold to pay your debt!\n\r",
            character);
        if (!(character->specials2.load_room))
            character->specials2.load_room = calc_load_room(character, RENT_UNDEF);
    }

    recalc_worn_weight(character);

    character->specials2.load_room = calc_load_room(character, rent.rentcode);

    return fl;
}

void load_character(struct char_data* ch)
{
    extern struct char_data* character_list;
    FILE* fp;

    if (ch->in_room == NOWHERE)
        ch->in_room = ch->specials2.load_room;

    fp = Crash_load(ch);

    ch->next = character_list;
    character_list = ch;

    char_to_room(ch, ch->specials2.load_room);
    act("$n has entered the game.", TRUE, ch, 0, 0, TO_ROOM);

    if (fp) {
        if (!(feof(fp))) {
            Crash_alias_load(ch, fp);
            Crash_follower_load(ch, fp);
        }
        fclose(fp);
    }
}

int calc_load_room(struct char_data* ch, int load_result)
{
    int load_room;
    int old_room;
    int i;

    extern const int mortal_maze_room[MAX_MAZE_RENT_MAPPINGS][2];
    extern int r_immort_start_room;
    extern int r_frozen_start_room;

    old_room = ch->specials2.load_room;
    load_room = real_room(old_room);

    /* if an immortal's load_room < 0, put them in the imm start room */
    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        if (PLR_FLAGGED(ch, PLR_LOADROOM)) {
            if ((load_room = real_room(GET_LOADROOM(ch))) < 0)
                load_room = r_immort_start_room;
        } else
            load_room = r_immort_start_room;
        /* if they're INVSTART then we start them invis */
        if (PLR_FLAGGED(ch, PLR_INVSTART))
            GET_INVIS_LEV(ch) = GET_LEVEL(ch);
    } else {
        /* frozen people go to the frozen realroom */
        if (PLR_FLAGGED(ch, PLR_FROZEN))
            load_room = r_frozen_start_room;
        else {
            if (ch->in_room == NOWHERE)
                load_room = r_mortal_start_room[GET_RACE(ch)];
            else if ((load_room = real_room(ch->in_room)) < 0)
                load_room = r_mortal_start_room[GET_RACE(ch)];

            /* Look through maze mappings. If ch was in a maze room
             * before termination of game, he/she will unrent in the
             * mapped-to rooms. */
            for (i = 0; i < MAX_MAZE_RENT_MAPPINGS; i++)
                if (old_room / 100 == mortal_maze_room[i][0] / 100) { /* room/100 = zone */
                    load_room = real_room(mortal_maze_room[i][1]);
                    break;
                }
        }
    }

    if ((load_result == RENT_CRASH) && (ch->in_room >= EXTENSION_ROOM_HEAD))
        log("Error: objsave.cc tried to load in room > EXTENSION_ROOM_HEAD");
    if (GET_RACE(ch) == 0)
        load_room = r_immort_start_room;
    if (load_room < 0)
        load_room = r_mortal_start_room[GET_RACE(ch)];

    /* here checking for bugged characters */
    if (ch->tmpabilities.str < 1 || ch->abilities.str < 1 || ch->tmpabilities.dex < 1 || ch->abilities.dex < 1 || ch->tmpabilities.move < 1 || ch->points.spirit < 0 || ch->points.spirit > 100000 || ch->tmpabilities.move > 1000)
        load_room = real_room(r_bugged_start_room);

    /* Special exception for characters that aren't level 1 yet... */
    if (!GET_LEVEL(ch))
        load_room = r_mortal_start_room[GET_RACE(ch)];

    return load_room;
}

int Crash_obj2store(obj_data* obj, char_data* ch,
    int pos, FILE* fl)
{
    obj_file_elem object;

    // Assign this the deprecation value so we know that this is a new save.
    object.item_number_deprecated = DEPRECATED_ID_VALUE;

    if (obj->item_number >= 0) {
        object.item_number = obj_index[obj->item_number].virt;
    } else {
        object.item_number = obj->item_number;
    }

    object.value[0] = obj->obj_flags.value[0];
    object.value[1] = obj->obj_flags.value[1];
    object.value[2] = obj->obj_flags.value[2];
    object.value[3] = obj->obj_flags.value[3];
    object.value[4] = obj->obj_flags.value[4];
    object.extra_flags = obj->obj_flags.extra_flags;
    object.weight = obj->obj_flags.weight;
    object.timer = obj->obj_flags.timer;
    object.bitvector = obj->obj_flags.bitvector;
    object.loaded_by = obj->loaded_by;
    for (int index = 0; index < MAX_OBJ_AFFECT; index++)
        object.affected[index] = obj->affected[index];
    object.wear_pos = pos;

    // Stash the player_id in extra_flags for scalps, since we have an array of shorts and player
    // ids can exceed that.  Scalp loading knows how to interpret this, and assigns the in-game object
    // an extra_flags of 0.
    if (object.item_number == generic_scalp) {
        object.extra_flags = obj->obj_flags.value[4];
    }

    if (fwrite(&object, sizeof(obj_file_elem), 1, fl) < 1) {
        perror("Writing crash data Crash_obj2store");
        return 0;
    }

    return 1;
}

int Crash_save(struct obj_data* obj, struct char_data* ch, int pos, FILE* fp);

void Crash_follower_save(struct char_data* ch, FILE* fp)
{
    extern struct index_data* mob_index;
    struct follow_type *k, *next_fol;
    struct follower_file_elem fol_elem, dummy_fol;
    int x;
    struct obj_file_elem dummy_object;

    dummy_object.item_number = SENTINEL_ITEM_ID_VALUE;
    dummy_object.item_number_deprecated = DEPRECATED_ID_VALUE;
    for (k = ch->followers; k; k = next_fol) {
        next_fol = k->next;
        if (!IS_NPC(k->follower))
            continue;
        if (k->follower->in_room != ch->in_room)
            continue;
        fol_elem.fol_vnum = mob_index[k->follower->nr].virt;
        fol_elem.mount_vnum = 0;
        if (IS_RIDING(k->follower))
            if ((k->follower->mount_data.mount)->mount_data.rider == k->follower)
                fol_elem.mount_vnum = mob_index[k->follower->mount_data.mount->nr].virt;
        if (k->follower->followers)
            if (IS_NPC(k->follower->followers->follower))
                fol_elem.mount_vnum = mob_index[k->follower->followers->follower->nr].virt;
        fol_elem.wimpy = k->follower->specials2.wimp_level;
        fol_elem.exp = k->follower->points.exp;
        if (MOB_FLAGGED(k->follower, MOB_ORC_FRIEND))
            fol_elem.flag_config = FOL_ORC_FRIEND;
        else if (affected_by_spell(k->follower, SKILL_TAME))
            fol_elem.flag_config = FOL_TAMED;
        else if (MOB_FLAGGED(k->follower, MOB_PET))
            fol_elem.flag_config = FOL_GUARDIAN;
        else
            fol_elem.flag_config = FOL_MOUNT;
        fol_elem.spare1 = 0;
        fol_elem.spare2 = 0;
        if (fwrite(&fol_elem, sizeof(struct follower_file_elem), 1, fp) < 1) {
            perror("Writing crash data Crash_follower_save");
            return;
        }
        for (x = 0; x < MAX_WEAR; x++)
            if (k->follower->equipment[x])
                if (!Crash_is_unrentable(k->follower->equipment[x]))
                    if (!Crash_save(k->follower->equipment[x], k->follower, x, fp)) {
                        fclose(fp);
                        return;
                    }
        if (fwrite(&dummy_object, sizeof(struct obj_file_elem), 1, fp) < 1) {
            perror("Writing dummy_object Crash_follower_save");
            return;
        }
    }
    if (IS_RIDING(ch)) {
        fol_elem.fol_vnum = mob_index[ch->mount_data.mount->nr].virt;
        fol_elem.mount_vnum = 0;
        fol_elem.wimpy = 0;
        fol_elem.exp = 0;
        fol_elem.spare1 = 0;
        fol_elem.spare2 = 0;
        fol_elem.flag_config = 0;
        if (fwrite(&fol_elem, sizeof(struct follower_file_elem), 1, fp) < 1) {
            perror("Writing crash data Crash_follower_save ch->mount");
            return;
        }
        if (fwrite(&dummy_object, sizeof(struct obj_file_elem), 1, fp) < 1) {
            perror("Writing crash data Crash_follower_save mount dummy_object");
            return;
        }
    }
    dummy_fol.fol_vnum = -17;
    if (fwrite(&dummy_fol, sizeof(struct follower_file_elem), 1, fp) < 1) {
        perror("Writing crash data Crash_follower_save");
        return;
    }
}

void Crash_follower_load(struct char_data* ch, FILE* fp)
{
    struct follower_file_elem fol_elem;
    struct char_data *mob, *mount;
    struct affected_type af;
    struct obj_file_elem object;
    struct obj_data* obj;
    int tmp;

    do {
        if (!read_crashsave_record(fp, &fol_elem, sizeof(struct follower_file_elem), 1, "reading follower data in Crash_follower_load")) {
            fclose(fp);
            return;
        }
        if (fol_elem.fol_vnum == -17)
            break;
        if ((tmp = real_mobile(fol_elem.fol_vnum)) < 0)
            break;
        mob = read_mobile(tmp, REAL);
        char_to_room(mob, ch->in_room);

        while (true) {
            if (!read_crashsave_record(fp, &object, sizeof(struct obj_file_elem), 1, "reading follower object data in Crash_follower_load")) {
                fclose(fp);
                return;
            }

            if (object.item_number_deprecated != DEPRECATED_ID_VALUE) {
                object.item_number = object.item_number_deprecated;
                object.item_number_deprecated = DEPRECATED_ID_VALUE;
            }

            if (object.item_number == SENTINEL_ITEM_ID_VALUE)
                break;

            if (object.wear_pos > MAX_WEAR || object.wear_pos < 0)
                continue;

            obj = Crash_obj2char(mob, &object);
            if (!obj) {
                sprintf(buf, "LOAD ERROR, equipment lost for follower of %s.", GET_NAME(ch));
                log(buf);
                return;
            }
            equip_char(mob, obj, object.wear_pos);
        }
        switch (fol_elem.flag_config) {
        case FOL_ORC_FRIEND: // We should really have an add_recruit function
            af.type = SKILL_RECRUIT;
            af.duration = -1;
            af.modifier = 0;
            af.location = APPLY_NONE;
            af.bitvector = AFF_CHARM;
            affect_to_char(mob, &af);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);
            break;

        case FOL_TAMED: // need same for tame
        {
            af.type = SKILL_TAME;
            af.duration = -1;
            af.modifier = 0;
            af.location = APPLY_NONE;
            af.bitvector = AFF_CHARM;
            affect_to_char(mob, &af);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);

            // If the mob tame is flagged as aggressive and the mob is tamed,
            // assume that the mob has been calmed and add that spell effect to it.
            if (MOB_FLAGGED(mob, MOB_AGGRESSIVE)) {
                affected_type calm_affect;
                calm_affect.type = SKILL_CALM;
                calm_affect.duration = -1;
                calm_affect.modifier = 0;
                calm_affect.location = APPLY_NONE;
                calm_affect.bitvector = 0;
                affect_to_char(mob, &calm_affect);
            }

            if (GET_SPEC(ch) == PLRSPEC_PETS) {
                mob->abilities.str += 2;
                mob->tmpabilities.str += 2;
                mob->constabilities.str += 2;
                mob->points.ENE_regen += 40;
                mob->points.damage += 2;
            }

        } break;

        case FOL_GUARDIAN: // and guardian
        {
            extern int scale_guardian(int, const char_data*, char_data*, bool);
            SET_BIT(mob->specials.affected_by, AFF_CHARM);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);
            int guardian_type = get_guardian_type(ch->player.race, mob);
            scale_guardian(guardian_type, ch, mob, true);
            mob->damage_details.reset();
        } break;

        case FOL_MOUNT: {
            // If the prototype mount is flagged as aggressive but the mob is a mount,
            // assume that the mob has been calmed and add that spell effect to it.
            if (MOB_FLAGGED(mob, MOB_AGGRESSIVE)) {
                affected_type calm_affect;
                calm_affect.type = SKILL_CALM;
                calm_affect.duration = -1;
                calm_affect.modifier = 0;
                calm_affect.location = APPLY_NONE;
                calm_affect.bitvector = 0;
                affect_to_char(mob, &calm_affect);
            }
        } break;
        }
        REMOVE_BIT(MOB_FLAGS(mob), MOB_SPEC);
        mob->specials.store_prog_number = 0;
        REMOVE_BIT(MOB_FLAGS(mob), MOB_AGGRESSIVE);
        REMOVE_BIT(MOB_FLAGS(mob), MOB_STAY_ZONE);
        mob->specials2.pref = 0;
        add_follower(mob, ch, FOLLOW_MOVE);
        if ((tmp = real_mobile(fol_elem.mount_vnum)) > 0) {
            mount = read_mobile(tmp, REAL);
            char_to_room(mount, ch->in_room);
            add_follower(mount, mob, FOLLOW_MOVE);
        }
    } while (1);
}

void extract_followers(struct char_data* ch)
{
    struct obj_data* obj;
    struct follow_type *k, *next_fol;
    int x;

    for (k = ch->followers; k; k = next_fol) {
        next_fol = k->next;
        if (!IS_NPC(k->follower))
            continue;
        for (x = 0; x < MAX_WEAR; x++)
            if (k->follower->equipment[x])
                extract_obj(unequip_char(k->follower, x));
        for (x = 0; (x < 1000) && k->follower->carrying; x++) {
            obj = k->follower->carrying;
            obj_from_char(k->follower->carrying);
            extract_obj(obj);
        }
        extract_followers(k->follower);
        extract_char(k->follower);
    }
}

int Crash_alias_load(struct char_data* ch, FILE* fp)
{
    struct alias_list *list = NULL, *list2;
    int tmp, count;

    GET_ALIAS(ch) = 0;
    count = 0;
    tmp = 0;
    if (!read_crashsave_record(fp, ch->specials.board_point, sizeof(sh_int), MAX_MAXBOARD, "reading board points in Crash_alias_load"))
        return FALSE;
    do {
        CREATE1(list2, alias_list);
        if (!read_crashsave_record(fp, &(list2->keyword), 20, 1, "reading alias keyword in Crash_alias_load")) {
            RELEASE(list2);
            return FALSE;
        }
        if (!*(list2->keyword)) {
            RELEASE(list2);
            return TRUE;
        }
        if (!read_crashsave_record(fp, &tmp, sizeof(int), 1, "reading alias length in Crash_alias_load")) {
            RELEASE(list2);
            return FALSE;
        }
        if (tmp <= 0) {
            log("Alias_load error!");
            RELEASE(list2);
            return FALSE;
        }
        CREATE(list2->command, char, tmp + 1);
        if (!read_crashsave_record(fp, list2->command, tmp, 1, "reading alias command in Crash_alias_load")) {
            RELEASE(list2->command);
            RELEASE(list2);
            return FALSE;
        }
        list2->command[tmp] = '\0';
        if (count > MAX_ALIAS) { // We should have a create_alias function
            RELEASE(list2->command);
            RELEASE(list2); // to take care of this stuff
            continue;
        }
        if (GET_ALIAS(ch) == 0)
            list = GET_ALIAS(ch) = list2;
        else {
            list->next = list2;
            list = list->next;
        }
        count++;
    } while (1);
}

int Crash_alias_save(struct char_data* ch, FILE* fp)
{
    static struct obj_data dummy_obj;
    struct alias_list* list;
    int tmp, tmp2;

    dummy_obj.item_number = -17;

    if (!Crash_obj2store(&dummy_obj, ch, -1, fp))
        return FALSE;

    fwrite(ch->specials.board_point, sizeof(sh_int), MAX_MAXBOARD, fp);

    if (ch->specials.alias) {

        for (list = ch->specials.alias; list; list = list->next) {
            if (fwrite(&(list->keyword), 20, 1, fp) < 1) {
                perror("Writing crash data Crash_alias_save 1");
                return FALSE;
            }
            tmp = strlen(list->command);

            if (tmp <= 0)
                continue;

            if (fwrite(&(tmp), sizeof(int), 1, fp) < 1) {
                perror("Writing crash data Crash_alias_save 2");
                return FALSE;
            }

            if (fwrite(list->command, tmp, 1, fp) < 1) {
                perror("Writing crash data Crash_alias_save 3");
                return FALSE;
            }
        }
    }

    tmp2 = 0;
    for (tmp = 0; tmp < 20; tmp++) {
        if (fwrite(&tmp2, 1, 1, fp) < 1) {
            perror("Writing crash data Crash_alias_save 4");
            return FALSE;
        }
    }
    return TRUE;
}

int Crash_save(struct obj_data* obj, struct char_data* ch, int pos, FILE* fp)
{
    struct obj_data* tmp;
    int result, tmpos;

    if (!obj)
        return TRUE;

    if (obj) {
        result = Crash_obj2store(obj, ch, pos, fp);
        if (!result) {
            printf("could not store.\n");
            return 0;
        }

        tmpos = (pos < MAX_WEAR) ? MAX_WEAR : pos;
        Crash_save(obj->contains, ch, tmpos + 1, fp);
        Crash_save(obj->next_content, ch, pos, fp);

        for (tmp = obj->in_obj; tmp; tmp = tmp->in_obj)
            GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(obj);
    }

    return TRUE;
}

void Crash_restore_weight(struct obj_data* obj)
{
    if (obj) {
        Crash_restore_weight(obj->contains);
        Crash_restore_weight(obj->next_content);
        if (obj->in_obj)
            GET_OBJ_WEIGHT(obj->in_obj) += GET_OBJ_WEIGHT(obj);
    }
}

void Crash_extract_objs(struct obj_data* obj)
{
    if (obj) {
        Crash_extract_objs(obj->contains);
        Crash_extract_objs(obj->next_content);
        extract_obj(obj);
    }
}

int Crash_is_unrentable(struct obj_data* obj)
{
    if (!obj)
        return 0;

    if (IS_SET(obj->obj_flags.extra_flags, ITEM_NORENT) || (obj->obj_flags.cost_per_day < -1) || (obj->item_number <= -1) || (GET_ITEM_TYPE(obj) == ITEM_KEY))
        return 1;

    return 0;
}

void Crash_extract_norents(struct obj_data* obj)
{
    if (obj) {
        Crash_extract_norents(obj->contains);
        Crash_extract_norents(obj->next_content);
        if (Crash_is_unrentable(obj))
            extract_obj(obj);
    }
}

void Crash_extract_expensive(struct obj_data* obj)
{
    struct obj_data *tobj, *max;

    max = obj;
    for (tobj = obj; tobj; tobj = tobj->next_content)
        if (tobj->obj_flags.cost_per_day > max->obj_flags.cost_per_day)
            max = tobj;
    extract_obj(max);
}

void Crash_calculate_rent(struct obj_data* obj, int* cost)
{
    if (obj) {
        *cost += MAX(0, cost_per_day(obj) >> 4);
        Crash_calculate_rent(obj->contains, cost);
        Crash_calculate_rent(obj->next_content, cost);
    }
}

void Crash_crashsave(struct char_data* ch, int rent_code)
{
    struct rent_info rent;
    int j;
    FILE* fp;

    if (IS_NPC(ch))
        return;

    if (!(fp = Crash_get_file_by_name(GET_NAME(ch), "wb"))) {
        log("Couldn't open save file.");
        return;
    }
    rent.rentcode = rent_code;

    rent.time = time(0);
    if (!Crash_write_rentcode(ch, fp, &rent)) {
        log("crashsave: mark1");
        fclose(fp);
        return;
    }
    if (!Crash_save(ch->carrying, ch, MAX_WEAR, fp)) {
        fclose(fp);
        log("crashsave: mark2");
        return;
    }
    Crash_restore_weight(ch->carrying);

    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j]) {
            if (!Crash_save(ch->equipment[j], ch, j, fp)) {
                fclose(fp);
                return;
            }
            Crash_restore_weight(ch->equipment[j]);
        }
    Crash_alias_save(ch, fp);
    Crash_follower_save(ch, fp);
    fclose(fp);
    refresh_account_backed_object_file(ch);
    REMOVE_BIT(PLR_FLAGS(ch), PLR_CRASH);
}

void Crash_idlesave(struct char_data* ch)
{
    char buf[MAX_INPUT_LENGTH];
    struct rent_info rent;
    int j;
    int cost;
    FILE* fp;

    if (IS_NPC(ch))
        return;

    if (!Crash_get_filename(GET_NAME(ch), buf))
        return;
    if (!(fp = fopen(buf, "w+b")))
        return;

    Crash_extract_norents(ch->carrying);
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j])
            Crash_extract_norents(ch->equipment[j]);

    cost = 0;

    rent.net_cost_per_hour = cost;

    rent.rentcode = RENT_TIMEDOUT;
    rent.time = time(0);
    rent.gold = GET_GOLD(ch);
    if (!Crash_write_rentcode(ch, fp, &rent)) {
        fclose(fp);
        return;
    }

    if (!Crash_save(ch->carrying, ch, MAX_WEAR, fp)) {
        fclose(fp);
        return;
    }
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j]) {
            if (!Crash_save(ch->equipment[j], ch, j, fp)) {
                fclose(fp);
                return;
            }
    }
    Crash_alias_save(ch, fp);
    fclose(fp);
    refresh_account_backed_object_file(ch);

    Crash_extract_objs(ch->carrying);
}

void Crash_rentsave(struct char_data* ch, int cost)
{
    char buf[MAX_INPUT_LENGTH];
    struct rent_info rent;
    struct obj_data* tmpobj;
    int j;
    FILE* fp;

    if (IS_NPC(ch))
        return;

    if (!Crash_get_filename(GET_NAME(ch), buf))
        return;
    if (!(fp = fopen(buf, "w+b")))
        return;

    Crash_extract_norents(ch->carrying);

    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j])
            Crash_extract_norents(ch->equipment[j]);

    rent.net_cost_per_hour = cost;
    rent.rentcode = RENT_RENTED;
    rent.time = time(0);
    rent.gold = GET_GOLD(ch);
    if (!Crash_write_rentcode(ch, fp, &rent)) {
        fclose(fp);
        return;
    }
    if (!Crash_save(ch->carrying, ch, MAX_WEAR, fp)) {
        fclose(fp);
        return;
    }
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j]) {
            tmpobj = unequip_char(ch, j);
            Crash_save(tmpobj, ch, j, fp);
            Crash_extract_objs(tmpobj);
            ch->equipment[j] = 0;
        }
    Crash_alias_save(ch, fp);
    Crash_follower_save(ch, fp);
    extract_followers(ch);
    fclose(fp);
    refresh_account_backed_object_file(ch);

    Crash_extract_objs(ch->carrying);
}

/* ************************************************************************
 * Routines used for the "Offer"                                           *
 ************************************************************************* */

int Crash_report_unrentables(struct char_data* ch, struct char_data* recep,
    struct obj_data* obj)
{
    char buf[128];
    int has_norents = 0;

    if (obj) {
        if (Crash_is_unrentable(obj)) {
            has_norents = 1;
            sprintf(buf, "$n tells you, 'You cannot store %s.'", OBJS(obj, ch));
            act(buf, FALSE, recep, 0, ch, TO_VICT);
        }
        has_norents += Crash_report_unrentables(ch, recep, obj->contains);
        has_norents += Crash_report_unrentables(ch, recep, obj->next_content);
    }

    return has_norents;
}

void Crash_report_rent(struct char_data* ch, struct char_data* recep,
    struct obj_data* obj, long* cost, long* nitems, int factor)
{
    if (obj) {
        if (!Crash_is_unrentable(obj)) {
            (*nitems)++;
            *cost += MAX(0, (cost_per_day(obj) >> 1) * factor);
        }
        Crash_report_rent(ch, recep, obj->contains, cost, nitems, factor);
        Crash_report_rent(ch, recep, obj->next_content, cost, nitems, factor);
    }
}

int Crash_offer_rent(struct char_data* ch, struct char_data* receptionist,
    int factor, char mode)
/* mode == FALSE means we supress output of a successful rent */

{
    char buf[MAX_INPUT_LENGTH];
    int i;
    long totalcost = 0, numitems = 0, norent = 0, timeval;

    norent = Crash_report_unrentables(ch, receptionist, ch->carrying);
    for (i = 0; i < MAX_WEAR; i++)
        norent += Crash_report_unrentables(ch, receptionist, ch->equipment[i]);

    /* they've got norent objects */
    if (norent)
        return -1;

    i = 0;

    totalcost = ch->player.level * factor;

    Crash_report_rent(ch, receptionist, ch->carrying, &totalcost,
        &numitems, factor);

    for (i = 0; i < MAX_WEAR; i++)
        Crash_report_rent(ch, receptionist, ch->equipment[i], &totalcost,
            &numitems, factor);

    /* nothing worth renting with */
    if (!numitems) {
        act("$n tells you, 'But you are not carrying anything!  Just quit!'",
            FALSE, receptionist, 0, ch, TO_VICT);
        return -1;
    }

    /* RENT FORMULA */
    if (mode) {
        sprintf(buf, "$n tells you, 'It will cost you %s%s.'",
            money_message(totalcost * RENT_HALFTIME * 24 / (RENT_HALFTIME + 24)),
            (factor == RENT_FACTOR ? " for the first day" : ""));
        act(buf, FALSE, receptionist, 0, ch, TO_VICT);
    }
    totalcost = 0;
    if (GET_GOLD(ch) < RENT_HALFTIME * totalcost)
        timeval = RENT_HALFTIME * GET_GOLD(ch) / totalcost / (RENT_HALFTIME - GET_GOLD(ch) / totalcost);
    else
        timeval = 99999;

    if (!timeval) {
        act("$n tells you, 'You haven't enough money to rent.'",
            FALSE, receptionist, 0, ch, TO_VICT);
        return -1;
    }

    if (mode) {
        do {
            if (timeval >= 99999) {
                sprintf(buf, "$n tells you, 'You have enough gold "
                             "for a lifetime of rent.'");
                break;
            }
            if (timeval < 24) {
                sprintf(buf, "$n tells you, 'You have enough gold "
                             "for %ld hour%s of rent.'",
                    timeval,
                    (timeval == 1) ? "" : "s");
                break;
            }

            timeval /= 24;

            if (timeval < 31) {
                sprintf(buf, "$n tells you, 'You have enough gold "
                             "for %ld day%s of rent.'",
                    timeval,
                    (timeval == 1) ? "" : "s");
                break;
            }

            timeval /= 12;

            if (timeval < 12) {
                sprintf(buf, "$n tells you, 'You have enough gold "
                             "for at least %ld month%s of rent.'",
                    timeval, (timeval == 1) ? "" : "s");
                break;
            }
            sprintf(buf, "$n tells you, 'You have enough gold "
                         "for at least %ld year%s of rent.'",
                timeval, (timeval == 1) ? "" : "s");
        } while (0);

        act(buf, FALSE, receptionist, 0, ch, TO_VICT);
    }

    totalcost = 0;
    return totalcost;
}

int gen_receptionist(struct char_data* ch, int cmd, char* arg, int mode)
{
    int cost = 0;
    static char newname[MAX_NAME_LENGTH + 1];
    char tmpname[MAX_NAME_LENGTH + 1];
    static long retirer = 0, namechanger = 0;
    struct char_data* recep = 0;
    struct char_data* tch;
    int save_room;
    char* action_tabel[9] = { "smile ", "twiddle ", "think ", "frown ", "glare ", "pout ", "sneeze ", "stare ", "yawn " };
    long rent_deadline;

    extern int valid_name(char*);
    extern int rename_char(struct char_data*, char*);
    extern int _parse_name(char*, char*);
    extern int number(int, int);
    extern int r_retirement_home_room;

    if ((!ch->desc) || IS_NPC(ch))
        return (FALSE);

    for (tch = world[ch->in_room].people; (tch) && (!recep); tch = tch->next_in_room)
        if (IS_MOB(tch) && (mob_index[tch->nr].func == receptionist))
            recep = tch;
    if (!recep) {
        log("SYSERR: Fubar'd receptionist.");
        return FALSE;
    }

    if ((cmd != CMD_RENT) && (cmd != CMD_OFFER)) {
        if (!number(0, 30))
            do_action(recep, action_tabel[number(0, 8)], 0, 0, 0);
        return FALSE;
    }
    if (!AWAKE(recep)) {
        send_to_char("She is unable to talk to you...\n\r", ch);
        return TRUE;
    }
    if (!CAN_SEE(recep, ch)) {
        act("$n says, 'I don't deal with people I can't see!'", FALSE, recep, 0, 0, TO_ROOM);
        return TRUE;
    }
    if (IS_AGGR_TO(recep, ch)) {
        act("$n says, 'I won't deal with you, $N.  Get out!'", FALSE, recep, 0, ch, TO_ROOM);
        return TRUE;
    }
    if (IS_RIDING(ch)) {
        send_to_char("Sorry you cannot rent while riding.\r\n", ch);
        return TRUE;
    }
    if (GET_RACE(recep) == RACE_WOOD && GET_RACE(ch) != RACE_WOOD) {
        act("$n tells you, 'Only those with Elven blood may rent here.  Please go elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return TRUE;
    }

    if (GET_RACE(recep) == RACE_DWARF && GET_RACE(ch) != RACE_DWARF) {
        act("$n tells you, 'Dwarves only here.  Try elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_BEORNING && GET_RACE(ch) != RACE_BEORNING) {
        act("$n tells you, 'Beornings only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_OLOGHAI && GET_RACE(ch) != RACE_OLOGHAI) {
        act("$n tells you, 'Olog-Hais only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_HARADRIM && GET_RACE(ch) != RACE_HARADRIM) {
        act("$n tells you, 'Haradrims only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if ((ch) && (!RP_RACE_CHECK(recep, ch))) {
        act("$n tells you, 'You may not stay here.  Please try elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (affected_by_spell(ch, SPELL_ANGER)) {
        if ((GET_RACE(recep) == 11) || (GET_RACE(recep) == 13))
            act("$n tells you, 'Wait until the blood dries, snaga, or you'll join your kill tonight.'",
                FALSE, recep, 0, ch, TO_VICT);
        else
            act("$n tells you, 'You're too angry.  Come back when you've cooled down.'",
                FALSE, recep, 0, ch, TO_VICT);
        return 1;
    }

    if (cmd == CMD_RENT) {
        if ((cost = Crash_offer_rent(ch, recep, 1, FALSE)) < 0)
            return TRUE;

        /* did they put 'retire' or 'namechange' after 'rent'? */
        while (*arg <= ' ' && *arg)
            ++arg;

        if (!strcmp(arg, "retire")) {
            if (!ch->desc)
                return TRUE;

            if (IS_SET(PLR_FLAGS(ch), PLR_RETIRED)) {
                act("$n tells you, 'But you're already retired.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (ch->specials2.idnum != retirer) {
                act("$n tells you, 'So you'd like to retire, eh?'\r\n"
                    "$n tells you, 'Repeat the request to make it final.'",
                    FALSE, recep, 0, ch, TO_VICT);
                retirer = ch->specials2.idnum;
                return TRUE;
            } else {
                retire(ch);
                char_from_room(ch);
                char_to_room(ch, r_retirement_home_room);
                retirer = 0;
            }
        } else if (is_abbrev("namechange", arg)) {
            act("$n tells you, 'Sorry, we're having some internal technical "
                "difficulties.  Please try again later.'",
                FALSE, recep, 0, ch, TO_VICT);
            *newname = 0;
            *tmpname = 0;
            namechanger = 0;
            return TRUE;
            if (!ch->desc)
                return TRUE;

            if (IS_SET(PLR_FLAGS(ch), PLR_IS_NCHANGED)) {
                act("$n tells you, 'Sorry, you can only change your name once in a "
                    "lifetime.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            /* eat off 'namechange' and any space/garbarge */
            while (*arg && isalpha(*arg))
                ++arg;
            while (*arg && *arg <= ' ')
                ++arg;

            if (!*arg) {
                act("$n tells you, 'You have to tell me what to change your name to.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (find_player_in_table(arg, -1) != -1) {
                act("$n tells you, 'Sorry, that name's already in use.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (_parse_name(arg, tmpname) || fill_word(tmpname) || !valid_name(tmpname)) {
                sprintf(buf, "$n tells you, 'Sorry, '%s' is an invalid name.'", arg);
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                *newname = 0;
                *tmpname = 0;
                namechanger = 0;
                return TRUE;
            }
            *tmpname = toupper(*tmpname);

            if (ch->specials2.idnum != namechanger) {
                strncpy(newname, tmpname, MAX_NAME_LENGTH);
                sprintf(buf, "$n tells you, 'You'd like to change your name to '%s', "
                             "is that correct?\r\n"
                             "$n tells you, 'Repeat the request to make it final.'",
                    newname);
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                namechanger = ch->specials2.idnum;
                return TRUE;
            }
            /* they didn't type the same name the second time */
            else if (ch->specials2.idnum == namechanger && *newname && strcmp(newname, tmpname)) {
                strncpy(newname, tmpname, MAX_NAME_LENGTH);
                sprintf(buf, "$n tells you, 'So you'd rather be named '%s'?'\r\n"
                             "$n tells you, 'Repeat the request to make it final.'",
                    newname);
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }
            /* they typed 'rent namechange x' twice in a row */
            else {
                if (rename_char(ch, newname) < 0) {
                    act("$n tells you, 'Sorry, we're having some internal technical "
                        "difficulties.  Please try again later.'",
                        FALSE, recep, 0, ch, TO_VICT);
                    *newname = 0;
                    *tmpname = 0;
                    namechanger = 0;
                    return TRUE;
                }
                act("$n tells you, 'Your secret's safe with me.'",
                    FALSE, recep, 0, ch, TO_VICT);
                SET_BIT(PLR_FLAGS(ch), PLR_IS_NCHANGED);
                *newname = 0;
                *tmpname = 0;
                namechanger = 0;
            }
        }

        /* we don't charge for rent; yet */
        cost = 0;

        /* the normal rent process */
        if (cost) {
            rent_deadline = (GET_GOLD(ch) / (cost));
            if (rent_deadline < RENT_HALFTIME)
                rent_deadline = RENT_HALFTIME * rent_deadline / (RENT_HALFTIME - rent_deadline);
            else {
                rent_deadline = 99999;
                sprintf(buf, "You have enough money for a lifetime of rent.\n\r");
            }

            act(buf, FALSE, recep, 0, ch, TO_VICT);
        }

        if (mode == RENT_FACTOR) {
            affected_type* aff = affected_by_spell(ch, SPELL_FAME_WAR);
            act("$n stores your belongings and helps you into your private chamber.",
                FALSE, recep, 0, ch, TO_VICT);
            if (aff) {
                remove_fame_war_bonuses(ch, aff);
                affect_remove(ch, aff);
            }
            Crash_rentsave(ch, cost);
            sprintf(buf, "%s has rented (%d/day, %d tot.)", GET_NAME(ch),
                cost, GET_GOLD(ch));
        }

        mudlog(buf, NRM, (sh_int)MAX(LEVEL_IMMORT, GET_INVIS_LEV(ch)), TRUE);
        act("$n helps $N into $S private chamber.", FALSE, recep, 0, ch, TO_NOTVICT);
        save_room = ch->in_room;
        extract_char(ch);
        ch->in_room = world[save_room].number;
        save_char(ch, ch->in_room, 0);
    } else { /* Offer */
        Crash_offer_rent(ch, recep, mode, TRUE);
        act("$N gives $n an offer.", FALSE, ch, 0, recep, TO_ROOM);
    }

    return TRUE;
}

SPECIAL(receptionist)
{
    if (callflag != SPECIAL_COMMAND)
        return FALSE;

    return gen_receptionist(ch, cmd, arg, RENT_FACTOR);
}

ACMD(do_rent)
{
    int save_room;

    send_to_char("Field-rent is disabled. You have to go to an inn now.\n\r", ch);
    return;

    if (ch->specials.fighting) {
        send_to_char("No way! You are fighting still!\n\r", ch);
        return;
    }

    if (affected_by_spell(ch, SPELL_ANGER)) {
        send_to_char("You're too angry.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to rent.\n\r", ch);
        return;
    }

    act("You set a camp right on the spot.\n\r"
        "Be aware - this feature will be removed later.",
        FALSE, ch, 0, 0, TO_CHAR);
    act("$n sets a small camp and rents in it.", FALSE, ch, 0, 0, TO_ROOM);

    Crash_rentsave(ch, 0);
    sprintf(buf, "%s has field-rented (%d total gold)", GET_NAME(ch),
        GET_GOLD(ch));

    mudlog(buf, NRM, (sh_int)MAX(LEVEL_IMMORT, GET_INVIS_LEV(ch)), TRUE);

    save_room = ch->in_room;
    extract_char(ch);
    ch->in_room = world[save_room].number;
    save_char(ch, ch->in_room, 0);
}

void Crash_save_all(void)
{
    struct descriptor_data* d;
    for (d = descriptor_list; d; d = d->next) {
        if ((d->connected == CON_PLYNG) && !IS_NPC(d->character)) {
            if (PLR_FLAGGED(d->character, PLR_CRASH)) {
                Crash_crashsave(d->character);
                if (GET_LEVEL(d->character) < LEVEL_IMMORT)
                    save_char(d->character, NOWHERE, 1);
                else
                    save_char(d->character, NOWHERE, 0);
                REMOVE_BIT(PLR_FLAGS(d->character), PLR_CRASH);
            }
        }
    }
}

void Emergency_save(void)
{
    struct descriptor_data* d;
    for (d = descriptor_list; d; d = d->next) {
        if ((d->connected == CON_PLYNG) && !IS_NPC(d->character)) {
            Crash_crashsave(d->character);
            save_char(d->character, r_mortal_start_room[GET_RACE(d->character)], 0);
        }
    }
}

int cost_per_day(struct obj_data* obj)
{
    return (((obj->obj_flags.cost_per_day == -1) ? obj->obj_flags.cost / 100 : obj->obj_flags.cost_per_day) / ((obj->obj_flags.level <= 5) ? 8 : 4));
}

#define RENT_TIME 2592000 /* number of seconds one must remain retired */
long secs_to_unretire(struct char_data* ch)
/* returns the time, in seconds, until one may unretire */
{
    if (!IS_SET(PLR_FLAGS(ch), PLR_RETIRED))
        return 0;

    return (ch->specials2.retiredon + RENT_TIME) - time(0);
}
