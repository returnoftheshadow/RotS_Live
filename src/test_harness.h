#pragma once

#include "interpre.h"

/*
 * Harness mode, enabled by the -t startup flag, lets an integration test drive the game
 * deterministically: ROTS_RANDOM_SEED seeds the random number generator, the game loop's
 * wall-clock hourly block stops, and implementors run that work on demand with `harness tick`.
 * Nothing refuses -t on a production port; a live server started with it never advances game
 * time or the weather on its own and never idles players out.
 */
extern int harness_mode; // 1 while the server runs with -t; 0 otherwise

// 1 only while `harness affects` runs affect_update(): every slow person affect then treats the
// current time phase as its own, so each one ticks once per call. Room affects keep their own
// phase checks.
extern int harness_force_affect_phase;

// Seeds std::rand(), the generator behind number(), from ROTS_RANDOM_SEED. Does nothing outside
// harness mode or when the variable is unset; a value that is not an unsigned integer is logged
// and ignored. Returns true when a seed was applied.
bool seed_random_from_environment();

// The implementor-only `harness` command: `tick` runs one pass of the game loop's hourly and fast
// periodic work, `affects` runs one forced affect tick with no regeneration. Refused unless the
// server runs with -t.
ACMD(do_harness);
