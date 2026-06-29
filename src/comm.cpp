/* ************************************************************************
 *   File: comm.c                                        Part of CircleMUD *
 *  Usage: Communication, socket handling, main(), central game loop       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <ctype.h>
#include <errno.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "platdef.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>

#include "big_brother.h"
#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "protocol.h"
#include "script.h"
#include "crashsave_schedule.h"
#include "skill_timer.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "zone.h"

#include <cstdlib>
#include <ctime>
#include <vector>

#define MAX_HOSTNAME 256
#define OPT_USEC 250000 /* time delay corresponding to 4 passes/sec */
#define DFLT_PORT 1024
#define MAX_PLAYERS 255
#define MAX_DESCRIPTORS_AVAILABLE 256

/* externs */
extern int restrict;
extern int mini_mud;
extern int new_mud;
extern int no_rent_check;
extern FILE *player_fl;
extern char *DFLT_DIR;
extern int mortal_start_room[];
extern struct room_data world;           /* In db.c */
extern struct char_data *character_list; /* In db.c */
extern struct index_data *mob_index;
extern int top_of_world;                   /* In db.c */
extern struct time_info_data time_info;    /* In db.c */
extern struct char_data *waiting_list;     /* in db.cpp */
extern struct char_data *fast_update_list; /* in db.cpp */
extern char help[];
extern unsigned long stat_ticks_passed;
extern unsigned long stat_mortals_counter;
extern unsigned long stat_immortals_counter;
// Zenith's adjustment to show stats (whitie/darkie)
extern unsigned long stat_whitie_counter;
extern unsigned long stat_darkie_counter;
extern unsigned long stat_whitie_legend_counter;
extern unsigned long stat_darkie_legend_counter;
extern int get_percent_absorb(char_data *character);

/* local globals */
struct descriptor_data *descriptor_list = 0, *next_to_process = 0;
struct txt_block *bufpool = 0; /* pool of large output buffers */
int buf_largecount;            /* # of large buffers which exist */
int buf_overflows;             /* # of overflows of output */
int buf_switches;              /* # of switches from small to large buf */
int circle_shutdown = 0;       /* clean shutdown */
int circle_reboot = 0;         /* reboot the game after a shutdown */
int no_specials = 0;           /* Suppress ass. of special routines */
int last_desc = 0;             /* last unique num assigned to a desc. */
SocketType mother_desc = 0;    /* file desc of the mother connection */
SocketType maxdesc;            /* highest desc num used */
int avail_descs;               /* max descriptors available */
int tics = 0;                  /* for extern checkpointing */
int has_proxy;                 /* Game expects to be proxied */

FILE *fpCommand; // DEBUGGING
int iCommands = 0;

struct txt_block *txt_block_pool = 0;
int txt_block_counter = 0;

extern int nameserver_is_slow; /* see config.c */
extern int autosave_time;      /* see config.c */

/* functions in this file */
int get_from_q(struct txt_q *queue, char *dest);
void run_the_game(sh_int port);
void game_loop(SocketType s);
SocketType init_socket(sh_int port);
SocketType pnew_connection(SocketType s);
SocketType pnew_descriptor(SocketType s);
int process_output(struct descriptor_data *t);
int process_input(struct descriptor_data *t);
void close_sockets(SocketType s);
struct timeval timediff(struct timeval *a, struct timeval *b);
void flush_queues(struct descriptor_data *d);
void nonblock(SocketType s);
int perform_subst(struct descriptor_data *t, char *orig, char *subst);
void complete_delay(struct char_data *ch);
void stat_update();

/* extern fcnts */
void boot_db(void);
void affect_update(void); /* In spells.c */
void fast_update(void);   /* In spells.c */
void point_update(void);  /* In limits.c */
void mobile_activity(void);
void string_add(struct descriptor_data *d, char *str);
void perform_violence(int);
void show_string(struct descriptor_data *d, char *input);
void check_reboot(void);
int isbanned(char *hostname);
void weather_and_time(int mode);
void *virt_program_number(int number);
void *virt_obj_program_number(int number);
void replace_aliases(char_data *ch, char *line);

// int gethostname(char *, int);

ACMD(do_cast);
SPECIAL(intelligent);
char *wait_wheel[8] = {"\r|\r", "\r\\\r", "\r-\r", "\r/\r", "\r|\r", "\r\\\r", "\r-\r", "\r/\r"};

