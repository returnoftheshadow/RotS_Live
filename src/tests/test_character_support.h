#pragma once

struct char_data;
struct char_prof_data;

namespace test_support {

// Allocates a character the way the server does (CREATE, a calloc wrapper) and clears it with
// clear_char(clear_mode). The mode is load-bearing: MOB_VOID allocates the skills and knowledge
// arrays, MOB_ISNPC does not, and free_char logs a SYSERR for an NPC that has them.
char_data* allocate_test_character(int clear_mode);

// Releases a character from allocate_test_character through free_char, the server's own release
// path, so the calloc/free pairing AddressSanitizer checks is the same one production uses.
void release_test_character(char_data* character);

// Sets up a stack-local, human, level-10 standing NPC with no fight, using `profs` as its
// profession data: enough state for affect_to_char() and affect_remove().
void make_stack_npc(char_data& character, char_prof_data& profs);

// make_stack_npc() with 500 hit points, far above the damage any single poison tick, wear or
// removal in the poison suites deals.
void make_sturdy_stack_npc(char_data& character, char_prof_data& profs);

// Registers `character` under `abs_number` (pointer and slot, as register_npc_char() does)
// for the scope and unregisters it on exit, so an early ASSERT_ return cannot leave a
// stack character registered for the next test.
class ScopedCharExists {
public:
    ScopedCharExists(char_data& character, int abs_number);
    ~ScopedCharExists();
    ScopedCharExists(const ScopedCharExists&) = delete;
    ScopedCharExists& operator=(const ScopedCharExists&) = delete;

private:
    char_data& m_character; // the character whose registration this scope owns
};

// Removes every affect still on `character` on scope exit, the cleanup the tick pins
// otherwise do by hand at their end.
class ScopedAffectCleanup {
public:
    explicit ScopedAffectCleanup(char_data& character)
        : m_character(character)
    {
    }
    ~ScopedAffectCleanup();
    ScopedAffectCleanup(const ScopedAffectCleanup&) = delete;
    ScopedAffectCleanup& operator=(const ScopedAffectCleanup&) = delete;

private:
    char_data& m_character; // the character whose leftover affects this scope removes
};

} // namespace test_support
