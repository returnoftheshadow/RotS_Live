/* zone.cc */

#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memmove */
#include <strings.h>

#include "comm.h" /* For TO_ROOM */
#include "db.h" /* For buf2 and struct reset_com */
#include "handler.h" /* For FOLLOW_MOVE */
#include "pkill.h" /* For pkill_get_XXX_fame() */
#include "structs.h" /* For struct owner_list */
#include "utils.h" /* For CREATE */
#include "zone.h"

struct zone_data* zone_table;
int top_of_zone_table;

/*
 * XXX: These structures have been moved here from zone.h since
 * they aren't part of the exported zone handling API.  I'd
 * rather see these structures disappear completely and be re-
 * placed by a -single- generic queue implementation.
 */

/* for queueing zones for update */
struct reset_q_element {
    int zone_to_reset; /* ref to zone_data */
    struct reset_q_element* next;
};

/* structure for the update queue */
struct reset_q_type {
    struct reset_q_element* head;
    struct reset_q_element* tail;
};

/*
 * Given a cleanly opened zone file, load the zone information
 * such as coordinates, owners, description, etc. and load all of
 * the zone's commands.
 *
 * Replace the static 'zone' index with the top_of_zone_table.
 */
void load_zones(FILE* fl)
{
    static int zone;
    int cmd_no;
    char buf[81], command;
    struct owner_list* owner;
    extern char* fread_string(FILE*, char*);

    bzero(&zone_table[zone], sizeof(struct zone_data));
    fscanf(fl, " #%d\n", &zone_table[zone].number);
    sprintf(buf2, "beginning of zone #%d", zone_table[zone].number);

    zone_table[zone].name = fread_string(fl, buf2);
    zone_table[zone].description = fread_string(fl, buf2);
    zone_table[zone].map = fread_string(fl, buf2);

    /* Read in the owner list.  An owner of '0' ends the list. */
    CREATE1(zone_table[zone].owners, owner_list);
    owner = zone_table[zone].owners;
    for (;;) {
        fscanf(fl, "%d", &owner->owner);
        if (owner->owner) {
            CREATE1(owner->next, owner_list);
            owner = owner->next;
        } else
            break;
    }

    /* Eat up the rest of the line */
    while (fgetc(fl) != '\n')
        continue;
    fscanf(fl, "%c %d %d %d\n",
        &zone_table[zone].symbol,
        &zone_table[zone].x,
        &zone_table[zone].y,
        &zone_table[zone].level);
    fscanf(fl, "%d\n", &zone_table[zone].top);
    fscanf(fl, "%d\n", &zone_table[zone].lifespan);
    fscanf(fl, "%d\n", &zone_table[zone].reset_mode);

    /* Read the command list */
    for (cmd_no = 0;; ++cmd_no) {
        fscanf(fl, "%c", &command);

        /*
         * Marks the end of the zone command list
         * XXX: Question: if we originally allocated a command structure
         * even for 'S' commands (which are unused), then does other code
         * depend on finding 'S' commands to terminate the list?  We could
         * use the cmd_no data we have here to set a number in the zone
         * structure to tell us how many commands there are, so we don't
         * NEED the terminating 'S' command.
         */
        if (command == 'S') {
            vmudlog(CMP, "Encountered S command on command number #%d.",
                cmd_no);
            break;
        }

        if (!cmd_no)
            CREATE(zone_table[zone].cmd, struct reset_com, 1);
        else {
            RECREATE(zone_table[zone].cmd, struct reset_com, cmd_no + 1, cmd_no);
            if (!(zone_table[zone].cmd)) {
                perror("reset command load");
                exit(0);
            }
        }

        /* XXX: still preserving the 'S' command */
        zone_table[zone].cmd[cmd_no].command = command;

        fscanf(fl, "%hd %hd %hd %hd %hd %hd",
            &zone_table[zone].cmd[cmd_no].if_flag,
            &zone_table[zone].cmd[cmd_no].arg1,
            &zone_table[zone].cmd[cmd_no].arg2,
            &zone_table[zone].cmd[cmd_no].arg3,
            &zone_table[zone].cmd[cmd_no].arg4,
            &zone_table[zone].cmd[cmd_no].arg5);

        zone_table[zone].cmd[cmd_no].existing = 0;

        switch (zone_table[zone].cmd[cmd_no].command) {
        case 'M':
        case 'N':
        case 'X':
        case 'H':
        case 'E':
        case 'K':
        case 'Q':
            fscanf(fl, "%hd %hd",
                &zone_table[zone].cmd[cmd_no].arg6,
                &zone_table[zone].cmd[cmd_no].arg7);
            break;
        case 'P':
            fscanf(fl, "%hd", &zone_table[zone].cmd[cmd_no].arg6);
        default:
            break;
        }

        /*
         * Read in the comment.
         * XXX: The comment is only saved to the zone structure in
         * shapezon.cc.  This *is* somewhat efficient, since we don't
         * need zone comments unless someone's actually looking at
         * them . . but that won't be very general.  We should save
         * the comment here.
         */
        fgets(buf, 80, fl);
        vmudlog(NRM, "Got command: %c %d %d %d %d %d %d %d.",
            zone_table[zone].cmd[cmd_no].command,
            zone_table[zone].cmd[cmd_no].arg1,
            zone_table[zone].cmd[cmd_no].arg2,
            zone_table[zone].cmd[cmd_no].arg3,
            zone_table[zone].cmd[cmd_no].arg4,
            zone_table[zone].cmd[cmd_no].arg5,
            zone_table[zone].cmd[cmd_no].arg6,
            zone_table[zone].cmd[cmd_no].arg7);
    }
    zone_table[zone].cmdno = cmd_no;
    zone++;

    top_of_zone_table = zone - 1;
}

