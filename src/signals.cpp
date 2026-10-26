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

#include "signal_requests.h"
#include "signals.h"
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
void Crash_save_all(void);
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

// A signal can interrupt the game loop anywhere, even halfway through closing a connection, so
// these handlers only record the request; the game loop performs it through game_signal_steps().

void reread_wizlists(int fake)
{
    signal(SIGUSR1, reread_wizlists);
    record_wizlist_reread_request();
}

void unrestrict_game(int fake)
{
    signal(SIGUSR2, unrestrict_game);
    record_unrestrict_request();
}

void hupsig(int signal_number)
{
    record_stop_request(signal_number);
}

void logsig(int fake)
{
    record_ignored_signal();
}

/* kick out players etc */

void reboot_wizlists(void);
extern int restrict;
extern int num_invalid;

namespace {

// The work the recorded signals ask for, run by the game loop between passes.
class GameSignalSteps : public SignalRequestSteps {
public:
    void stop(int signal_number) override
    {
        log("Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...");
        // The autosave's snapshot: every playing character and their gear at one moment, so a
        // file written by save, rent or a level since the last autosave is not left out of step.
        // Connections are not closed one by one: closing a playing connection saves the character
        // again without their gear; exit closes the connections without saving.
        Crash_save_all();
        bank_vaults_write_behind(); /* a service stop skips game_loop's shutdown saving */
        close(mother_desc);
        exit(0);
    }

    void reread_wizlists() override
    {
        mudlog("Rereading wizlists.", CMP, LEVEL_IMMORT, FALSE);
        reboot_wizlists();
    }

    void unrestrict_game() override
    {
        mudlog("Received SIGUSR2 - unrestricting game (emergent)",
            BRF, LEVEL_IMMORT, TRUE);
        restrict = 0;
        num_invalid = 0;
    }

    void log_ignored_signal() override
    {
        log("Signal received.  Ignoring.");
    }
};

// The one set of steps the game loop uses.
GameSignalSteps game_steps;

} // namespace

SignalRequestSteps& game_signal_steps()
{
    return game_steps;
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

void diesig(int fake)
{
    // Try to save everyone.
    Emergency_save();
    exit(0);
}
