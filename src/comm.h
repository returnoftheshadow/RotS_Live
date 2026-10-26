/* ************************************************************************
 *   File: comm.h                                        Part of CircleMUD *
 *  Usage: header file: prototypes of public communication functions       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef COMM_H
#define COMM_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stdarg.h>

#include "utils.h" /* For the TRUE macro */

struct StartupOptions {
    sh_int port;
    std::string dir;
    bool mini_mud;
    bool new_mud;
    bool no_rent_check;
    bool restrict_game;
    bool no_specials;
    bool has_proxy;
    // The seed given with --random-seed; empty when the server draws its own at boot.
    std::optional<unsigned int> random_seed;
};

/* comm.c */
void send_to_all(const char* messg);
void send_to_char(const char* messg, const struct char_data* ch);
void send_to_char(const char* message, int character_id);
const char* get_char_name(int character_id);
struct char_data* get_character(int character_id);
void send_to_except(const char* messg, const struct char_data* ch);
void send_to_room(const char* messg, int room);
void send_to_room_except(const char* messg, int room, const struct char_data* ch);
void send_to_room_except_two(const char* messg, int room, const struct char_data* ch1, const struct char_data* ch2);
void send_to_outdoor(const char* messg, int mode);
void send_to_sector(const char* messg, int sector_type);
void perform_to_all(char* messg, struct char_data* ch);
void close_socket(struct descriptor_data* d, int drop_all = TRUE);
void check_state_deadlines(time_t now);
void break_spell(struct char_data* ch);
void abort_delay(char_data* wait_ch);
/* Ends <ch>'s delay and carries out what it was waiting for: the delayed command, or the rest of
 * the script DO_WAIT paused. A forced completion (delay.subcmd set to -1 first, as WAIT_STATE and
 * raw_kill do when they replace or end a delay) runs the command's interrupted branch, and drops
 * the rest of a paused script without running it. */
void complete_delay(struct char_data* ch);
/* Runs one pulse of the waiting list: every waiting character's delay goes down by one, and each
 * that runs out is completed (a paused script resumes, a delayed command runs). A character
 * freed while its delay completes is not touched again. */
void process_waiting_list();
std::string mob_age_message(const struct char_data* victim);
struct txt_block* get_from_txt_block_pool(const char* line = 0);
void put_to_txt_block_pool(struct txt_block*);

void vsend_to_char(const struct char_data* ch, const char* format, ...);

void act(const char* str, int hide_invisible, struct char_data* ch,
    const struct obj_data* obj, void* vict_obj, int type, char spam_only = FALSE);

inline constexpr int TO_ROOM = 0;
inline constexpr int TO_VICT = 1;
inline constexpr int TO_NOTVICT = 2;
inline constexpr int TO_CHAR = 3;

int write_to_descriptor(int desc, const char* txt);
void write_to_q(char* txt, struct txt_q* queue);
void write_to_output(const char* txt, struct descriptor_data* d);
int output_space_left(const struct descriptor_data* d);
void output_mark_overflow(struct descriptor_data* d);
int wrap_added_length(const char* text);
void page_string(struct descriptor_data* d, char* str, int keep_internal);
bool parse_startup_options(int argc, char** argv, StartupOptions* options, std::string* error_message);

// A function that returns a fresh random seed each time it is called.
using RandomSeedSource = unsigned int();

// Seeds the random numbers behind number() and dice(): with requested_seed when it is set,
// otherwise with a seed from draw_fresh_seed. Logs the seed used, so the run's random numbers
// can be repeated by passing it to --random-seed.
void seed_random_numbers(
    std::optional<unsigned int> requested_seed, RandomSeedSource& draw_fresh_seed);

// Returns a seed taken from the system clock, which differs from one boot to the next.
unsigned int draw_clock_seed();

// Reads a random seed: decimal digits only, from 0 to the largest unsigned int. On failure it
// leaves out_seed unchanged and, when out_error_message is not null, says why.
bool parse_random_seed_value(std::string_view text, unsigned int& out_seed, std::string* out_error_message);

// Prints a backtrace of up to ten stack frames to stderr and exits with status 1; installed for
// SIGSEGV.
void sigsegv_handler(int sig);

class GameTickSchedule;

// Opens the listening socket on port, boots the world, runs the game loop until shutdown and
// closes the sockets; exits with status 52 instead of returning when a reboot was requested.
// schedule decides each pass's world updates and is kept alive until the loop ends; with a
// null schedule no world update runs, and the loss is logged once.
void run_the_game(sh_int port, std::shared_ptr<GameTickSchedule> schedule);

// 1 when connections arrive through the proxy server, set from the -x option at start-up.
extern int has_proxy;

// 1 when shops are not loaded and special routines are neither assigned nor run, set from the
// -s option at start-up.
extern int no_specials;

// The command log, last_cmds in the data directory; opened by main() before the game runs.
extern FILE* fpCommand;

/* #define SEND_TO_Q(messg, desc)  write_to_q((messg), &(desc)->output) */
#define SEND_TO_Q(messg, desc) write_to_output((messg), desc)

#define USING_SMALL(d) ((d)->output == (d)->small_outbuf)
#define USING_LARGE(d) (!USING_SMALL(d))

// Implemented in spec_ass.cc.
typedef int (*special_func_ptr)(char_data* host, char_data* character, int cmd, char* argument, int call_flag, waiting_type* wait_list);
void* virt_program_number(int number);

special_func_ptr get_special_function(int number);

#endif /* COMM_H */
