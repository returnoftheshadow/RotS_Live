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

#include <string>
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
};

/* comm.c */
void send_to_all(const char* messg);
void send_to_char(const char* messg, struct char_data* ch);
void send_to_char(const char* message, int character_id);
const char* get_char_name(int character_id);
struct char_data* get_character(int character_id);
void send_to_except(const char* messg, struct char_data* ch);
void send_to_room(const char* messg, int room);
void send_to_room_except(const char* messg, int room, struct char_data* ch);
void send_to_room_except_two(const char* messg, int room, struct char_data* ch1, struct char_data* ch2);
void send_to_outdoor(const char* messg, int mode);
void send_to_sector(const char* messg, int sector_type);
void perform_to_all(char* messg, struct char_data* ch);
void close_socket(struct descriptor_data* d, int drop_all = TRUE);
void break_spell(struct char_data* ch);
void abort_delay(char_data* wait_ch);
void complete_delay(struct char_data* ch);
struct txt_block* get_from_txt_block_pool(char* line = 0);
void put_to_txt_block_pool(struct txt_block*);

void vsend_to_char(struct char_data* ch, char* format, ...);

void act(const char* str, int hide_invisible, struct char_data* ch,
    struct obj_data* obj, void* vict_obj, int type, char spam_only = FALSE);

#define TO_ROOM 0
#define TO_VICT 1
#define TO_NOTVICT 2
#define TO_CHAR 3

int write_to_descriptor(int desc, char* txt);
void write_to_q(char* txt, struct txt_q* queue);
void write_to_output(const char* txt, struct descriptor_data* d);
void page_string(struct descriptor_data* d, char* str, int keep_internal);
bool parse_startup_options(int argc, char** argv, StartupOptions* options, std::string* error_message);

/* #define SEND_TO_Q(messg, desc)  write_to_q((messg), &(desc)->output) */
#define SEND_TO_Q(messg, desc) write_to_output((messg), desc)

#define USING_SMALL(d) ((d)->output == (d)->small_outbuf)
#define USING_LARGE(d) (!USING_SMALL(d))

// Implemented in spec_ass.cc.
typedef int (*special_func_ptr)(char_data* host, char_data* character, int cmd, char* argument, int call_flag, waiting_type* wait_list);
void* virt_program_number(int number);

special_func_ptr get_special_function(int number);

#endif /* COMM_H */
