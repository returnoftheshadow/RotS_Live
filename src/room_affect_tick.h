// src/room_affect_tick.h
#pragma once
// TASK-021. One tick of a room affect, run from the caster_snapshot recorded
// for (room, spell) at cast time.
//
// affect_update_room() used to tick a room affect by RE-CASTING its spell with
// the occupant standing in for the caster
// (`(skills[loc].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`), so
// every formula input -- caster level, saving-throw DC, spell penetration,
// specialization -- came from the victim rather than from whoever cast the
// spell, and a lethal tick credited nobody. room_affect_tick() runs the same
// four spell bodies from the snapshot instead, and credits the recorded caster
// (or nobody, once that character is gone) with any kill.
//
// This header is deliberately flat/top-level, matching every other header in
// this depot: its one caller is affect_update_room() in src/limits.cpp, and
// the tests include it the same way they include handler.h/spells.h.

struct affected_type;
struct char_data;
struct room_data;

// Runs one room-affect tick of `spell` on `occupant`, from the snapshot
// recorded for (room, spell). Returns true when `spell` has a tick body here
// (blaze, poison, haze, mist) and false for anything else -- the caller keeps
// its historical re-cast arm as the fallback for a room affect this file does
// not know about. `affect` is the room's live affect node; it is currently
// unread (every tick body re-derives its inputs from the snapshot) and is part
// of the signature so a future tick can read the affect's own duration/modifier
// without changing every call site.
bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& affect);
