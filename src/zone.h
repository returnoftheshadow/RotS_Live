/* zone.h */

#ifndef ZONE_H
#define ZONE_H

#include <stdio.h>

struct owner_list {
    int owner; /* one of the owners of zone/object */
    struct owner_list* next; /* next owner */
};

/* zone definition structure. for the 'zone-table'   */
struct zone_data {
    char* name; /* name of this zone                  */
    char* description;
    char* map;
    int lifespan; /* how long between resets (minutes)  */
    int age; /* current age of this zone (minutes) */
    int top; /* upper limit for rooms in this zone */
    int x, y; /* NEW - zone coordinates, for the map*/
    char symbol; /* NEW - symbol for the zone on the map */
    int level;
    int cmds_disabled; /* commands turned off by renum_zone_one due to bad vnums */
    int white_power, dark_power, magi_power; /* power of races present */
    struct extra_descr_data* zone_short_description; /* summary of a zone */
    struct extra_descr_data* zone_description; /* zone description */
    struct extra_descr_data* zone_map; /* for zone map   */

    int min_level_look; /* minimum level required for zone map etc */
    struct owner_list* owners;

    int reset_mode; /* conditions for reset (see below)   */
    int number; /* virtual number of this zone	  */
    int cmdno; /* Number of zone commands */
    struct reset_com* cmd; /* command table for reset	          */
    /*
     *  Reset mode:
     *  0: Don't reset, and don't update age
     *  1: Reset if no PC's are located in zone
     *  2: Just reset
     */
};

void zone_update(void);
void load_zones(FILE*);
void renum_zone_table(void);
void renum_zone_one(int, struct char_data* to = 0);
void reset_zone(int);

/*
 * Report a failure caused by the zone command reset_zone is running, naming
 * the zone and the command number.  Returns false when no zone command is
 * running, so a caller reached from elsewhere too can fall back to its own
 * message.
 */
bool report_zone_cmd_failure(const char* what);

extern struct zone_data* zone_table;
extern int top_of_zone_table;

#endif /* ZONE_H */
