#pragma once

#include "interpre.h"

/*
 * Harness mode: enabled by the -t startup flag (StartupOptions::harness_mode).
 * Off in every autorun deployment. While on, ROTS_RANDOM_SEED seeds the
 * generators and implementors may run `harness tick` to fire the game loop's
 * hourly block on demand, so an integration test never waits a real minute
 * for a poison or room-affect tick.
 */
extern int harness_mode; // 1 while the server runs under the integration harness

// Seeds std::rand() and random() from ROTS_RANDOM_SEED. Applies only in harness
// mode; returns true when a seed was applied.
bool seed_random_from_environment();

ACMD(do_harness);
