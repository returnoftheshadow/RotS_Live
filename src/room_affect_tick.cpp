// src/room_affect_tick.cpp -- TASK-021 port.
//
// The four room-affect tick bodies, run from the caster_snapshot recorded for
// (room, spell) instead of by re-casting the spell with the occupant as its own
// caster. Each body below reproduces the arm of the original ASPELL that
// affect_update_room()'s re-cast used to reach, with three deliberate
// differences (all of them the point of the task):
//
//   * every formula input comes from the SNAPSHOT (mage/mystic caster level,
//     saving-throw DC, spell penetration, specialization) rather than from the
//     victim standing in for the caster;
//   * a lethal tick credits the RECORDED caster -- who may be standing
//     somewhere else, and may no longer exist -- through damage_credited()/
//     apply_spell_damage_credited() rather than crediting the victim itself;
//   * poison_tick() records the poisoner on the victim, so a later poison death
//     resolves back to whoever cast it (resolve_poisoner(), fight.cpp).
//
// ENGAGEMENT IS NOT ONE OF THEM. The ENGAGING attacker every tick hands to
// damage_credited()/apply_spell_damage_credited() is always the OCCUPANT
// itself -- exactly the `attacker == victim` shape the pre-TASK-021
// self-re-cast produced -- so damage()'s whole `victim != attacker` block
// (set_fighting both ways, remember(), the 1-in-11 charmed-pet `hit()` on the
// pet's master) never runs from a room tick. Only the CREDITED killer moved:
// it is the resolved caster, or nobody. An earlier round of this port briefly
// engaged a same-room caster; the final whole-branch review overturned that --
// a room affect would otherwise put a resting caster into a fight with their
// own group-mates and pets, and the pet-master `hit()` arm could free a
// character out from under affect_update_room()'s occupant walk.
//
// The saved arm's two messages are both kept, but re-aimed: the victim-facing
// line always reaches the occupant (the old caster == victim shape suppressed
// it outright inside act()), and the caster-facing "$N shrugs off your poison
// with ease." is delivered only when the recorded caster is still alive AND
// standing in this room -- otherwise there is nobody to address. See
// poison_tick() below for the full account.

#include "room_affect_tick.h"

#include "caster_snapshot.h"
#include "comm.h"
#include "handler.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct room_data world;

// saves_mystic() lives in spell_pa.cpp and no shared header declares it --
// mystic.cpp keeps its own local declaration for the same reason.
char saves_mystic(struct char_data* ch);

namespace {

// True when the recorded caster is still alive and standing in the occupant's
// room -- the only situation in which there is somebody present to address a
// caster-facing message to. This is about MESSAGES only: it never decides who
// engages whom (see the file banner; the engaging attacker is always the
// occupant).
bool caster_is_present(const char_data* caster, const char_data* occupant)
{
    return caster != nullptr && caster->in_room == occupant->in_room;
}

// mage.cpp's spell_blaze() victim arm. `dam = number(8, level) + 10`, halved on
// a save, then handed to apply_spell_damage_credited() -- which runs the ONE
// shared scale_spell_damage() the live cast uses, reading the saving throw from
// the snapshot rather than from a live caster.
void blaze_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    const int level = get_mage_caster_level(who);
    const int save_bonus = get_save_bonus(who, *occupant, game_types::PS_Fire, game_types::PS_Cold);
    const bool saved = new_saves_spell(who, occupant, save_bonus);

    int dam = number(8, level) + 10;
    if (saved)
        dam >>= 1;

    // Engaging attacker == the occupant itself (never `caster`): a tick damages,
    // it does not start a fight. Only the credit moves.
    apply_spell_damage_credited(who, occupant, occupant, caster, dam, SPELL_BLAZE, 0);
}

