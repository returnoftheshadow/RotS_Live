#pragma once

#include "../structs.h"

extern index_data* mob_index;

namespace test_support {

// Publishes a one-entry mob prototype table, with no spec-proc, for the scope and restores the
// previous table on exit. The death pipeline (raw_kill()'s SPECIAL_DEATH probe and
// make_physical_corpse()) reads mob_index[character->nr] for any IS_NPC() character, so an NPC
// that dies in a test needs nr = 0 and this scope.
class ScopedMobIndex {
  public:
    ScopedMobIndex() : m_previous(mob_index) {
        m_entry = index_data{};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedMobIndex() { mob_index = m_previous; }
    ScopedMobIndex(const ScopedMobIndex&) = delete;
    ScopedMobIndex& operator=(const ScopedMobIndex&) = delete;

  private:
    index_data* m_previous; // the table installed before the scope (normally null)
    index_data m_entry{};   // prototype slot 0, the one the test NPC's nr names
};

} // namespace test_support
