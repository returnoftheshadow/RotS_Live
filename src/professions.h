#ifndef PROFESSIONS_H
#define PROFESSIONS_H

#include "platdef.h" /* For sh_int */

/* 'prof' for PC's */
#define MAX_PROFS 4
#define PROF_GENERAL 0
#define PROF_MAGIC_USER 1
#define PROF_MAGE 1
#define PROF_CLERIC 2
#define PROF_THIEF 3
#define PROF_RANGER 3
#define PROF_WARRIOR 4

#define DEFAULT_PROFS 10

struct prof_type {
    char letter;
    sh_int Class_points[5];
};

#endif /* PROFESSIONS_H */
