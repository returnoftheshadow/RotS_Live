/* profs.h */

#ifndef PROFS_H
#define PROFS_H

void advance_level_prof(int i, struct char_data* ch);
void roll_abilities(struct char_data* ch, int min, int max);
void advance_level(struct char_data* ch);
void draw_line(char* buf, int length);
void draw_coofs(char* buf, struct char_data* ch);

#endif /* PROFS_H */
