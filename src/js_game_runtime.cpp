#include "js_game_runtime.h"
#include "js_source_policy.h"

#include "json_utils.h"

#include <cctype>
#include <sstream>
#include <utility>

namespace {

using json_utils::JsonReader;

constexpr std::size_t MaxGameDiagnosticLength = 120;
constexpr std::size_t MaxGameMutationCount = 64;
constexpr std::size_t CharacterBodypartHitCount = 11;

JsRuntimeEvalResult sanitize_game_result(JsRuntimeEvalResult result);

std::string js_quote(const std::string &value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (ch < 0x20 || ch >= 0x80) {
                static const char *hex = "0123456789abcdef";
                out << "\\x" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
            } else {
                out << ch;
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

const char *js_bool(bool value) { return value ? "true" : "false"; }

std::string nullable_literal(bool present, const std::string &literal) {
    return present ? literal : "null";
}

std::string string_array_literal(const std::vector<std::string> &values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            out << ",";
        out << js_quote(values[index]);
    }
    out << "]";
    return out.str();
}

std::string int_array_literal(const std::vector<int> &values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0)
            out << ",";
        out << values[index];
    }
    out << "]";
    return out.str();
}

std::string ability_scores_literal(const JsGameAbilityScoresFixture &abilities) {
    std::ostringstream out;
    out << "{"
        << "\"strength\":" << abilities.strength << ","
        << "\"intelligence\":" << abilities.intelligence << ","
        << "\"willpower\":" << abilities.willpower << ","
        << "\"dexterity\":" << abilities.dexterity << ","
        << "\"constitution\":" << abilities.constitution << ","
        << "\"leadership\":" << abilities.leadership << "}";
    return out.str();
}

std::string character_points_literal(const JsGameCharacterPointsFixture &points) {
    std::ostringstream out;
    out << "{"
        << "\"bodypartHits\":";
    out << "[";
    for (std::size_t index = 0; index < CharacterBodypartHitCount; ++index) {
        if (index > 0)
            out << ",";
        out << (index < points.bodypart_hits.size() ? points.bodypart_hits[index] : 0);
    }
    out << "],"
        << "\"gold\":" << points.gold << ","
        << "\"experience\":" << points.experience << ","
        << "\"spirit\":" << points.spirit << ","
        << "\"manaRegen\":" << points.mana_regen << ","
        << "\"healthRegen\":" << points.health_regen << ","
        << "\"moveRegen\":" << points.move_regen << ","
        << "\"offense\":" << points.offense << ","
        << "\"damage\":" << points.damage << ","
        << "\"energyRegen\":" << points.energy_regen << ","
        << "\"parry\":" << points.parry << ","
        << "\"dodge\":" << points.dodge << ","
        << "\"encumbrance\":" << points.encumbrance << ","
        << "\"willpower\":" << points.willpower << ","
        << "\"spellPenetration\":" << points.spell_penetration << ","
        << "\"spellPower\":" << points.spell_power << "}";
    return out.str();
}

std::string nullable_string_literal(const std::string &value) {
    return value.empty() ? "null" : js_quote(value);
}

std::string character_specials_literal(const JsGameCharacterSpecialsFixture &specials) {
    std::ostringstream out;
    out << "{"
        << "\"isFighting\":" << js_bool(specials.is_fighting) << ","
        << "\"isHunting\":" << js_bool(specials.is_hunting) << ","
        << "\"hasMemory\":" << js_bool(specials.has_memory) << ","
        << "\"position\":" << js_quote(specials.position) << ","
        << "\"defaultPosition\":" << js_quote(specials.default_position) << ","
        << "\"carryWeight\":" << specials.carry_weight << ","
        << "\"wornWeight\":" << specials.worn_weight << ","
        << "\"encumbranceWeight\":" << specials.encumbrance_weight << ","
        << "\"carryItems\":" << specials.carry_items << ","
        << "\"timer\":" << specials.timer << ","
        << "\"wasInRoom\":" << specials.was_in_room << ","
        << "\"energy\":" << specials.energy << ","
        << "\"currentParry\":" << specials.current_parry << ","
        << "\"lastDirection\":" << nullable_string_literal(specials.last_direction) << ","
        << "\"attackType\":" << specials.attack_type << ","
        << "\"scriptNumber\":" << specials.script_number << ","
        << "\"currentBodypart\":" << specials.current_bodypart << ","
        << "\"tactics\":" << nullable_string_literal(specials.tactics) << ","
        << "\"promptNumber\":" << specials.prompt_number << ","
        << "\"promptValue\":" << specials.prompt_value << ","
        << "\"homeZone\":" << specials.home_zone << ","
        << "\"loadLine\":" << specials.load_line << "}";
    return out.str();
}

std::string character_conditions_literal(const JsGameCharacterConditionsFixture &conditions) {
    std::ostringstream out;
    out << "{"
        << "\"drunk\":" << conditions.drunk << ","
        << "\"full\":" << conditions.full << ","
        << "\"thirst\":" << conditions.thirst << "}";
    return out.str();
}

std::string character_specials2_literal(const JsGameCharacterSpecials2Fixture &specials2) {
    std::ostringstream out;
    out << "{"
        << "\"loadRoom\":" << specials2.load_room << ","
        << "\"spellsToLearn\":" << specials2.spells_to_learn << ","
        << "\"alignment\":" << specials2.alignment << ","
        << "\"actFlags\":" << string_array_literal(specials2.act_flags) << ","
        << "\"preferenceFlags\":" << string_array_literal(specials2.preference_flags) << ","
        << "\"wimpLevel\":" << specials2.wimp_level << ","
        << "\"freezeLevel\":" << specials2.freeze_level << ","
        << "\"savingThrow\":" << specials2.saving_throw << ","
        << "\"rawPerception\":" << specials2.raw_perception << ","
        << "\"perception\":" << specials2.perception << ","
        << "\"conditions\":" << character_conditions_literal(specials2.conditions) << ","
        << "\"miniLevel\":" << specials2.mini_level << ","
        << "\"maxMiniLevel\":" << specials2.max_mini_level << ","
        << "\"morale\":" << specials2.morale << ","
        << "\"rerolls\":" << specials2.rerolls << ","
        << "\"legEncumbrance\":" << specials2.leg_encumbrance << ","
        << "\"retiredOn\":" << specials2.retired_on << ","
        << "\"hideFlags\":" << string_array_literal(specials2.hide_flags) << ","
        << "\"tactics\":" << js_quote(specials2.tactics) << ","
        << "\"shooting\":" << js_quote(specials2.shooting) << ","
        << "\"casting\":" << js_quote(specials2.casting) << ","
        << "\"twoHanded\":" << js_bool(specials2.two_handed) << "}";
    return out.str();
}

