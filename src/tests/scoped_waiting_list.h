#pragma once

#include "../structs.h"

extern char_data* waiting_list;

namespace test_support {

// Saves and clears the global waiting_list for the scope and restores it on exit. extract_char()
// walks the list, so a stale entry an earlier test left behind would otherwise be read.
class ScopedWaitingList {
  public:
    ScopedWaitingList() : m_previous(waiting_list) { waiting_list = nullptr; }
    ~ScopedWaitingList() { waiting_list = m_previous; }
    ScopedWaitingList(const ScopedWaitingList&) = delete;
    ScopedWaitingList& operator=(const ScopedWaitingList&) = delete;

  private:
    char_data* m_previous; // waiting_list before the scope
};

} // namespace test_support
