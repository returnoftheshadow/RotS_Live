#pragma once

#include "../character_affect_list.h"

#include <ostream>

// Prints `list` in gtest failure messages as its head pointer and removal count; without it,
// gtest dumps the object's raw bytes. Declared in the global namespace, beside the type, so
// gtest finds it by argument-dependent lookup. Include this header in every test file that
// compares a character's `affected`, so every instantiation of gtest's printer sees it.
inline void PrintTo(const character_affect_list& list, std::ostream* output) {
    *output << "character_affect_list{head: "
            << static_cast<const void*>(static_cast<affected_type*>(list))
            << ", removals: " << list.removal_count() << "}";
}
