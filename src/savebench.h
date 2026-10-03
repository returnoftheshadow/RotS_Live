#ifndef SAVEBENCH_H
#define SAVEBENCH_H

#include "interpre.h" // for ACMD

// Implementor-only command: profile the account-JSON save/load pipeline for the invoking
// character against its real on-disk files, writing only to throwaway paths. Never mutates
// live state. Usage: "savebench [iterations]" (default 100, clamped 1..10000).
ACMD(do_savebench);

#endif // SAVEBENCH_H
