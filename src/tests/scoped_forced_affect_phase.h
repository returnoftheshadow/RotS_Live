#pragma once

#include "../test_harness.h"

namespace test_support {

// Sets harness_force_affect_phase for the scope, so every slow affect ticks on each
// affect_update_person() call, and restores the flag's previous value on exit.
class ScopedForcedAffectPhase {
  public:
    ScopedForcedAffectPhase() : m_previous(harness_force_affect_phase) {
        harness_force_affect_phase = 1;
    }
    ~ScopedForcedAffectPhase() { harness_force_affect_phase = m_previous; }
    ScopedForcedAffectPhase(const ScopedForcedAffectPhase&) = delete;
    ScopedForcedAffectPhase& operator=(const ScopedForcedAffectPhase&) = delete;

  private:
    int m_previous; // the flag's value before the scope
};

} // namespace test_support