std::string zone_literal(const JsGameZoneFixture &zone) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(zone.id) << ","
        << "\"name\":" << js_quote(zone.name) << ","
        << "\"description\":" << nullable_literal(zone.has_description, js_quote(zone.description))
        << ","
        << "\"map\":" << nullable_literal(zone.has_map, js_quote(zone.map)) << ","
        << "\"vnum\":" << zone.vnum << ","
        << "\"level\":" << zone.level << ","
        << "\"lifespan\":" << zone.lifespan << ","
        << "\"age\":" << zone.age << ","
        << "\"topRoomVnum\":" << zone.top_room_vnum << ","
        << "\"x\":" << zone.x << ","
        << "\"y\":" << zone.y << ","
        << "\"symbol\":" << js_quote(zone.symbol) << ","
        << "\"whitePower\":" << zone.white_power << ","
        << "\"darkPower\":" << zone.dark_power << ","
        << "\"magiPower\":" << zone.magi_power << ","
        << "\"minimumLookLevel\":" << zone.minimum_look_level << ","
        << "\"resetMode\":" << zone.reset_mode << "}";
    return out.str();
}

std::string room_literal(const JsGameRoomFixture &room) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(room.id) << ","
        << "\"name\":" << js_quote(room.name) << ","
        << "\"description\":" << js_quote(room.description) << ","
        << "\"vnum\":" << room.vnum << ","
        << "\"level\":" << room.level << ","
        << "\"sectorType\":" << js_quote(room.sector_type) << ","
        << "\"flags\":" << string_array_literal(room.flags) << ","
        << "\"alignment\":" << room.alignment << ","
        << "\"light\":" << room.light << ","
        << "\"isSunlit\":" << js_bool(room.is_sunlit) << ","
        << "\"zone\":" << nullable_literal(room.has_zone, zone_literal(room.zone)) << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string profession_literal(const JsGameProfessionFixture &profession) {
    std::ostringstream out;
    out << "{"
        << "\"key\":" << js_quote(profession.key) << ","
        << "\"name\":" << js_quote(profession.name) << ","
        << "\"level\":" << profession.level << ","
        << "\"points\":" << profession.points << ","
        << "\"coefficient\":" << profession.coefficient << ","
        << "\"experience\":" << profession.experience << "}";
    return out.str();
}

std::string professions_literal(const std::vector<JsGameProfessionFixture> &professions) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < professions.size(); ++index) {
        if (index > 0)
            out << ",";
        out << profession_literal(professions[index]);
    }
    out << "]";
    return out.str();
}

std::string specializations_literal(const JsGameSpecializationFixture &specializations) {
    std::ostringstream out;
    out << "{"
        << "\"selectedId\":" << specializations.selected_id << ","
        << "\"selectedKey\":" << js_quote(specializations.selected_key) << ","
        << "\"selectedName\":" << js_quote(specializations.selected_name) << ","
        << "\"currentId\":" << specializations.current_id << ","
        << "\"currentKey\":" << js_quote(specializations.current_key) << ","
        << "\"currentName\":" << js_quote(specializations.current_name) << ","
        << "\"isMageSpecialization\":" << js_bool(specializations.is_mage_specialization) << ","
        << "\"hasRuntimeState\":" << js_bool(specializations.has_runtime_state) << "}";
    return out.str();
}

std::string damage_entry_literal(const JsGameDamageEntryFixture &entry) {
    std::ostringstream out;
    out << "{"
        << "\"sourceId\":" << entry.source_id << ","
        << "\"sourceKind\":" << js_quote(entry.source_kind) << ","
        << "\"sourceName\":" << js_quote(entry.source_name) << ","
        << "\"instanceCount\":" << entry.instance_count << ","
        << "\"totalDamage\":" << entry.total_damage << ","
        << "\"largestDamage\":" << entry.largest_damage << ","
        << "\"averageDamage\":" << entry.average_damage << ","
        << "\"percentOfTotal\":" << entry.percent_of_total << "}";
    return out.str();
}

std::string damage_details_literal(const JsGameDamageDetailsFixture &damage_details) {
    std::ostringstream out;
    out << "{"
        << "\"elapsedCombatSeconds\":" << damage_details.elapsed_combat_seconds << ","
        << "\"totalDamage\":" << damage_details.total_damage << ","
        << "\"damagePerSecond\":" << damage_details.damage_per_second << ","
        << "\"entries\":[";
    for (std::size_t index = 0; index < damage_details.entries.size(); ++index) {
        if (index > 0)
            out << ",";
        out << damage_entry_literal(damage_details.entries[index]);
    }
    out << "]}";
    return out.str();
}

std::string skill_value_literal(const JsGameSkillValueFixture &skill) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << skill.id << ","
        << "\"name\":" << js_quote(skill.name) << ","
        << "\"profession\":" << js_quote(skill.profession) << ","
        << "\"level\":" << skill.level << ","
        << "\"practice\":" << skill.practice << ","
        << "\"minimumPosition\":" << skill.minimum_position << ","
        << "\"manaCost\":" << skill.mana_cost << ","
        << "\"beats\":" << skill.beats << ","
        << "\"targets\":" << skill.targets << ","
        << "\"learnDifficulty\":" << skill.learn_difficulty << ","
        << "\"learnType\":" << skill.learn_type << ","
        << "\"isFast\":" << js_bool(skill.is_fast) << ","
        << "\"specialization\":" << skill.specialization << "}";
    return out.str();
}

std::string skill_values_literal(const std::vector<JsGameSkillValueFixture> &skills) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < skills.size(); ++index) {
        if (index > 0)
            out << ",";
        out << skill_value_literal(skills[index]);
    }
    out << "]";
    return out.str();
}

std::string knowledge_value_literal(const JsGameKnowledgeValueFixture &knowledge) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << knowledge.id << ","
        << "\"name\":" << js_quote(knowledge.name) << ","
        << "\"profession\":" << js_quote(knowledge.profession) << ","
        << "\"level\":" << knowledge.level << ","
        << "\"knowledge\":" << knowledge.knowledge << ","
        << "\"minimumPosition\":" << knowledge.minimum_position << ","
        << "\"manaCost\":" << knowledge.mana_cost << ","
        << "\"beats\":" << knowledge.beats << ","
        << "\"targets\":" << knowledge.targets << ","
        << "\"learnDifficulty\":" << knowledge.learn_difficulty << ","
        << "\"learnType\":" << knowledge.learn_type << ","
        << "\"isFast\":" << js_bool(knowledge.is_fast) << ","
        << "\"specialization\":" << knowledge.specialization << "}";
    return out.str();
}

std::string knowledge_values_literal(const std::vector<JsGameKnowledgeValueFixture> &knowledge) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < knowledge.size(); ++index) {
        if (index > 0)
            out << ",";
        out << knowledge_value_literal(knowledge[index]);
    }
    out << "]";
    return out.str();
}

