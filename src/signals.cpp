/* ************************************************************************
 *   File: signals.c                                     Part of CircleMUD *
 *  Usage: Signal trapping and signal handlers                             *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "structs.h"
#include "utils.h"

extern struct descriptor_data* descriptor_list;
extern SocketType mother_desc;

void checkpointing(int);
void logsig(int);
void diesig(int);
void hupsig(int);
void badcrash(int);
void unrestrict_game(int);
void reread_wizlists(int);
void Emergency_save(void);
void bank_vaults_write_behind(); /* mob_progs/banker.cpp */

int graceful_tried = 0;

void signal_setup(void)
{
    // struct itimerval itime;
    // struct timeval interval;

    //  return;
    signal(SIGUSR1, reread_wizlists);
    signal(SIGUSR2, unrestrict_game);

    /* just to be on the safe side: */

    signal(SIGHUP, hupsig);
    signal(SIGILL, diesig);
    //   signal(SIGSEGV, diesig); Also should be included once the bugs are found :-)
    signal(SIGFPE, diesig);
    /*   signal(SIGTRAP, diesig); */
    signal(SIGFPE, diesig);
    signal(SIGBUS, diesig);
    /*   signal(SIGIOT, diesig);  */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, hupsig);
    signal(SIGALRM, logsig);
    signal(SIGTERM, hupsig);
    //   signal(SIGSEGV, badcrash);
    //   signal(SIGBUS, badcrash);   This line and above commented out 11 Jan 00.  Put back in one day :-)

    /* set up the deadlock-protection */

    //   interval.tv_sec = 900;    /* 15 minutes */
    //   interval.tv_usec = 0;
    // itime.it_interval = interval;
    // itime.it_value = interval;
    // setitimer(ITIMER_VIRTUAL, &itime, 0);
    signal(SIGVTALRM, checkpointing);
}

void checkpointing(int fake)
{
    extern int tics;

    if (!tics) {
        log("CHECKPOINT shutdown: tics not updated");
        abort();
    } else
        tics = 0;
}

void reread_wizlists(int fake)
{
    void reboot_wizlists(void);

    signal(SIGUSR1, reread_wizlists);
    mudlog("Rereading wizlists.", CMP, LEVEL_IMMORT, FALSE);
    reboot_wizlists();
}

void unrestrict_game(int fake)
{
    extern int restrict;
    extern int num_invalid;

    signal(SIGUSR2, unrestrict_game);
    mudlog("Received SIGUSR2 - unrestricting game (emergent)",
        BRF, LEVEL_IMMORT, TRUE);
    int ban_list = 0;
    restrict = 0;
    num_invalid = 0;
}

/* kick out players etc */

void close_sockets(SocketType s);

void hupsig(int fake)
{
    extern SocketType mother_desc;
    log("Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...");
    Emergency_save();
    bank_vaults_write_behind(); /* a service stop ends here, not in game_loop */
    close_sockets(mother_desc);
    exit(0); /* something more elegant should perhaps be substituted */
}

void badcrash(int fake)
{
    void close_socket(struct descriptor_data * d);
    struct descriptor_data* desc;

    log("SIGSEGV or SIGBUS received.  Trying to shut down gracefully.");
    Emergency_save();
    if (!graceful_tried) {
        close(mother_desc);
        log("Trying to close all sockets.");
        graceful_tried = 1;
        for (desc = descriptor_list; desc; desc = desc->next)
            close(desc->descriptor);
    }
    abort();
}

void logsig(int fake)
{
    log("Signal received.  Ignoring.");
}
void diesig(int fake)
{
    // Try to save everyone.
    Emergency_save();
    exit(0);
}
