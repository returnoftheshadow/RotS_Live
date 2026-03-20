/* ************************************************************************
 *   File: weather.c                                     Part of CircleMUD *
 *  Usage: functions handling time and the weather                         *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <stdio.h>

#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "protocol.h"
#include "structs.h"
#include "utils.h"

#define WEATHER_MESSAGE_RISE 0
#define WEATHER_MESSAGE_SET 1

extern struct room_data world;

int sun_events[12][2] = { // Each month has two variables - sunrise and sunset
    { 9, 17 }, // The default value for any month is set here.
    { 8, 18 },
    { 7, 19 },
    { 6, 20 },
    { 5, 21 },
    { 4, 22 },
    { 5, 21 },
    { 6, 20 },
    { 7, 19 },
    { 8, 18 },
    { 9, 17 },
    { 9, 16 }
};

extern struct time_info_data time_info;

// Weather messages should be moved to a special file like spell messages
// once this code is settled.

const char* weather_messages[8][13] = {
    { "", // Indoors - no message
        "The sun rises behind the buildings.\n\r",
        "The sun rises across the fields.\n\r",
        "The sun rises behind the trees.\n\r",
        "The sun rises behind the hills.\n\r",
        "The sun rises behind the mountains.\n\r",
        "The sun rises, the water glittering with morning light.\n\r",
        "The sun rises, the water glittering with morning light.\n\r",
        "", // Underwater - needs no message
        "The sun rises, its first rays lighting the road.\n\r",
        "Sunrise can just be detected far above.\n\r",
        "Sunrise can just be detected through the thick trees.\n\r",
        "The sun rises illuminating the morning mist rising from the swamp.\n\r" },
    { "",
        "The sun sets behind the buildings.\n\r",
        "The sun sets across the fields.\n\r",
        "The sun sets behind the trees.\n\r",
        "The sun sets behind the hills.\n\r",
        "The sun sets behind the mountains.\n\r",
        "The sun sets, the water turning grey and mysterious.\n\r",
        "The sun sets, the water turning black and menacing.\n\r",
        "",
        "It becomes hard to see far down the road as the sun sets.\n\r",
        "Far above, the sun sets.\n\r",
        "The setting sun can just be seen through the thick trees.\n\r",
        "As the sun sets the marshy area becomes alive with night-time sound.\n\r" },
    { "",
        "Not a cloud can be seen in the sky.\n\r",
        "Above the fields, not a cloud can be seen in the sky.\n\r",
        "Through the trees a cloudless sky can be seen.\n\r",
        "No clouds can be seen in the sky.\n\r",
        "No clouds can be seen in the sky above the mountains.\n\r",
        "Above the water a cloudless sky can be seen.\n\r",
        "Above the water a cloudless sky can be seen.\n\r",
        "",
        "Not a cloud can be seen in the sky.\n\r",
        "Far above, a cloudless sky can just be seen.\n\r",
        "The thick trees almost block out the sky, though it appears cloudless.\n\r",
        "A slight mist rises from the damp land into the cloudless sky.\n\r" },
    { "",
        "Clouds cover the sky.\n\r",
        "Clouds race through the sky above the fields.\n\r",
        "Clouds can be seen above in the sky.\n\r",
        "Clouds race across the sky above the hills.\n\r",
        "Higher still than the mountains are the thick clouds which cover the sky.\n\r",
        "The cloudy sky makes the water appear cold and lifeless.\n\r",
        "Clouds race across the sky turning the water dark and sullen.\n\r",
        "",
        "The dark cloudy sky makes it difficult to see far down the road.\n\r",
        "Far above, a cloudy sky can just be seen.\n\r",
        "It is almost impossible to see the sky here, though it appears to be cloudy.\n\r",
        "The sky is hidden by clouds making this area damp and chill.\n\r" },
    { "",
        "Puddles form on the street as heavy rain falls.\n\r",
        "Rain falls heavily on the fields.\n\r",
        "Large drops of water fall from the trees, as heavy rain falls overhead.\n\r",
        "Rain falls heavily on the hills, forming little streams as it runs away.\n\r",
        "Rain lashes down over the mountains, chilling you to the bone.\n\r",
        "Rain falls heavily onto the water, dampening its motion.\n\r",
        "Rain lashes down onto the water, stinging the eyes and soaking you to the skin.\n\r",
        "",
        "Puddles form in ruts in the road as rain quickly falls.\n\r",
        "A steady stream of water pours in from above.\n\r",
        "Heavy rain can be heard above through the trees,\n\r  and droplets of water fall heavily to the ground here.\n\r",
        "Rain falls heavily on the already saturated ground making movement difficuilt.\n\r" },
    { "",
        "Lightning lights up the sullen sky through the rain.\n\r",
        "Lightning lights up the fields as the rain continues.\n\r",
        "Flashes of lightning can be seen through the trees.\n\r",
        "Flashes of lightning are visible over the hills and driving rain.\n\r",
        "The mountains are lit by flashes of lightning as the storm intensifies.\n\r",
        "The water is lit by flashes of lightning.\n\r",
        "The water is occasionally lit by flashes of lightning.\n\r",
        "",
        "The road is suddenly lit by violent flashes of lightning as the rain continues.\n\r",
        "Occasional flashes of lightning illuminate this area.\n\r",
        "Flashes of lightning and the sound of heavy rain comes through the trees.\n\r",
        "The storm intesifies as flashes of lightning pierce the sky.\n\r" },
    { "",
        "The street is slowly covered by softly falling snow.\n\r",
        "The fields become blankets of white as snow quickly covers the ground.\n\r",
        "Wet snow falls through the trees, but cannot cover the ground.\n\r",
        "The hills quickly become covered in thick snow.\n\r",
        "Driving snow and a freezing wind blow fiercely down the mountain.\n\r",
        "Snow falls gently instantly thawing as it hits the water.\n\r",
        "Snow falls gently but disappears as soon as it hits the water.\n\r",
        "",
        "The road slowly starts to disappear under a carpet of snow.\n\r",
        "The occasional slowflake drifts down.\n\r",
        "Now and again the occasional snowflake drifts through the trees.\n\r",
        "Snow falls on the wet ground, but only in a few places does it manage to settle.\n\r" },
    { "",
        "",
        "",
        "",
        "It is hard to see anything as the snowstorm intensifies.\n\r",
        "Blizzard conditions prevail as the storm intensifies.\n\r",
        "",
        "",
        "",
        "",
        "",
        "",
        "" }

};

void weather_and_time(int mode);
void another_hour(int mode);
void weather_change(void);
void weather_to_char(char_data* ch);

extern char* moon_phase[];

void send_msdp_function(void (*func)(descriptor_data* desc))
{
    extern struct descriptor_data* descriptor_list;

    for (auto desc = descriptor_list; desc; desc = desc->next) {
        if (!desc->character || IS_NPC(desc->character)) {
            continue;
        }

        if (!desc->pProtocol) {
            continue;
        }

        func(desc);
    }
}

int get_sun_level(int room)
{
    int level;

    if (weather_info.sunlight == SUN_DARK || IS_SET(world[room].room_flags, DARK) || world[room].sector_type == SECT_INSIDE)
        return 0;
    switch (world[room].sector_type) {
    case SECT_CITY:
    case SECT_FIELD:
    case SECT_HILLS:
    case SECT_MOUNTAIN:
    case SECT_ROAD:
    case SECT_SWAMP:
    case SECT_WATER_SWIM:
    case SECT_WATER_NOSWIM:
        level = 10;
        break;
    case SECT_FOREST:
        level = 8;
        break;
    case SECT_DENSE_FOREST:
        level = 6;
        break;
    default:
        level = 0;
        break;
    }
    if (weather_info.snow[world[room].sector_type])
        level += 4;
    if (weather_info.sky[world[room].sector_type] == SKY_CLOUDLESS)
        level += 2;
    if (weather_info.sky[world[room].sector_type] > SKY_CLOUDY)
        level -= 1;
    if IS_SET (world[room].room_flags, SHADOWY)
        level /= 2;
    if (weather_info.sunlight == SUN_RISE || weather_info.sunlight == SUN_SET)
        level /= 2;
    return level;
}

void weather_and_time(int mode)
{
    another_hour(mode);
    if (mode)
        weather_change();
}

void weather_message(int message_type)
{
    int count;

    for (count = 1; count < 13; count++) // Sector_types start at 0 for Inside
        if (count != 8) // Sector Underwater
            send_to_sector(weather_messages[message_type][count], count);
}

int check_sun_change()
{
    if (weather_info.sunlight == SUN_RISE) {
        weather_info.sunlight = SUN_LIGHT;
        send_to_outdoor("The day has now begun.\n\r", OUTDOORS_LIGHT);
        return 1;
    }
    if (weather_info.sunlight == SUN_SET) {
        weather_info.sunlight = SUN_DARK;
        send_to_outdoor("Night falls over the land.\n\r", OUTDOORS_LIGHT);
        return 1;
    }
    return 0;
}

void set_sun_info(int rise, int set)
{

    if (check_sun_change())
        return;
    if (time_info.hours == rise) {
        weather_info.sunlight = SUN_RISE;
        weather_message(WEATHER_MESSAGE_RISE);
    }
    if (time_info.hours == set) {
        weather_info.sunlight = SUN_SET;
        weather_message(WEATHER_MESSAGE_SET);
    }
}

int get_season()
{
    switch (time_info.month) {
    case 2:
    case 3:
    case 4:
        return SEASON_SPRING;
    case 5:
    case 6:
    case 7:
        return SEASON_SUMMER;
    case 8:
    case 9:
    case 10:
        return SEASON_AUTUMN;
    case 11:
    case 0:
    case 1:
        return SEASON_WINTER;
    default:
        return -1;
    }
}

int sector_age_value(int sector_type)
{
    switch (sector_type) {
    case SECT_CITY:
    case SECT_FIELD:
    case SECT_HILLS:
    case SECT_MOUNTAIN:
    case SECT_ROAD:
    case SECT_SWAMP:
        switch (weather_info.sky[sector_type]) {
        case SKY_CLOUDLESS:
            return 1;
        case SKY_CLOUDY:
            return 1;
        case SKY_RAINING:
            return 2;
        case SKY_LIGHTNING:
            return 2;
        case SKY_SNOWING:
            return 4;
        case SKY_BLIZZARD:
            return 5;
        default:
            return 1;
        }
    default:
        return 1;
    }
}

void weather_to_char(char_data* ch)
{
    int sector_type, weather_type;

    sector_type = world[ch->in_room].sector_type;
    weather_type = weather_info.sky[world[ch->in_room].sector_type];
    if (OUTSIDE(ch))
        send_to_char(weather_messages[weather_type + 2][sector_type], ch);
    else
        send_to_char("You can have no feeling about the weather here.\n\r", ch);
}

void age_bleed_tracks()
{
    room_data* tmproom;
    int roomnum, tmp;
    extern int top_of_world;
    extern struct room_data world;

    for (roomnum = 0; roomnum <= top_of_world; roomnum++) {
        tmproom = &world[roomnum];
        for (tmp = 0; tmp < NUM_OF_BLOOD_TRAILS; tmp++) {
            tmproom->bleed_track[tmp].condition += (weather_info.snow[tmproom->sector_type] ? number(0, 1) : sector_age_value(tmproom->sector_type));
        }
    }
}

void age_room_tracks()
{
    room_data* tmproom;
    int roomnum, tmp;
    extern int top_of_world;
    extern struct room_data world;

    for (roomnum = 0; roomnum <= top_of_world; roomnum++) {
        tmproom = &world[roomnum];
        for (tmp = 0; tmp < NUM_OF_TRACKS; tmp++)
            tmproom->room_track[tmp].condition += (weather_info.snow[tmproom->sector_type] ? number(0, 1) : sector_age_value(tmproom->sector_type));
    }
}

void another_hour(int mode)
{
    int new_moon_phase, moon_rise, bootup;

    if (mode == -1) {
        bootup = 1;
        mode = 1;
    } else
        bootup = 0;

    time_info.hours++;

    if (mode && !(bootup))
        set_sun_info(sun_events[time_info.month][0], sun_events[time_info.month][1]);

    if (time_info.hours > 23) {
        time_info.hours -= 24;
        time_info.day++;

        if (time_info.day > 29) {
            time_info.day = 0;
            time_info.month++;

            if (time_info.month > 11) {
                time_info.month = 0;
                time_info.year++;
            }
        }
        time_info.moon++;
        if (time_info.moon > 27)
            time_info.moon = 0;
    }

    if (bootup)
        return;

    new_moon_phase = time_info.moon % 8;
    moon_rise = (SUN_RISE + 24 * time_info.moon / 28) % 24;

    if (time_info.hours == moon_rise) {
        weather_info.moonphase = new_moon_phase;
        if (weather_info.moonphase != MOON_NEW) {
            weather_info.moonlight = 1;
            sprintf(buf, "The %s moon shows in the sky.\n\r",
                moon_phase[new_moon_phase]);
            send_to_outdoor(buf, OUTDOORS_LIGHT);
        }
    }
    if (time_info.hours == (moon_rise + 12) % 24) {
        if (weather_info.moonphase != MOON_NEW) {
            weather_info.moonlight = 0;
            sprintf(buf, "The %s moon goes off the sky.\n\r",
                moon_phase[new_moon_phase]);
            send_to_outdoor(buf, OUTDOORS_LIGHT);
        }
    }
    age_room_tracks();
    age_bleed_tracks();

    send_msdp_function([](descriptor_data* desc) {
        char time[64];
        sprintf(time, "It is about %d:00 %s on ",
            time_info.hours % 12 == 0 ? 12 : time_info.hours % 12,
            time_info.hours >= 12 ? "PM" : "AM");

        MSDPSetString(desc, eMSDP_WORLD_TIME, time);
        MSDPSend(desc, eMSDP_WORLD_TIME);
    });
}

void weather_change(void)
{
    int diff = 0, change, SectorType;

    switch (get_season()) { // We therefore get lower pressure in winter.
    case SEASON_SPRING:
        diff = (weather_info.pressure > 1000 ? -2 : 2);
        break;
    case SEASON_SUMMER:
        diff = (weather_info.pressure > 1015 ? -2 : 2);
        break;
    case SEASON_AUTUMN:
        diff = (weather_info.pressure > 995 ? -2 : 2);
        break;
    case SEASON_WINTER:
        diff = (weather_info.pressure > 980 ? -3 : 2);
        break;
    default:
        break;
    }

    /*   if ((time_info.month >= 9) && (time_info.month <= 16))
      diff = (weather_info.pressure > 985 ? -2 : 2);
   else
      diff = (weather_info.pressure > 1015 ? -2 : 2); */

    weather_info.change += (dice(1, 4) * diff + dice(2, 6) - dice(2, 6));

    weather_info.change = MIN(weather_info.change, 12);
    weather_info.change = MAX(weather_info.change, -12);

    weather_info.pressure += weather_info.change;

    weather_info.pressure = MIN(weather_info.pressure, 1040);
    weather_info.pressure = MAX(weather_info.pressure, 960);

    change = 0;
    /*
   How this works:  SECT_FIELD is taken as being the benchmark for weather.  Most other sectors have
   the same weather as fields (though with different messages and effects).  Hills and mountains
   can have weather one or two degrees worse than fields with possible blizzards in winter.
*/

    switch (weather_info.sky[SECT_FIELD]) {
    case SKY_CLOUDLESS:
        if (weather_info.pressure < 990)
            weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        else if (weather_info.pressure < 1010)
            if (dice(1, 6) == 1)
                weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        break;
    case SKY_CLOUDY:
        if (weather_info.pressure < 970)
            if (get_season() == SEASON_WINTER && dice(1, 6) == 1)
                weather_info.sky[SECT_FIELD] = SKY_SNOWING;
            else if (weather_info.pressure < 990)
                if (dice(1, 4) == 1)
                    weather_info.sky[SECT_FIELD] = SKY_RAINING;
        if (dice(1, 4) == 1 && weather_info.pressure > 1000)
            weather_info.sky[SECT_FIELD] = SKY_CLOUDLESS;
        break;
    case SKY_RAINING:
        if (weather_info.pressure < 970)
            if (dice(1, 4) == 1)
                weather_info.sky[SECT_FIELD] = SKY_LIGHTNING;
            else
                weather_info.sky[SECT_FIELD] = SKY_RAINING;
        else if (weather_info.pressure > 1030)
            weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        else if (weather_info.pressure > 1010)
            if (dice(1, 4) == 1)
                weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        break;
    case SKY_LIGHTNING:
        if (weather_info.pressure > 1010)
            weather_info.sky[SECT_FIELD] = SKY_RAINING;
        else if (weather_info.pressure > 990)
            if (dice(1, 4) == 1)
                weather_info.sky[SECT_FIELD] = SKY_RAINING;
        if (get_season() == SEASON_WINTER && dice(1, 6) == 1)
            weather_info.sky[SECT_FIELD] = SKY_SNOWING;
        break;
    case SKY_SNOWING:
        if (weather_info.pressure > 1010)
            weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        else if (weather_info.pressure > 990)
            if (dice(1, 4) == 1)
                weather_info.sky[SECT_FIELD] = SKY_CLOUDY;
        break;
    default:
        weather_info.sky[SECT_FIELD] = SKY_CLOUDLESS;
        break;
    }

    weather_info.sky[SECT_CITY] = (weather_info.sky[SECT_FIELD] == SKY_CLOUDLESS ? SKY_CLOUDLESS : weather_info.sky[SECT_FIELD] - 1);
    weather_info.sky[SECT_FOREST] = weather_info.sky[SECT_FIELD];
    weather_info.sky[SECT_HILLS] = ((get_season() == SEASON_WINTER) && (dice(1, 4) == 1) ? weather_info.sky[SECT_FIELD] + 1 : weather_info.sky[SECT_FIELD]);
    weather_info.sky[SECT_MOUNTAIN] = ((dice(1, 6) == 1) ? MIN(weather_info.sky[SECT_HILLS] + 1, 5) : weather_info.sky[SECT_HILLS]);
    weather_info.sky[SECT_WATER_SWIM] = weather_info.sky[SECT_FIELD];
    weather_info.sky[SECT_WATER_NOSWIM] = weather_info.sky[SECT_FIELD];
    weather_info.sky[SECT_ROAD] = weather_info.sky[SECT_FIELD];
    weather_info.sky[SECT_CRACK] = weather_info.sky[SECT_CITY];
    weather_info.sky[SECT_DENSE_FOREST] = weather_info.sky[SECT_CITY];
    weather_info.sky[SECT_SWAMP] = weather_info.sky[SECT_CITY];

    // So at the moment it takes an equal number of ticks for snow to melt as it did to
    // fall.  weather_info.temperature should come into effect here, but is not
    // implemented yet.

    for (SectorType = 1; SectorType < 13; SectorType++) {
        if (SectorType == SECT_WATER_SWIM || SectorType == SECT_WATER_NOSWIM)
            continue;
        if (weather_info.sky[SectorType] == SKY_SNOWING || weather_info.sky[SectorType] == SKY_BLIZZARD)
            weather_info.snow[SectorType] += 1;
        else if (weather_info.snow[SectorType])
            weather_info.snow[SectorType] -= 1;
    }
    for (SectorType = 1; SectorType < 13; SectorType++)
        send_to_sector(weather_messages[weather_info.sky[SectorType] + 2][SectorType], SectorType);

    extern struct descriptor_data* descriptor_list;

    send_msdp_function([](descriptor_data* desc) {
        auto sector_type = world[desc->character->in_room].sector_type;
        auto weather_type = weather_info.sky[sector_type];
        if (OUTSIDE(desc->character)) {
            MSDPSetString(desc, eMDSP_WEATHER, weather_messages[weather_type + 2][sector_type]);
        } else {
            MSDPSetString(desc, eMDSP_WEATHER, "You can have no feeling about the weather here.");
        }
        MSDPSend(desc, eMDSP_WEATHER);
    });
}