std::string affect_literal(const JsGameAffectFixture &affect) {
    std::ostringstream out;
    out << "{"
        << "\"type\":" << affect.type << ","
        << "\"name\":" << js_quote(affect.name) << ","
        << "\"duration\":" << affect.duration << ","
        << "\"timePhase\":" << affect.time_phase << ","
        << "\"modifier\":" << affect.modifier << ","
        << "\"location\":" << affect.location << ","
        << "\"locationName\":" << js_quote(affect.location_name) << ","
        << "\"bitvector\":" << affect.bitvector << ","
        << "\"bitvectorNames\":" << string_array_literal(affect.bitvector_names) << ","
        << "\"counter\":" << affect.counter << "}";
    return out.str();
}

std::string affects_literal(const std::vector<JsGameAffectFixture> &affects) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < affects.size(); ++index) {
        if (index > 0)
            out << ",";
        out << affect_literal(affects[index]);
    }
    out << "]";
    return out.str();
}

std::string object_affect_literal(const JsGameObjectAffectFixture &affect) {
    std::ostringstream out;
    out << "{"
        << "\"slotIndex\":" << affect.slot_index << ","
        << "\"location\":" << affect.location << ","
        << "\"locationName\":" << js_quote(affect.location_name) << ","
        << "\"modifier\":" << affect.modifier << "}";
    return out.str();
}

std::string object_affects_literal(const std::vector<JsGameObjectAffectFixture> &affects) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < affects.size(); ++index) {
        if (index > 0)
            out << ",";
        out << object_affect_literal(affects[index]);
    }
    out << "]";
    return out.str();
}

std::string equipment_slots_literal(const std::vector<JsGameEquipmentSlotFixture> &equipment);
std::string inventory_objects_literal(const std::vector<JsGameEquipmentObjectFixture> &inventory);
std::string character_reference_literal(const JsGameCharacterReferenceFixture &character);
std::string
character_references_literal(const std::vector<JsGameCharacterReferenceFixture> &characters);

std::string mount_literal(const JsGameMountFixture &mount) {
    std::ostringstream out;
    out << "{"
        << "\"mount\":"
        << nullable_literal(mount.has_mount, character_reference_literal(mount.mount)) << ","
        << "\"rider\":"
        << nullable_literal(mount.has_rider, character_reference_literal(mount.rider)) << ","
        << "\"nextRider\":"
        << nullable_literal(mount.has_next_rider, character_reference_literal(mount.next_rider))
        << ","
        << "\"isRiding\":" << js_bool(mount.is_riding) << ","
        << "\"isMounted\":" << js_bool(mount.is_mounted) << ","
        << "\"__rotsReadOnlySnapshot\":true"
        << "}";
    return out.str();
}

std::string character_profile_literal(const JsGameCharacterProfileFixture &profile) {
    std::ostringstream out;
    out << "{"
        << "\"name\":" << js_quote(profile.name) << ","
        << "\"shortDescription\":" << js_quote(profile.short_description) << ","
        << "\"longDescription\":"
        << nullable_literal(profile.has_long_description, js_quote(profile.long_description)) << ","
        << "\"description\":"
        << nullable_literal(profile.has_description, js_quote(profile.description)) << ","
        << "\"title\":" << nullable_literal(profile.has_title, js_quote(profile.title)) << ","
        << "\"deathCry\":" << nullable_literal(profile.has_death_cry, js_quote(profile.death_cry))
        << ","
        << "\"deathCry2\":"
        << nullable_literal(profile.has_death_cry2, js_quote(profile.death_cry2)) << ","
        << "\"corpseNumber\":" << profile.corpse_number << ","
        << "\"raceId\":" << profile.race_id << ","
        << "\"sex\":" << profile.sex << ","
        << "\"bodyType\":" << profile.body_type << ","
        << "\"profession\":" << profile.profession << ","
        << "\"level\":" << profile.level << ","
        << "\"language\":" << profile.language << ","
        << "\"hometown\":" << profile.hometown << ","
        << "\"birthEpochSeconds\":" << profile.birth_epoch_seconds << ","
        << "\"logonEpochSeconds\":" << profile.logon_epoch_seconds << ","
        << "\"playedSeconds\":" << profile.played_seconds << ","
        << "\"weight\":" << profile.weight << ","
        << "\"height\":" << profile.height << ","
        << "\"ranking\":" << profile.ranking << ","
        << "\"talks\":" << int_array_literal(profile.talks) << "}";
    return out.str();
}

std::string character_literal(const JsGameCharacterFixture &character) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(character.id) << ","
        << "\"name\":" << js_quote(character.name) << ","
        << "\"race\":" << js_quote(character.race) << ","
        << "\"vnum\":";
    if (character.vnum >= 0)
        out << character.vnum;
    else
        out << "null";
    out << ","
        << "\"prototypeVnum\":";
    if (character.prototype_vnum >= 0)
        out << character.prototype_vnum;
    else
        out << "null";
    out << ","
        << "\"level\":" << character.level << ","
        << "\"experience\":" << character.experience << ","
        << "\"rank\":" << character.rank << ","
        << "\"hitPoints\":" << character.hit_points << ","
        << "\"maxHitPoints\":" << character.max_hit_points << ","
        << "\"classPoints\":" << character.class_points << ","
        << "\"interruptCount\":" << character.interrupt_count << ","
        << "\"interruptTime\":" << character.interrupt_time << ","
        << "\"specialBusy\":" << js_bool(character.special_busy) << ","
        << "\"profile\":" << character_profile_literal(character.profile) << ","
        << "\"baseAbilities\":" << ability_scores_literal(character.base_abilities) << ","
        << "\"currentAbilities\":" << ability_scores_literal(character.current_abilities) << ","
        << "\"rolledAbilities\":" << ability_scores_literal(character.rolled_abilities) << ","
        << "\"points\":" << character_points_literal(character.points) << ","
        << "\"specials\":" << character_specials_literal(character.specials) << ","
        << "\"specials2\":" << character_specials2_literal(character.specials2) << ","
        << "\"professions\":" << professions_literal(character.professions) << ","
        << "\"specializations\":" << specializations_literal(character.specializations) << ","
        << "\"damageDetails\":" << damage_details_literal(character.damage_details) << ","
        << "\"skills\":" << skill_values_literal(character.skills) << ","
        << "\"knowledge\":" << knowledge_values_literal(character.knowledge) << ","
        << "\"affects\":" << affects_literal(character.affects) << ","
        << "\"equipment\":" << equipment_slots_literal(character.equipment) << ","
        << "\"inventory\":" << inventory_objects_literal(character.inventory) << ","
        << "\"followers\":" << character_references_literal(character.followers) << ","
        << "\"master\":"
        << nullable_literal(character.has_master, character_reference_literal(character.master))
        << ","
        << "\"mount\":" << mount_literal(character.mount) << ","
        << "\"isNpc\":" << js_bool(character.is_npc) << ","
        << "\"isPlayer\":" << js_bool(!character.is_npc) << ","
        << "\"room\":" << nullable_literal(character.has_room, room_literal(character.room)) << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string object_flags_literal(const JsGameObjectFlagsFixture &flags) {
    std::ostringstream out;
    out << "{"
        << "\"itemType\":" << js_quote(flags.item_type) << ","
        << "\"wearFlags\":" << string_array_literal(flags.wear_flags) << ","
        << "\"extraFlags\":" << string_array_literal(flags.extra_flags) << ","
        << "\"level\":" << flags.level << ","
        << "\"weight\":" << flags.weight << ","
        << "\"cost\":" << flags.cost << ","
        << "\"costPerDay\":" << flags.cost_per_day << ","
        << "\"timer\":" << flags.timer << ","
        << "\"rarity\":" << flags.rarity << ","
        << "\"material\":" << js_quote(flags.material) << "}";
    return out.str();
}