void sigsegv_handler(int sig) {
    void *array[10];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

/* *********************************************************************
 *  main game loop and related stuff				       *
 ********************************************************************* */
#ifndef TESTING
int main(int argc, char **argv) {
    signal(SIGSEGV, sigsegv_handler);

    // initialize the random number generator
    std::srand(std::time(0));

    sh_int port;
    char buf[512];
    int pos = 1;
    char *dir;

    /* lets put the rots process in rwxrwx--- file mode */
    umask(S_IRWXO);

    port = DFLT_PORT;
    dir = DFLT_DIR;

    while ((pos < argc) && (*(argv[pos]) == '-')) {
        switch (*(argv[pos] + 1)) {
        case 'd':
            if (*(argv[pos] + 2))
                dir = argv[pos] + 2;
            else if (++pos < argc)
                dir = argv[pos];
            else {
                log("Directory arg expected after option -d.");
                exit(0);
            }
            break;
        case 'm':
            mini_mud = 1;
            no_rent_check = 1;
            log("Running in minimized mode & with no rent check.");
            break;
        case 'n':
            new_mud = 1;
            no_rent_check = 1;
            log("Running in pnew mode & with no rent check.");
            break;
        case 'q':
            no_rent_check = 1;
            log("Quick boot mode -- rent check supressed.");
            break;
        case 'r':
            restrict = 1;
            log("Restricting game -- no pnew players allowed.");
            break;
        case 's':
            no_specials = 1;
            log("Suppressing assignment of special routines.");
            break;
        case 'p':
            has_proxy = 1;
            log("Expecting proxy server.");
            break;
        default:
            sprintf(buf, "SYSERR: Unknown option -%c in argument string.", *(argv[pos] + 1));
            log(buf);
            break;
        }
        pos++;
    }

    if (pos < argc) {
        if (!isdigit(*argv[pos])) {
            fprintf(stderr, "Usage: %s [-m] [-q] [-r] [-s] [-p] [-d pathname] [ port # ]\n",
                    argv[0]);
            exit(0);
        } else if ((port = atoi(argv[pos])) <= 1024) {
            printf("Illegal port #\n");
            exit(0);
        }
    }

    /* Create the pidfile and log some info */
    sprintf(buf, "echo %d > .ageland.pid", getpid());
    system(buf);
    sprintf(buf, "Running game as pid %d.", getpid());
    log(buf);

    sprintf(buf, "Running game on port %d.", port);
    log(buf);

    if (chdir(dir) < 0) {
        perror("Fatal error changing to data directory");
        exit(0);
    }

    sprintf(buf, "Using %s as data directory.", dir);
    log(buf);

    // Open command log
    system("mv -f last_cmds crash_cmds");
    fpCommand = fopen("last_cmds", "w");
    srandom(time(0));
    run_the_game(port);
    return (0);
}
#endif

// TODO(drelidan):  Move this into a place that makes sense.  We're cooking pasta!
std::vector<char_data *> specialized_mages;

/* Init sockets, run game, and cleanup sockets */
void run_the_game(sh_int port) {
    int s;

    void signal_setup(void);

    descriptor_list = NULL;

    log("Signal trapping.");
    signal_setup();

    log("Opening mother connection.");
    mother_desc = s = init_socket(port);

    boot_db();

    sprintf(buf, "The char_data size is %d.", sizeof(char_data));
    log(buf);
    log("Entering game loop.");

    specialized_mages.clear();
    game_loop(s);

    close_sockets(s);
    // fclose(player_fl);

    if (circle_reboot) {
        log("Rebooting.");
        exit(52); /* what's so great about HHGTTG, anyhow? */
    }

    log("Normal termination of game.");
}

void clean_expose_elements() {
    for (char_iter iter = specialized_mages.begin(); iter != specialized_mages.end();) {
        char_data *mage = *iter;
        if (mage->extra_specialization_data.is_mage_spec()) {
            elemental_spec_data *spec_data = mage->extra_specialization_data.get_mage_spec();
            if (spec_data->exposed_target) {
                // The mage has cast 'expose elements' on a target.  If that target is no longer
                // in the room, remove this.
                int room_number = mage->in_room;
                const room_data &current_room = world[room_number];

                bool found_target = false;
                for (char_data *person = current_room.people; person;
                     person = person->next_in_room) {
                    if (person == spec_data->exposed_target) {
                        found_target = true;
                        break;
                    }
                }

                if (!found_target) {
                    send_to_char("Your target is no longer vulnerable to your spells.\r\n", mage);
                    spec_data->reset();
                }
            }

            ++iter;
        } else {
            // character no longer has a mage spec - remove them from our tracked list
            iter = specialized_mages.erase(iter);
        }
    }
}

/* MORE SPAGHETTI!!! */

/* Implementation from for function defined in utils.h */
void track_specialized_mage(char_data *mage) {
    if (!mage)
        return;

    char_iter found_mage = std::find(specialized_mages.begin(), specialized_mages.end(), mage);
    if (found_mage == specialized_mages.end()) {
        specialized_mages.push_back(mage);
    }
}

/* Implementation from for function defined in utils.h */
void untrack_specialized_mage(char_data *mage) {
    if (!mage)
        return;

    char_iter found_mage = std::remove(specialized_mages.begin(), specialized_mages.end(), mage);
    if (found_mage != specialized_mages.end()) {
        specialized_mages.erase(found_mage);
    }
}

void add_prompt(char *prompt, struct char_data *ch, long flag);

/* Accept pnew connects, relay commands, and call 'heartbeat-functs' */
timeval opt_time;
int pulse = 0; // moved here from being a local variable

int get_health_percent(char_data *character) {
    const float current_health = GET_HIT(character);
    const float max_health = GET_MAX_HIT(character);
    const float health_percent = (current_health / max_health) * 100.0f;

    return (int)health_percent;
}

void msdp_update() {
    for (auto desc = descriptor_list; desc; desc = desc->next) {
        if (!desc->character || IS_NPC(desc->character)) {
            continue;
        }

        if (!desc->pProtocol) {
            continue;
        }

        if (desc->character->in_room == NOWHERE) {
            return;
        }

        MSDPSetString(desc, eMSDP_CHARACTER_NAME, GET_NAME(desc->character));
        MSDPSetNumber(desc, eMSDP_ALIGNMENT, GET_ALIGNMENT(desc->character));
        MSDPSetNumber(desc, eMSDP_EXPERIENCE_MAX,
                      xp_to_level(GET_LEVEL(desc->character) + 1) -
                          xp_to_level(GET_LEVEL(desc->character)));
        MSDPSetNumber(desc, eMSDP_EXPERIENCE,
                      xp_to_level(GET_LEVEL(desc->character) + 1) - GET_EXP(desc->character));
        MSDPSetNumber(desc, eMSDP_HEALTH, GET_HIT(desc->character));
        MSDPSetNumber(desc, eMSDP_HEALTH_MAX, GET_MAX_HIT(desc->character));

        MSDPSetString(desc, eMSDP_ROOM_NAME, world[desc->character->in_room].name);
        MSDPSetNumber(desc, eMSDP_ROOM_VNUM, world[desc->character->in_room].number);
        MSDPSetNumber(desc, eMSDP_LEVEL, GET_LEVEL(desc->character));
        MSDPSetNumber(desc, eMSDP_MANA, GET_MANA(desc->character));
        MSDPSetNumber(desc, eMSDP_MANA_MAX, GET_MAX_MANA(desc->character));
        MSDPSetNumber(desc, eMSDP_MOVEMENT, GET_MOVE(desc->character));
        MSDPSetNumber(desc, eMSDP_MOVEMENT_MAX, GET_MAX_MOVE(desc->character));
        MSDPSetNumber(desc, eMSDP_MONEY, GET_GOLD(desc->character));
        MSDPSetString(desc, eMSDP_RACE, pc_races[utils::get_race(*desc->character)]);
        MSDPSetNumber(desc, eMSDP_STR, GET_STR(desc->character));
        MSDPSetNumber(desc, eMSDP_INT, GET_INT(desc->character));
        MSDPSetNumber(desc, eMSDP_WILL, GET_WILL(desc->character));
        MSDPSetNumber(desc, eMSDP_DEX, GET_DEX(desc->character));
        MSDPSetNumber(desc, eMSDP_CON, GET_CON(desc->character));
        MSDPSetNumber(desc, eMSDP_LEA, GET_LEA(desc->character));
        MSDPSetNumber(desc, eMSDP_STR_PERM, GET_STR_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_INT_PERM, GET_INT_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_WIL_PERM, GET_WILL_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_DEX_PERM, GET_DEX_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_CON_PERM, GET_CON_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_LEA_PERM, GET_LEA_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_WIMPY, WIMP_LEVEL(desc->character));
        MSDPSetNumber(desc, eMDSP_SPELL_SAVE, GET_SAVE(desc->character));

        player_spec::battle_mage_handler battle_mage_handler(desc->character);
        MSDPSetNumber(
            desc, eMDSP_SPELL_PEN,
            battle_mage_handler.get_bonus_spell_pen(desc->character->points.get_spell_pen()));
        MSDPSetNumber(
            desc, eMDSP_SPELL_POWER,
            battle_mage_handler.get_bonus_spell_power(desc->character->points.get_spell_power()));

        MSDPSetNumber(desc, eMDSP_ARMOUR_ABS, get_percent_absorb(desc->character));
        MSDPSetNumber(desc, eMDSP_OFFENSIVE_BONUS, get_real_OB(desc->character));
        MSDPSetNumber(desc, eMDSP_PARRY, get_real_parry(desc->character));
        MSDPSetNumber(desc, eMDSP_DODGE, get_real_dodge(desc->character));
        MSDPSetNumber(desc, eMDSP_ATTACK_SPEED, utils::get_energy_regen(*desc->character) / 5);

        extern char *tactics[];
        MSDPSetString(desc, eMDSP_TACTIC, tactics[GET_TACTICS(desc->character) - 1]);

        MSDPSetNumber(desc, eMDSP_PERCEPTION, GET_PERCEPTION(desc->character));
        MSDPSetNumber(desc, eMDSP_WILLPOWER, GET_WILLPOWER(desc->character));
        MSDPSetNumber(desc, eMDSP_SKILL_ENCUMBRANCE, utils::get_encumbrance(*desc->character));
        MSDPSetNumber(desc, eMDSP_MOVEMENT_ENCUMBRANCE,
                      utils::get_leg_encumbrance(*desc->character));
        MSDPSetNumber(desc, eMDSP_HEALTH_REGENERATION, (int)hit_gain(desc->character));
        MSDPSetNumber(desc, eMDSP_STAMINA_REGENERATION, (int)mana_gain(desc->character));
        MSDPSetNumber(desc, eMDSP_MOVEMENT_REGENERATION, (int)move_gain(desc->character));

        auto sector_type = world[desc->character->in_room].sector_type;
        auto weather_type = weather_info.sky[sector_type];
        extern char *weather_messages[8][13];

        if (OUTSIDE(desc->character)) {
            MSDPSetString(desc, eMDSP_WEATHER, weather_messages[weather_type + 2][sector_type]);
        } else {
            MSDPSetString(desc, eMDSP_WEATHER, "You can have no feeling about the weather here.");
        }

        auto opponent = desc->character->specials.fighting;
        if (opponent && utils::is_npc(*opponent)) {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, get_health_percent(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, GET_NAME(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, std::to_string(GET_LEVEL(opponent)).c_str());
        } else if (opponent && utils::is_pc(*opponent)) {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, get_health_percent(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, pc_star_types[utils::get_race(*opponent)]);
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, "???");
        } else {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, 0);
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, "");
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, "");
        }

        MSDPSetNumber(desc, eMSDP_SPIRIT, GET_SPIRIT(desc->character));

        MSDPUpdate(desc);
    }
}

