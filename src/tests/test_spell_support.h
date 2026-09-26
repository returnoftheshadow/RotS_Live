#pragma once

#include "../caster_snapshot.h"
#include "../spells.h"

namespace test_support {

// Runs `spell` directly, snapshotting the caster once first as run_spell()
// does, for tests that exercise a spell body without the skills[] table.
inline void cast_spell(spell_function spell, char_data* caster, char* arg, int type,
    char_data* victim, obj_data* obj, int digit, int is_object)
{
    const caster_snapshot caster_at_cast = caster_snapshot::capture(*caster);
    spell(caster, arg, type, victim, obj, digit, is_object, caster_at_cast);
}

} // namespace test_support