std::string equipment_object_literal(const JsGameEquipmentObjectFixture &object) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(object.id) << ","
        << "\"name\":" << js_quote(object.name) << ","
        << "\"description\":" << js_quote(object.description) << ","
        << "\"shortDescription\":" << js_quote(object.short_description) << ","
        << "\"actionDescription\":"
        << nullable_literal(object.has_action_description, js_quote(object.action_description))
        << ","
        << "\"vnum\":";
    if (object.vnum >= 0)
        out << object.vnum;
    else
        out << "null";
    out << ","
        << "\"flags\":" << object_flags_literal(object.flags) << ","
        << "\"affects\":" << object_affects_literal(object.affects) << ","
        << "\"room\":" << nullable_literal(object.has_room, room_literal(object.room)) << ","
        << "\"carriedBy\":null,"
        << "\"wornBy\":null,"
        << "\"__rotsReadOnlySnapshot\":true,"
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string equipment_slot_literal(const JsGameEquipmentSlotFixture &slot) {
    std::ostringstream out;
    out << "{"
        << "\"slotIndex\":" << slot.slot_index << ","
        << "\"slotName\":" << js_quote(slot.slot_name) << ","
        << "\"object\":" << nullable_literal(slot.has_object, equipment_object_literal(slot.object))
        << "}";
    return out.str();
}

std::string equipment_slots_literal(const std::vector<JsGameEquipmentSlotFixture> &equipment) {
    constexpr const char *WearSlotNames[] = {
        "light",     "fingerRight", "fingerLeft", "neck1",     "neck2", "body",
        "head",      "legs",        "feet",       "hands",     "arms",  "shield",
        "aboutBody", "waist",       "wristRight", "wristLeft", "wield", "hold",
        "back",      "belt1",       "belt2",      "belt3"};
    std::ostringstream out;
    out << "[";
    const std::size_t slot_count =
        equipment.empty() ? sizeof(WearSlotNames) / sizeof(WearSlotNames[0]) : equipment.size();
    for (std::size_t index = 0; index < slot_count; ++index) {
        if (index > 0)
            out << ",";
        if (equipment.empty()) {
            JsGameEquipmentSlotFixture empty_slot;
            empty_slot.slot_index = static_cast<int>(index);
            empty_slot.slot_name = WearSlotNames[index];
            out << equipment_slot_literal(empty_slot);
        } else {
            out << equipment_slot_literal(equipment[index]);
        }
    }
    out << "]";
    return out.str();
}

std::string inventory_objects_literal(const std::vector<JsGameEquipmentObjectFixture> &inventory) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < inventory.size(); ++index) {
        if (index > 0)
            out << ",";
        out << equipment_object_literal(inventory[index]);
    }
    out << "]";
    return out.str();
}

std::string character_reference_literal(const JsGameCharacterReferenceFixture &character) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(character.id) << ","
        << "\"name\":" << js_quote(character.name) << ","
        << "\"race\":" << js_quote(character.race) << ","
        << "\"vnum\":";
    if (character.vnum >= 0)
        out << character.vnum;
    else
        out << "null";
    out << ","
        << "\"prototypeVnum\":";
    if (character.prototype_vnum >= 0)
        out << character.prototype_vnum;
    else
        out << "null";
    out << ","
        << "\"level\":" << character.level << ","
        << "\"isNpc\":" << js_bool(character.is_npc) << ","
        << "\"isPlayer\":" << js_bool(!character.is_npc) << ","
        << "\"__rotsReadOnlySnapshot\":true,"
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string
character_references_literal(const std::vector<JsGameCharacterReferenceFixture> &characters) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < characters.size(); ++index) {
        if (index > 0)
            out << ",";
        out << character_reference_literal(characters[index]);
    }
    out << "]";
    return out.str();
}

std::string object_literal(const JsGameObjectFixture &object) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << js_quote(object.id) << ","
        << "\"name\":" << js_quote(object.name) << ","
        << "\"description\":" << js_quote(object.description) << ","
        << "\"shortDescription\":" << js_quote(object.short_description) << ","
        << "\"actionDescription\":"
        << nullable_literal(object.has_action_description, js_quote(object.action_description))
        << ","
        << "\"vnum\":";
    if (object.vnum >= 0)
        out << object.vnum;
    else
        out << "null";
    out << ","
        << "\"flags\":" << object_flags_literal(object.flags) << ","
        << "\"affects\":" << object_affects_literal(object.affects) << ","
        << "\"room\":" << nullable_literal(object.has_room, room_literal(object.room)) << ","
        << "\"carriedBy\":"
        << nullable_literal(object.has_carried_by, character_literal(object.carried_by)) << ","
        << "\"wornBy\":" << nullable_literal(object.has_worn_by, character_literal(object.worn_by))
        << ","
        << "\"isValid\":function() { return true; }"
        << "}";
    return out.str();
}

std::string trigger_literal(const JsGameTriggerFixture &trigger) {
    const char *trigger_kind = trigger.legacy_name.rfind("SPECIAL_", 0) == 0 ? "mudlle" : "legacy";
    std::ostringstream out;
    out << "{"
        << "\"kind\":" << js_quote(trigger_kind) << ","
        << "\"name\":" << js_quote(trigger.name) << ","
        << "\"handlerName\":" << js_quote(trigger.name) << ","
        << "\"legacyName\":" << js_quote(trigger.legacy_name) << ","
        << "\"hostType\":" << js_quote(trigger.host_type) << ","
        << "\"legacyValue\":" << trigger.legacy_value << ","
        << "\"blocksGameplay\":" << js_bool(trigger.blocks_gameplay) << "}";
    return out.str();
}

std::string target_literal(const JsGameTargetFixture &target) {
    if (target.has_character)
        return character_literal(target.character);
    if (target.has_object)
        return object_literal(target.object);
    if (target.has_room)
        return room_literal(target.room);
    return "null";
}

std::string target_types_literal(const std::vector<std::string> &target_types) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < target_types.size(); ++index) {
        if (index > 0)
            out << ",";
        out << js_quote(target_types[index]);
    }
    out << "]";
    return out.str();
}

