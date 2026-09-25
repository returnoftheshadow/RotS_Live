#include "test_affect_support.h"

#include "../handler.h"
#include "../spells.h"

namespace test_support {

affected_type inert_affect(int affect_type, int duration) {
    affected_type affect{};
    affect.type = affect_type;
    affect.duration = duration;
    affect.modifier = 0;
    affect.location = APPLY_NONE;
    affect.bitvector = 0;
    return affect;
}

void add_filler_affects(char_data& character, int filler_count) {
    for (int filler = 0; filler < filler_count; ++filler) {
        affected_type armor = inert_affect(SPELL_ARMOR, 10);
        affect_to_char(&character, &armor);
    }
}

int count_affects_of_type(const char_data& character, int affect_type) {
    int matches = 0;
    for (const affected_type* affect = character.affected; affect != nullptr;
         affect = affect->next) {
        if (affect->type == affect_type) {
            ++matches;
        }
    }
    return matches;
}

} // namespace test_support
