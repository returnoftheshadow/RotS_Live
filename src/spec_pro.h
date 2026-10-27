/* spec_pro.h */

#ifndef SPEC_PRO_H
#define SPEC_PRO_H

/* The spell a program-4 mage casts for <level_roll>, a number from 0 to its mage level, from
 * column <spell_type> (0 to 3) of its spell table. A roll picks a later row every five levels, or
 * every eight above 50; a roll past the last row casts from the last row. */
int mage_spell_for_roll(int level_roll, int spell_type);

#endif /* SPEC_PRO_H */