bool source_has_unsafe_wrapper_boundary(const std::string &source) {
    int brace_depth = 0;
    int paren_depth = 0;
    int bracket_depth = 0;
    char string_quote = '\0';
    bool escaped = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < source.size(); ++i) {
        char ch = source[i];
        char next = i + 1 < source.size() ? source[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n' || ch == '\r')
                in_line_comment = false;
            continue;
        }

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }

        if (string_quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == string_quote) {
                string_quote = '\0';
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }

        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }

        if (ch == '\'' || ch == '"' || ch == '`') {
            string_quote = ch;
            continue;
        }

        if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}') {
            if (brace_depth == 0)
                return true;
            --brace_depth;
        } else if (ch == '(') {
            ++paren_depth;
        } else if (ch == ')') {
            if (paren_depth == 0)
                return true;
            --paren_depth;
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']') {
            if (bracket_depth == 0)
                return true;
            --bracket_depth;
        }
    }

    return string_quote != '\0' || in_block_comment || brace_depth != 0 || paren_depth != 0 ||
           bracket_depth != 0;
}

bool is_identifier_start(unsigned char ch) { return std::isalpha(ch) || ch == '_' || ch == '$'; }

bool is_identifier_continue(unsigned char ch) {
    return is_identifier_start(ch) || std::isdigit(ch);
}

bool is_safe_handler_identifier(const std::string &handler_name) {
    if (handler_name.empty())
        return false;
    if (!is_identifier_start(static_cast<unsigned char>(handler_name[0])))
        return false;
    for (std::size_t index = 1; index < handler_name.size(); ++index) {
        if (!is_identifier_continue(static_cast<unsigned char>(handler_name[index])))
            return false;
    }
    return true;
}

bool parse_mutation(JsonReader *reader, JsRuntimeMutation *mutation, std::string *error_message) {
    if (reader == nullptr || mutation == nullptr)
        return false;

    std::string value_kind;
    return reader->parse_object(
               [&](const std::string &name, JsonReader *nested_reader, std::string *nested_error) {
                   if (name == "targetType")
                       return nested_reader->parse_string(&mutation->target_type, nested_error);
                   if (name == "targetId")
                       return nested_reader->parse_string(&mutation->target_id, nested_error);
                   if (name == "property")
                       return nested_reader->parse_string(&mutation->property, nested_error);
                   if (name == "valueKind")
                       return nested_reader->parse_string(&value_kind, nested_error);
                   if (name == "value")
                       return nested_reader->parse_string(&mutation->value, nested_error);
                   return nested_reader->skip_value(nested_error);
               },
               error_message) &&
           (value_kind == "string" || value_kind == "null" || value_kind == "number") &&
           (mutation->value_kind = value_kind, mutation->has_value = value_kind != "null", true);
}

bool parse_game_envelope(const std::string &envelope, JsRuntimeEvalResult *result) {
    if (result == nullptr)
        return false;

    bool saw_allow = false;
    bool saw_mutations = false;
    std::string parse_error;
    JsonReader reader(envelope);
    const bool parsed = reader.parse_root_object(
        [&](const std::string &name, JsonReader *nested_reader, std::string *error_message) {
            if (name == "allow") {
                bool allow = true;
                if (!nested_reader->parse_bool(&allow, error_message))
                    return false;
                result->value = allow ? JsRuntimeValue::Allow : JsRuntimeValue::Block;
                saw_allow = true;
                return true;
            }
            if (name == "mutations") {
                saw_mutations = true;
                return nested_reader->parse_array(
                    [&](JsonReader *mutation_reader, std::string *mutation_error) {
                        if (result->mutations.size() >= MaxGameMutationCount) {
                            if (mutation_error)
                                *mutation_error = "JavaScript game mutation limit exceeded.";
                            return false;
                        }
                        JsRuntimeMutation mutation;
                        if (!parse_mutation(mutation_reader, &mutation, mutation_error))
                            return false;
                        result->mutations.push_back(std::move(mutation));
                        return true;
                    },
                    error_message);
            }
            return nested_reader->skip_value(error_message);
        },
        &parse_error);

    if (!parsed || !saw_allow || !saw_mutations) {
        result->status = JsRuntimeStatus::Error;
        result->diagnostic = "JavaScript game script returned an invalid internal result";
        result->mutations.clear();
        return false;
    }

    return true;
}

JsRuntimeEvalResult evaluate_game_source(const std::string &source, const JsRuntimeLimits &limits,
                                         const char *filename) {
    JsRuntime runtime(limits);
    JsRuntimeEvalResult result =
        sanitize_game_result(runtime.evaluate_trusted_wrapped_source(source, filename));
    if (result.status != JsRuntimeStatus::Ok)
        return result;
    if (!result.has_string_value) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game script returned an invalid internal result";
        return result;
    }
    parse_game_envelope(result.string_value, &result);
    result.has_string_value = false;
    result.string_value.clear();
    return sanitize_game_result(std::move(result));
}

JsRuntimeEvalResult validate_builder_source_policy(const std::string &source) {
    const std::vector<JsSourcePolicyViolation> violations = js_source_policy_validate(source);
    JsRuntimeEvalResult result;
    if (!violations.empty()) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = violations.front().message;
    } else {
        result.status = JsRuntimeStatus::Ok;
    }
    return result;
}

