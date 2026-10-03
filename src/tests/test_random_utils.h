#pragma once

// Test-only RNG control for the ageland_tests linker-wrap seam.
// Proc-heavy tests can queue normalized values in [0.0, 1.0) and the wrapped
// number() overloads will consume them instead of real randomness.
void clear_test_random_values();
void push_test_random_value(double value);
