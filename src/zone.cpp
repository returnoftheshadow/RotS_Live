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
        /*
         * Read the rest of the line, however long the comment is.  Reading
         * only one buffer's worth left a long comment's tail unread, and the
         * next pass took that tail as a command: a phantom row, or, when the
         * tail was just the line end, the next real command swallowed whole.
         */
        buf[0] = '\0';
        while (fgets(buf, 80, fl) && !strchr(buf, '\n'))
            ;
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

    for (zone = 0; zone <= top_of_zone_table; zone++)
        renum_zone_one(zone);
}

/*
 * Renumber all virtual numbers referring to objects, mobiles,
 * rooms, etc. to reflect the real numbers of the corresponding
 * data.  The real numbers are the real indexes of the objects,
 * mobiles, etc. in their tables.
 */

/*
 * Builder-facing resolution of a zone-command vnum.  renum_zone_one overwrites
 * each arg in place with its real number, so the ORIGINAL vnum only exists here
 * -- by reset time it is gone.  That is why the diagnostic has to be produced at
 * this point rather than where the bad index is later used.
 */
enum zone_ref_kind { ZREF_MOB,
    ZREF_OBJ,
    ZREF_ROOM };
static const char* zref_name[] = { "mobile", "object", "room" };

/* Every vnum in the current command that names nothing, in arg order.  A
 * command resolves at most 7 args (K's object slots), so this never fills;
 * the bound check only guards against that changing. */
#define ZONE_MAX_BAD 8
static int zone_bad_vnum[ZONE_MAX_BAD];
static int zone_bad_kind[ZONE_MAX_BAD];
static int zone_bad_count;
/* The required arg whose failure disables the command, whatever its value,
 * and its place in the list above (-1 when it is 0 or 65535, so not listed). */
static int zone_req_vnum;
static int zone_req_kind;
static int zone_req_index;
static bool zone_req_failed;

static int zresolve(int vnum, int kind, bool required)
{
    int real = (kind == ZREF_MOB) ? real_mobile(vnum)
        : (kind == ZREF_OBJ)      ? real_object(vnum)
                                  : real_room(vnum);
    int listed = -1;

    /* No vnum given: an unused optional arg of K, P or L holds 0, or NOWHERE
     * (-1), which reads back as 65535 because load_zones reads these int args
     * with "%hd".  Nothing was referenced, so there is nothing to report.
     * A real vnum of 65535 cannot be told apart from this. */
    if (real < 0 && vnum > 0 && vnum != 65535 && zone_bad_count < ZONE_MAX_BAD) {
        listed = zone_bad_count++;
        zone_bad_vnum[listed] = vnum;
        zone_bad_kind[listed] = kind;
    }
    /* A required arg that fails disables the command, so it is always named,
     * even when it is 0 or 65535. */
    if (real < 0 && required && !zone_req_failed) {
        zone_req_failed = true;
        zone_req_vnum = vnum;
        zone_req_kind = kind;
        zone_req_index = listed;
    }
    return real;
}
static int zresolve_mob(int v) { return zresolve(v, ZREF_MOB, false); }
static int zresolve_obj(int v) { return zresolve(v, ZREF_OBJ, false); }
static int zresolve_room(int v) { return zresolve(v, ZREF_ROOM, false); }
/* The args that disable the command when they fail (renum's a and b). */
static int zrequire_mob(int v) { return zresolve(v, ZREF_MOB, true); }
static int zrequire_obj(int v) { return zresolve(v, ZREF_OBJ, true); }
static int zrequire_room(int v) { return zresolve(v, ZREF_ROOM, true); }

/* One renum line: to the log and area gods, and to the builder who ran
 * implement unless mudlog already reached them. */
static void zone_renum_send(char* errbuf, struct char_data* to)
{
    mudlog(errbuf, NRM, LEVEL_AREAGOD, TRUE);
    if (to && !mudlog_reaches(to, LEVEL_AREAGOD, NRM)) {
        send_to_char(errbuf, to);
        send_to_char("\n\r", to);
    }
}

static void zone_renum_report(int zone, int comm, const char* what, int vnum,
    bool disabled, struct char_data* to)
{
    char errbuf[256];

    sprintf(errbuf, "ZONE ERROR: zone #%d, command %d (%c): %s vnum %d not found%s",
        zone_table[zone].number, comm + 1, zone_table[zone].cmd[comm].command, what, vnum,
        disabled ? " - command disabled" : "");
    zone_renum_send(errbuf, to);
}

