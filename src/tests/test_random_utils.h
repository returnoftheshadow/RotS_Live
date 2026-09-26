#pragma once

#include <cstddef>

// Test-only RNG control for the ageland_tests linker-wrap seam.
// Proc-heavy tests can queue normalized values in [0.0, 1.0) and the wrapped
// number() overloads will consume them instead of real randomness.
void clear_test_random_values();
void push_test_random_value(double value);

// How many queued values have not been drawn yet, so a test can tell whether code drew any.
std::size_t queued_test_random_value_count();
