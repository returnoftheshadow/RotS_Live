#include "caster_snapshot.h"
#include "char_utils.h"
#include "handler.h"
#include "utils.h"
#include <cstdio>

caster_snapshot caster_snapshot::capture(const char_data& caster)
{
    caster_snapshot snap {};
    // Macros like GET_LEVELA/GET_NAME null-guard their argument (e.g. IS_NPC's
    // "(ch) && ..."), which gcc's -Wnonnull-compare flags as comparing a
    // reference's address to NULL even though it can never be null here. Route
    // every such macro through this pointer instead of taking &caster inline.
    const char_data* const ch = &caster;
    snap.abs_number = caster.abs_number;
    snap.identity_ptr = const_cast<char_data*>(&caster);
    snap.level_a = GET_LEVELA(ch);
    snap.mage_prof_level = utils::get_prof_level(PROF_MAGE, caster);
    snap.cleric_prof_level = utils::get_prof_level(PROF_CLERIC, caster);
    snap.intel = caster.tmpabilities.intel;
    snap.wil = caster.tmpabilities.wil;
    snap.perception = GET_PERCEPTION(snap.identity_ptr); // get_race_perception() takes a non-const char_data*
    snap.willpower = GET_WILLPOWER(ch);
    snap.spell_power = caster.points.spell_power;
    snap.spell_pen = caster.points.spell_pen;
    snap.tactics = caster.specials.tactics;
    snap.specialization = utils::get_specialization(caster);
    snap.race = GET_RACE(ch);
    snap.is_npc = utils::is_npc(caster);
    snap.is_charmed = utils::is_affected_by(caster, AFF_CHARM);
    snap.is_pc_for_spell_pen = !snap.is_npc
        || (utils::is_mob_flagged(caster, MOB_ORC_FRIEND) && snap.is_charmed && caster.master && utils::is_pc(*caster.master));
    snap.master_mage_prof_level = (snap.is_npc && snap.is_charmed && caster.master)
        ? utils::get_prof_level(PROF_MAGE, *caster.master) : 0;
    const char* name = GET_NAME(ch);
    std::snprintf(snap.name, sizeof(snap.name), "%s", name ? name : "someone");
    return snap;
}

caster_snapshot caster_snapshot::none()
{
    caster_snapshot snap {};
    snap.abs_number = -1;
    std::snprintf(snap.name, sizeof(snap.name), "%s", "nobody");
    return snap;
}

bool caster_snapshot::same_character_as(const char_data& ch) const
{
    return !is_none() && identity_ptr == &ch && ch.abs_number == abs_number;
}

char_data* caster_snapshot::resolve() const
{
    // Never dereferences identity_ptr: abs_number slots are recycled by
    // register_npc_char() after free_char(), so a stale identity_ptr can
    // point at freed storage that has since been reallocated to an
    // unrelated character (TASK-021 fix round 1). char_by_abs_number()
    // looks up the CURRENT owner of the slot instead; identity_ptr is only
    // ever compared, never read through.
    if (is_none())
        return nullptr;
    char_data* live = char_by_abs_number(abs_number);
    return (live != nullptr && live == identity_ptr) ? live : nullptr;
}