/*
 * Renumber the entire zone table
 */
void renum_zone_table(void)
{
    int zone;
    void renum_zone_one(int);

    for (zone = 0; zone <= top_of_zone_table; zone++)
        renum_zone_one(zone);
}

/*
 * Renumber all virtual numbers referring to objects, mobiles,
 * rooms, etc. to reflect the real numbers of the corresponding
 * data.  The real numbers are the real indexes of the objects,
 * mobiles, etc. in their tables.
 */
void renum_zone_one(int zone)
{
    int comm, a, b;

    for (comm = 0; comm < zone_table[zone].cmdno; comm++) {
        vmudlog(CMP, "Doing renum_zone_one on command #%d.", comm);
        a = b = 0;

        switch (zone_table[zone].cmd[comm].command) {
        case 'A':
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 4:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = real_mobile(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'L':
            zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = real_mobile(zone_table[zone].cmd[comm].arg3);
                break;
            case 1:
            case 2:
            case 3:
                a = zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'M':
            a = zone_table[zone].cmd[comm].arg1 = real_mobile(zone_table[zone].cmd[comm].arg1);
            b = zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'O':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            if (zone_table[zone].cmd[comm].arg2 != NOWHERE)
                b = zone_table[zone].cmd[comm].arg2 = real_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'G':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            break;
        case 'E':
            a = zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            break;
        case 'P': /* room and obj_to can be null, then load to last_obj */
            zone_table[zone].cmd[comm].arg1 = real_room(zone_table[zone].cmd[comm].arg1);
            a = zone_table[zone].cmd[comm].arg2 = real_object(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
            break;
        case 'K':
            if (zone_table[zone].cmd[comm].arg1)
                zone_table[zone].cmd[comm].arg1 = real_object(zone_table[zone].cmd[comm].arg1);
            zone_table[zone].cmd[comm].arg2 = real_object(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = real_object(zone_table[zone].cmd[comm].arg3);
            zone_table[zone].cmd[comm].arg4 = real_object(zone_table[zone].cmd[comm].arg4);
            zone_table[zone].cmd[comm].arg5 = real_object(zone_table[zone].cmd[comm].arg5);
            zone_table[zone].cmd[comm].arg6 = real_object(zone_table[zone].cmd[comm].arg6);
            zone_table[zone].cmd[comm].arg7 = real_object(zone_table[zone].cmd[comm].arg7);
            a = b = 1;
            break;
        case 'D':
            a = zone_table[zone].cmd[comm].arg1 = real_room(zone_table[zone].cmd[comm].arg1);
            break;
        }

        /*
         * If we ever received a negative value from any of the real_*
         * functions, we've got an invalid virtual number.  Thus we
         * disable the command with the special '*' zone command.
         */
        if (a < 0 || b < 0) {
            vmudlog(CMP, "Invalid virtual number in zone reset command: "
                         "zone #%d, command %d.  Command disabled.\n",
                zone_table[zone].number, comm + 1);

            zone_table[zone].cmd[comm].command = '*';
        }
    }
}

/*
 * Update zone ages, queue for reset if necessary, and dequeue
 * when possible.
 */

#define ZO_DEAD 999
#define ZO_QUED 888

void zone_update(void)
{
    int i, should_reset;
    static int timer;
    struct reset_q_element *update_u, *temp;
    static struct reset_q_type reset_q;
    void put_to_reset_q_pool(struct reset_q_element*);
    struct reset_q_element* get_from_reset_q_pool(void);
    int is_empty(int);

    /*
     * The 4 constant comes from 4 passes per second.  This apparently
     * means that one minute has passed and is not accurate unless
     * PULSE_ZONE is a multiple of 4 or a factor of 60.
     *
     * I don't think this applies anymore, since comm.cc uses pulses
     * to determine whether or not zone_update should even be called.
     */
    if (((++timer * PULSE_ZONE) / 4) >= 60) {
        timer = 0;

        /* since one minute has passed, increment zone ages */
        for (i = 0; i <= top_of_zone_table; i++) {
            /* Used to be age < zone_table[i].lifespan */
            if (zone_table[i].age < ZO_DEAD && zone_table[i].reset_mode)
                zone_table[i].age++;

            switch (zone_table[i].reset_mode) {
            case 0:
                zone_table[i].age = ZO_DEAD;
                should_reset = 0;
                break;
            case 1:
                should_reset = is_empty(i) && zone_table[i].age >= zone_table[i].lifespan;
                break;
            case 2:
                should_reset = zone_table[i].age >= zone_table[i].lifespan;
                break;
            case 3:
                should_reset = (is_empty(i) && zone_table[i].age >= zone_table[i].lifespan) || zone_table[i].age >= zone_table[i].lifespan * 3;
                break;
            default:
                should_reset = 0;
                vmudlog(CMP, "Unknown reset mode %d for zone #%d.",
                    zone_table[i].reset_mode, zone_table[i].number);
                break;
            }

            if (should_reset && zone_table[i].age < ZO_DEAD) {
                /* enqueue zone */
                update_u = get_from_reset_q_pool();
                update_u->zone_to_reset = i;
                update_u->next = 0;

                if (reset_q.head == NULL)
                    reset_q.head = reset_q.tail = update_u;
                else {
                    reset_q.tail->next = update_u;
                    reset_q.tail = update_u;
                }

                zone_table[i].age = ZO_DEAD;
            }
        }
    }

    /*
     * Dequeue a single zone (if possible) and reset.
     *
     * XXX: What the hell is the point of this for loop if there's
     * an unconditional break in its body?
     */
    for (update_u = reset_q.head; update_u; update_u = update_u->next) {
        reset_zone(update_u->zone_to_reset);
        vmudlog(CMP, "Automatic zone reset: zone #%d, %s.",
            zone_table[update_u->zone_to_reset].number,
            zone_table[update_u->zone_to_reset].name);

        /* dequeue */
        if (update_u == reset_q.head)
            reset_q.head = reset_q.head->next;
        else {
            for (temp = reset_q.head; temp->next != update_u; temp = temp->next)
                continue;
            ;
            if (!update_u->next)
                reset_q.tail = temp;
            temp->next = update_u->next;
        }

        put_to_reset_q_pool(update_u);
        update_u = NULL;
        break;
    }
}

/*
 * Return 1 if the if_flag is satisfied, else return 0.
 * The algorithm works as follows: if we do not care about a
 * particular requirement, then its require_XXX variable is
 * set to 0.  If we require that an event occur, then the
 * appropriate require_XXX variable is set to 1.  If we
 * require that an event NOT occur, then the require_XXX
 * variable is set to -1.  The if_flag is a bitvector as
 * follows:
 *    Bit 1 (0x01) - require the previous zone cmd to occur
 *    Bit 2 (0x02) - require the previous mob to load
 *    Bit 3 (0x04) - require the previous object to load
 *    Bit 4 (0x08) - invert: require that specified events do NOT occur
 *    Bit 5 (0x10) - require that the good side lead the race war
 *    Bit 6 (0x20) - require that the evil side lead the race war
 *    Bit 7 (0x40) - check if sun is up
 *    Bit 8 (0x80) - check if player in zone
 *
 * Note that some fine tuned properties of bitvectors aren't
 * kept here: for example, you can't require one event to
 * occur and require one event to not occur, because the
 * inversion bit (bit 4) is either set or not set, inverting
 * all requirements.  We'd need an anti-requirement bit for
 * each event if we wanted to allow that sort of control.
 */
int check_if_flag(int if_flag, int last_cmd, int last_mob, int last_obj, int zone)
{
    int is_empty(int);

    int require_last_cmd;
    int require_last_mob;
    int require_last_obj;
    int require_good_fame_lead;
    int require_evil_fame_lead;
    int invert_requirements;
    int require_sun_up;
    int require_players_in_zone;

    require_last_cmd = if_flag & 0x01;
    require_last_mob = if_flag & 0x02;
    require_last_obj = if_flag & 0x04;
    require_good_fame_lead = if_flag & 0x10;
    require_evil_fame_lead = if_flag & 0x20;
    require_sun_up = if_flag & 0x40;
    require_players_in_zone = if_flag & 0x80;

    invert_requirements = if_flag & 0x08;
    if (invert_requirements) {
        if (require_last_cmd && last_cmd == 1) {
            return 0;
        }
        if (require_last_mob && last_mob == 1) {
            return 0;
        }
        if (require_last_obj && last_obj == 1) {
            return 0;
        }
        if (require_good_fame_lead && pkill_get_good_fame() > pkill_get_evil_fame()) {
            return 0;
        }
        if (require_evil_fame_lead && pkill_get_evil_fame() > pkill_get_good_fame()) {
            return 0;
        }
        if (require_sun_up && (weather_info.sunlight == SUN_LIGHT || weather_info.sunlight == SUN_RISE)) {
            return 0;
        }
        if (require_players_in_zone && !is_empty(zone)) {
            return 0;
        }
    } else {
        if (require_last_cmd && last_cmd == 0) {
            return 0;
        }
        if (require_last_mob && last_mob == 0) {
            return 0;
        }
        if (require_last_obj && last_obj == 0) {
            return 0;
        }
        if (require_good_fame_lead && pkill_get_good_fame() <= pkill_get_evil_fame()) {
            return 0;
        }
        if (require_evil_fame_lead && pkill_get_evil_fame() <= pkill_get_good_fame()) {
            return 0;
        }
        if (require_sun_up && (weather_info.sunlight == SUN_DARK || weather_info.sunlight == SUN_SET)) {
            return 0;
        }
        if (require_players_in_zone && is_empty(zone)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Perform a zone reset on the specified zone.  This entails
 * walking over the entire list of zone commands and performing
 * those which "need" to be performed.  This policy of need is
 * defined on a command-by-command basis, and is generally
 * highly influenced by the if flag.
 */
void reset_zone(int zone)
{
/* XXX: ZCMD needs to be removed */
#define ZCMD zone_table[zone].cmd[cmd_no]
    int cmd_no, last_cmd, last_mob, last_obj;
    int should_execute;
    long tmp, tmp2;
    struct char_data *mob, *tmpmob, *tmpch;
    struct obj_data *obj, *obj_to, *tmpobj;
    extern int rev_dir[];
    extern int top_of_world;
    extern struct room_data world;
    extern struct index_data* mob_index;
    extern struct index_data* obj_index;
    extern struct char_data* character_list;
    int set_exit_state(struct room_data*, int, int);
    void add_follower(struct char_data*, struct char_data*, int mode);
    void extract_char(struct char_data*);
    void extract_obj(struct obj_data*);
    /* XXX: int used for room virtual number */
    void char_to_room(struct char_data*, int);
    ACMD(do_wear);

    last_cmd = last_mob = last_obj = 0;
    mob = tmpmob = tmpch = NULL;
    obj = obj_to = tmpobj = NULL;

    for (cmd_no = 0; cmd_no < zone_table[zone].cmdno; cmd_no++) {

        /* Make sure the if_flag requirements are met */
        should_execute = check_if_flag(ZCMD.if_flag, last_cmd, last_mob, last_obj, zone);
        if (should_execute) {
            switch (ZCMD.command) {
            case '*': /* ignore command */
                break;
            case 'L': /* sets the last_mob or last_obj */
                switch (ZCMD.arg1) {
                case 0: /* Sets last_mob */
                    if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0) {
                        for (tmp = 0, tmpmob = world[ZCMD.arg2].people;
                             tmpmob; tmpmob = tmpmob->next_in_room) {
                            if (IS_NPC(tmpmob) && tmpmob->nr == ZCMD.arg3)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpmob) {
                            mob = tmpmob;
                            last_mob = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_mob = 0;
                    mob = 0;
                    break;
                case 1: /* Sets last_obj from the room */
                    if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0) {
                        for (tmp = 0, tmpobj = world[ZCMD.arg2].contents;
                             tmpobj; tmpobj = tmpobj->next_content) {
                            if (tmpobj->item_number == ZCMD.arg3)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpobj) {
                            obj = tmpobj;
                            last_obj = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_obj = 0;
                    obj = 0;
                    break;
                case 2: /* last_obj from the contents of last_obj */
                    if (ZCMD.arg3 >= 0 && last_obj && obj) {
                        for (tmp = 0, tmpobj = obj->contains;
                             tmpobj; tmpobj = tmpobj->next_content) {
                            if (tmpobj->item_number == ZCMD.arg3)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpobj) {
                            obj = tmpobj;
                            last_obj = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_obj = 0;
                    obj = 0;
                    break;
                case 3: /* last_obj from the inventory of last_mob */
                    if (ZCMD.arg3 >= 0 && last_mob && mob) {
                        for (tmp = 0, tmpobj = mob->carrying;
                             tmpobj; tmpobj = tmpobj->next_content) {
                            if (tmpobj->item_number == ZCMD.arg3)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpobj) {
                            obj = tmpobj;
                            last_obj = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_obj = 0;
                    obj = 0;
                    break;
                case 4:
                    if (last_mob && mob && ZCMD.arg4 >= 0 && ZCMD.arg4 < MAX_WEAR) {
                        tmpobj = mob->equipment[ZCMD.arg4];
                        if (tmpobj && ZCMD.arg3 >= 0 && tmpobj->item_number != ZCMD.arg3)
                            tmpobj = 0;
                        if (tmpobj) {
                            obj = tmpobj;
                            last_obj = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_obj = 0;
                    obj = 0;
                    break;
                case 5: /* Sets last_mob from the world */
                    if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0) {
                        for (tmp = 0, tmpmob = character_list;
                             tmpmob; tmpmob = tmpmob->next) {
                            if (IS_NPC(tmpmob) && tmpmob->nr == ZCMD.arg3)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpmob) {
                            mob = tmpmob;
                            last_mob = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_mob = 0;
                    mob = 0;
                    break;
                case 6: /* Sets last_mob from the zone */
                    if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0) {
                        tmp2 = world[ZCMD.arg1].zone;
                        for (tmp = 0, tmpmob = character_list;
                             tmpmob; tmpmob = tmpmob->next) {
                            if (IS_NPC(tmpmob) && tmpmob->nr == ZCMD.arg3 && tmpmob->in_room >= 0 && world[tmpmob->in_room].zone == tmp2)
                                tmp++;
                            if (tmp >= ZCMD.arg4)
                                break;
                        }
                        if (tmpmob) {
                            mob = tmpmob;
                            last_mob = last_cmd = 1;
                            break;
                        }
                    }
                    last_cmd = last_mob = 0;
                    mob = 0;
                    break;
                }
                break;
            case 'A':
                if ((ZCMD.arg1 != 5 && ZCMD.arg1 != 10 && !mob) || (ZCMD.arg1 == 5 && !obj)) {
                    last_cmd = 0;
                    break;
                }

                last_cmd = 1;
                switch (ZCMD.arg1) {
                case 1: /* Set gold */
                    GET_GOLD(mob) = ZCMD.arg2;
                    break;
                case 2: /* Set difficulty coef */
                    mob->specials.prompt_number = ZCMD.arg2;
                    break;
                case 3: /* Set trophy coef */
                    mob->specials.trophy_line = ZCMD.arg2;
                    break;
                case 4: /* Set to follow */
                    tmpch = world[mob->in_room].people;
                    for (tmp = 0; tmpch; tmpch = tmpch->next_in_room) {
                        if (IS_NPC(tmpch) && (tmpch->nr < 0 || tmpch->nr == ZCMD.arg3))
                            tmp++;
                        if (tmp == ZCMD.arg2)
                            break;
                    }
                    if (tmpch)
                        add_follower(mob, tmpch, FOLLOW_MOVE);
                    else
                        last_cmd = 0;
                    break;
                case 5:
                    if (last_obj && obj && ZCMD.arg2 >= 0 && ZCMD.arg2 < 5)
                        obj->obj_flags.value[ZCMD.arg2] = ZCMD.arg3;
                    else
                        last_cmd = 0;
                    break;
                case 6:
                    if (0x01 & ZCMD.arg2)
                        last_cmd = 0;
                    else
                        last_cmd = 1;
                    if (0x02 & ZCMD.arg2)
                        last_mob = 0;
                    if (0x04 & ZCMD.arg2)
                        last_obj = 0;
                    break;
                case 7:
                    mob->specials.store_prog_number = ZCMD.arg2;
                    SET_BIT(MOB_FLAGS(mob), MOB_SPEC);
                    break;
                case 8:
                    if (ZCMD.arg2)
                        SET_BIT(mob->specials2.act, 1 << ZCMD.arg3);
                    else
                        REMOVE_BIT(mob->specials2.act, 1 << ZCMD.arg3);
                    break;
                case 9:
                    mob->specials.butcher_item = ZCMD.arg2;
                    break;
                case 10:
                    if (ZCMD.arg2 == 1 && last_mob && mob) {
                        extract_char(mob);
                        mob = 0;
                        last_mob = 0;
                    } else if (ZCMD.arg2 == 2 && last_obj && obj) {
                        extract_obj(obj);
                        last_obj = 0;
                        obj = 0;
                    } else
                        last_cmd = 0;
                    break;
                case 11: /* Set race aggressions */
                    mob->specials2.pref = ZCMD.arg2;
                    break;
                case 12: /* Assign script to mob */
                    mob->specials.script_number = ZCMD.arg2;
                    mob->specials.script_info = 0; /* Probably unnecessary */
                    break;
                default:
                    vmudlog(CMP, "Unrecognized 'A' command: %d %d %d in zone #%d.",
                        ZCMD.arg1, ZCMD.arg2, ZCMD.arg3, zone_table[zone].number);
                    last_cmd = 0;
                }
                break;
            case 'M': /* read a mobile */
                if ((!ZCMD.arg3 || mob_index[ZCMD.arg1].number < ZCMD.arg3) && (!ZCMD.arg6 || ZCMD.existing < ZCMD.arg6) && (ZCMD.arg4 == 100 || ZCMD.arg4 > number(0, 99))) {
                    mob = read_mobile(ZCMD.arg1, REAL);
                    ZCMD.existing++;
                    GET_DIFFICULTY(mob) = ZCMD.arg5;
                    GET_LOADLINE(mob) = (cmd_no + 1);
                    GET_LOADZONE(mob) = zone;
                    mob->specials.trophy_line = (byte)ZCMD.arg7;
                    char_to_room(mob, ZCMD.arg2);
                    act("$n arrives.", TRUE, mob, 0, 0, TO_ROOM);
                    last_mob = last_cmd = 1;
                } else {
                    mob = 0;
                    last_mob = last_cmd = 0;
                }
                break;
            case 'O': /* read an object */
                if ((!ZCMD.arg3 || obj_index[ZCMD.arg1].number < ZCMD.arg3) && (ZCMD.arg4 == 100 || ZCMD.arg4 > number(0, 99))) {
                    if (ZCMD.arg2 >= 0) {
                        if (!ZCMD.arg5 || (count_obj_in_list(ZCMD.arg1, world[ZCMD.arg2].contents) < ZCMD.arg5)) {
                            obj = read_object(ZCMD.arg1, REAL);
                            obj_to_room(obj, ZCMD.arg2);
                            last_cmd = last_obj = 1;
                        } else
                            last_cmd = last_obj = 0;
                    } else {
                        obj = read_object(ZCMD.arg1, REAL);
                        obj->in_room = NOWHERE;
                        last_cmd = 1;
                    }
                } else {
                    last_cmd = 0;
                    obj = 0;
                }
                break;
            case 'P': /* object to object */
                if (ZCMD.arg1 == NOWHERE || ZCMD.arg3 < 0)
                    obj_to = obj;
                else
                    obj_to = get_obj_in_list_num(ZCMD.arg3, world[ZCMD.arg1].contents);
                if (!obj_to) {
                    last_cmd = 0;
                    break;
                }

                tmp = count_obj_in_list(ZCMD.arg2, obj_to->contains);
                if ((!ZCMD.arg4 || obj_index[ZCMD.arg2].number < ZCMD.arg4) && (ZCMD.arg5 == 100 || ZCMD.arg5 > number(0, 99)) && (!ZCMD.arg6 || tmp < ZCMD.arg6)) {
                    obj = read_object(ZCMD.arg2, REAL);
                    obj_to_obj(obj, obj_to);
                    last_cmd = 1;
                } else
                    last_cmd = 0;
                break;
            case 'G': /* Object to character */
                if (!mob) {
                    last_cmd = 0;
                    break;
                }

                if ((!ZCMD.arg3 || obj_index[ZCMD.arg1].number < ZCMD.arg3) && (ZCMD.arg4 == 100 || ZCMD.arg4 > number(0, 99))) {
                    obj = read_object(ZCMD.arg1, REAL);
                    obj_to_char(obj, mob);
                    last_cmd = 1;
                    last_obj = 1;
                } else
                    last_cmd = 0;
                break;
            case 'K':
                /*
                 * This usually isn't an error.  If a mob loading command
                 * exists but doesn't /execute/, then this error will get
                 * triggered.
                 */
                if (!mob || !last_mob) {
                    last_cmd = 0;
                    break;
                }
                if (ZCMD.arg1 >= 0) {
                    obj = read_object(ZCMD.arg1, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg2 >= 0) {
                    obj = read_object(ZCMD.arg2, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg3 >= 0) {
                    obj = read_object(ZCMD.arg3, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg4 >= 0) {
                    obj = read_object(ZCMD.arg4, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg5 >= 0) {
                    obj = read_object(ZCMD.arg5, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg6 >= 0) {
                    obj = read_object(ZCMD.arg6, REAL);
                    obj_to_char(obj, mob);
                }
                if (ZCMD.arg7 >= 0) {
                    obj = read_object(ZCMD.arg7, REAL);
                    obj_to_char(obj, mob);
                }
                do_wear(mob, "all", 0, 0, 0);
                last_cmd = 1;
                last_obj = 0;
                break;
            case 'E': /* object to equipment list */
                if (!mob) {
                    last_cmd = 0;
                    break;
                }
                if ((!ZCMD.arg3 || obj_index[ZCMD.arg1].number < ZCMD.arg3) && (ZCMD.arg4 == 100 || ZCMD.arg4 > number(0, 99))) {
                    if (ZCMD.arg2 < 0 || ZCMD.arg2 >= MAX_WEAR) {
                        last_cmd = 0;
                    } else {
                        obj = read_object(ZCMD.arg1, REAL);
                        equip_char(mob, obj, ZCMD.arg2);
                        last_cmd = 1;
                    }
                } else
                    last_cmd = 0;
                break;
            case 'D': /* set state of door */
                if (ZCMD.arg1 > top_of_world)
                    break;
                if (ZCMD.arg2 < 0 || ZCMD.arg2 > 5)
                    break;
                if (!world[ZCMD.arg1].dir_option[ZCMD.arg2])
                    break;
                if (set_exit_state(&world[ZCMD.arg1], ZCMD.arg2, ZCMD.arg3))
                    last_cmd = 1;
                else
                    last_cmd = 0;
                tmp = world[ZCMD.arg1].dir_option[ZCMD.arg2]->to_room;
                if (tmp == NOWHERE)
                    break;
                if (!world[tmp].dir_option[rev_dir[ZCMD.arg2]])
                    break;
                if (world[tmp].dir_option[rev_dir[ZCMD.arg2]]->to_room != ZCMD.arg1)
                    break;
                set_exit_state(&world[tmp], rev_dir[ZCMD.arg2], ZCMD.arg3);
                break;
            }
        } else {
            last_cmd = 0;
            if (ZCMD.command == 'M')
                last_mob = 0;
        }
    }

    zone_table[zone].age = 0;
}

static struct reset_q_element* reset_q_pool;

/*
 * Return the head of the current pool if it exists.
 * Otherwise create a new element and return it.
 *
 * XXX: The modularity is cute, but do we really need it?
 * This file is the only place in the entire codebase that
 * makes use of any of these reset_q_pool functions, and
 * usually they're only called at one place.
 */
struct reset_q_element*
get_from_reset_q_pool(void)
{
    struct reset_q_element* resnew;

    if (reset_q_pool) {
        resnew = reset_q_pool;
        reset_q_pool = resnew->next;
    } else
        CREATE(resnew, struct reset_q_element, 1);

    return resnew;
}

/*
 * This used to just free(oldres).
 */
void put_to_reset_q_pool(struct reset_q_element* oldres)
{
    oldres->next = reset_q_pool;
    reset_q_pool = oldres;
}

/*
 * For use in reset_zone; return TRUE if zone 'nr' is free
 * of players.
 *
 * XXX: This is a terrible name.  How would anyone casually
 * reading the code know that "is_empty" has anything to do
 * with a zone and the players in it?
 */
int is_empty(int zone_nr)
{
    struct descriptor_data* i;
    extern struct room_data world;
    extern struct descriptor_data* descriptor_list;

    for (i = descriptor_list; i; i = i->next)
        if (!i->connected)
            if (world[i->character->in_room].zone == zone_nr)
                return 0;

    return 1;
}