// mystic.cpp's spell_poison() victim arm. `number(0, magus_save)` there is
// always `number(0, 0)` (magus_save is a zero-initialized local the function
// never writes), so it is spelled out as such here.
void poison_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    if (!saves_poison(occupant, who) && (number(0, 0) < 50)) {
        affected_type af {};
        af.type = SPELL_POISON;
        af.duration = get_mystic_caster_level(who) + 1;
        af.modifier = -2;
        af.location = APPLY_STR;
        af.bitvector = AFF_POISON;
        affect_join(occupant, &af, FALSE, FALSE);

        // The origin resolve_poisoner() reads when this poison eventually
        // kills, written through the one shared writer (fight.cpp) so the two
        // halves of the record can never disagree. `caster` is the RESOLVED
        // character -- null when the recorded caster is gone, and null when
        // this room affect never had one, in which case nobody is credited.
        // (Stamping who.abs_number/who.identity_ptr directly here instead
        // would, for a live caster, be the same record; for a departed one it
        // would write a stale pair resolve_poisoner() rejects anyway, and for
        // an affect with NO record at all it would name the occupant as its
        // own poisoner -- which would make a player's death by a
        // builder-placed poison read as a player kill, the opposite of this
        // tick's documented "nobody credited" fallback.)
        record_poison_origin(occupant, caster);

        send_to_char("You feel very sick.\n\r", occupant);
        // Engaging attacker == the occupant itself; see blaze_tick() above and
        // the file banner. limits.cpp's ordinary poison DoT ticks the same way
        // (`damage_credited(i, i, resolve_poisoner(*i), ...)`).
        damage_credited(occupant, occupant, caster, 5, SPELL_POISON, 0);
    } else {
        // The original saved arm (mystic.cpp) sent TWO lines, both anchored on
        // the caster: a TO_VICT line to the poisoned character and a TO_CHAR
        // line to whoever cast it. Anchoring the first on `caster` is also
        // what lets it through act()'s `recipient != ch` gate -- with
        // caster == victim, which is all the pre-TASK-021 room re-cast could
        // produce, act() suppressed the victim's own line entirely and
        // delivered only the (self-addressed) second one. With no caster left
        // to anchor on there is nothing for act() to render, so the line is
        // sent directly, the way this function's other victim-facing message
        // ("You feel very sick.") already is.
        if (caster != nullptr)
            act("You feel your body fend off the poison.", TRUE, caster, 0, occupant, TO_VICT);
        else
            send_to_char("You feel your body fend off the poison.\n\r", occupant);

        // ...and the caster-facing line only when there IS a caster to address:
        // still alive AND standing in this room. A caster who walked away, or
        // who is gone entirely, is told nothing.
        if (caster_is_present(caster, occupant))
            act("$N shrugs off your poison with ease.", FALSE, caster, 0, occupant, TO_CHAR);
    }
}

// mystic.cpp's spell_haze() victim arm, for `type == SPELL_TYPE_SPELL` with
// `is_object == 0` -- the shape the room re-cast always produced.
void haze_tick(const caster_snapshot& who, char_data* occupant)
{
    int level = get_mystic_caster_level(who);
    if (who.specialization == game_types::PS_Illusion)
        level += 6;

    const int my_duration = number(0, 1);
    if (!affected_by_spell(occupant, SPELL_HAZE) && !saves_mystic(occupant)) {
        affected_type af {};
        af.type = SPELL_HAZE;
        af.duration = my_duration;
        af.modifier = level;
        af.location = APPLY_NONE;
        af.bitvector = AFF_HAZE;
        affect_to_char(occupant, &af);
        act("You feel dizzy as your surroundings seem to blur and twist.\n\r",
            TRUE, occupant, 0, occupant, TO_CHAR);
        act("$n staggers, overcome by dizziness!", FALSE, occupant, 0, 0, TO_ROOM);
    }
}

// mage.cpp's spell_mist_of_baazunga(), renewal arm. Two long-standing quirks of
// that function are preserved verbatim: the renewal is silent (the "breathes
// out dark mists" messages only ever fired on a FRESH cast), and an adjacent
// room that already carries a mist is renewed against the MAIN room's
// `level / 5` rather than against the smaller `level / 6` it would be seeded
// with.
void mist_tick(const caster_snapshot& who, room_data* room)
{
    const int level = get_mage_caster_level(who);

    if (affected_type* here = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA)) {
        if (here->duration < level / 5)
            here->duration = level / 5;
    }

    for (int direction = 0; direction < NUM_OF_DIRS; direction++) {
        if (!room->dir_option[direction] || room->dir_option[direction]->to_room == NOWHERE)
            continue;

        room_data* const next = &world[room->dir_option[direction]->to_room];
        if (affected_type* there = room_affected_by_spell(next, SPELL_MIST_OF_BAAZUNGA)) {
            if (there->duration < level / 5)
                there->duration = level / 5;
            continue;
        }

        affected_type af2 {};
        af2.type = ROOMAFF_SPELL;
        af2.duration = level / 6;
        af2.modifier = IS_SET(next->room_flags, SHADOWY) ? 1 : 0;
        af2.location = SPELL_MIST_OF_BAAZUNGA;
        af2.bitvector = 0;
        affect_to_room(next, &af2, who);
    }
}

} // namespace

bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& /*affect*/)
{
    const caster_snapshot* const recorded = room_affect_caster(room, spell);
    const bool has_caster = recorded != nullptr && !recorded->is_none();

    // A builder-placed affect, or one that predates the caster store, carries
    // no caster: tick from the occupant's own stats, exactly as the
    // pre-TASK-021 self-re-cast did.
    const caster_snapshot who = has_caster ? *recorded : caster_snapshot::capture(*occupant);
    char_data* const caster = has_caster ? recorded->resolve() : nullptr;

    switch (spell) {
    case SPELL_BLAZE:
        blaze_tick(who, caster, occupant);
        return true;
    case SPELL_POISON:
        poison_tick(who, caster, occupant);
        return true;
    case SPELL_HAZE:
        haze_tick(who, occupant);
        return true;
    case SPELL_MIST_OF_BAAZUNGA:
        mist_tick(who, room);
        return true;
    default:
        return false;
    }
}