std::string trigger_context_preamble(const JsGameTriggerContextFixture &context) {
    std::ostringstream wrapped;
    wrapped
        << "'use strict';\n"
        << "Object.defineProperty(Object.prototype, 'constructor', { value: undefined, "
           "writable: false, configurable: false });\n"
        << "const __rotsFunctionPrototype = Object.getPrototypeOf(function() {});\n"
        << "Object.defineProperty(__rotsFunctionPrototype, 'constructor', { value: "
           "undefined, writable: false, configurable: false });\n"
        << "const __rotsAsyncFunctionPrototype = Object.getPrototypeOf(async function () {});\n"
        << "Object.defineProperty(__rotsAsyncFunctionPrototype, 'constructor', { value: "
           "undefined, writable: false, configurable: false });\n"
        << "const __rotsGeneratorFunctionPrototype = Object.getPrototypeOf(function* () {});\n"
        << "Object.defineProperty(__rotsGeneratorFunctionPrototype, 'constructor', { value: "
           "undefined, writable: false, configurable: false });\n"
        << "const __rotsAsyncGeneratorFunctionPrototype = Object.getPrototypeOf(async function* () "
           "{});\n"
        << "Object.defineProperty(__rotsAsyncGeneratorFunctionPrototype, 'constructor', { value: "
           "undefined, writable: false, configurable: false });\n"
        << "Object.defineProperty(Array.prototype, 'constructor', { value: undefined, "
           "writable: false, configurable: false });\n"
        << "Object.freeze(Object.prototype);\n"
        << "Object.freeze(Array.prototype);\n"
        << "Object.freeze(__rotsFunctionPrototype);\n"
        << "Object.freeze(__rotsAsyncFunctionPrototype);\n"
        << "Object.freeze(__rotsGeneratorFunctionPrototype);\n"
        << "Object.freeze(__rotsAsyncGeneratorFunctionPrototype);\n"
        << "const __rotsNumberIsInteger = Number.isInteger;\n"
        << "const __rotsString = String;\n"
        << "const __rotsJsonStringify = JSON.stringify;\n"
        << "Object.freeze(Number);\n"
        << "Object.freeze(JSON);\n"
        << "const __rotsMutations = [];\n"
        << "function __rotsDeepFreeze(value) {\n"
        << "  if (value && (typeof value === 'object' || typeof value === 'function') && "
           "!Object.isFrozen(value)) {\n"
        << "    if (value.__rotsReadOnlySnapshot === true) delete value.__rotsReadOnlySnapshot;\n"
        << "    if (typeof value === 'object' && !Array.isArray(value)) "
           "Object.setPrototypeOf(value, null);\n"
        << "    Object.freeze(value);\n"
        << "    for (const key of Object.keys(value)) __rotsDeepFreeze(value[key]);\n"
        << "  }\n"
        << "  return value;\n"
        << "}\n"
        << "function __rotsMutationResult(ok, code, message, field) {\n"
        << "  const result = { ok: ok, code: code, message: message, field: field };\n"
        << "  Object.setPrototypeOf(result, null);\n"
        << "  return Object.freeze(result);\n"
        << "}\n"
        << "function __rotsValidateTextSetter(value, field, maxLength, nullable) {\n"
        << "  if (value === null && nullable) return __rotsMutationResult(true, 'ok', null, "
           "field);\n"
        << "  if (typeof value !== 'string') return __rotsMutationResult(false, 'invalid-value', "
           "'Expected text value.', field);\n"
        << "  if (field === 'name' && value.trim().length === 0) return "
           "__rotsMutationResult(false, 'invalid-value', 'Name must not be blank.', field);\n"
        << "  if (value.indexOf('\\u0000') !== -1) return __rotsMutationResult(false, "
           "'invalid-value', 'Text contains unsupported characters.', field);\n"
        << "  if (field === 'map' && (value.indexOf('~') !== -1 || "
           "/(^|[\\r\\n])\\s*#/.test(value))) return __rotsMutationResult(false, 'invalid-value', "
           "'Text contains unsupported characters.', field);\n"
        << "  if (value.length > maxLength) return __rotsMutationResult(false, 'out-of-range', "
           "'Text is too long.', field);\n"
        << "  return __rotsMutationResult(true, 'ok', null, field);\n"
        << "}\n"
        << "function __rotsValidateSymbolSetter(value) {\n"
        << "  if (typeof value !== 'string') return __rotsMutationResult(false, 'invalid-value', "
           "'Expected text value.', 'symbol');\n"
        << "  if (value.length !== 1) return __rotsMutationResult(false, 'invalid-value', 'Symbol "
           "must be one character.', 'symbol');\n"
        << "  const code = value.charCodeAt(0);\n"
        << "  if (code <= 32 || code >= 127) return __rotsMutationResult(false, 'invalid-value', "
           "'Symbol contains unsupported characters.', 'symbol');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'symbol');\n"
        << "}\n"
        << "function __rotsValidateCoordinateSetter(value, field) {\n"
        << "  if (typeof value !== 'number' || !__rotsNumberIsInteger(value)) return "
           "__rotsMutationResult(false, 'invalid-value', 'Expected integer coordinate.', field);\n"
        << "  if (value < 0 || value > 25) return __rotsMutationResult(false, 'out-of-range', "
           "'Coordinate is outside the supported map range.', field);\n"
        << "  return __rotsMutationResult(true, 'ok', null, field);\n"
        << "}\n"
        << "function __rotsValidateResetModeSetter(value) {\n"
        << "  if (typeof value !== 'number' || !__rotsNumberIsInteger(value)) return "
           "__rotsMutationResult(false, 'invalid-value', 'Expected integer reset mode.', "
           "'resetMode');\n"
        << "  if (value < 0 || value > 3) return __rotsMutationResult(false, 'out-of-range', "
           "'Reset mode is outside the supported range.', 'resetMode');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'resetMode');\n"
        << "}\n"
        << "function __rotsValidateLifespanSetter(value) {\n"
        << "  if (typeof value !== 'number' || !__rotsNumberIsInteger(value)) return "
           "__rotsMutationResult(false, 'invalid-value', 'Expected integer lifespan.', "
           "'lifespan');\n"
        << "  if (value < 1 || value > 10080) return __rotsMutationResult(false, 'out-of-range', "
           "'Lifespan is outside the supported range.', 'lifespan');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'lifespan');\n"
        << "}\n"
        << "function __rotsValidateLevelSetter(value) {\n"
        << "  if (typeof value !== 'number' || !__rotsNumberIsInteger(value)) return "
           "__rotsMutationResult(false, 'invalid-value', 'Expected integer level.', 'level');\n"
        << "  if (value < 0 || value > 100) return __rotsMutationResult(false, 'out-of-range', "
           "'Level is outside the supported range.', 'level');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'level');\n"
        << "}\n"
        << "function __rotsValidateRaritySetter(value) {\n"
        << "  if (typeof value !== 'number' || !__rotsNumberIsInteger(value)) return "
           "__rotsMutationResult(false, 'invalid-value', 'Expected integer rarity.', 'rarity');\n"
        << "  if (value < 0 || value > 255) return __rotsMutationResult(false, 'out-of-range', "
           "'Rarity is outside the supported range.', 'rarity');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'rarity');\n"
        << "}\n"
        << "function __rotsValidateSectorTypeSetter(value) {\n"
        << "  const sectors = "
           "['Floor','City','Field','Forest','Hills','Mountain','Water','Water_noswim','Underwater'"
           ",'Road','Crack','Dense_forest','Swamp'];\n"
        << "  if (typeof value !== 'string') return __rotsMutationResult(false, 'invalid-value', "
           "'Expected sector type name.', 'sectorType');\n"
        << "  if (sectors.indexOf(value) === -1) return __rotsMutationResult(false, "
           "'invalid-value', 'Sector type must be a canonical live sector name.', 'sectorType');\n"
        << "  return __rotsMutationResult(true, 'ok', null, 'sectorType');\n"
        << "}\n"
        << "function __rotsAttachTextSetter(handle, targetType, property, setterName, maxLength, "
           "nullable) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle[property];\n"
        << "  Object.defineProperty(handle, property, {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, setterName, {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateTextSetter(value, property, maxLength, nullable);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: property, valueKind: value === null ? 'null' : "
           "'string', value: value === null ? '' : value });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachSymbolSetter(handle, targetType) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle.symbol;\n"
        << "  Object.defineProperty(handle, 'symbol', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setSymbol', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateSymbolSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: 'symbol', valueKind: 'string', value: value });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachCoordinateSetter(handle, targetType, property, setterName) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle[property];\n"
        << "  Object.defineProperty(handle, property, {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, setterName, {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateCoordinateSetter(value, property);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: property, valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachResetModeSetter(handle, targetType) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle.resetMode;\n"
        << "  Object.defineProperty(handle, 'resetMode', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setResetMode', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateResetModeSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: 'resetMode', valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachLifespanSetter(handle, targetType) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle.lifespan;\n"
        << "  Object.defineProperty(handle, 'lifespan', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setLifespan', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateLifespanSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: 'lifespan', valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachLevelSetter(handle, targetType) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle.level;\n"
        << "  Object.defineProperty(handle, 'level', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setLevel', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateLevelSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: targetType, targetId: "
           "__rotsString(handle.id), property: 'level', valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachObjectLevelSetter(handle) {\n"
        << "  if (!handle || typeof handle !== 'object' || !handle.flags || typeof handle.flags "
           "!== 'object') return;\n"
        << "  let current = handle.flags.level;\n"
        << "  Object.defineProperty(handle.flags, 'level', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setLevel', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateLevelSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: 'object', targetId: "
           "__rotsString(handle.id), property: 'level', valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachObjectRaritySetter(handle) {\n"
        << "  if (!handle || typeof handle !== 'object' || !handle.flags || typeof handle.flags "
           "!== 'object') return;\n"
        << "  let current = handle.flags.rarity;\n"
        << "  Object.defineProperty(handle.flags, 'rarity', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setRarity', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateRaritySetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: 'object', targetId: "
           "__rotsString(handle.id), property: 'rarity', valueKind: 'number', value: "
           "__rotsString(value) });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachSectorTypeSetter(handle) {\n"
        << "  if (!handle || typeof handle !== 'object') return;\n"
        << "  let current = handle.sectorType;\n"
        << "  Object.defineProperty(handle, 'sectorType', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    get: function() { return current; }\n"
        << "  });\n"
        << "  Object.defineProperty(handle, 'setSectorType', {\n"
        << "    enumerable: true,\n"
        << "    configurable: true,\n"
        << "    writable: false,\n"
        << "    value: (value) => {\n"
        << "      const result = __rotsValidateSectorTypeSetter(value);\n"
        << "      if (result.ok) {\n"
        << "        current = value;\n"
        << "        __rotsMutations.push({ targetType: 'room', targetId: __rotsString(handle.id), "
           "property: 'sectorType', valueKind: 'string', value: value });\n"
        << "      }\n"
        << "      return result;\n"
        << "    }\n"
        << "  });\n"
        << "}\n"
        << "function __rotsAttachSetterApi(value, seen) {\n"
        << "  if (!value || typeof value !== 'object') return;\n"
        << "  if (seen.indexOf(value) !== -1) return;\n"
        << "  seen.push(value);\n"
        << "  if (value.__rotsReadOnlySnapshot === true) return;\n"
        << "  if ('shortDescription' in value || 'actionDescription' in value || 'carriedBy' in "
           "value) {\n"
        << "    __rotsAttachTextSetter(value, 'object', 'name', 'setName', 256, false);\n"
        << "    __rotsAttachTextSetter(value, 'object', 'description', 'setDescription', 8192, "
           "false);\n"
        << "    __rotsAttachTextSetter(value, 'object', 'shortDescription', 'setShortDescription', "
           "8192, false);\n"
        << "    __rotsAttachTextSetter(value, 'object', 'actionDescription', "
           "'setActionDescription', 8192, true);\n"
        << "    __rotsAttachObjectLevelSetter(value);\n"
        << "    __rotsAttachObjectRaritySetter(value);\n"
        << "  }\n"
        << "  if ('sectorType' in value || 'isSunlit' in value) {\n"
        << "    __rotsAttachTextSetter(value, 'room', 'name', 'setName', 256, false);\n"
        << "    __rotsAttachTextSetter(value, 'room', 'description', 'setDescription', 8192, "
           "false);\n"
        << "    __rotsAttachLevelSetter(value, 'room');\n"
        << "    __rotsAttachSectorTypeSetter(value);\n"
        << "  }\n"
        << "  if ('topRoomVnum' in value || 'resetMode' in value) {\n"
        << "    __rotsAttachTextSetter(value, 'zone', 'name', 'setName', 256, false);\n"
        << "    __rotsAttachTextSetter(value, 'zone', 'description', 'setDescription', 8192, "
           "true);\n"
        << "    __rotsAttachTextSetter(value, 'zone', 'map', 'setMap', 8192, true);\n"
        << "    __rotsAttachSymbolSetter(value, 'zone');\n"
        << "    __rotsAttachCoordinateSetter(value, 'zone', 'x', 'setX');\n"
        << "    __rotsAttachCoordinateSetter(value, 'zone', 'y', 'setY');\n"
        << "    __rotsAttachResetModeSetter(value, 'zone');\n"
        << "    __rotsAttachLifespanSetter(value, 'zone');\n"
        << "    __rotsAttachLevelSetter(value, 'zone');\n"
        << "  }\n"
        << "  for (const key of Object.keys(value)) __rotsAttachSetterApi(value[key], seen);\n"
        << "}\n"
        << "const console = __rotsDeepFreeze({ log: function() { return undefined; } });\n"
        << "const RotS = __rotsDeepFreeze({\n"
        << "  ScriptResult: {\n"
        << "    allow: function() { return true; },\n"
        << "    block: function() { return false; }\n"
        << "  }\n"
        << "});\n"
        << "const ctx = " << js_game_trigger_context_literal(context) << ";\n"
        << "__rotsAttachSetterApi(ctx, []);\n"
        << "__rotsDeepFreeze(ctx);\n";
    return wrapped.str();
}