void game_loop(SocketType s) {
    fd_set input_set, output_set, exc_set;
    struct timeval last_time, now, timespent, timeout, null_time;
    char comm[MAX_INPUT_LENGTH];
    char prompt[MAX_INPUT_LENGTH];
    char *pptr;
    struct descriptor_data *point, *next_point;
    struct char_data *wait_ch, *wait_tmp;
    int mask;
    // Accumulates heartbeat pulses and fires the periodic crash-save snapshot
    // once per configured interval (autosave_time seconds). Persists across the
    // game-loop iterations below.
    AutosaveTimer autosave_timer;
    int sockets_connected, sockets_playing;
    int tmp, was_updated;
    char disp, tmpflag;
    char buf[100];

    null_time.tv_sec = 0;
    null_time.tv_usec = 0;

    opt_time.tv_usec = OPT_USEC; /* Init time values */
    opt_time.tv_sec = 0;
    gettimeofday(&last_time, NULL);

    maxdesc = s;

#if defined(OPEN_MAX)
    avail_descs = OPEN_MAX - 8;
#elif defined(USE_TABLE_SIZE)
    {
        int retval;

        retval = setdtablesize(64);
        if (retval == -1)
            log("SYSERR: unable to set table size");
        else {
            sprintf(buf, "%s %d\n", "dtablesize set to: ", retval);
            log(buf);
        }
        avail_descs = getdtablesize() - 8;
    }
#else
    avail_descs = MAX_DESCRIPTORS_AVAILABLE;
#endif

    avail_descs = std::min(avail_descs, MAX_PLAYERS);

    mask = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGALRM) | sigmask(SIGTERM) |
           sigmask(SIGURG) | sigmask(SIGXCPU) | sigmask(SIGHUP) | sigmask(SIGSEGV) |
           sigmask(SIGBUS);

    /* Main loop */
    while (!circle_shutdown) {
        /* Check what's happening out there */
        FD_ZERO(&input_set);
        FD_ZERO(&output_set);
        FD_ZERO(&exc_set);
        FD_SET(s, &input_set);
        for (point = descriptor_list; point; point = point->next)
            if (point->descriptor) {
                FD_SET(point->descriptor, &input_set);
                FD_SET(point->descriptor, &exc_set);
                FD_SET(point->descriptor, &output_set);
            }

        /* check out the time */
        gettimeofday(&now, NULL);
        timespent = timediff(&now, &last_time);
        timeout = timediff(&opt_time, &timespent);
        last_time.tv_sec = now.tv_sec + timeout.tv_sec;
        last_time.tv_usec = now.tv_usec + timeout.tv_usec;

        if (last_time.tv_usec >= 1000000) {
            last_time.tv_usec -= 1000000;
            last_time.tv_sec++;
        }

        sigsetmask(mask);

        if ((tmp = select(maxdesc + 1, &input_set, &output_set, &exc_set, &null_time) < 0)) {
            if (errno != EINTR) {
                perror("Select poll");
                return;
            }
        }

        if (select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0, &timeout) < 0) {
            if (errno != EINTR) {
                perror("Select sleep");
                exit(1);
            }
        }

        sigsetmask(0);

        /* Respond to whatever might be happening */

        /* Pnew connection? */
        if (FD_ISSET(s, &input_set)) {
            if (pnew_descriptor(s) == 0) // here was <0, had to change
            {
                perror("Pnew connection");
            }
        }

        /* kick out the freaky folks */
        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &exc_set)) {
                    FD_CLR(point->descriptor, &input_set);
                    FD_CLR(point->descriptor, &output_set);
                    close_socket(point, FALSE);
                }
            }
        }

        /* take our input off of the TCP queues */
        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &input_set)) {
                    if (process_input(point) < 0) {
                        close_socket(point, FALSE);
                    }
                }
            }
        }

        /* process_commands */
        for (wait_ch = waiting_list; wait_ch; wait_ch = wait_tmp) {
            if (wait_ch->delay.wait_value > 0) {
                (wait_ch->delay.wait_value)--;
            }

            if (wait_ch->delay.wait_value > 0) {
                if (!IS_NPC(wait_ch) && IS_AFFECTED(wait_ch, AFF_WAITWHEEL)) {
                    if (PRF_FLAGGED(wait_ch, PRF_SPINNER)) {
                        write_to_descriptor(wait_ch->desc->descriptor,
                                            wait_wheel[wait_ch->delay.wait_value % 8]);
                    }
                }

                wait_tmp = wait_ch->delay.next;
            } else if (wait_ch->delay.wait_value == 0) {
                /* here is the block calling actual procedures */
                complete_delay(wait_ch);
                wait_tmp = wait_ch->delay.next;

                if (wait_ch->delay.wait_value == 0)
                    /* look out for the similar code in raw_kill() */
                    abort_delay(wait_ch);
            } else {
                wait_tmp = wait_ch->delay.next;
            }
        }

        for (point = descriptor_list; point; point = next_to_process) {
            next_to_process = point->next;
            if (point->descriptor) {
                if (point->character)
                    tmpflag = (!IS_AFFECTED(point->character, AFF_WAITING));
                else
                    tmpflag = 1;

                if (tmpflag && (get_from_q(&point->input, comm))) {
                    if (point->character && !IS_NPC(point->character) &&
                        point->connected == CON_PLYNG &&
                        point->character->specials.was_in_room != NOWHERE) {
                        if (point->character->in_room != NOWHERE) {
                            char_from_room(point->character);
                        }

                        char_to_room(point->character, point->character->specials.was_in_room);
                        point->character->specials.was_in_room = NOWHERE;
                        act("$n has returned.", TRUE, point->character, 0, 0, TO_ROOM);
                        point->character->specials.timer = 0;
                    }
                    if (point->character && IS_AFFECTED(point->character, AFF_WAITWHEEL)) {
                        point->character->delay.wait_value = 1;
                        point->character->specials.timer = 0;
                    }
                    if (point->character && IS_SET(PLR_FLAGS(point->character), PLR_WRITING)) {
                        string_add(point, comm);
                    }

                    point->prompt_mode = 1;
                    if (!point->connected) {
                        if (point->showstr_point) {
                            show_string(point, comm);
                        } else {
                            if (!IS_SET(PLR_FLAGS(point->character), PLR_WRITING)) {
                                replace_aliases(point->character, comm);
                                command_interpreter(point->character, comm, 0);
                            }
                        }
                    } else {
                        nanny(point, comm);
                    }
                }
            }
        }

        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &output_set) && *(point->output)) {
                    if (process_output(point) < 0) {
                        close_socket(point, FALSE);
                    } else {
                        point->prompt_mode = 1;
                    }
                }
            }
        }

        /* kick out the Phreaky Pholks II  -JE */
        for (point = descriptor_list; point; point = next_to_process) {
            next_to_process = point->next;
            if (point->descriptor) {
                if (STATE(point) == CON_CLOSE) {
                    close_socket(point, FALSE);
                }
            }
        }

        /* give the people some prompts */
        for (point = descriptor_list; point; point = point->next)
            if (point->prompt_mode && point->descriptor) {
                if (point->character) {
                    tmp = (IS_SET(PLR_FLAGS(point->character), PLR_WRITING));
                } else {
                    tmp = !(point->connected);
                }
                if (tmp) {
                    write_to_descriptor(point->descriptor, "] ");
                } else if (!point->connected) {
                    if (point->showstr_point)
                        write_to_descriptor(point->descriptor,
                                            "*** Press return to continue, q to quit ***");
                    else { /*if point->showstr_point */
                        struct char_data *opponent;
                        struct char_data *tank;

                        pptr = prompt;

                        if (GET_INVIS_LEV(point->character)) {
                            sprintf(prompt, "i%d", GET_INVIS_LEV(point->character));
                            disp = FALSE;
                        } else
                            prompt[0] = 0;

                        if (IS_RIDING(point->character))
                            sprintf(prompt, "%s R", prompt);

                        if (PRF_FLAGGED(point->character, PRF_ADVANCED_PROMPT)) {
                            sprintf(prompt, "%s [", prompt);
                            add_prompt(prompt, point->character, PROMPT_ADVANCED);
                        } else if (((GET_HIT(point->character) < GET_MAX_HIT(point->character)) ||
                                    point->character->specials.fighting) &&
                                   PRF_FLAGGED(point->character, PRF_PROMPT)) {
                            sprintf(prompt, "%s HP:", prompt);
                        }

                        opponent = point->character->specials.fighting;

                        if (!PRF_FLAGGED(point->character, PRF_ADVANCED_PROMPT)) {
                            add_prompt(prompt, point->character,
                                       PRF_FLAGGED(point->character, PRF_DISPTEXT)  ? PRF_DISPTEXT
                                       : !PRF_FLAGGED(point->character, PRF_PROMPT) ? 0
                                                                                    : PROMPT_ALL);
                        }

                        if (opponent && IS_MENTAL(opponent)) {
                            sprintf(prompt, "%s Mind:", prompt);
                            add_prompt(prompt, point->character, PROMPT_STAT);
                        }

                        if (IS_RIDING(point->character))
                            add_prompt(prompt, point->character->mount_data.mount, PROMPT_MOVE);

                        if (GET_RACE(point->character) == RACE_BEORNING) {
                            affected_type *maul_buff =
                                affected_by_spell(point->character, SKILL_MAUL);
                            if (maul_buff && maul_buff->location == APPLY_MAUL) {
                                sprintf(prompt, "%s Maul:", prompt);
                                add_prompt(prompt, point->character, PROMPT_MAUL);
                            }
                        }

                        const obj_data *quiver = point->character->equipment[WEAR_BACK];
                        if (quiver && quiver->is_quiver()) {
                            sprintf(prompt, "%s A:(", prompt);
                            add_prompt(prompt, point->character, PROMPT_ARROWS);
                        }

                        if (point->character->specials.position == POSITION_FIGHTING) {
                            if (opponent) {
                                if (opponent->specials.fighting != point->character) {
                                    tank = opponent->specials.fighting;
                                    if (tank) {
                                        sprintf(prompt, "%s, %s:", prompt,
                                                PERS(tank, point->character, FALSE, FALSE));
                                        add_prompt(prompt, tank,
                                                   (IS_MENTAL(opponent)) ? PROMPT_STAT
                                                                         : PROMPT_HIT);
                                    }
                                }
                                sprintf(prompt, "%s, %s:", prompt,
                                        PERS(opponent, point->character, FALSE, FALSE));

                                add_prompt(prompt, opponent,
                                           (IS_MENTAL(point->character))
                                               ? PROMPT_STAT
                                               : (IS_SHADOW(opponent) ? PROMPT_STAT : PROMPT_HIT));
                            }
                        }

                        // Check for a blank space in the first position or the last
                        if (prompt[0] == ' ')
                            pptr++;
                        if (prompt[strlen(prompt) - 1] == ' ')
                            prompt[strlen(prompt) - 1] = '\0';

                        disp = TRUE;
                        if (point->character->specials.position == POSITION_SHAPING)
                            strcat(prompt, "]");
                        else
                            strcat(prompt, ">");

                        if (point->character)
                            tmpflag = !IS_AFFECTED(point->character, AFF_WAITWHEEL);
                        else
                            tmpflag = 1;
                        if (tmpflag)
                            write_to_descriptor(point->descriptor, pptr);
                    }
                }
                point->prompt_mode = 0;
            }

        /* handle heartbeat stuff */
        /* Note: pulse now changes every 1/4 sec  */

        pulse++;
        was_updated = 0;

        if (!((pulse + 3) % PULSE_ZONE)) {
            zone_update();
        }
        if (!((pulse + 9) % PULSE_MOBILE)) {
            mobile_activity();
            was_updated = 1;
        }
        perform_violence(pulse % (PULSE_VIOLENCE * 2));
        /* parry is restored in 2 combat (PULSE_VIOLENCE) rounds */

        if (!((pulse % (SECS_PER_MUD_HOUR * 4)))) {
            weather_and_time(1);
            point_update(); // putting affect_total call in point_update.
            stat_update();
            was_updated = 1;
        }
        if (!(pulse % (PULSE_FAST_UPDATE)) /*&& !was_updated*/) {
            // now increasing hp/mp/mana/spirit fast in fast_update..
            fast_update();
            affect_update();

            // clean-up expose elements
            clean_expose_elements();
        }

        msdp_update();

        // Periodic point-in-time snapshot of all connected players. Fires every
        // autosave_time seconds (clamped to >= 1s); see crashsave_schedule.h.
        if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) {
            Crash_save_all();
        }

        if (!(pulse % 4)) {
            game_timer::skill_timer &st_instance = game_timer::skill_timer::instance();
            st_instance.update_skill_timer();
        }

        if (!(pulse % 1200)) {
            sockets_connected = sockets_playing = 0;

            for (point = descriptor_list; point; point = next_point) {
                next_point = point->next;
                if (point->descriptor) {
                    sockets_connected++;
                    if (!point->connected) {
                        sockets_playing++;
                    }
                }
            }

            sprintf(buf, "nusage: %-3d sockets connected, %-3d sockets playing", sockets_connected,
                    sockets_playing);
            log(buf);

#ifdef RUSAGE
            {
                struct rusage rusagedata;

                getrusage(0, &rusagedata);
                sprintf(buf, "rusage: %d %d %d %d %d %d %d", rusagedata.ru_utime.tv_sec,
                        rusagedata.ru_stime.tv_sec, rusagedata.ru_maxrss, rusagedata.ru_ixrss,
                        rusagedata.ru_ismrss, rusagedata.ru_idrss, rusagedata.ru_isrss);
                log(buf);
            }
#endif
        }

        if (pulse >= 2400)
            pulse = 0;

        tics++; /* tics since last checkpoint signal */

        // Save chars before a shutdown or reboot.  --S
        if (circle_shutdown || circle_reboot) {
            struct char_data *ch;

            for (ch = character_list; ch; ch = ch->next) {
                if (!IS_NPC(ch) && ch->desc) {
                    save_char(ch, NOWHERE, 0);
                    Crash_crashsave(ch);
                }
            }
        }
    }
}

