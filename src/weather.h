/* weather.h */

#ifndef WEATHER_H
#define WEATHER_H

#include <string>

struct char_data;

/* Returns the weather message for sector_type under its current sky, or NULL (logged) when the
 * sector type or its sky value has no message. */
const char* weather_message_for_sector(int sector_type);

/* The MSDP WEATHER value for character: the weather message character feels without its line
 * break, or the no-feeling text indoors, where character's room has a sector or sky with no
 * message, or for a null character (logged). Indoors no sector or sky is looked up. */
std::string msdp_weather_value(const char_data* character);

#endif /* WEATHER_H */