JsRuntimeEvalResult sanitize_game_result(JsRuntimeEvalResult result) {
    if (result.status == JsRuntimeStatus::Error &&
        result.diagnostic.size() > MaxGameDiagnosticLength)
        result.diagnostic.resize(MaxGameDiagnosticLength);
    if (result.status == JsRuntimeStatus::Error && result.diagnostic.find("TypeError:") != 0 &&
        result.diagnostic.find("SyntaxError:") != 0 &&
        result.diagnostic.find("compiled JavaScript ") != 0) {
        result.diagnostic = "JavaScript game script failed";
    }
    return result;
}

} // namespace

JsGameRuntime::JsGameRuntime(const JsRuntimeLimits &limits) : m_limits(limits) {}

JsRuntimeEvalResult JsGameRuntime::evaluate_trigger_body(const std::string &source,
                                                         const JsGameTriggerContextFixture &context,
                                                         const char *filename) {
    if (source_has_unsafe_wrapper_boundary(source)) {
        JsRuntimeEvalResult result;
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game script body is not structurally valid";
        return result;
    }
    JsRuntimeEvalResult policy_result = validate_builder_source_policy(source);
    if (policy_result.status == JsRuntimeStatus::Error)
        return sanitize_game_result(policy_result);

    std::ostringstream wrapped;
    wrapped
        << trigger_context_preamble(context)
        << "const __rotsReturn = (function(ctx, __rotsMutations, __rotsAttachTextSetter, "
           "__rotsAttachSymbolSetter, __rotsAttachCoordinateSetter, __rotsAttachResetModeSetter, "
           "__rotsAttachLifespanSetter, __rotsAttachLevelSetter, __rotsAttachObjectLevelSetter, "
           "__rotsAttachObjectRaritySetter, __rotsAttachSectorTypeSetter, "
           "__rotsAttachSetterApi, __rotsValidateTextSetter, "
           "__rotsValidateSymbolSetter, "
           "__rotsValidateCoordinateSetter, __rotsValidateResetModeSetter, "
           "__rotsValidateLifespanSetter, __rotsValidateLevelSetter, __rotsValidateRaritySetter, "
           "__rotsValidateSectorTypeSetter, "
           "__rotsMutationResult, __rotsDeepFreeze, __rotsJsonStringify, __rotsNumberIsInteger, "
           "__rotsString) {\n"
        << "  'use strict';\n"
        << source << "\n"
        << "})(ctx, undefined, undefined, undefined, undefined, undefined, undefined, undefined, "
           "undefined, undefined, undefined, undefined, undefined, undefined, undefined, "
           "undefined, undefined, undefined, undefined, undefined, undefined);\n"
        << "__rotsJsonStringify.call(JSON, { allow: __rotsReturn === undefined || "
           "!!__rotsReturn, mutations: "
           "__rotsMutations });";

    return evaluate_game_source(wrapped.str(), m_limits, filename);
}

