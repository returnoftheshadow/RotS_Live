/* game_heartbeat.cpp */

#include "game_heartbeat.h"

#include "platdef.h"

#include "comm.h"
#include "damage_meters.h"
#include "handler.h"
#include "limits.h"
#include "skill_timer.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <ctime>
#include <stdio.h>

// Defined in translation units that publish no header declaration for them.
void affect_update(void);
void check_pre_login_idle();
void clean_expose_elements();
void fast_update(void);
void mobile_activity(void);
void msdp_update();
void perform_violence();
void stat_update();
void weather_and_time(int mode);
extern int autosave_time;
extern struct descriptor_data* descriptor_list;

//============================================================================
GameHeartbeat::GameHeartbeat(const std::shared_ptr<GameTickSchedule>& schedule)
    : tick_schedule(schedule)
{
    if (schedule == nullptr) {
        report_schedule_loss();
    }
}

//============================================================================
void GameHeartbeat::report_schedule_loss()
{
    if (schedule_loss_logged) {
        return;
    }

    schedule_loss_logged = true;
    log("SYSERR: GameHeartbeat: no tick schedule; world updates are stopped.");
}

//============================================================================
void GameHeartbeat::run_pass()
{
    struct descriptor_data *point, *next_point;
    int sockets_connected, sockets_playing;
    int was_updated;
    char buf[100];

    /* handle heartbeat stuff */
    /* Note: pulse now changes every 1/4 sec  */

    pulse++;
    GameTickSchedule::WorldPass world_pass;
    const std::shared_ptr<GameTickSchedule> live_schedule = tick_schedule.lock();
    if (live_schedule) {
        world_pass = live_schedule->next_pass();
        set_current_time_phase(world_pass.time_phase);
    } else {
        report_schedule_loss();
    }
    was_updated = 0;

    if (world_pass.run_zone_update) {
        zone_update();
    }
    if (world_pass.run_mobile_activity) {
        mobile_activity();
        was_updated = 1;
    }
    const float combat_seconds = combat_timer.get_elapsed_seconds();
    // The meters run before combat, so a character killed during this pass is still
    // credited with it.
    tick_damage_meters(combat_seconds);
    if (world_pass.run_violence) {
        perform_violence();
    }

    if (world_pass.run_hourly_update) {
        weather_and_time(1);
        point_update(); // putting affect_total call in point_update.
        stat_update();
        was_updated = 1;
    }
    if (world_pass.run_fast_update /*&& !was_updated*/) {
        // now increasing hp/mp/mana/spirit fast in fast_update..
        fast_update();
        affect_update();

        // clean-up expose elements
        clean_expose_elements();
    }

    msdp_update();

    if (!(pulse % (60 * 4))) /* one minute */
    {
        check_pre_login_idle();
    }

    // Periodic point-in-time crash-save snapshot cadence, driven by the configurable seconds
    // interval (autosave_time) through the unit-tested scheduler. Default 30s == 120 pulses (the
    // source's original cadence). Crash_save_all now saves EVERY connected player each cadence
    // (a consistent point-in-time snapshot), not only inventory-dirty ones.
    if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) {
        Crash_save_all();
    }

    if (world_pass.run_skill_timer) {
        game_timer::skill_timer& st_instance = game_timer::skill_timer::instance();
        st_instance.update_skill_timer();
    }

    if (!(pulse % 4)) {
        check_state_deadlines(time(0));
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
}
