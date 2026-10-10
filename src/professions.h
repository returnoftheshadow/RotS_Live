#ifndef PROFESSIONS_H
#define PROFESSIONS_H

#include "platdef.h" /* For sh_int */

// Profession indexes into prof_coof and prof_level; slot 0 is the general profession.
// PROF_MAGIC_USER and PROF_MAGE name the same profession, as do PROF_THIEF and PROF_RANGER.
// MAX_PROFS counts the four professions after the general slot.
#define MAX_PROFS 4
#define PROF_GENERAL 0
#define PROF_MAGIC_USER 1
#define PROF_MAGE 1
#define PROF_CLERIC 2
#define PROF_THIEF 3
#define PROF_RANGER 3
#define PROF_WARRIOR 4

// The number of standard classes the creation menu offers.
#define DEFAULT_PROFS 10

// A standard class: its creation-menu letter and its creation points per profession slot.
struct prof_type {
    // The letter that picks this class on the creation menu.
    char letter;
    // Creation points per profession, indexed like prof_coof; slot 0 is zero.
    sh_int Class_points[5];
};

#endif /* PROFESSIONS_H */