//=============================================================================
int get_sunlight_level(int current_time, int sun_rise_time, int sun_set_time)
{
    if (current_time == sun_rise_time) {
        return SUN_RISE;
    } else if (current_time == sun_set_time) {
        return SUN_SET;
    } else if (current_time > sun_rise_time && current_time < sun_set_time) {
        return SUN_LIGHT;
    } else {
        return SUN_DARK;
    }
}

//=============================================================================
int get_seasonal_pressure(int month)
{
    int max_pressure = 80;
    // Greater atmospheric pressure in Winter and Spring than in Summer and Fall.
    if (month >= 7 && month <= 12)
        max_pressure = 50;

    return dice(1, max_pressure);
}

//=============================================================================
int get_sky_conditions(int pressure)
{
    if (pressure <= 980) {
        return SKY_LIGHTNING;
    } else if (pressure <= 1000) {
        return SKY_RAINING;
    } else if (pressure <= 1020) {
        return SKY_CLOUDY;
    } else {
        return SKY_CLOUDLESS;
    }
}

//=============================================================================
void initialize_weather()
{
    // Initialize everything to 0.
    for (int count = 1; count > 13; count++) {
        weather_info.sky[count] = 0;
        weather_info.snow[count] = 0;
        weather_info.temperature[count] = 0;
    }

    int sun_rise_time = sun_events[time_info.month][0];
    int sun_set_time = sun_events[time_info.month][1];

    weather_info.sunlight = get_sunlight_level(time_info.hours, sun_rise_time, sun_set_time);

    sprintf(buf, "   Current Gametime: %dH %dD %dM %dY.", time_info.hours, time_info.day, time_info.month, time_info.year);
    log(buf);

    weather_info.pressure = 960;
    weather_info.pressure += get_seasonal_pressure(time_info.month);

    weather_info.change = 0;

    weather_info.sky[SECT_FIELD] = get_sky_conditions(weather_info.pressure);

    another_hour(-1);

    sun_rise_time = sun_events[time_info.month][0];
    sun_set_time = sun_events[time_info.month][1];

    weather_info.sunlight = get_sunlight_level(time_info.hours, sun_rise_time, sun_set_time);
}
