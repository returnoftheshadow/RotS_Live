#pragma once

struct affected_type;
struct char_data;
struct room_data;

// Runs one room-affect tick of `spell` on `occupant`, using the caster
// recorded for (room, spell) at cast time: every formula input is that
// caster's cast-time state, and a lethal tick credits that caster, or nobody
// once it is gone. Returns true when `spell` has a tick body here (blaze,
// poison, haze, mist) and false for anything else, which the caller must
// handle itself. `affect` is the room's live affect node for `spell`; no tick
// body reads it today.
bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& affect);