/* ******************************************************************
 *  general utility stuff (for local use)			    *
 ****************************************************************** */

int get_from_q(struct txt_q *queue, char *dest) {
    struct txt_block *tmp;

    /* Q empty? */
    if (!queue->head)
        return (0);

    tmp = queue->head;
    strcpy(dest, queue->head->text);
    queue->head = queue->head->next;

    //   RELEASE(tmp->text);
    //   RELEASE(tmp);
    put_to_txt_block_pool(tmp);
    return (1);
}

void write_to_output(const char *txt, struct descriptor_data *t) {
    int size;

    size = strlen(txt);

    /* if we're in the overflow state already, ignore this */
    if (t->bufptr < 0)
        return;

    /* if we have enough space, just write to buffer and that's it! */
    if (t->bufspace >= size) {
        strcpy(t->output + t->bufptr, txt);
        t->bufspace -= size;
        t->bufptr += size;
    } else { /* otherwise, try to switch to a large buffer */
        if (t->large_outbuf || ((size + strlen(t->output)) > LARGE_BUFSIZE)) {
            /* we're already using large buffer, or even the large buffer
            in't big enough -- switch to overflow state */
            t->bufptr = -1;
            buf_overflows++;
            return;
        }

        buf_switches++;
        /* if the pool has a buffer in it, grab it */
        if (bufpool) {
            t->large_outbuf = bufpool;
            bufpool = bufpool->next;
        } else { /* else create one */
            CREATE(t->large_outbuf, struct txt_block, 1);
            CREATE(t->large_outbuf->text, char, LARGE_BUFSIZE);
            buf_largecount++;
        }

        strcpy(t->large_outbuf->text, t->output);
        t->output = t->large_outbuf->text;
        strcpy(t->output + strlen(t->output), txt);
        t->bufspace = LARGE_BUFSIZE - 1 - strlen(t->output);
        t->bufptr = strlen(t->output);
    }
}

struct txt_block *get_from_txt_block_pool(char *line) {
    struct txt_block *pnew;
    int tmp;

    if (txt_block_pool) {
        pnew = txt_block_pool;
        txt_block_pool = pnew->next;
    } else {
        CREATE(pnew, struct txt_block, 1);
        CREATE(pnew->text, char, MAX_INPUT_LENGTH /*strlen(txt) + 1*/);
        txt_block_counter++;
    }
    if (line) {
        tmp = strlen(line);
        if (tmp > MAX_INPUT_LENGTH - 1)
            tmp = MAX_INPUT_LENGTH - 1;
        strncpy(pnew->text, line, tmp);
        pnew->text[tmp] = 0;
    }
    return pnew;
}
void put_to_txt_block_pool(struct txt_block *pold) {

    pold->next = txt_block_pool;
    txt_block_pool = pold;
}

void write_to_q(char *txt, struct txt_q *queue) {
    struct txt_block *pnew;

    pnew = get_from_txt_block_pool();

    if (strlen(txt) > 255)
        txt[255] = 0;
    strcpy(pnew->text, txt);

    /* Q empty? */
    if (!queue->head) {
        pnew->next = NULL;
        queue->head = queue->tail = pnew;
    } else {
        queue->tail->next = pnew;
        queue->tail = pnew;
        pnew->next = NULL;
    }
}

void write_to_q_lang(char *txt, struct txt_q *queue, int freq) {
    struct txt_block *pnew;
    int i, len;

    pnew = get_from_txt_block_pool();

    len = strlen(txt);
    if (len > 255)
        len = 255;
    //   strcpy(pnew->text, txt);
    for (i = 0; i < len; i++) {
        if (number(1, 100) > freq)
            pnew->text[i] = number(60, 90);
        else
            pnew->text[i] = txt[i];
    }
    txt[i] = 0;

    /* Q empty? */
    if (!queue->head) {
        pnew->next = NULL;
        queue->head = queue->tail = pnew;
    } else {
        queue->tail->next = pnew;
        queue->tail = pnew;
        pnew->next = NULL;
    }
}

struct timeval timediff(struct timeval *a, struct timeval *b) {
    struct timeval rslt, tmp;

    tmp = *a;

    if ((rslt.tv_usec = tmp.tv_usec - b->tv_usec) < 0) {
        rslt.tv_usec += 1000000;
        --(tmp.tv_sec);
    }
    if ((rslt.tv_sec = tmp.tv_sec - b->tv_sec) < 0) {
        rslt.tv_usec = 0;
        rslt.tv_sec = 0;
    }
    return (rslt);
}