JsRuntimeEvalResult JsGameRuntime::evaluate_trigger_package_handler(
    const std::string &package_source, const std::string &handler_name,
    const JsGameTriggerContextFixture &context, const char *filename) {
    if (!is_safe_handler_identifier(handler_name)) {
        JsRuntimeEvalResult result;
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript game handler name is not a safe identifier";
        return result;
    }
    JsRuntimeEvalResult policy_result = validate_builder_source_policy(package_source);
    if (policy_result.status == JsRuntimeStatus::Error)
        return sanitize_game_result(policy_result);

    std::ostringstream wrapped;
    wrapped
        << trigger_context_preamble(context) << "const exports = Object.create(null);\n"
        << "const __rotsPackage = (function(exports, __rotsMutations, __rotsAttachTextSetter, "
           "__rotsAttachSymbolSetter, __rotsAttachCoordinateSetter, __rotsAttachResetModeSetter, "
           "__rotsAttachLifespanSetter, __rotsAttachLevelSetter, __rotsAttachObjectLevelSetter, "
           "__rotsAttachObjectRaritySetter, __rotsAttachSectorTypeSetter, "
           "__rotsAttachSetterApi, __rotsValidateTextSetter, "
           "__rotsValidateSymbolSetter, "
           "__rotsValidateCoordinateSetter, __rotsValidateResetModeSetter, "
           "__rotsValidateLifespanSetter, __rotsValidateLevelSetter, __rotsValidateRaritySetter, "
           "__rotsValidateSectorTypeSetter, "
           "__rotsMutationResult, __rotsDeepFreeze, __rotsJsonStringify, __rotsNumberIsInteger, "
           "__rotsString) {\n"
        << "  'use strict';\n"
        << package_source << "\n"
        << "  return { fallback: typeof " << handler_name << " === 'function' ? " << handler_name
        << " : undefined };\n"
        << "})(exports, undefined, undefined, undefined, undefined, undefined, undefined, "
           "undefined, "
           "undefined, undefined, undefined, undefined, undefined, undefined, undefined, "
           "undefined, undefined, undefined, undefined, undefined, undefined);\n"
        << "const __rotsHandler = typeof exports." << handler_name << " === 'function' ? exports."
        << handler_name << "\n"
        << "  : __rotsPackage.fallback;\n"
        << "if (typeof __rotsHandler !== 'function') throw new TypeError('JavaScript game handler "
           "is not callable');\n"
        << "const __rotsReturn = __rotsHandler(ctx);\n"
        << "__rotsJsonStringify.call(JSON, { allow: __rotsReturn === undefined || "
           "!!__rotsReturn, mutations: "
           "__rotsMutations });";

    return evaluate_game_source(wrapped.str(), m_limits, filename);
}

std::string js_game_trigger_context_literal(const JsGameTriggerContextFixture &context) {
    std::ostringstream out;
    out << "{"
        << "\"self\":" << nullable_literal(context.has_self, character_literal(context.self)) << ","
        << "\"actor\":" << nullable_literal(context.has_actor, character_literal(context.actor))
        << ","
        << "\"speaker\":"
        << nullable_literal(context.has_speaker, character_literal(context.speaker)) << ","
        << "\"attacker\":"
        << nullable_literal(context.has_attacker, character_literal(context.attacker)) << ","
        << "\"victim\":" << nullable_literal(context.has_victim, character_literal(context.victim))
        << ","
        << "\"killer\":" << nullable_literal(context.has_killer, character_literal(context.killer))
        << ","
        << "\"object\":" << nullable_literal(context.has_object, object_literal(context.object))
        << ","
        << "\"weapon\":" << nullable_literal(context.has_weapon, object_literal(context.weapon))
        << ","
        << "\"room\":" << nullable_literal(context.has_room, room_literal(context.room)) << ","
        << "\"zone\":" << nullable_literal(context.has_zone, zone_literal(context.zone)) << ","
        << "\"text\":" << nullable_literal(context.has_text, js_quote(context.text)) << ","
        << "\"wearSlot\":" << nullable_literal(context.has_wear_slot, js_quote(context.wear_slot))
        << ","
        << "\"command\":" << nullable_literal(context.has_command, js_quote(context.command)) << ","
        << "\"args\":" << nullable_literal(context.has_args, js_quote(context.args)) << ","
        << "\"target\":" << nullable_literal(context.has_target, target_literal(context.target))
        << ","
        << "\"tick\":";
    if (context.has_tick)
        out << context.tick;
    else
        out << "null";
    out << ","
        << "\"direction\":" << nullable_literal(context.has_direction, js_quote(context.direction))
        << ","
        << "\"reverseDirection\":"
        << nullable_literal(context.has_reverse_direction, js_quote(context.reverse_direction))
        << ","
        << "\"targ1\":" << nullable_literal(context.has_targ1, target_literal(context.targ1)) << ","
        << "\"targ2\":" << nullable_literal(context.has_targ2, target_literal(context.targ2)) << ","
        << "\"targetTypes\":" << target_types_literal(context.target_types) << ","
        << "\"dying\":" << nullable_literal(context.has_dying, character_literal(context.dying))
        << ","
        << "\"hostType\":" << js_quote(context.trigger.host_type) << ","
        << "\"trigger\":" << trigger_literal(context.trigger) << "}";
    return out.str();
}