void renum_zone_one(int zone, struct char_data* to)
{
    int comm, a, b;
    zone_table[zone].cmds_disabled = 0;

    for (comm = 0; comm < zone_table[zone].cmdno; comm++) {
        vmudlog(CMP, "Doing renum_zone_one on command #%d.", comm);
        a = b = 0;
        zone_bad_count = 0;
        zone_req_failed = false;

        switch (zone_table[zone].cmd[comm].command) {
        case 'A':
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 4:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = zrequire_mob(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'L':
            zone_table[zone].cmd[comm].arg2 = zresolve_room(zone_table[zone].cmd[comm].arg2);
            switch (zone_table[zone].cmd[comm].arg1) {
            case 0:
            case 5:
            case 6:
                a = zone_table[zone].cmd[comm].arg3 = zrequire_mob(zone_table[zone].cmd[comm].arg3);
                break;
            case 1:
            case 2:
            case 3:
                a = zone_table[zone].cmd[comm].arg3 = zrequire_obj(zone_table[zone].cmd[comm].arg3);
                break;
            }
            break;
        case 'M':
            a = zone_table[zone].cmd[comm].arg1 = zrequire_mob(zone_table[zone].cmd[comm].arg1);
            b = zone_table[zone].cmd[comm].arg2 = zrequire_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'O':
            a = zone_table[zone].cmd[comm].arg1 = zrequire_obj(zone_table[zone].cmd[comm].arg1);
            if (zone_table[zone].cmd[comm].arg2 != NOWHERE)
                b = zone_table[zone].cmd[comm].arg2 = zrequire_room(zone_table[zone].cmd[comm].arg2);
            break;
        case 'G':
            a = zone_table[zone].cmd[comm].arg1 = zrequire_obj(zone_table[zone].cmd[comm].arg1);
            break;
        case 'E':
            a = zone_table[zone].cmd[comm].arg1 = zrequire_obj(zone_table[zone].cmd[comm].arg1);
            break;
        case 'P': /* room and obj_to can be null, then load to last_obj */
            zone_table[zone].cmd[comm].arg1 = zresolve_room(zone_table[zone].cmd[comm].arg1);
            a = zone_table[zone].cmd[comm].arg2 = zrequire_obj(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = zresolve_obj(zone_table[zone].cmd[comm].arg3);
            break;
        case 'K':
            if (zone_table[zone].cmd[comm].arg1)
                zone_table[zone].cmd[comm].arg1 = zresolve_obj(zone_table[zone].cmd[comm].arg1);
            zone_table[zone].cmd[comm].arg2 = zresolve_obj(zone_table[zone].cmd[comm].arg2);
            zone_table[zone].cmd[comm].arg3 = zresolve_obj(zone_table[zone].cmd[comm].arg3);
            zone_table[zone].cmd[comm].arg4 = zresolve_obj(zone_table[zone].cmd[comm].arg4);
            zone_table[zone].cmd[comm].arg5 = zresolve_obj(zone_table[zone].cmd[comm].arg5);
            zone_table[zone].cmd[comm].arg6 = zresolve_obj(zone_table[zone].cmd[comm].arg6);
            zone_table[zone].cmd[comm].arg7 = zresolve_obj(zone_table[zone].cmd[comm].arg7);
            a = b = 1;
            break;
        case 'D':
            a = zone_table[zone].cmd[comm].arg1 = zrequire_room(zone_table[zone].cmd[comm].arg1);
            break;
        case '*': /* disabled */
        case 'S': /* end of the list */
        case '.': /* a row with no letter, as shapezon writes one */
            break;
        default:
            /* reset_zone has no case for this letter, so the command does
             * nothing.  load_zones reads N, X, H and Q, but nothing runs
             * them.  A row with no letter at all is empty, not an error. */
            if (zone_table[zone].cmd[comm].command > ' ') {
                char errbuf[256];

                sprintf(errbuf, "ZONE ERROR: zone #%d, command %d (%c): unknown command - does nothing",
                    zone_table[zone].number, comm + 1, zone_table[zone].cmd[comm].command);
                zone_renum_send(errbuf, to);
            }
            break;
        }

        /*
         * If we ever received a negative value from any of the real_*
         * functions, we've got an invalid virtual number.  Thus we
         * disable the command with the special '*' zone command.
         */
        /* A command is disabled on exactly the same condition as before this
         * diagnostic existed (a or b negative).  The extra vnum checks below
         * only ADD reporting -- they must never silently stop content from
         * loading that used to load. */
        bool disable = (a < 0 || b < 0);

        /* One line per vnum that names nothing, so the builder sees every
         * problem in the command at once.  The vnum that disabled it carries
         * the suffix; when that vnum is 0 or 65535 it is not in the list, so
         * it gets its own line first. */
        int i;

        if (disable && zone_req_failed && zone_req_index < 0)
            zone_renum_report(zone, comm, zref_name[zone_req_kind], zone_req_vnum, true, to);
        for (i = 0; i < zone_bad_count; i++)
            zone_renum_report(zone, comm, zref_name[zone_bad_kind[i]], zone_bad_vnum[i],
                disable && i == zone_req_index, to);

        if (disable) {
            zone_table[zone].cmds_disabled++;
            zone_table[zone].cmd[comm].command = '*';
        }

        /* Renum's lines are a one-off record of bad data.  A command left
         * enabled goes on failing at every reset, so mark it and let
         * reset_zone say so each time it actually happens. */
        zone_table[zone].cmd[comm].bad_arg = zone_bad_count ? 1 + zone_bad_kind[0] : 0;
    }
}

/*
 * The zone command reset_zone is executing, or -1 between commands.  A load
 * that fails does so several calls deep -- in world[] with a room index that
 * never resolved, or in a K slot that quietly skips its object -- and none of
 * those places can say which zone command sent them there.
 */
static int running_zone = -1;
static int running_cmd_no = -1;
/* Set only around the L command's own room lookups, the one place a zone
 * command's line can itself send a missing room to world[]. */
static bool zone_own_room_lookup = false;

/*
 * Name the zone command that caused a failure: its zone and its command
 * number, which is the address of the line to go and look at.  Fires on every
 * occurrence, not once per boot -- how often a zone actually trips the error
 * is part of what the log is for.  Returns false when no zone command is
 * running, so a caller reachable from elsewhere can fall back to its own
 * message.
 */
bool report_zone_cmd_failure(const char* what)
{
    if (running_zone < 0)
        return false;

    struct reset_com& cmd = zone_table[running_zone].cmd[running_cmd_no];
    char errbuf[256];

    sprintf(errbuf, "ZONE ERROR: zone #%d, command %d (%c): %s",
        zone_table[running_zone].number, running_cmd_no + 1, cmd.command, what);
    mudlog(errbuf, NRM, LEVEL_AREAGOD, TRUE);

    return true;
}

/* world[] was given a negative room while a zone command is running.  Say
 * whether the command's own line named the missing room, or the lookup came
 * from code the command set off (loading or equipping a mob, and so on). */
bool report_zone_negative_room(void)
{
    return report_zone_cmd_failure(zone_own_room_lookup
            ? "room not found - searched room 0 instead"
            : "negative room lookup while running this command");
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
            running_zone = zone;
            running_cmd_no = cmd_no;

            switch (ZCMD.command) {
            case '*': /* ignore command */
                break;
            case 'L': /* sets the last_mob or last_obj */
                switch (ZCMD.arg1) {
                case 0: /* Sets last_mob */
                    if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0) {
                        zone_own_room_lookup = true;
                        tmpmob = world[ZCMD.arg2].people;
                        zone_own_room_lookup = false;
                        for (tmp = 0;
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
                        zone_own_room_lookup = true;
                        tmpobj = world[ZCMD.arg2].contents;
                        zone_own_room_lookup = false;
                        for (tmp = 0;
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
                        zone_own_room_lookup = true;
                        tmp2 = world[ZCMD.arg2].zone;
                        zone_own_room_lookup = false;
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
                    /* The room or container named never resolved and there is
                     * no last object to fall back on, so nothing loads.  This
                     * happens before the count and % checks, which need a
                     * container -- the bad vnum fails every run regardless. */
                    if (ZCMD.bad_arg == 1 + ZREF_ROOM)
                        report_zone_cmd_failure("room not found - nothing loaded");
                    else if (ZCMD.bad_arg)
                        report_zone_cmd_failure("container not found - nothing loaded");
                    last_cmd = 0;
                    break;
                }

                tmp = count_obj_in_list(ZCMD.arg2, obj_to->contains);
                if ((!ZCMD.arg4 || obj_index[ZCMD.arg2].number < ZCMD.arg4) && (ZCMD.arg5 == 100 || ZCMD.arg5 > number(0, 99)) && (!ZCMD.arg6 || tmp < ZCMD.arg6)) {
                    obj = read_object(ZCMD.arg2, REAL);
                    obj_to_obj(obj, obj_to);
                    last_cmd = 1;
                    /* The object did load, but the room or container named
                     * never resolved, so it went into the last object loaded
                     * instead.  Only now is anything actually misplaced. */
                    if (ZCMD.bad_arg == 1 + ZREF_ROOM)
                        report_zone_cmd_failure("room not found - put in last object loaded");
                    else if (ZCMD.bad_arg)
                        report_zone_cmd_failure("container not found - put in last object loaded");
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
                /* Past the gate: the mob loaded and is being equipped now, so a
                 * slot whose vnum never resolved is equipment actually going
                 * missing, not a condition that simply came back false. */
                if (ZCMD.bad_arg)
                    report_zone_cmd_failure("object not loaded");
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

            running_zone = running_cmd_no = -1;
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
