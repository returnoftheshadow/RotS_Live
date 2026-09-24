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
// itself -- exactly the `attacker == victim` shape the old
// self-re-cast produced -- so damage()'s whole `victim != attacker` block
// (set_fighting both ways, remember(), the 1-in-11 charmed-pet `hit()` on the
// pet's master) never runs from a room tick. Only the CREDITED killer moved:
// it is the resolved caster, or nobody. Engaging a same-room caster was
// considered and rejected: a room affect would otherwise put a resting caster
// into a fight with their own group-mates and pets, and the pet-master `hit()`
// arm could free a character out from under affect_update_room()'s occupant
// walk.
//
// The saved arm's two messages are both kept: the victim-facing line is sent
// straight to the occupant, so it always arrives (the old caster == victim
// shape suppressed it outright inside act()), and the caster-facing "$N shrugs
// off your poison with ease." is delivered only when the recorded caster is
// still alive AND standing in this room -- otherwise there is nobody to
// address. See poison_tick() below for the full account.

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

// mage.cpp's spell_blaze() victim arm. The burn comes from the cast's own
// blaze_burn_damage(), then goes to apply_spell_damage_credited() -- which runs the
// ONE shared scale_spell_damage() the live cast uses, reading the saving throw from
// the snapshot rather than from a live caster.
void blaze_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    const int level = get_mage_caster_level(who);
    const int save_bonus = get_save_bonus(who, *occupant, game_types::PS_Fire, game_types::PS_Cold);
    const bool saved = new_saves_spell(who, occupant, save_bonus);

    const int dam = blaze_burn_damage(level, saved);

    // Engaging attacker == the occupant itself (never `caster`): a tick damages,
    // it does not start a fight. Only the credit moves.
    // The credit follows the caster even when the occupant is a same-side
    // groupmate: owner ruling, 2026-09-24.
    apply_spell_damage_credited(who, occupant, occupant, caster, dam, SPELL_BLAZE, 0);
}

// mystic.cpp's spell_poison() victim arm, applying the cast's own
// poison_victim_affect(). `number(0, magus_save)` there is always `number(0, 0)`
// (magus_save is a zero-initialized local the function never writes), so it is
// spelled out as such here.
void poison_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    if (!saves_poison(occupant, who) && (number(0, 0) < 50)) {
        affected_type poison_affect = poison_victim_affect(who);
        affect_join(occupant, &poison_affect, FALSE, FALSE);

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
        // The victim-facing line goes straight to the occupant. It carries no
        // act() codes, and routing it through act() anchored on the caster let
        // CAN_SEE() drop it, or render it as "glances directly at you", whenever
        // the occupant could not see a remote or invisible caster. (The old
        // room re-cast, with caster == victim, never showed it at all.)
        send_to_char("You feel your body fend off the poison.\n\r", occupant);

        // ...and the caster-facing line only when there IS a caster to address:
        // still alive AND standing in this room. A caster who walked away, or
        // who is gone entirely, is told nothing.
        if (caster_is_present(caster, occupant)) {
            act("$N shrugs off your poison with ease.", FALSE, caster, 0, occupant, TO_CHAR);
        }
    }
}

// mystic.cpp's spell_haze() victim arm, for `type == SPELL_TYPE_SPELL` with
// `is_object == 0` -- the shape the room re-cast always produced. The level and
// the affect come from the cast's own illusion_caster_level() and haze_victim_affect().
void haze_tick(const caster_snapshot& who, char_data* occupant)
{
    const int level = illusion_caster_level(who);

    const int my_duration = number(0, 1);
    if (!affected_by_spell(occupant, SPELL_HAZE) && !saves_mystic(occupant)) {
        affected_type haze_affect = haze_victim_affect(level, my_duration);
        affect_to_char(occupant, &haze_affect);
        act("You feel dizzy as your surroundings seem to blur and twist.\n\r",
            TRUE, occupant, 0, occupant, TO_CHAR);
        act("$n staggers, overcome by dizziness!", FALSE, occupant, 0, 0, TO_ROOM);
    }
}

// mage.cpp's spell_mist_of_baazunga(), renewal arm, with the spread falloff. The
// room's own mist carries its spread generation in `counter`; this tick renews the
// room at that generation's level and seeds empty neighbours one generation
// further out. Two long-standing quirks of the cast are preserved: the renewal
// is silent, and a neighbour that already carries a mist is renewed against
// THIS room's `level / 5` rather than against the smaller `level / 6` it would be
// seeded with. A renewal from a room nearer the source also pulls the
// neighbour's generation in, never out. The pull is independent of the duration
// renewal: it happens even when the neighbour's duration is not raised and its
// caster record is another caster's.
void mist_tick(const caster_snapshot& who, room_data* room)
{
    // get_mage_caster_level() rolls its rounding on every call, so roll it once
    // and derive every generation's level from the one result.
    const int caster_level = get_mage_caster_level(who);

    int generation = 0;
    affected_type* const here = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA);
    if (here) {
        generation = here->counter;
    }
    const int level = mist_effective_level(caster_level, generation);
    if (here && here->duration < level / 5) {
        here->duration = level / 5;
    }
    const int next_generation = generation + 1;
    const int seed_level = mist_effective_level(caster_level, next_generation);

    for (int direction = 0; direction < NUM_OF_DIRS; direction++) {
        if (!room->dir_option[direction] || room->dir_option[direction]->to_room == NOWHERE) {
            continue;
        }

        room_data* const next = &world[room->dir_option[direction]->to_room];
        if (affected_type* there = room_affected_by_spell(next, SPELL_MIST_OF_BAAZUNGA)) {
            if (there->duration < level / 5) {
                there->duration = level / 5;
            }
            if (there->counter > next_generation) {
                there->counter = next_generation;
            }
            continue;
        }

        // A seed that would last no tick at all is not placed: it would only
        // darken the room for one update and vanish.
        if (seed_level / 6 <= 0) {
            continue;
        }

        affected_type seeded_mist {};
        seeded_mist.type = ROOMAFF_SPELL;
        seeded_mist.duration = seed_level / 6;
        if (IS_SET(next->room_flags, SHADOWY)) {
            seeded_mist.modifier = 1;
        } else {
            seeded_mist.modifier = 0;
        }
        seeded_mist.location = SPELL_MIST_OF_BAAZUNGA;
        seeded_mist.bitvector = 0;
        seeded_mist.counter = next_generation;
        affect_to_room(next, &seeded_mist, who);
    }
}

} // namespace

bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& /*affect*/)
{
    const caster_snapshot* const recorded = room_affect_caster(room, spell);
    const bool has_caster = recorded != nullptr && !recorded->is_none();

    // A builder-placed affect, or one that predates the caster store, carries
    // no caster: tick from the occupant's own stats, exactly as the old
    // self-re-cast did.
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
