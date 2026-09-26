#pragma once

#include "../structs.h"

extern char_data* combat_list;

namespace test_support {

// Saves and clears the global combat_list for the scope and restores it on exit, so fighters a
// test engages never reach another test's walk of the list.
class ScopedCombatList {
  public:
    ScopedCombatList() : m_previous(combat_list) { combat_list = nullptr; }
    ~ScopedCombatList() { combat_list = m_previous; }
    ScopedCombatList(const ScopedCombatList&) = delete;
    ScopedCombatList& operator=(const ScopedCombatList&) = delete;

  private:
    char_data* m_previous; // combat_list before the scope
};

} // namespace test_support
