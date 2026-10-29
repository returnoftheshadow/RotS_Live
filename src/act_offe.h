#ifndef ACT_OFFE_H
#define ACT_OFFE_H

struct char_data;

/* Rescue helpers (act_offe.cpp), shared by do_rescue and its tests. */

/* Whoever in the rescuer's room is fighting `victim` and may be pulled off it
 * (wraith rules included), or NULL. */
char_data* find_rescue_attacker(char_data* rescuer, char_data* victim);

/* The charmed follower beside the leader, and visible to them, who is closest
 * to dying among those someone is fighting, or NULL. `saw_follower`, when
 * given, is set to whether any such follower stood in the room at all;
 * `follower_here` to whether any charmed follower is there, seen or not. */
char_data* find_most_hurt_rescuable_follower(char_data* leader, bool* saw_follower = nullptr,
    bool* follower_here = nullptr);

#endif