/* Empty the queues before closing connection */
void flush_queues(struct descriptor_data *d) {
    if (d->large_outbuf) {
        d->large_outbuf->next = bufpool;
        bufpool = d->large_outbuf;
        d->large_outbuf = 0;
    }

    while (get_from_q(&d->input, buf2))
        ;
}

/* ******************************************************************
 *  socket handling						    *
 ****************************************************************** */

SocketType init_socket(sh_int port) {
    SocketType s;
    int opt = 1;
    char hostname[MAX_HOSTNAME + 1];
    struct sockaddr_in sa;
    struct sockaddr *saddr;

    saddr = (struct sockaddr *)&sa;
    bzero((char *)saddr, sizeof(struct sockaddr_in));
    if (gethostname(hostname, MAX_HOSTNAME)) {
        perror("gethostname");
        exit(1);
    }

    sa.sin_port = htons(port);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = 0;

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Init-socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt REUSEADDR");
        exit(1);
    }

    /* #ifdef USE_LINGER */
    {
        struct linger ld;

        ld.l_onoff = 0; // FINGOLFIN changed from 1 on 24th April 2001
        ld.l_linger = 0;
        if (setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&ld, sizeof(ld)) < 0) {
            perror("setsockopt LINGER");
            exit(1);
        }
    }
    /* #endif */

    if (bind(s, saddr, sizeof(struct sockaddr)) != 0) {
        perror("bind");
        close(s);
        system("touch ../.killscript");
        exit(1);
    }

    /* nonblock handles its own problems */
    nonblock(s);

    listen(s, 3);

    return (s);
}

SocketType pnew_connection(SocketType s) {
    struct sockaddr_in isa;
    socklen_t i;
    SocketType t;

    i = sizeof(isa);
    if ((t = accept(s, (struct sockaddr *)(&isa), &i)) < 0) {
        perror("Accept");
        return (0); // probably incorrect..
    }
    sprintf(buf, "Socket %d connected.", t);
    mudlog(buf, NRM, LEVEL_IMPL, TRUE);

    return (t);
}

SocketType pnew_descriptor(SocketType s) {
    SocketType desc;
    struct descriptor_data *pnewd, *point, *next_point;
    socklen_t size;
    int sockets_connected, sockets_playing, i;
    struct sockaddr_in sock;
    struct hostent *from;
    extern char *GREETINGS;

    if ((desc = pnew_connection(s)) == 0) // here was <0, too bad
        return (0);                       // here was -1, too bad...

    sockets_connected = sockets_playing = 0;

    for (point = descriptor_list; point; point = next_point) {
        next_point = point->next;
        if (point->descriptor) {
            sockets_connected++;
            if (!point->connected)
                sockets_playing++;
        }
    }

    /*	if ((maxdesc + 1) >= avail_descs) */
    if (sockets_connected >= avail_descs) {
        write_to_descriptor(desc, "Sorry, RotS is full right now... try again later!  :-)\n\r");
        close(desc);
        return (0);
    } else if (desc > maxdesc)
        maxdesc = desc;

    CREATE(pnewd, struct descriptor_data, 1);

    /* find info */
    size = sizeof(sock);

    int err = 0;

    // The game is running behind a proxy; read the peer address from the connection
    if (has_proxy) {
        err = read(desc, &sock.sin_addr.s_addr, sizeof(sock.sin_addr.s_addr));

        if (err < 0) {
            perror("reading proxy header");
        }
    } else {
        err = getpeername(desc, (struct sockaddr *)&sock, &size);

        if (err < 0) {
            perror("getpeername");
        }
    }

    // Done with synchronous reading, switch to nonblocking
    nonblock(desc);

    if (err < 0) {
        *pnewd->host = '\0';
    } else if (nameserver_is_slow ||
               !(from = gethostbyaddr((char *)&sock.sin_addr, sizeof(sock.sin_addr), AF_INET))) {
        if (!nameserver_is_slow)
            perror("gethostbyaddr");
        i = sock.sin_addr.s_addr;
        sprintf(pnewd->host, "%d.%d.%d.%d", (i & 0x000000FF), (i & 0x0000FF00) >> 8,
                (i & 0x00FF0000) >> 16, (i & 0xFF000000) >> 24);
    } else {
        strncpy(pnewd->host, from->h_name, 49);
        *(pnewd->host + 49) = '\0';
    }

    if (isbanned(pnewd->host) == BAN_ALL) {
        close(desc);
        if (strcmp(pnewd->host, "shrout.org")) // Don't log if from shout.org.
        {
            sprintf(buf2, "Connection attempt denied from [%s]", pnewd->host);
            mudlog(buf2, NRM, LEVEL_GOD, TRUE);
        }
        RELEASE(pnewd);
        return (0);
    }

    /*  Uncomment this if you want pnew connections logged.  It's usually not
    necessary, and just adds a lot of unnecessary bulk to the logs.

   sprintf(buf2, "Pnew connection from [%s]", pnewd->host);
   log(buf2);
*/

    /* init desc data */
    pnewd->descriptor = desc;
    pnewd->connected = CON_NME;
    pnewd->bad_pws = 0;
    pnewd->pos = -1;
    //   pnewd->wait = 1;
    pnewd->prompt_mode = 0;
    *pnewd->buf = '\0';
    pnewd->str = 0;
    pnewd->showstr_head = 0;
    pnewd->showstr_point = 0;
    *pnewd->last_input = '\0';
    pnewd->output = pnewd->small_outbuf;
    *(pnewd->output) = '\0';
    pnewd->bufspace = SMALL_BUFSIZE - 1;
    pnewd->large_outbuf = NULL;
    pnewd->input.head = NULL;
    pnewd->next = descriptor_list;
    pnewd->character = 0;
    pnewd->original = 0;
    pnewd->snoop.snooping = 0;
    pnewd->snoop.snoop_by = 0;
    pnewd->login_time = time(0);
    pnewd->dflags = 0;
    pnewd->last_input_time = pnewd->login_time;

    if (++last_desc == 1000)
        last_desc = 1;
    //   pnewd->desc_num = last_desc;
    pnewd->desc_num = int(desc);

    /* prepend to list */

    descriptor_data *cur_list = descriptor_list;
    descriptor_list = pnewd;

    SEND_TO_Q(GREETINGS, pnewd);
    SEND_TO_Q("By what name do you wish to be known? ", pnewd);

    return (1);
}

extern sh_int screen_width; /* config.cpp */

void append_lines(char *target, char *source, int *len) {
    register sh_int i, tmp;
    int sourcelen;

    tmp = *len;
    sourcelen = strlen(source);

    for (i = 0; i < sourcelen; i++) {
        *(target++) = source[i];
        tmp++;
        if (source[i] == '\r')
            tmp = 0;
        if (source[i] == '\n')
            tmp--;
        if (tmp > screen_width) {
            *(target++) = '\n';
            *(target++) = '\r';
            tmp = 0;
        }
    }
    *len = tmp;
    *target = 0;
}

char process_output_buffer[LARGE_BUFSIZE + 20];

int process_output(struct descriptor_data *t) {
    char *i = process_output_buffer, *c;
    int wid_count, i_shift;
    /* start writing at the 2nd space so we can prepend "% " for snoop */
    wid_count = 0;
    if (!t->prompt_mode && !t->connected) {
        strcpy(i + 2, "\n\r");
        i_shift = 2;
    } else
        i_shift = 0;
    if ((t->character) && (IS_SET(PRF_FLAGS(t->character), PRF_WRAP)))
        append_lines(i + 2 + i_shift, t->output, &wid_count);
    else
        strcpy(i + 2 + i_shift, t->output);

    /* they don't have latin-1 set. unaccent all of our latin-1 chars */
    if (t->character)
        if (!PRF_FLAGGED(t->character, PRF_LATIN1))
            for (c = i + 2; *c; ++c)
                *c = unaccent(*c);

    if (t->bufptr < 0)
        strcat(i + 2, "**OVERFLOW**");

    if (!t->connected &&
        !(t->character && !IS_NPC(t->character) && PRF_FLAGGED(t->character, PRF_COMPACT)))
        strcat(i + 2, "\n\r");

    if (write_to_descriptor(t->descriptor, i + 2) < 0)
        return -1;

    if (t->snoop.snoop_by) {
        i[0] = '%';
        i[1] = ' ';
        SEND_TO_Q(i, t->snoop.snoop_by->desc);
    }

    /* if we were using a large buffer, put the large buffer on the buffer
      pool and switch back to the small one */
    if (t->large_outbuf) {
        t->large_outbuf->next = bufpool;
        bufpool = t->large_outbuf;
        t->large_outbuf = NULL;
        t->output = t->small_outbuf;
    }

    /* reset total bufspace back to that of a small buffer */
    t->bufspace = SMALL_BUFSIZE - 1;
    t->bufptr = 0;
    *(t->output) = '\0';

    return 1;
}

