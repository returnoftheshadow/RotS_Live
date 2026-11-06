/* ************************************************************************
 *   File: mail.h                                        Part of CircleMUD *
 *  Usage: header file for mail system                                     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef MAIL_H
#define MAIL_H

#include <cstddef>

/******* MUD MAIL SYSTEM HEADER FILE ****************************
 ***     written by Jeremy Elson (jelson@server.cs.jhu.edu)   ***
 ****   compliments of CircleMUD (circle.cs.jhu.edu 4000)    ****
 ***************************************************************/

/* INSTALLATION INSTRUCTIONS in MAIL.C */

/* You can modify the following constants to fit your own MUD.  */

/* command numbers of the "mail", "check", and "receive" commands
   in your interpreter. */

inline constexpr int CMD_CHECK = 153;
inline constexpr int CMD_RECEIVE = 154;
inline constexpr int CMD_MAIL = 155;

/* minimum level a player must be to send mail	*/
inline constexpr int MIN_MAIL_LEVEL = 10;

/* # of gold coins required to send mail	*/
inline constexpr int STAMP_PRICE = 200;

/* Maximum size of mail in bytes (arbitrary)	*/
inline constexpr int MAX_MAIL_SIZE = 4000;

/* Max size of player names			*/
inline constexpr int NAME_SIZE = 15;

/* size of mail file allocation blocks		*/
inline constexpr int BLOCK_SIZE = 100;

/* NOTE:  Make sure that your block size is big enough -- if not,
   HEADER_BLOCK_DATASIZE will end up negative.  This is a bad thing.
   Check the define below to make sure it is >0 when choosing values
   for NAME_SIZE and BLOCK_SIZE.  100 is a nice round number for
   BLOCK_SIZE and is the default ... why bother trying to change it
   anyway?

   The mail system will always allocate disk space in chunks of size
   BLOCK_SIZE.
*/

/* USER CHANGABLE DEFINES ABOVE **
***************************************************************************
**   DON'T TOUCH DEFINES BELOW  */

int scan_file(void);
int has_mail(const char* recipient);
void store_mail(char* to, const char* from, char* message_pointer);
char* read_delete(char* recipient, char* recipient_formatted);

inline constexpr std::size_t INT_SIZE = sizeof(int);
inline constexpr std::size_t CHAR_SIZE = sizeof(char);
inline constexpr std::size_t LONG_SIZE = sizeof(long);

inline constexpr std::size_t HEADER_BLOCK_DATASIZE =
    (BLOCK_SIZE - 1 - ((CHAR_SIZE * (NAME_SIZE + 1) * 2) + (3 * LONG_SIZE)));
/* size of the data part of a header block */

inline constexpr std::size_t DATA_BLOCK_DATASIZE = (BLOCK_SIZE - LONG_SIZE - 1);
/* size of the data part of a data block */

/* note that an extra space is allowed in all string fields for the
   terminating null character.  */

inline constexpr int HEADER_BLOCK = -1;
inline constexpr int LAST_BLOCK = -2;
inline constexpr int DELETED_BLOCK = -3;

/* note: next_block is part of header_blk in a data block; we can't combine
   them here because we have to be able to differentiate a data block from a
   header block when booting mail system.
*/

struct header_block_type_d {
    long block_type; /* is this a header block or data block? */
    long next_block; /* if header block, link to next block   */
    char from[NAME_SIZE + 1]; /* who is this letter from?		 */
    char to[NAME_SIZE + 1]; /* who is this letter to?		 */
    long mail_time; /* when was the letter mailed?		 */
    char txt[HEADER_BLOCK_DATASIZE + 1]; /* the actual text	*/
};

struct data_block_type_d {
    long block_type; /* -1 if header block, -2 if last data block
                                   in mail, otherwise a link to the next */
    char txt[DATA_BLOCK_DATASIZE + 1]; /* the actual text		 */
};

typedef struct header_block_type_d header_block_type;
typedef struct data_block_type_d data_block_type;

struct position_list_type_d {
    long position;
    struct position_list_type_d* next;
};

typedef struct position_list_type_d position_list_type;

struct mail_index_type_d {
    char recipient[NAME_SIZE + 1]; /* who the mail is for */
    position_list_type* list_start; /* list of mail positions    */
    struct mail_index_type_d* next;
};

typedef struct mail_index_type_d mail_index_type;

#endif /* MAIL_H */
