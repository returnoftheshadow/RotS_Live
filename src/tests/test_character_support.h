#pragma once

struct char_data;
struct char_prof_data;

namespace test_support {

// Allocates a character the way the server does (CREATE, a calloc wrapper) and clears it with
// clear_char(clear_mode). The mode is load-bearing: MOB_VOID allocates the skills and knowledge
// arrays, MOB_ISNPC does not, and free_char logs a SYSERR for an NPC that has them.
char_data* allocate_test_character(int clear_mode);

// Releases a character from allocate_test_character through free_char, the server's own release
// path, so the calloc/free pairing AddressSanitizer checks is the same one production uses. A
// null `character` is reported as a test failure and nothing is released.
void release_test_character(char_data* character);

// The hit points fill_sturdy_stack_npc() gives, far above the damage any single poison tick,
// wear or removal deals.
constexpr int kSturdyNpcHitPoints = 500;

// Sets up a stack-local, human, level-10 standing NPC with no fight: enough state for
// affect_to_char() and affect_remove(). `out_character` keeps a pointer to `profs`, which must
// outlive it; only these fields are set, so pass a value-initialized character.
void fill_stack_npc(char_data& out_character, char_prof_data& profs);

// fill_stack_npc() with kSturdyNpcHitPoints hit points.
void fill_sturdy_stack_npc(char_data& out_character, char_prof_data& profs);

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
    explicit ScopedAffectCleanup(char_data& character) : m_character(character) {}
    ~ScopedAffectCleanup();
    ScopedAffectCleanup(const ScopedAffectCleanup&) = delete;
    ScopedAffectCleanup& operator=(const ScopedAffectCleanup&) = delete;

  private:
    char_data& m_character; // the character whose leftover affects this scope removes
};

} // namespace test_support