// New version (similar to circle ver 3) April 2001 - Fingolfin
// Not windows compatible (not finished and not working...), but c'est la vie...

int write_to_descriptor_new(int desc, char *txt) {
    int result, length;

    length = strlen(txt);

    while (length > 0) {

        result = write(desc, txt, length);

        if (result <= 0) {
            if (result == 0) {
                /* This should never happen! */
                log("SYSERR: Huh??  write() returned 0???  Please report this!");
                return (-1);
            }

            if (errno == EWOULDBLOCK) {
                perror("WARNING: socket would block, about to close");
                return (-1);
            } else {
                /* Fatal error.  Disconnect the player. */
                perror("SYSERR: Write to socket");
                return (-1);
            }
        }

        txt += result;
        length -= result;
    }
    return (0);
}

// old version replaced with above April 2001 - Fingolfin

int write_to_descriptor(int desc, char *txt) {
    int sofar, thisround, total;

    total = strlen(txt);
    sofar = 0;

    if (desc <= 0) {
        return 0;
    }

    try {
        do {
            thisround = write(desc, txt + sofar, total - sofar);
            if (thisround < 0) {
                perror("Write to socket");
                return (-1);
            }
            sofar += thisround;
        } while (sofar < total);
    } catch (...) {
        vmudlog(NRM, "Exception in write_to_descriptor");
        return -1;
    }

    return (0);
}

void break_spell(struct char_data *ch) {
    //  if(IS_AFFECTED(ch, AFF_WAITWHEEL)){
    //        printf("breaking spell for %s\n", ch->player.name);
    //    REMOVE_BIT(ch->specials.affected_by, AFF_WAITWHEEL);
    //    REMOVE_BIT(ch->specials.affected_by, AFF_WAITING);
    //    if(ch->desc)
    //      write_to_descriptor(ch->desc->descriptor,"\r");
    ch->delay.wait_value = 0;
    ch->delay.subcmd = -1;

    //     if((ch->delay.cmd == -1) && IS_NPC(ch)){
    //       if(IS_NPC(ch) && mob_index[ch->nr].func){
    //		printf("calling special-return\n");
    // 	(*mob_index[ch->nr].func)
    // 	  (ch, 0, -1, "", 0, &(ch->delay));
    //       }
    //     }
    //     else if(ch->delay.cmd > 0){
    //       //	      printf("gonna interpret, name=%s, delay=%p\n",wait_ch->player.name,
    //       &(wait_ch->delay)); command_interpreter(ch, "", &(ch->delay));
    //     }
    //  }
    //  else{
    // no problem - just couldn't break it
    // send_to_char("Minor problem in break_spell. Please notify imps. :)\n\r",
    //		 ch);
    //  }
}

char process_input_tmp[MAX_INPUT_LENGTH + 2];
char process_input_buffer[MAX_INPUT_LENGTH + 60];
int process_input(struct descriptor_data *t) {
    int sofar, thisround, begin, squelch, i, k, flag, failed_subst = 0;
    char *tmp = process_input_tmp;
    char *buffer = process_input_buffer;

    if (!t->descriptor)
        return (0);

    sofar = flag = 0;
    begin = strlen(t->buf);

    /* Read in some stuff */
    do {
        char inbuf[2048];
        thisround = read(t->descriptor, inbuf, sizeof(inbuf));
        if (thisround > 0) {
            /* Filter out telnet/MSDP negotiation if protocol is active */
            if (t->pProtocol) {
                char filtered[MAX_STRING_LENGTH];
                filtered[0] = '\0';
                ProtocolInput(t, inbuf, thisround, filtered);

                int add = (int)strlen(filtered);
                int room = MAX_STRING_LENGTH - (begin + sofar) - 1;
                if (add > room)
                    add = room;
                if (add > 0) {
                    memcpy(t->buf + begin + sofar, filtered, add);
                    sofar += add;
                    *(t->buf + begin + sofar) = 0;
                }
            } else {
                /* No protocol yet; append raw bytes (clamped) */
                int room = MAX_STRING_LENGTH - (begin + sofar) - 1;
                int add = thisround;
                if (add > room)
                    add = room;
                if (add > 0) {
                    memcpy(t->buf + begin + sofar, inbuf, add);
                    sofar += add;
                    *(t->buf + begin + sofar) = 0;
                }
            }
        } else {
            if (thisround < 0) {
                if (errno != EWOULDBLOCK) {
                    perror("Read1 - ERROR");
                    return (-1);
                } else
                    break;
            } else {
                log("EOF encountered on socket read.");
                return (-1);
            }
        }
    } while (!ISNEWL(*(t->buf + begin + sofar - 1)));

    if (t->character)
        t->character->specials.timer = 0;

    *(t->buf + begin + sofar) = 0;

    /* if no pnewline is contained in input, return without proc'ing */
    for (i = begin; !ISNEWL(*(t->buf + i)); i++)
        if (!*(t->buf + i))
            return (0);

    /* input contains 1 or more pnewlines; process the stuff */
    for (i = 0, k = 0; *(t->buf + i);) {
        if (!ISNEWL(*(t->buf + i)) && !(flag = (k >= (MAX_INPUT_LENGTH - 2))))
            /* is this a backspace? */
            if ((*(t->buf + i) == '\b') || ((unsigned char)*(t->buf + i) == 177))
                if (k) { /* more than one char ? */
                    if (*(tmp + --k) == '$')
                        k--;
                    i++;
                } else
                    i++; /* no or just one char.. Skip backsp */
            else if (isascii(unaccent(*(t->buf + i))) && isprint(unaccent(*(t->buf + i)))) {
                /* trans char, double for '$' (printf)	*/
                if ((*(tmp + k) = *(t->buf + i)) == '$')
                    *(tmp + ++k) = '$';
                k++;
                i++;
            } else
                i++;
        else { /* the char pointed to IS a newline, or we've overflowed */
            *(tmp + k) = 0;
            /* is this a blank line? are they casting? end the cast if so */
            if ((*tmp == 0) && (STATE(t) == CON_PLYNG) && IS_AFFECTED(t->character, AFF_WAITWHEEL))
                break_spell(t->character);

            /* they entered a command, no? so we update their last_input_time */
            t->last_input_time = time(0);

            /* if they input !, then we repeat the last input */
            if (*tmp == '!')
                strcpy(tmp, t->last_input);
            else if (*tmp == '^') {
                if (!(failed_subst = perform_subst(t, t->last_input, tmp)))
                    strcpy(t->last_input, tmp);
            } else if (*tmp > ' ')
                strcpy(t->last_input, tmp);

            // COMMAND LOG
            if (iCommands == 200) {
                fclose(fpCommand);
                fpCommand = fopen("last_cmds", "w");
                iCommands = 0;
            }

            if ((t->connected == CON_PLYNG) && (t->character))
                fprintf(fpCommand, "%3d %-16s: %s\n", t->descriptor, GET_NAME(t->character), tmp);

            iCommands++;
            fflush(fpCommand);

            if (!failed_subst)
                write_to_q(tmp, &t->input);

            if (t->snoop.snoop_by) {
                SEND_TO_Q("% ", t->snoop.snoop_by->desc);
                SEND_TO_Q(tmp, t->snoop.snoop_by->desc);
                SEND_TO_Q("\n\r", t->snoop.snoop_by->desc);
            }

            if (flag) {
                sprintf(buffer, "Line too long.  Truncated to:\n\r%s\n\r", tmp);
                if (write_to_descriptor(t->descriptor, buffer) < 0)
                    return (-1);

                /* skip the rest of the line */
                for (; !ISNEWL(*(t->buf + i)); i++)
                    ;
            }

            /* find end of entry */
            for (; ISNEWL(*(t->buf + i)); i++)
                ;

            /* squelch the entry from the buffer */
            for (squelch = 0;; squelch++)
                if ((*(t->buf + squelch) = *(t->buf + i + squelch)) == '\0')
                    break;
            k = 0;
            i = 0;
        }
    }
    return 1;
}

char perform_subst_pnew[MAX_INPUT_LENGTH + 5];

int perform_subst(struct descriptor_data *t, char *orig, char *subst) {
    char *pnew = perform_subst_pnew;

    char *first, *second, *strpos;

    first = subst + 1;
    if (!(second = strchr(first, '^'))) {
        SEND_TO_Q("Invalid substitution.\n\r", t);
        return 1;
    }

    *(second++) = '\0';

    if (!(strpos = strstr(orig, first))) {
        SEND_TO_Q("Invalid substitution.\n\r", t);
        return 1;
    }

    strncpy(pnew, orig, (strpos - orig));
    pnew[(strpos - orig)] = '\0';
    strcat(pnew, second);
    if (((strpos - orig) + strlen(first)) < strlen(orig))
        strcat(pnew, strpos + strlen(first));
    strcpy(subst, pnew);

    return 0;
}

