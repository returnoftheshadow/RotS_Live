/* weather.h */

#ifndef WEATHER_H
#define WEATHER_H

/* Returns the weather message for sector_type under its current sky, or NULL (logged) when the
 * sector type or its sky value has no message. */
const char* weather_message_for_sector(int sector_type);

#endif /* WEATHER_H */
