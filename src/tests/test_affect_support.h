#pragma once

#include "../structs.h"

namespace test_support {

// An affect of `affect_type` that changes no stat and sets no flag.
affected_type inert_affect(int affect_type, int duration);

// Adds `filler_count` inert SPELL_ARMOR affects ahead of everything already on `character`.
void add_filler_affects(char_data& character, int filler_count);

// The number of `affect_type` affects on `character`, over the whole list: unlike
// affected_by_spell(), it does not stop after MAX_AFFECT entries.
int count_affects_of_type(const char_data& character, int affect_type);

} // namespace test_support