void close_sockets(SocketType s) {
    log("Closing all sockets.");
    while (descriptor_list) {
        close_socket(descriptor_list);
    }

    close(s);
}

void close_socket(descriptor_data *conn_descriptor, int drop_all) {
    descriptor_data *tmp;
    char buf[100];

    // Alert Big Brother that the character is leaving.
    char_data *character = conn_descriptor->character;
    if (character && utils::is_pc(*character)) {
        game_rules::big_brother &bb_instance = game_rules::big_brother::instance();
        bb_instance.on_character_disconnected(character);
    }

    if (conn_descriptor->descriptor) {
        sprintf(buf, "Closing socket %d.", conn_descriptor->descriptor);
        mudlog(buf, NRM, LEVEL_IMPL, TRUE);

        close(conn_descriptor->descriptor);
        conn_descriptor->descriptor = 0;
        conn_descriptor->desc_num = -1;
    }

    flush_queues(conn_descriptor);
    if (conn_descriptor->descriptor == maxdesc) {
        --maxdesc;
    }

    /* Forget snooping */
    if (conn_descriptor->snoop.snooping) {
        conn_descriptor->snoop.snooping->desc->snoop.snoop_by = 0;
    }
    if (conn_descriptor->snoop.snoop_by) {
        send_to_char("Your victim is no longer among us.\n\r", conn_descriptor->snoop.snoop_by);
        conn_descriptor->snoop.snoop_by->desc->snoop.snooping = 0;
    }

    if (conn_descriptor->character) {
        if (conn_descriptor->connected == CON_PLYNG) {
            save_char(conn_descriptor->character, NOWHERE, 0);
            act("$n has lost $s link.", TRUE, conn_descriptor->character, 0, 0, TO_ROOM);
            sprintf(buf, "Closing link to: %s [%s].", GET_NAME(conn_descriptor->character),
                    conn_descriptor->host);
            mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(conn_descriptor->character)),
                   TRUE);
            //	 d->character->desc = 0;
            conn_descriptor->connected = CON_LINKLS;
        } else {
            if (conn_descriptor->character->player.name) {
                sprintf(buf, "Losing player: %s [%s].", GET_NAME(conn_descriptor->character),
                        conn_descriptor->host);
            } else {
                sprintf(buf, "Losing Unnamed player [%s].", conn_descriptor->host);
            }
            mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(conn_descriptor->character)),
                   TRUE);
            free_char(conn_descriptor->character);
            drop_all = 1;
        }
    } else {
        mudlog("Losing descriptor without char.", NRM, LEVEL_IMMORT, TRUE);
        drop_all = 1;
    }

    if ((conn_descriptor->connected != CON_LINKLS) || drop_all) {
        //    mudlog("Detaching descriptor.", NRM, LEVEL_IMPL, TRUE);
        if (next_to_process == conn_descriptor) /* to avoid crashing the process loop */
            next_to_process = next_to_process->next;
        if (conn_descriptor == descriptor_list) /* this is the head of the list */
            descriptor_list = descriptor_list->next;
        else { /* This is somewhere inside the list */
            /* Locate the previous element */
            for (tmp = descriptor_list; (tmp->next != conn_descriptor) && tmp; tmp = tmp->next)
                ;
            if (tmp) {
                tmp->next = conn_descriptor->next;
            }
        }
        RELEASE(conn_descriptor->showstr_head);
        RELEASE(conn_descriptor);
    }
}

void nonblock(SocketType s) {
    unsigned long flags = 0;
    flags = fcntl(s, F_GETFL, flags);
    flags |= O_NONBLOCK;
    if (fcntl(s, F_SETFL, flags) < 0) {
        perror("Fatal error executing nonblock (comm.c)");
        exit(1);
    }
}

/* ****************************************************************
 *	Public routines for system-to-player-communication	  *
 *******************************************************************/
void send_to_char(const char *message, char_data *character) {
    // Early out if we have no message or character.
    if (message == nullptr || message[0] == 0 || character == nullptr) {
        return;
    }

    // Early out if the character isn't connected.
    if (character->desc == nullptr || character->desc->connected != CON_PLYNG) {
        return;
    }

    SEND_TO_Q(message, character->desc);
}

void send_to_char(const char *message, int character_id) {
    if (message && message[0] != 0) {
        for (descriptor_data *connection = descriptor_list; connection;
             connection = connection->next) {
            char_data *character = connection->character;
            if (character && character->abs_number == character_id &&
                connection->connected == CON_PLYNG) {
                SEND_TO_Q(message, connection);
                break;
            }
        }
    }
}

const char *get_char_name(int character_id) {
    char_data *character = get_character(character_id);
    if (character) {
        return character->player.name;
    }
    return nullptr;
}

char_data *get_character(int character_id) {
    for (descriptor_data *connection = descriptor_list; connection; connection = connection->next) {
        char_data *character = connection->character;
        if (character && character->abs_number == character_id &&
            connection->connected == CON_PLYNG) {
            return character;
        }
    }

    return NULL;
}

void vsend_to_char(char_data *character, char *format, ...) {
#define BUFSIZE 2048
    char buf[BUFSIZE];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, BUFSIZE - 1, format, ap);
    buf[BUFSIZE - 1] = '\0';
    va_end(ap);

    send_to_char(buf, character);
}

void send_to_all(const char *message) {
    if (message) {
        for (descriptor_data *i = descriptor_list; i; i = i->next) {
            if (i->connected == CON_PLYNG) {
                SEND_TO_Q(message, i);
            }
        }
    }
}

void send_to_outdoor(const char *messg, int mode) {
    struct descriptor_data *i;

    if (messg)
        for (i = descriptor_list; i; i = i->next)
            if (!i->connected && (i->character->in_room != NOWHERE))
                if ((OUTSIDE(i->character) &&
                     ((mode != OUTDOORS_LIGHT) ||
                      !IS_SET(world[i->character->in_room].room_flags, DARK))) &&
                    (i->character->specials.position > POSITION_SLEEPING) &&
                    (!PLR_FLAGGED(i->character, PLR_WRITING)))
                    SEND_TO_Q(messg, i);
}

//  For weather messages - sends to outdoor sector
void send_to_sector(const char *messg, int sector_type) {
    struct descriptor_data *i;

    if (sector_type > 12 || sector_type < 0)
        return;
    if (messg)
        for (i = descriptor_list; i; i = i->next)
            if (!i->connected && (i->character->in_room != NOWHERE))
                if ((world[i->character->in_room].sector_type == sector_type) &&
                    (i->character->specials.position > POSITION_SLEEPING) &&
                    (!PLR_FLAGGED(i->character, PLR_WRITING)) && OUTSIDE(i->character))
                    SEND_TO_Q(messg, i);
}

void send_to_except(const char *messg, struct char_data *ch) {
    struct descriptor_data *i;

    if (messg)
        for (i = descriptor_list; i; i = i->next)
            if (ch->desc != i && !i->connected)
                SEND_TO_Q(messg, i);
}

void send_to_room(const char *messg, int room) {
    struct char_data *i;

    if (messg)
        for (i = world[room].people; i; i = i->next_in_room)
            if (i->desc)
                SEND_TO_Q(messg, i->desc);
}

void send_to_room_except(const char *messg, int room, struct char_data *ch) {
    struct char_data *i;

    if (messg)
        for (i = world[room].people; i; i = i->next_in_room)
            if (i != ch && i->desc)
                SEND_TO_Q(messg, i->desc);
}

void send_to_room_except_two(const char *messg, int room, struct char_data *ch1,
                             struct char_data *ch2) {
    struct char_data *i;

    if (messg)
        for (i = world[room].people; i; i = i->next_in_room)
            if (i != ch1 && i != ch2 && i->desc)
                SEND_TO_Q(messg, i->desc);
}

/*
 * The PERS function (still capitalized because it used to be
 * a macro) returns a colored string that is terminated.  Thus,
 * the terminating color of the string terminates any other
 * color on the line, and we set the clobbered_color flag.  It
 * turns out that enemy is the only color that we do not wish
 * to use to color as a full line--that is why PERS takes care
 * of its own coloring.
 *
 * That is, we want to have:
 *   <HIT>You force your Will against <ENMY>*an Uruk*<NORM><HIT>'s X!<NORM>
 * But we don't want to have:
 *   <NORM>You wield <OBJ>a shadowy blade<NORM><NORM>.<NORM>
 */
