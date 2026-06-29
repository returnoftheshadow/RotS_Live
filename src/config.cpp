/* ************************************************************************
*   File: config.c                                      Part of CircleMUD *

*  Usage: Configuration of various aspects of CircleMUD operation         *

*                                                                         *

*  All rights reserved.  See license.doc for complete information.        *

*                                                                         *

*  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *

*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *

************************************************************************ */

#include <stdio.h>

#include "platdef.h"
#include "structs.h"

#define TRUE 1
#define YES 1
#define FALSE 0
#define NO 0

/* GAME PLAY OPTIONS */
/* number of tics (usually 75 seconds) before PC/NPC corpses decompose */
int max_npc_corpse_time = 10;
int max_pc_corpse_time = 45;
int LOOT_DECAY_TIME = 5;
int average_mob_life = 40;

sh_int screen_width = 79; /* for line wrapping, if turned on */

/* RENT/CRASHSAVE OPTIONS */
/* How often (in SECONDS) should the MUD crash-save connected players? Driven through
   autosave_interval_pulses(); clamped to a 15s floor. Default 240s == the historical
   4-minute cadence. */
int autosave_time = 240;

/* ROOM NUMBERS */

/* virtual number of room that retired players should enter at */
int retirement_home_room = 1151;

/* virtual number of room that immorts should enter at by default */
int immort_start_room = 1101;

/* virtual number of room that frozen players should enter at */
int frozen_start_room = 1110;

const char* NOEFFECT = "Nothing seems to happen.\r\n";

/* GAME OPERATION OPTIONS */

/* default directory to use as data directory */
char* DFLT_DIR = "lib";

/* Some nameservers (such as the one here at JHU) are slow and cause the
   game to lag terribly every time someone logs in.  The lag is caused by
   the gethostbyaddr() function -- the function which resolves a numeric
   IP address (such as 128.220.13.30) into an alphabetic name (such as
   circle.cs.jhu.edu).

   The nameserver at JHU can get so bad at times that the incredible lag
   caused by gethostbyaddr() isn't worth the luxury of having names
   instead of numbers for players' sitenames.

   If your nameserver is fast, set the variable below to NO.  If your
   nameserver is slow, of it you would simply prefer to have numbers
   instead of names for some other reason, set the variable to YES.

   You can experiment with the setting of nameserver_is_slow on-line using
   the SLOWNS command from within the MUD.
*/

int nameserver_is_slow = YES;

char* MENU = "\n\r"
             "Welcome to Arda!\n\r"
             "0) Exit from the MUD.\n\r"
             "1) Enter the game.\n\r"
             "3) Read the background story.\n\r"
             "4) Change password.\n\r"
             "5) Delete this character.\n\r"
             "6) View class powers.\n\r"
             "\n\r"
             "   Make your choice: ";

char* GREETINGS = "\n\r"
                  "\n\r"
                  "                               Welcome to\n\r"
                  "                          RETURN OF THE SHADOW\n\r"
                  "                        or  ARDA: The Fourth Age\n\r"
                  "\n\r"
                  "                         Based on CircleMud \n\r"
                  "                       Original concept and code by:\n\r"
                  "                  Hans Henrik Staerfeldt, Katja Nyboe,\n\r"
                  "           Tom Madsen, Michael Seifert, and Sebastian Hammer\n\r"
                  "       Currently maintained by Maga, Lowtheim, Drelidan, and others\n\r\n\r"
                  "                       Webpage: http://www.rotsmud.org\n\r"
                  "          Log in with your account email address at the prompt.\n\r"
                  "\n\r";

char* WELC_MESSG = "\n\r"
                   "Here we go..."
                   "\n\r";

char* START_MESSG = "Welcome.  This is your new MUD character.\n\r"
                    "Don't forget to role play!\n\r";

/* AUTOWIZ OPTIONS */

/* If yes, what is the lowest level which should be on the wizlist?  (All
   immort levels below the level you specify will go on the immlist instead. */
int min_wizlist_lev = LEVEL_GOD;

int mobile_master_idnum = 51566; // Raziel
int object_master_idnum = 1293; // Erika
int object_master2_idnum = 35795; // Incanus
