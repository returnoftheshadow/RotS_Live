#pragma once

struct char_data;

namespace test_support {

// Allocates a character the way the server does (CREATE, a calloc wrapper) and clears it with
// clear_char(clear_mode). The mode is load-bearing: MOB_VOID allocates the skills and knowledge
// arrays, MOB_ISNPC does not, and free_char logs a SYSERR for an NPC that has them.
char_data* allocate_test_character(int clear_mode);

// Releases a character from allocate_test_character through free_char, the server's own release
// path, so the calloc/free pairing AddressSanitizer checks is the same one production uses.
void release_test_character(char_data* character);

} // namespace test_support