void convert_string(const char *str, int hide_invisible, struct char_data *ch, struct obj_data *obj,
                    void *vict_obj, struct char_data *to, const char *buf) {
    int clobbered_color;
    char *used_color;
    register char *strp, *point;
    register const char *i;

    i = NULL;
    used_color = NULL;
    clobbered_color = FALSE;

    for (strp = (char *)str, point = (char *)buf;;)
        if (*strp == '$') {
            switch (*(++strp)) {
            case 'C': /* This is a two-letter color code */
                switch (*(++strp)) {
                case 'N': /* Narrate */
                    i = CC_USE(to, COLOR_NARR);
                    break;
                case 'C': /* Chat */
                    i = CC_USE(to, COLOR_CHAT);
                    break;
                case 'Y': /* Yell */
                    i = CC_USE(to, COLOR_YELL);
                    break;
                case 'T': /* Tell */
                    i = CC_USE(to, COLOR_TELL);
                    break;
                case 'S': /* Say */
                    i = CC_USE(to, COLOR_SAY);
                    break;
                case 'R': /* Room name */
                    i = CC_USE(to, COLOR_ROOM);
                    break;
                case 'H': /* Hit */
                    i = CC_USE(to, COLOR_HIT);
                    break;
                case 'D': /* Damage */
                    i = CC_USE(to, COLOR_DAMG);
                    break;
                case 'K': /* Character */
                    i = CC_USE(to, COLOR_CHAR);
                    break;
                case 'O': /* Object */
                    i = CC_USE(to, COLOR_OBJ);
                    break;
                case 'E': /* Description */
                    i = CC_USE(to, COLOR_DESC);
                    break;
                case 'G': /* Group Tell */
                    i = CC_USE(to, COLOR_GTELL);
                    break;
                default:
                    vmudlog(NRM, "ERROR: Unrecognized color code '%c'.", *strp);
                }
                used_color = (char *)i;
                break;
            case 'K': /* PERS, but force_visible */
                i = PERS((struct char_data *)vict_obj, to, FALSE, TRUE);
                break;
            case 'n': /* See note at top of function on PERS and color */
                i = PERS(ch, to, FALSE, FALSE);
                clobbered_color = TRUE;
                break;
            case 'N': /* See note at top of function on PERS and color */
                i = PERS((struct char_data *)vict_obj, to, FALSE, FALSE);
                clobbered_color = TRUE;
                break;
            case 'm':
                i = HMHR(ch);
                break;
            case 'M':
                i = HMHR((struct char_data *)vict_obj);
                break;
            case 's':
                i = HSHR(ch);
                break;
            case 'S':
                i = HSHR((struct char_data *)vict_obj);
                break;
            case 'e':
                i = HSSH(ch);
                break;
            case 'E':
                i = HSSH((struct char_data *)vict_obj);
                break;
            case 'o':
                i = OBJN(obj, to);
                break;
            case 'O':
                i = OBJN((struct obj_data *)vict_obj, to);
                break;
            case 'p':
                i = OBJS(obj, to);
                break;
            case 'P':
                i = OBJS((struct obj_data *)vict_obj, to);
                break;
            case 'a':
                i = SANA(obj);
                break;
            case 'A':
                i = SANA((struct obj_data *)vict_obj);
                break;
            case 'T':
                i = (char *)vict_obj;
                break;
            case 'F':
                i = fname((char *)vict_obj);
                break;
            case 'b':
                i = GET_CURRPART(ch);
                break;
            case 'B':
                i = GET_CURRPART((struct char_data *)vict_obj);
                break;
            case '$':
                i = "$";
                break;
            default:
                log("SYSERR: Illegal $-code to act():");
                strcpy(buf1, "SYSERR: ");
                strcat(buf1, str);
                log(buf1);
                break;
            }
            while ((*point = *(i++)))
                ++point;
            ++strp;

            if (clobbered_color && used_color != NULL) {
                i = used_color;
                while ((*point = *(i++)))
                    ++point;
                clobbered_color = 0;
            }
        } else if (!(*(point++) = *(strp++)))
            break;

    *(--point) = '\n';
    *(++point) = '\r';
    *(++point) = '\0';

    if (used_color)
        sprintf(point, CC_NORM(to));

    /* Find the first character in the string, ignoring ANSI colors */
    for (strp = (char *)buf; *strp == '\x1B'; ++strp)
        while (*strp != 'm')
            ++strp;

    if (isalpha(*strp))
        CAP(strp);
}

char act_buffer[MAX_STRING_LENGTH];
void act(const char *str, int hide_invisible, struct char_data *ch, struct obj_data *obj,
         void *vict_obj, int type, char spam_only) {
    struct char_data *to;
    char *buf = act_buffer;

    if (!str)
        return;
    if (!*str)
        return;

    to = 0;

    if (type == TO_VICT)
        to = (struct char_data *)vict_obj;
    else {
        if (type == TO_CHAR)
            to = ch;
        else if (ch && ch->in_room != NOWHERE)
            to = world[ch->in_room].people;
        else if (obj && obj->in_room != NOWHERE)
            to = world[obj->in_room].people;
    }

    if (!to)
        return;
    //   printf("act(%s) called, to=%p\n",str, to);
    for (; to; to = to->next_in_room) {
        if (to->desc && (to != ch || type == TO_CHAR) && (CAN_SEE(to, ch) || !hide_invisible) &&
            (AWAKE(to) || type == TO_VICT) && !PLR_FLAGGED(to, PLR_WRITING) &&
            !(type == TO_NOTVICT && to == (struct char_data *)vict_obj) &&
            (!spam_only || PRF_FLAGGED(to, PRF_SPAM))) {
            convert_string(str, hide_invisible, ch, obj, vict_obj, to, buf);
            if (*buf != '\0')
                SEND_TO_Q(buf, to->desc);
        }
        if ((type == TO_VICT) || (type == TO_CHAR))
            return;
    }
}

void complete_delay(struct char_data *ch) {
    SPECIAL(*tmpfunc);

    ch->delay.wait_value = 0;
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITWHEEL);
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITING);

    if (ch->delay.cmd == CMD_SCRIPT) {
        continue_char_script(ch);
        return;
    }

    if (ch->delay.cmd == -1 && IS_NPC(ch)) {
        /* Here calls special procedure */
        if (mob_index[ch->nr].func)
            (*mob_index[ch->nr].func)(ch, 0, -1, "", SPECIAL_NONE, &(ch->delay));
        else if (ch->specials.store_prog_number) {
            tmpfunc = (SPECIAL(*))virt_program_number(ch->specials.store_prog_number);
            tmpfunc(ch, 0, ch->delay.cmd, "", SPECIAL_DELAY, &(ch->delay));
        } else if (ch->specials.union1.prog_number)
            intelligent(ch, 0, -1, "", SPECIAL_DELAY, &(ch->delay));
    } else if (ch->delay.cmd > 0)
        command_interpreter(ch, "", &(ch->delay));
}

int in_waiting_list(char_data *ch) {
    if (waiting_list == NULL || ch == NULL)
        return 0;

    for (char_data *iter = waiting_list; iter; iter = iter->delay.next)
        if (iter == ch)
            return 1;

    return 0;
}

void abort_delay(char_data *wait_ch) {
    char_data *wait_tmp2;
    REMOVE_BIT(wait_ch->specials.affected_by, AFF_WAITWHEEL);
    REMOVE_BIT(wait_ch->specials.affected_by, AFF_WAITING);

    if (wait_ch == waiting_list) {
        waiting_list = wait_ch->delay.next;
    } else {
        for (wait_tmp2 = waiting_list; wait_tmp2; wait_tmp2 = wait_tmp2->delay.next)
            if (wait_tmp2->delay.next == wait_ch)
                break;
        if (wait_tmp2)
            wait_tmp2->delay.next = wait_ch->delay.next;
    }
    wait_ch->delay.next = 0;
    wait_ch->delay.wait_value = 0;
    wait_ch->delay.priority = 0;

    if (in_waiting_list(wait_ch)) {
        log("SYSERR: abort_delay, character remaining in waiting_list");
        abort_delay(wait_ch);
    }
}

void stat_update() {
    stat_ticks_passed++;

    for (descriptor_data *point = descriptor_list; point; point = point->next) {
        if (point->character && (STATE(point) == CON_PLYNG)) {
            if (GET_LEVEL(point->character) < LEVEL_IMMORT) {
                stat_mortals_counter++;
                if (GET_RACE(point->character) < 10 && GET_RACE(point->character) != 0) {
                    stat_whitie_counter++;
                    if (GET_LEVEL(point->character) >= 30) {
                        stat_whitie_legend_counter++;
                    }
                }
                if (GET_RACE(point->character) >= 10) {
                    stat_darkie_counter++;
                    if (GET_LEVEL(point->character) >= 30) {
                        stat_darkie_legend_counter++;
                    }
                }

            } else {
                stat_immortals_counter++;
            }
        }
    }
}
