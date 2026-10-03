/* ************************************************************************
 *   File: boards.h                                      Part of CircleMUD *
 *  Usage: header file for bulletin boards                                 *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef BOARDS_H
#define BOARDS_H

#include "platdef.h" /* For byte typedefs */
#include <stdio.h> /* FILE, for write_board_message_html */

#define NUM_OF_BOARDS 24
// #define NUM_OF_BOARDS      (board_info_type::num_of_boards)
#define MAX_BOARD_MESSAGES 100 /* arbitrary */
#define MAX_BIG_BOARD_MESSAGES 500 /* arbitrary */
#define MAX_MAIL_MESSAGES 1000 /* arbitrary */
#define MAX_MESSAGE_LENGTH 4096 /* arbitrary, obsolete  */

#define BOARD_DIR "boards"
#define BOARD_HTML_DIR "boards"

#define INDEX_SIZE (((NUM_OF_BOARDS - 2) * MAX_BOARD_MESSAGES) + (2 * MAX_BIG_BOARD_MESSAGES) + MAX_MAIL_MESSAGES + 5)

struct board_msginfo {
    int slot_num; /* pos of message in "master index" */
    int msg_num; /* "absolute" number of the post */
    char* heading; /* pointer to message's heading */
    int level; /* level of poster */
    int post_time; /* when it was posted */
    int heading_len; /* size of header (for file write) */
    int message_len; /* size of message text (for file write) */
};

struct board_info_type {
    static byte num_of_boards;
    byte lnum; /* local number, should be 0 ... N */
    long vnum; /* vnum of this board */
    int read_lvl; /* min level to read messages on this board */
    int write_lvl; /* min level to write messages on this board */
    int remove_lvl; /* min level to remove messages from this board */
    char short_name[50]; /*filename without directories,used for html, too*/
    char filename[50]; /* file to save this board to */
    char title[50]; /* used in html only */
    int rnum; /* rnum of this board */

    int last_message; /* max number of the message written so far */
    int num_of_msgs;
    int max_of_msgs;
    byte tmp_allflag;
    byte is_changed;

    struct board_msginfo* msg_index;

    virtual void write_message(struct char_data* ch, char* arg, int num = 0);
    virtual int show_board(struct char_data* ch, char* arg, int allflag);
    virtual int select_msg(int msg, int softflag = 0); // 0 - hard search, !0-soft
    virtual int display_msg(struct char_data* ch, char* arg, int nextflag);
    int remove_msg(struct char_data* ch, char* arg);
    int count_msg(char_data* ch, int cur_num);
    void flush_board();
    void save_board();
    void load_board();
    void reset_board();
    virtual int msg_msgnum(int i) { return msg_index[i].msg_num; }
    virtual void msg_msgnum(int i, int j) { msg_index[i].msg_num = j; }
    virtual int approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num);
    // see mail_info_type for explanation

    board_info_type(int objnum, int l_read, int l_write, int l_rem,
        int max_msg, char* file, char* titlename);
    board_info_type();
};

struct mail_info_type : board_info_type {
    int number;
    virtual void write_message(struct char_data* ch, char* arg, int num = 0);
    virtual int approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num);
    // return 1 if msg was approved for list/read/etc
    // num is taken as the shown number of the previous message,
    // returned as the number to show with msg.
    mail_info_type(int objnum, int l_read, int l_write, int l_rem,
        int max_msg, char* file, char* titlename);
};
#define VNUM (vnum)
#define READ_LVL (read_lvl)
#define WRITE_LVL (write_lvl)
#define REMOVE_LVL (remove_lvl)
#define FILENAME (filename)
#define RNUM (rnum)

#define G_VNUM(i) ((i)->vnum)
#define G_READ_LVL(i) ((i)->read_lvl)
#define G_WRITE_LVL(i) ((i)->write_lvl)
#define G_REMOVE_LVL(i) ((i)->remove_lvl)
#define G_FILENAME(i) ((i)->filename)
#define G_RNUM(i) ((i)->rnum)

#define NEW_MSG_INDEX (msg_index[num_of_msgs])
#define MSG_HEADING(j) (msg_index[j].heading)
#define MSG_SLOTNUM(j) (msg_index[j].slot_num)
#define MSG_POSTTIME(j) (msg_index[j].post_time)
#define MSG_LEVEL(j) (msg_index[j].level)
#define MSG_CURMSG(ch) (ch->specials.board_point[lnum])

/* One message body for the web export: '\n' becomes "<br>", '\r' is dropped. */
void write_board_message_html(FILE* ht_fl, const char* text, int len);

#endif /* BOARDS_H */
