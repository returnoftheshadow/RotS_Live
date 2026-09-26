#pragma once

#include "../structs.h"

extern descriptor_data* descriptor_list;
extern descriptor_data* next_to_process;
extern SocketType maxdesc;

namespace test_support {

// Makes `descriptor` the whole descriptor_list for the scope and restores the list, the next
// descriptor to process and maxdesc on exit: close_socket() unlinks the descriptor it frees from
// that list and lowers maxdesc when the socket numbers match.
class ScopedDescriptorList {
  public:
    explicit ScopedDescriptorList(descriptor_data* descriptor)
        : m_previous_list(descriptor_list), m_previous_next_to_process(next_to_process),
          m_previous_maxdesc(maxdesc) {
        descriptor_list = descriptor;
        next_to_process = nullptr;
    }
    ~ScopedDescriptorList() {
        descriptor_list = m_previous_list;
        next_to_process = m_previous_next_to_process;
        maxdesc = m_previous_maxdesc;
    }
    ScopedDescriptorList(const ScopedDescriptorList&) = delete;
    ScopedDescriptorList& operator=(const ScopedDescriptorList&) = delete;

  private:
    descriptor_data* m_previous_list;            // descriptor_list before the scope
    descriptor_data* m_previous_next_to_process; // next_to_process before the scope
    SocketType m_previous_maxdesc;               // maxdesc before the scope
};

} // namespace test_support
