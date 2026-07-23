#include "../js_api_struct_mapping.h"
#include "../js_game_runtime.h"
#include "../spells.h"
#include "../structs.h"

#include <gtest/gtest.h>

extern char *sector_types[];
extern char num_of_sector_types;

namespace {

JsGameDamageEntryFixture make_damage_entry(int source_id, const std::string &source_kind,
                                           const std::string &source_name, int instance_count,
                                           int total_damage, int largest_damage,
                                           double average_damage, double percent_of_total) {
    JsGameDamageEntryFixture entry;
    entry.source_id = source_id;
    entry.source_kind = source_kind;
    entry.source_name = source_name;
    entry.instance_count = instance_count;
    entry.total_damage = total_damage;
    entry.largest_damage = largest_damage;
    entry.average_damage = average_damage;
    entry.percent_of_total = percent_of_total;
    return entry;
}

JsGameSkillValueFixture make_skill_value(int id, const std::string &name,
                                         const std::string &profession, int level, int practice,
                                         int minimum_position, int mana_cost, int beats,
                                         int targets, int learn_difficulty, int learn_type,
                                         bool is_fast, int specialization) {
    JsGameSkillValueFixture skill;
    skill.id = id;
    skill.name = name;
    skill.profession = profession;
    skill.level = level;
    skill.practice = practice;
    skill.minimum_position = minimum_position;
    skill.mana_cost = mana_cost;
    skill.beats = beats;
    skill.targets = targets;
    skill.learn_difficulty = learn_difficulty;
    skill.learn_type = learn_type;
    skill.is_fast = is_fast;
    skill.specialization = specialization;
    return skill;
}

JsGameKnowledgeValueFixture make_knowledge_value(int id, const std::string &name,
                                                 const std::string &profession, int level,
                                                 int knowledge, int minimum_position, int mana_cost,
                                                 int beats, int targets, int learn_difficulty,
                                                 int learn_type, bool is_fast, int specialization) {
    JsGameKnowledgeValueFixture value;
    value.id = id;
    value.name = name;
    value.profession = profession;
    value.level = level;
    value.knowledge = knowledge;
    value.minimum_position = minimum_position;
    value.mana_cost = mana_cost;
    value.beats = beats;
    value.targets = targets;
    value.learn_difficulty = learn_difficulty;
    value.learn_type = learn_type;
    value.is_fast = is_fast;
    value.specialization = specialization;
    return value;
}

JsGameAffectFixture make_affect(int type, const std::string &name, int duration, int time_phase,
                                int modifier, int location, const std::string &location_name,
                                long bitvector, const std::vector<std::string> &bitvector_names,
                                int counter) {
    JsGameAffectFixture affect;
    affect.type = type;
    affect.name = name;
    affect.duration = duration;
    affect.time_phase = time_phase;
    affect.modifier = modifier;
    affect.location = location;
    affect.location_name = location_name;
    affect.bitvector = bitvector;
    affect.bitvector_names = bitvector_names;
    affect.counter = counter;
    return affect;
}

JsGameObjectAffectFixture make_object_affect(int slot_index, int location,
                                             const std::string &location_name, int modifier) {
    JsGameObjectAffectFixture affect;
    affect.slot_index = slot_index;
    affect.location = location;
    affect.location_name = location_name;
    affect.modifier = modifier;
    return affect;
}

JsGameEquipmentSlotFixture make_equipment_slot(int slot_index, const std::string &slot_name,
                                               bool has_object) {
    JsGameEquipmentSlotFixture slot;
    slot.slot_index = slot_index;
    slot.slot_name = slot_name;
    slot.has_object = has_object;
    if (has_object) {
        slot.object.id = "object:2001";
        slot.object.name = "silver helm";
        slot.object.description = "A polished silver helm.";
        slot.object.short_description = "a silver helm";
        slot.object.has_action_description = true;
        slot.object.action_description = "The helm glints in the light.";
        slot.object.vnum = 2001;
        slot.object.flags.item_type = "armor";
        slot.object.flags.wear_flags = {"take", "head"};
        slot.object.flags.extra_flags = {"glow"};
        slot.object.flags.level = 12;
        slot.object.flags.weight = 7;
        slot.object.flags.cost = 450;
        slot.object.flags.cost_per_day = 15;
        slot.object.flags.timer = 30;
        slot.object.flags.rarity = 2;
        slot.object.flags.material = "metal";
        slot.object.affects.push_back(make_object_affect(0, 2, "DEX", 1));
        slot.object.extra_descriptions.push_back({"crest", "A tiny crest is etched inside."});
        slot.object.touched = true;
    }
    return slot;
}

JsGameEquipmentObjectFixture make_inventory_object(const std::string &id, const std::string &name,
                                                   int vnum) {
    JsGameEquipmentObjectFixture object;
    object.id = id;
    object.name = name;
    object.description = "A carried inventory item.";
    object.short_description = name;
    object.has_action_description = true;
    object.action_description = "The item is ready to use.";
    object.vnum = vnum;
    object.flags.item_type = "light";
    object.flags.wear_flags = {"take"};
    object.flags.extra_flags = {"glow"};
    object.flags.level = 4;
    object.flags.weight = 2;
    object.flags.cost = 25;
    object.flags.cost_per_day = 1;
    object.flags.timer = 0;
    object.flags.rarity = 1;
    object.flags.material = "wood";
    object.affects.push_back(make_object_affect(1, 24, "SPEED", 3));
    object.extra_descriptions.push_back({"grain", "The wood grain is dark and even."});
    object.touched = true;
    return object;
}

JsGameCharacterReferenceFixture make_character_reference(const std::string &id,
                                                         const std::string &name, bool is_npc) {
    JsGameCharacterReferenceFixture character;
    character.id = id;
    character.name = name;
    character.race = is_npc ? "Orc" : "Human";
    character.vnum = is_npc ? 4101 : -1;
    character.prototype_vnum = is_npc ? 4101 : -1;
    character.level = is_npc ? 12 : 92;
    character.is_npc = is_npc;
    return character;
}

void add_gatehouse_exits(JsGameRoomFixture &room) {
    JsGameRoomExitFixture north;
    north.direction_index = 0;
    north.direction = "north";
    north.has_to_room_vnum = true;
    north.to_room_vnum = 1205;
    north.keyword = "gate";
    north.description = "A raised portcullis opens onto the road.";
    north.key_vnum = 3001;
    north.width = 2;
    north.flags = {"door", "closed", "locked", "pickproof"};
    room.exits.push_back(north);

    JsGameRoomExitFixture down;
    down.direction_index = 5;
    down.direction = "down";
    down.has_to_room_vnum = false;
    down.keyword = "trapdoor";
    down.description = "A sealed trapdoor leads nowhere useful.";
    down.key_vnum = -1;
    down.width = 1;
    down.flags = {"noLook", "hidden"};
    room.exits.push_back(down);
}

JsGameRoomContentObjectFixture make_room_content_fixture() {
    JsGameRoomContentObjectFixture object;
    object.id = "object:303";
    object.name = "polished orb";
    object.description = "A polished orb rests on the floor.";
    object.short_description = "a polished orb";
    object.vnum = 303;
    object.flags.item_type = "treasure";
    object.flags.wear_flags = {"take"};
    object.extra_descriptions.push_back({"orb", "The orb reflects the gatehouse arch."});
    object.touched = false;
    return object;
}

JsGameTriggerContextFixture make_context() {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:1001";
    context.self.name = "Aldren";
    context.self.race = "Human";
    context.self.level = 42;
    context.self.experience = 42000;
    context.self.rank = 9;
    context.self.hit_points = 125;
    context.self.max_hit_points = 150;
    context.self.class_points = 6;
    context.self.interrupt_count = 2;
    context.self.interrupt_time = 11;
    context.self.special_busy = true;
    context.self.profile.name = "Aldren";
    context.self.profile.short_description = "Aldren the builder";
    context.self.profile.has_long_description = true;
    context.self.profile.long_description = "Aldren is reviewing the old gate.";
    context.self.profile.has_description = true;
    context.self.profile.description = "A builder with a weathered notebook.";
    context.self.profile.has_title = true;
    context.self.profile.title = "the careful mapper";
    context.self.profile.has_death_cry = true;
    context.self.profile.death_cry = "Aldren drops the notebook!";
    context.self.profile.has_death_cry2 = true;
    context.self.profile.death_cry2 = "A startled shout echoes nearby.";
    context.self.profile.corpse_number = 6100;
    context.self.profile.race_id = 1;
    context.self.profile.sex = 1;
    context.self.profile.body_type = 2;
    context.self.profile.profession = 4;
    context.self.profile.level = 42;
    context.self.profile.language = 17;
    context.self.profile.hometown = 12;
    context.self.profile.birth_epoch_seconds = 1700000000;
    context.self.profile.logon_epoch_seconds = 1700003600;
    context.self.profile.played_seconds = 7200;
    context.self.profile.weight = 180;
    context.self.profile.height = 72;
    context.self.profile.ranking = 9;
    context.self.profile.talks = {10, 20, 30};
    context.self.base_abilities.strength = 16;
    context.self.base_abilities.intelligence = 12;
    context.self.base_abilities.willpower = 14;
    context.self.base_abilities.dexterity = 15;
    context.self.base_abilities.constitution = 13;
    context.self.base_abilities.leadership = 8;
    context.self.current_abilities.strength = 18;
    context.self.current_abilities.intelligence = 13;
    context.self.current_abilities.willpower = 15;
    context.self.current_abilities.dexterity = 17;
    context.self.current_abilities.constitution = 16;
    context.self.current_abilities.leadership = 9;
    context.self.rolled_abilities.strength = 15;
    context.self.rolled_abilities.intelligence = 11;
    context.self.rolled_abilities.willpower = 13;
    context.self.rolled_abilities.dexterity = 14;
    context.self.rolled_abilities.constitution = 12;
    context.self.rolled_abilities.leadership = 7;
    context.self.points.bodypart_hits = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    context.self.points.gold = 123;
    context.self.points.experience = 42000;
    context.self.points.spirit = 33;
    context.self.points.mana_regen = -2;
    context.self.points.health_regen = 4;
    context.self.points.move_regen = 5;
    context.self.points.offense = 17;
    context.self.points.damage = 6;
    context.self.points.energy_regen = 8;
    context.self.points.parry = 19;
    context.self.points.dodge = 21;
    context.self.points.encumbrance = 2;
    context.self.points.willpower = 14;
    context.self.points.spell_penetration = 7;
    context.self.points.spell_power = 9;
    context.self.specials.is_fighting = true;
    context.self.specials.is_hunting = false;
    context.self.specials.has_memory = true;
    context.self.specials.position = "Fighting";
    context.self.specials.default_position = "Standing";
    context.self.specials.carry_weight = 120;
    context.self.specials.worn_weight = 40;
    context.self.specials.encumbrance_weight = 15;
    context.self.specials.carry_items = 6;
    context.self.specials.timer = 2;
    context.self.specials.was_in_room = 1203;
    context.self.specials.energy = 77;
    context.self.specials.current_parry = 12;
    context.self.specials.last_direction = "north";
    context.self.specials.attack_type = 5;
    context.self.specials.script_number = 901;
    context.self.specials.current_bodypart = 3;
    context.self.specials.tactics = "aggressive";
    context.self.specials.prompt_number = 1;
    context.self.specials.prompt_value = 42;
    context.self.specials.home_zone = 12;
    context.self.specials.load_line = 8;
    context.self.specials2.load_room = 3001;
    context.self.specials2.spells_to_learn = 3;
    context.self.specials2.alignment = 250;
    context.self.specials2.act_flags = {"writing", "incognito"};
    context.self.specials2.preference_flags = {"brief", "color", "advancedView"};
    context.self.specials2.wimp_level = 20;
    context.self.specials2.freeze_level = 5;
    context.self.specials2.saving_throw = 7;
    context.self.specials2.raw_perception = 81;
    context.self.specials2.perception = 87;
    context.self.specials2.conditions.drunk = 1;
    context.self.specials2.conditions.full = 15;
    context.self.specials2.conditions.thirst = 19;
    context.self.specials2.mini_level = 2;
    context.self.specials2.max_mini_level = 4;
    context.self.specials2.morale = 33;
    context.self.specials2.rerolls = 2;
    context.self.specials2.leg_encumbrance = 8;
    context.self.specials2.retired_on = 1705000000;
    context.self.specials2.hide_flags = {"hidingWell", "snuckIn"};
    context.self.specials2.tactics = "aggressive";
    context.self.specials2.shooting = "fast";
    context.self.specials2.casting = "slow";
    context.self.specials2.two_handed = true;
    context.self.professions = {
        {"mage", "Mage", 8, 121, 121, 8100},
        {"mystic", "Mystic", 5, 64, 64, 5200},
        {"ranger", "Ranger", 3, 25, 25, 3300},
        {"warrior", "Warrior", 2, 16, 16, 2400},
    };
    context.self.specializations.selected_id = game_types::PS_Cold;
    context.self.specializations.selected_key = "cold";
    context.self.specializations.selected_name = "cold";
    context.self.specializations.current_id = game_types::PS_Cold;
    context.self.specializations.current_key = "cold";
    context.self.specializations.current_name = "cold";
    context.self.specializations.is_mage_specialization = true;
    context.self.specializations.has_runtime_state = true;
    context.self.damage_details.elapsed_combat_seconds = 2.0;
    context.self.damage_details.total_damage = 35;
    context.self.damage_details.damage_per_second = 17.5;
    context.self.damage_details.entries.push_back(
        make_damage_entry(1, "skill", "Kick", 1, 5, 5, 5.0, 14.285714285714286));
    context.self.damage_details.entries.push_back(
        make_damage_entry(TYPE_HIT, "attack", "hit", 2, 30, 20, 15.0, 85.71428571428571));
    context.self.skills.push_back(make_skill_value(1, "Slashing", "warrior", 0, 3,
                                                   POSITION_FIGHTING, 0, 0, 16, 30, 1, false, 0));
    context.self.skills.push_back(make_skill_value(8, "Swimming", "ranger", 2, 2, POSITION_FIGHTING,
                                                   0, 0, 16, 25, 1, false, 0));
    context.self.knowledge.push_back(make_knowledge_value(
        1, "Slashing", "warrior", 0, 40, POSITION_FIGHTING, 0, 0, 16, 30, 1, false, 0));
    context.self.knowledge.push_back(make_knowledge_value(
        8, "Swimming", "ranger", 2, 55, POSITION_FIGHTING, 0, 0, 16, 25, 1, false, 0));
    context.self.affects.push_back(
        make_affect(56, "Sanctuary", 8, 1, 5, 2, "DEX", 128, {"SANCT"}, 6));
    context.self.affects.push_back(make_affect(999, "Unknown", 2, 3, -1, -1, "Unknown", 0, {}, 4));
    const char *equipment_slot_names[] = {
        "light",     "fingerRight", "fingerLeft", "neck1",     "neck2", "body",
        "head",      "legs",        "feet",       "hands",     "arms",  "shield",
        "aboutBody", "waist",       "wristRight", "wristLeft", "wield", "hold",
        "back",      "belt1",       "belt2",      "belt3"};
    for (int slot_index = 0; slot_index < 22; ++slot_index) {
        context.self.equipment.push_back(
            make_equipment_slot(slot_index, equipment_slot_names[slot_index], slot_index == 6));
    }
    context.self.inventory.push_back(make_inventory_object("object:3001", "oak torch", 3001));
    context.self.inventory.push_back(make_inventory_object("object:3002", "small key", 3002));
    context.self.followers.push_back(make_character_reference("mob:4101", "orc guard", true));
    context.self.has_master = true;
    context.self.master = make_character_reference("player:leader", "Leader", false);
    context.self.mount.has_mount = true;
    context.self.mount.mount = make_character_reference("mob:4200", "warhorse", true);
    context.self.mount.has_rider = true;
    context.self.mount.rider = make_character_reference("player:rider", "Rider", false);
    context.self.mount.has_next_rider = true;
    context.self.mount.next_rider = make_character_reference("mob:4201", "pack rider", true);
    context.self.mount.is_riding = true;
    context.self.mount.is_mounted = true;
    context.self.has_room = true;
    context.self.room.id = "room:1204";
    context.self.room.name = "Northern Gate";
    context.self.room.description = "A gatehouse opens toward the old road.";
    context.self.room.vnum = 1204;
    context.self.room.level = 7;
    context.self.room.sector_type = "City";
    context.self.room.flags = {"dark", "indoors"};
    context.self.room.extra_descriptions.push_back({"arch", "Ancient stonework frames the gate."});
    add_gatehouse_exits(context.self.room);
    context.self.room.contents.push_back(make_room_content_fixture());
    context.self.room.characters.push_back(make_character_reference("mob:4101", "orc guard", true));
    context.self.room.affects.push_back(make_affect(56, "Sanctuary", 8, 1, 5, 2, "DEX", 128,
                                                    {"SANCT"}, 6));
    context.self.room.alignment = -2;
    context.self.room.light = 1;
    context.self.room.is_sunlit = true;
    context.self.room.has_zone = true;
    context.self.room.zone.id = "zone:12";
    context.self.room.zone.name = "Old City";
    context.self.room.zone.has_description = true;
    context.self.room.zone.description = "The old city zone.";
    context.self.room.zone.has_map = true;
    context.self.room.zone.map = "N-G-S";
    context.self.room.zone.vnum = 12;
    context.self.room.zone.level = 6;
    context.self.room.zone.lifespan = 30;
    context.self.room.zone.age = 4;
    context.self.room.zone.top_room_vnum = 1299;
    context.self.room.zone.x = 8;
    context.self.room.zone.y = -3;
    context.self.room.zone.symbol = "C";
    context.self.room.zone.white_power = 120;
    context.self.room.zone.dark_power = 80;
    context.self.room.zone.magi_power = 35;
    context.self.room.zone.minimum_look_level = 2;
    context.self.room.zone.reset_mode = 1;

    context.has_actor = true;
    context.actor.id = "player:7";
    context.actor.name = "Builder";
    context.actor.race = "Elf";
    context.actor.level = 31;
    context.actor.experience = 31000;
    context.actor.rank = 4;
    context.actor.hit_points = 88;
    context.actor.max_hit_points = 92;
    context.actor.class_points = 3;
    context.actor.interrupt_count = 1;
    context.actor.interrupt_time = 5;
    context.actor.special_busy = false;
    context.actor.base_abilities.strength = 10;
    context.actor.base_abilities.intelligence = 18;
    context.actor.base_abilities.willpower = 13;
    context.actor.base_abilities.dexterity = 15;
    context.actor.base_abilities.constitution = 11;
    context.actor.base_abilities.leadership = 9;
    context.actor.current_abilities.strength = 11;
    context.actor.current_abilities.intelligence = 19;
    context.actor.current_abilities.willpower = 14;
    context.actor.current_abilities.dexterity = 16;
    context.actor.current_abilities.constitution = 12;
    context.actor.current_abilities.leadership = 10;
    context.actor.rolled_abilities.strength = 9;
    context.actor.rolled_abilities.intelligence = 17;
    context.actor.rolled_abilities.willpower = 12;
    context.actor.rolled_abilities.dexterity = 14;
    context.actor.rolled_abilities.constitution = 10;
    context.actor.rolled_abilities.leadership = 8;
    context.actor.points.bodypart_hits = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    context.actor.points.gold = 77;
    context.actor.points.experience = 31000;
    context.actor.points.spirit = 12;
    context.actor.points.mana_regen = 3;
    context.actor.points.health_regen = -1;
    context.actor.points.move_regen = 2;
    context.actor.points.offense = 13;
    context.actor.points.damage = 4;
    context.actor.points.energy_regen = 5;
    context.actor.points.parry = 8;
    context.actor.points.dodge = 10;
    context.actor.points.encumbrance = 1;
    context.actor.points.willpower = 12;
    context.actor.points.spell_penetration = 6;
    context.actor.points.spell_power = 7;
    context.actor.specials.position = "Standing";
    context.actor.specials.default_position = "Standing";
    context.actor.specials.last_direction = "east";
    context.actor.specials.tactics = "normal";
    context.actor.specials.energy = 44;
    context.actor.specials2.act_flags = {"isNpc", "memory"};
    context.actor.specials2.preference_flags = {"brief"};
    context.actor.specials2.conditions.full = 7;
    context.actor.specials2.tactics = "normal";
    context.actor.specials2.shooting = "slow";
    context.actor.specials2.casting = "fast";
    context.actor.professions = {
        {"mage", "Mage", 1, 25, 25, 1100},
        {"warrior", "Warrior", 4, 100, 100, 4400},
    };
    context.actor.specializations.selected_id = game_types::PS_WeaponMaster;
    context.actor.specializations.selected_key = "weaponMastery";
    context.actor.specializations.selected_name = "weapon mastery";
    context.actor.specializations.current_id = game_types::PS_WeaponMaster;
    context.actor.specializations.current_key = "weaponMastery";
    context.actor.specializations.current_name = "weapon mastery";
    context.actor.specializations.has_runtime_state = false;
    context.actor.damage_details.elapsed_combat_seconds = 4.0;
    context.actor.damage_details.total_damage = 12;
    context.actor.damage_details.damage_per_second = 3.0;
    context.actor.damage_details.entries.push_back(
        make_damage_entry(7, "skill", "Rescue", 3, 12, 6, 4.0, 100.0));
    context.actor.skills.push_back(make_skill_value(14, "Rescue", "warrior", 3, 5,
                                                    POSITION_FIGHTING, 0, 0, 16, 10, 1, false, 0));
    context.actor.knowledge.push_back(make_knowledge_value(
        14, "Rescue", "warrior", 3, 66, POSITION_FIGHTING, 0, 0, 16, 10, 1, false, 0));
    context.actor.affects.push_back(make_affect(14, "Rescue", 3, 2, 1, 0, "NONE", 0, {}, 9));

    context.has_object = true;
    context.object.id = "object:300";
    context.object.name = "silver lever";
    context.object.description = "A silver lever is bolted to the wall.";
    context.object.short_description = "a silver lever";
    context.object.has_action_description = true;
    context.object.action_description = "The lever clicks under your hand.";
    context.object.vnum = 300;
    context.object.flags.item_type = "weapon";
    context.object.flags.wear_flags = {"take", "wield"};
    context.object.flags.extra_flags = {"glow", "magic"};
    context.object.flags.level = 12;
    context.object.flags.weight = 7;
    context.object.flags.cost = 450;
    context.object.flags.cost_per_day = 15;
    context.object.flags.timer = 30;
    context.object.flags.rarity = 2;
    context.object.flags.material = "metal";
    context.object.affects.push_back(make_object_affect(0, 1, "STR", 2));
    context.object.affects.push_back(make_object_affect(1, 2, "DEX", -1));
    context.object.extra_descriptions.push_back({"runes", "Faint runes circle the lever."});
    context.object.extra_descriptions.push_back({"hinge", "The hinge is polished by use."});
    context.object.touched = true;
    context.object.has_container = true;
    context.object.container.id = "object:301";
    context.object.container.name = "oak chest";
    context.object.container.description = "A stout oak chest rests here.";
    context.object.container.short_description = "an oak chest";
    context.object.container.vnum = 301;
    context.object.container.flags.item_type = "container";
    context.object.container.flags.wear_flags = {"take"};
    context.object.container.flags.level = 8;
    context.object.container.extra_descriptions.push_back({"lid", "The lid is reinforced."});
    JsGameEquipmentObjectFixture contained_object;
    contained_object.id = "object:302";
    contained_object.name = "small gear";
    contained_object.description = "A small gear rests inside.";
    contained_object.short_description = "a small gear";
    contained_object.vnum = 302;
    contained_object.flags.item_type = "other";
    contained_object.flags.wear_flags = {"take"};
    contained_object.extra_descriptions.push_back({"teeth", "The gear teeth are sharp."});
    contained_object.touched = true;
    context.object.contents.push_back(std::move(contained_object));
    context.object.has_room = true;
    context.object.room.id = "room:1204";
    context.object.room.name = "Northern Gate";
    context.object.room.description = "A gatehouse opens toward the old road.";
    context.object.room.vnum = 1204;
    context.object.room.level = 7;
    context.object.room.sector_type = "City";
    context.object.room.flags = {"dark", "indoors"};
    context.object.room.extra_descriptions.push_back(
        {"arch", "Ancient stonework frames the gate."});
    add_gatehouse_exits(context.object.room);
    context.object.room.contents.push_back(make_room_content_fixture());
    context.object.room.characters.push_back(make_character_reference("mob:4101", "orc guard", true));
    context.object.room.affects.push_back(make_affect(56, "Sanctuary", 8, 1, 5, 2, "DEX", 128,
                                                      {"SANCT"}, 6));
    context.object.room.alignment = -2;
    context.object.room.light = 1;
    context.object.room.is_sunlit = true;
    context.object.room.has_zone = true;
    context.object.room.zone.id = "zone:12";
    context.object.room.zone.name = "Old City";
    context.object.room.zone.has_description = true;
    context.object.room.zone.description = "The old city zone.";
    context.object.room.zone.has_map = true;
    context.object.room.zone.map = "N-G-S";
    context.object.room.zone.vnum = 12;
    context.object.room.zone.level = 6;
    context.object.room.zone.lifespan = 30;
    context.object.room.zone.age = 4;
    context.object.room.zone.top_room_vnum = 1299;
    context.object.room.zone.x = 8;
    context.object.room.zone.y = -3;
    context.object.room.zone.symbol = "C";
    context.object.room.zone.white_power = 120;
    context.object.room.zone.dark_power = 80;
    context.object.room.zone.magi_power = 35;
    context.object.room.zone.minimum_look_level = 2;
    context.object.room.zone.reset_mode = 1;

    context.has_room = true;
    context.room.id = "room:1204";
    context.room.name = "Northern Gate";
    context.room.description = "A gatehouse opens toward the old road.";
    context.room.vnum = 1204;
    context.room.level = 7;
    context.room.sector_type = "City";
    context.room.flags = {"dark", "indoors"};
    context.room.extra_descriptions.push_back({"arch", "Ancient stonework frames the gate."});
    add_gatehouse_exits(context.room);
    context.room.contents.push_back(make_room_content_fixture());
    context.room.characters.push_back(make_character_reference("mob:4101", "orc guard", true));
    context.room.affects.push_back(make_affect(56, "Sanctuary", 8, 1, 5, 2, "DEX", 128,
                                               {"SANCT"}, 6));
    context.room.affects.push_back(make_affect(2, "Blindness", 3, 0, -4, 5, "DEX", 1,
                                               {"BLIND"}, 1));
    context.room.alignment = -2;
    context.room.light = 1;
    context.room.is_sunlit = true;
    context.room.has_zone = true;
    context.room.zone.id = "zone:12";
    context.room.zone.name = "Old City";
    context.room.zone.has_description = true;
    context.room.zone.description = "The old city zone.";
    context.room.zone.has_map = true;
    context.room.zone.map = "N-G-S";
    context.room.zone.vnum = 12;
    context.room.zone.level = 6;
    context.room.zone.lifespan = 30;
    context.room.zone.age = 4;
    context.room.zone.top_room_vnum = 1299;
    context.room.zone.x = 8;
    context.room.zone.y = -3;
    context.room.zone.symbol = "C";
    context.room.zone.white_power = 120;
    context.room.zone.dark_power = 80;
    context.room.zone.magi_power = 35;
    context.room.zone.minimum_look_level = 2;
    context.room.zone.reset_mode = 1;

    context.has_zone = true;
    context.zone.id = "zone:12";
    context.zone.name = "Old City";
    context.zone.has_description = true;
    context.zone.description = "The old city zone.";
    context.zone.has_map = true;
    context.zone.map = "N-G-S";
    context.zone.vnum = 12;
    context.zone.level = 6;
    context.zone.lifespan = 30;
    context.zone.age = 4;
    context.zone.top_room_vnum = 1299;
    context.zone.x = 8;
    context.zone.y = -3;
    context.zone.symbol = "C";
    context.zone.white_power = 120;
    context.zone.dark_power = 80;
    context.zone.magi_power = 35;
    context.zone.minimum_look_level = 2;
    context.zone.reset_mode = 1;

    context.has_text = true;
    context.text = "say \"open\"\\close\nnext line";

    context.trigger.name = "onPull";
    context.trigger.legacy_name = "ON_PULL";
    context.trigger.host_type = "object";
    context.trigger.legacy_value = 22;
    context.trigger.blocks_gameplay = true;
    return context;
}

void compact_room_for_surface_check(JsGameRoomFixture &room) {
    room.extra_descriptions.clear();
    room.exits.clear();
    room.contents.clear();
    room.characters.clear();
    room.affects.clear();
}

void compact_character_for_surface_check(JsGameCharacterFixture &character) {
    character.professions.clear();
    character.skills.clear();
    character.knowledge.clear();
    character.affects.clear();
    character.equipment.clear();
    character.inventory.clear();
    character.followers.clear();
    if (character.has_room)
        compact_room_for_surface_check(character.room);
}

void compact_equipment_object_for_surface_check(JsGameEquipmentObjectFixture &object) {
    object.affects.clear();
    object.extra_descriptions.clear();
    if (object.has_room)
        compact_room_for_surface_check(object.room);
}

void compact_object_for_surface_check(JsGameObjectFixture &object) {
    object.affects.clear();
    object.extra_descriptions.clear();
    if (object.has_container)
        compact_equipment_object_for_surface_check(object.container);
    if (object.has_room)
        compact_room_for_surface_check(object.room);
    if (object.has_carried_by)
        compact_character_for_surface_check(object.carried_by);
    if (object.has_worn_by)
        compact_character_for_surface_check(object.worn_by);
}

void compact_target_for_surface_check(JsGameTargetFixture &target) {
    if (target.has_character)
        compact_character_for_surface_check(target.character);
    if (target.has_object)
        compact_object_for_surface_check(target.object);
    if (target.has_room)
        compact_room_for_surface_check(target.room);
}

void compact_context_for_surface_check(JsGameTriggerContextFixture &context) {
    compact_character_for_surface_check(context.self);
    compact_character_for_surface_check(context.actor);
    compact_character_for_surface_check(context.speaker);
    compact_character_for_surface_check(context.attacker);
    compact_character_for_surface_check(context.victim);
    compact_character_for_surface_check(context.killer);
    compact_character_for_surface_check(context.dying);
    compact_object_for_surface_check(context.object);
    compact_object_for_surface_check(context.weapon);
    compact_room_for_surface_check(context.room);
    compact_target_for_surface_check(context.target);
    compact_target_for_surface_check(context.targ1);
    compact_target_for_surface_check(context.targ2);
}

void expect_ok_allows(const JsRuntimeEvalResult &result) {
    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Allow) << result.diagnostic;
}

std::size_t count_occurrences(const std::string &haystack, const std::string &needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

const char *runtime_owner_name(JsApiStructOwner owner) {
    switch (owner) {
    case JsApiStructOwner::CharData:
        return "Character";
    case JsApiStructOwner::ObjData:
        return "GameObject";
    case JsApiStructOwner::RoomData:
        return "Room";
    case JsApiStructOwner::ZoneData:
        return "Zone";
    }
    return "Unknown";
}

std::string quoted_js_string(const char *value) {
    std::string quoted = "'";
    for (const char *cursor = value; *cursor; ++cursor) {
        if (*cursor == '\\' || *cursor == '\'')
            quoted += '\\';
        quoted += *cursor;
    }
    quoted += "'";
    return quoted;
}

std::string setter_surface_list(JsApiStructOwner owner, bool callable) {
    std::set<std::string> names;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (mapping.owner != owner)
            continue;
        const bool setter_is_callable =
            std::string(mapping.setter_status) == "implemented-validated-setter";
        if (setter_is_callable == callable)
            names.insert(mapping.setter_name);
    }
    if (owner == JsApiStructOwner::ObjData && callable) {
        names.insert("setLevel");
        names.insert("setRarity");
    }

    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const std::string &name : names) {
        if (!first)
            out << ',';
        first = false;
        out << quoted_js_string(name.c_str());
    }
    out << ']';
    return out.str();
}

std::string generated_setter_surface_script() {
    std::ostringstream out;
    out << "const expected = {\n";
    const JsApiStructOwner owners[] = {JsApiStructOwner::CharData, JsApiStructOwner::ObjData,
                                       JsApiStructOwner::RoomData, JsApiStructOwner::ZoneData};
    for (std::size_t index = 0; index < sizeof(owners) / sizeof(owners[0]); ++index) {
        if (index > 0)
            out << ",\n";
        out << "  " << runtime_owner_name(owners[index])
            << ": { callable: " << setter_surface_list(owners[index], true)
            << ", absent: " << setter_surface_list(owners[index], false) << " }";
    }
    out << "\n};\n"
        << "function check(handle, owner) {\n"
        << "  if (!handle) return false;\n"
        << "  const actual = Object.getOwnPropertyNames(handle)\n"
        << "    .filter((name) => /^set[A-Z]/.test(name))\n"
        << "    .sort();\n"
        << "  const expectedCallable = expected[owner].callable.slice().sort();\n"
        << "  return actual.length === expectedCallable.length\n"
        << "    && actual.every((name, index) => name === expectedCallable[index])\n"
        << "    && expected[owner].callable.every((name) => typeof handle[name] === 'function')\n"
        << "    && expected[owner].absent.every((name) => typeof handle[name] === 'undefined');\n"
        << "}\n"
        << "function inferredOwner(handle) {\n"
        << "  if (!handle) return null;\n"
        << "  const matches = [];\n"
        << "  if ('isPlayer' in handle) matches.push('Character');\n"
        << "  if ('isSunlit' in handle || 'sectorType' in handle) matches.push('Room');\n"
        << "  if ('topRoomVnum' in handle || 'resetMode' in handle) matches.push('Zone');\n"
        << "  if ('shortDescription' in handle || 'actionDescription' in handle || 'carriedBy' in "
           "handle) matches.push('GameObject');\n"
        << "  return matches.length === 1 ? matches[0] : null;\n"
        << "}\n"
        << "function checkInferred(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  return owner !== null && check(handle, owner);\n"
        << "}\n"
        << "function checkNested(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  if (owner === null || !check(handle, owner)) return false;\n"
        << "  if ((owner === 'Character' || owner === 'GameObject') && handle.room && "
           "!checkNested(handle.room)) return false;\n"
        << "  if (owner === 'GameObject' && handle.carriedBy && !checkNested(handle.carriedBy)) "
           "return false;\n"
        << "  if (owner === 'GameObject' && handle.wornBy && !checkNested(handle.wornBy)) return "
           "false;\n"
        << "  if (owner === 'Room' && handle.zone && !checkNested(handle.zone)) return false;\n"
        << "  return true;\n"
        << "}\n"
        << "function requireFixtureNestedPath(handle) {\n"
        << "  const owner = inferredOwner(handle);\n"
        << "  if (owner === 'Character') return !!handle.room && !!handle.room.zone;\n"
        << "  if (owner === 'GameObject') return !!handle.room && !!handle.room.zone;\n"
        << "  if (owner === 'Room') return !!handle.zone;\n"
        << "  return owner === 'Zone';\n"
        << "}\n"
        << "return [ctx.self, ctx.actor, ctx.speaker, ctx.attacker, ctx.victim, ctx.killer, "
           "ctx.dying]\n"
        << "    .every(requireFixtureNestedPath)\n"
        << "  && [ctx.object, ctx.target, ctx.targ1, ctx.targ2].every(requireFixtureNestedPath)\n"
        << "  && requireFixtureNestedPath(ctx.room)\n"
        << "  && requireFixtureNestedPath(ctx.zone)\n"
        << "  && ctx.weapon.carriedBy && requireFixtureNestedPath(ctx.weapon.carriedBy)\n"
        << "  && ctx.weapon.wornBy && requireFixtureNestedPath(ctx.weapon.wornBy)\n"
        << "  && checkNested(ctx.self)\n"
        << "  && checkNested(ctx.actor)\n"
        << "  && checkNested(ctx.speaker)\n"
        << "  && checkNested(ctx.attacker)\n"
        << "  && checkNested(ctx.victim)\n"
        << "  && checkNested(ctx.killer)\n"
        << "  && checkNested(ctx.object)\n"
        << "  && checkNested(ctx.weapon)\n"
        << "  && checkNested(ctx.room)\n"
        << "  && checkNested(ctx.zone)\n"
        << "  && checkInferred(ctx.target)\n"
        << "  && checkInferred(ctx.targ1)\n"
        << "  && checkInferred(ctx.targ2)\n"
        << "  && checkNested(ctx.target)\n"
        << "  && checkNested(ctx.targ1)\n"
        << "  && checkNested(ctx.targ2)\n"
        << "  && checkNested(ctx.dying);";
    return out.str();
}

void set_character_target(JsGameTargetFixture &target, const JsGameCharacterFixture &character,
                          const char *id) {
    target = JsGameTargetFixture{};
    target.type = "character";
    target.has_character = true;
    target.character = character;
    target.character.id = id;
}

void set_object_target(JsGameTargetFixture &target, const JsGameObjectFixture &object,
                       const char *id) {
    target = JsGameTargetFixture{};
    target.type = "object";
    target.has_object = true;
    target.object = object;
    target.object.id = id;
}

void set_room_target(JsGameTargetFixture &target, const JsGameRoomFixture &room, const char *id) {
    target = JsGameTargetFixture{};
    target.type = "room";
    target.has_room = true;
    target.room = room;
    target.room.id = id;
}

} // namespace

TEST(JsGameRuntime, ExecutesScriptsAgainstReadOnlyGameContext) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.id === 'char:1001'\n"
        "  && ctx.self.name === 'Aldren'\n"
        "  && ctx.self.isPlayer === true\n"
        "  && ctx.self.experience === 42000\n"
        "  && ctx.self.rank === 9\n"
        "  && ctx.self.classPoints === 6\n"
        "  && ctx.self.interruptCount === 2\n"
        "  && ctx.self.interruptTime === 11\n"
        "  && ctx.self.specialBusy === true\n"
        "  && ctx.self.baseAbilities.strength === 16\n"
        "  && ctx.self.baseAbilities.intelligence === 12\n"
        "  && ctx.self.baseAbilities.willpower === 14\n"
        "  && ctx.self.baseAbilities.dexterity === 15\n"
        "  && ctx.self.baseAbilities.constitution === 13\n"
        "  && ctx.self.baseAbilities.leadership === 8\n"
        "  && ctx.self.currentAbilities.strength === 18\n"
        "  && ctx.self.currentAbilities.intelligence === 13\n"
        "  && ctx.self.currentAbilities.willpower === 15\n"
        "  && ctx.self.currentAbilities.dexterity === 17\n"
        "  && ctx.self.currentAbilities.constitution === 16\n"
        "  && ctx.self.currentAbilities.leadership === 9\n"
        "  && ctx.self.rolledAbilities.strength === 15\n"
        "  && ctx.self.rolledAbilities.intelligence === 11\n"
        "  && ctx.self.rolledAbilities.willpower === 13\n"
        "  && ctx.self.rolledAbilities.dexterity === 14\n"
        "  && ctx.self.rolledAbilities.constitution === 12\n"
        "  && ctx.self.rolledAbilities.leadership === 7\n"
        "  && ctx.self.points.bodypartHits.join(',') === '1,2,3,4,5,6,7,8,9,10,11'\n"
        "  && ctx.self.points.gold === 123\n"
        "  && ctx.self.points.experience === 42000\n"
        "  && ctx.self.points.spirit === 33\n"
        "  && ctx.self.points.manaRegen === -2\n"
        "  && ctx.self.points.healthRegen === 4\n"
        "  && ctx.self.points.moveRegen === 5\n"
        "  && ctx.self.points.offense === 17\n"
        "  && ctx.self.points.damage === 6\n"
        "  && ctx.self.points.energyRegen === 8\n"
        "  && ctx.self.points.parry === 19\n"
        "  && ctx.self.points.dodge === 21\n"
        "  && ctx.self.points.encumbrance === 2\n"
        "  && ctx.self.points.willpower === 14\n"
        "  && ctx.self.points.spellPenetration === 7\n"
        "  && ctx.self.points.spellPower === 9\n"
        "  && ctx.self.specials.isFighting === true\n"
        "  && ctx.self.specials.isHunting === false\n"
        "  && ctx.self.specials.hasMemory === true\n"
        "  && ctx.self.specials.position === 'Fighting'\n"
        "  && ctx.self.specials.defaultPosition === 'Standing'\n"
        "  && ctx.self.specials.carryWeight === 120\n"
        "  && ctx.self.specials.wornWeight === 40\n"
        "  && ctx.self.specials.encumbranceWeight === 15\n"
        "  && ctx.self.specials.carryItems === 6\n"
        "  && ctx.self.specials.timer === 2\n"
        "  && ctx.self.specials.wasInRoom === 1203\n"
        "  && ctx.self.specials.energy === 77\n"
        "  && ctx.self.specials.currentParry === 12\n"
        "  && ctx.self.specials.lastDirection === 'north'\n"
        "  && ctx.self.specials.attackType === 5\n"
        "  && ctx.self.specials.scriptNumber === 901\n"
        "  && ctx.self.specials.currentBodypart === 3\n"
        "  && ctx.self.specials.tactics === 'aggressive'\n"
        "  && ctx.self.specials.promptNumber === 1\n"
        "  && ctx.self.specials.promptValue === 42\n"
        "  && ctx.self.specials.homeZone === 12\n"
        "  && ctx.self.specials.loadLine === 8\n"
        "  && ctx.self.specials2.loadRoom === 3001\n"
        "  && ctx.self.specials2.spellsToLearn === 3\n"
        "  && ctx.self.specials2.alignment === 250\n"
        "  && ctx.self.specials2.actFlags.join(',') === 'writing,incognito'\n"
        "  && ctx.self.specials2.preferenceFlags.join(',') === 'brief,color,advancedView'\n"
        "  && ctx.self.specials2.wimpLevel === 20\n"
        "  && ctx.self.specials2.freezeLevel === 5\n"
        "  && ctx.self.specials2.savingThrow === 7\n"
        "  && ctx.self.specials2.rawPerception === 81\n"
        "  && ctx.self.specials2.perception === 87\n"
        "  && ctx.self.specials2.conditions.drunk === 1\n"
        "  && ctx.self.specials2.conditions.full === 15\n"
        "  && ctx.self.specials2.conditions.thirst === 19\n"
        "  && ctx.self.specials2.miniLevel === 2\n"
        "  && ctx.self.specials2.maxMiniLevel === 4\n"
        "  && ctx.self.specials2.morale === 33\n"
        "  && ctx.self.specials2.rerolls === 2\n"
        "  && ctx.self.specials2.legEncumbrance === 8\n"
        "  && ctx.self.specials2.retiredOn === 1705000000\n"
        "  && ctx.self.specials2.hideFlags.join(',') === 'hidingWell,snuckIn'\n"
        "  && ctx.self.specials2.tactics === 'aggressive'\n"
        "  && ctx.self.specials2.shooting === 'fast'\n"
        "  && ctx.self.specials2.casting === 'slow'\n"
        "  && ctx.self.specials2.twoHanded === true\n"
        "  && !('badPws' in ctx.self.specials2)\n"
        "  && !('idNumber' in ctx.self.specials2)\n"
        "  && !('owner' in ctx.self.specials2)\n"
        "  && !('roleplayFlags' in ctx.self.specials2)\n"
        "  && !('willTeach' in ctx.self.specials2)\n"
        "  && ctx.self.professions.length === 4\n"
        "  && ctx.self.professions[0].key === 'mage'\n"
        "  && ctx.self.professions[0].name === 'Mage'\n"
        "  && ctx.self.professions[0].level === 8\n"
        "  && ctx.self.professions[0].points === 121\n"
        "  && ctx.self.professions[0].coefficient === 121\n"
        "  && ctx.self.professions[0].experience === 8100\n"
        "  && ctx.self.professions[1].key === 'mystic'\n"
        "  && ctx.self.professions[1].name === 'Mystic'\n"
        "  && ctx.self.professions[1].level === 5\n"
        "  && ctx.self.professions[1].points === 64\n"
        "  && ctx.self.professions[1].coefficient === 64\n"
        "  && ctx.self.professions[1].experience === 5200\n"
        "  && ctx.self.professions[2].key === 'ranger'\n"
        "  && ctx.self.professions[2].name === 'Ranger'\n"
        "  && ctx.self.professions[2].level === 3\n"
        "  && ctx.self.professions[2].points === 25\n"
        "  && ctx.self.professions[2].coefficient === 25\n"
        "  && ctx.self.professions[2].experience === 3300\n"
        "  && ctx.self.professions[3].key === 'warrior'\n"
        "  && ctx.self.professions[3].name === 'Warrior'\n"
        "  && ctx.self.professions[3].level === 2\n"
        "  && ctx.self.professions[3].points === 16\n"
        "  && ctx.self.professions[3].coefficient === 16\n"
        "  && ctx.self.professions[3].experience === 2400\n"
        "  && ctx.self.specializations.selectedId === 2\n"
        "  && ctx.self.specializations.selectedKey === 'cold'\n"
        "  && ctx.self.specializations.selectedName === 'cold'\n"
        "  && ctx.self.specializations.currentId === 2\n"
        "  && ctx.self.specializations.currentKey === 'cold'\n"
        "  && ctx.self.specializations.currentName === 'cold'\n"
        "  && ctx.self.specializations.isMageSpecialization === true\n"
        "  && ctx.self.specializations.hasRuntimeState === true\n"
        "  && ctx.self.damageDetails.elapsedCombatSeconds === 2\n"
        "  && ctx.self.damageDetails.totalDamage === 35\n"
        "  && ctx.self.damageDetails.damagePerSecond === 17.5\n"
        "  && ctx.self.damageDetails.entries.length === 2\n"
        "  && ctx.self.damageDetails.entries[0].sourceId === 1\n"
        "  && ctx.self.damageDetails.entries[0].sourceKind === 'skill'\n"
        "  && ctx.self.damageDetails.entries[0].sourceName === 'Kick'\n"
        "  && ctx.self.damageDetails.entries[0].instanceCount === 1\n"
        "  && ctx.self.damageDetails.entries[0].totalDamage === 5\n"
        "  && ctx.self.damageDetails.entries[0].largestDamage === 5\n"
        "  && ctx.self.damageDetails.entries[0].averageDamage === 5\n"
        "  && Math.abs(ctx.self.damageDetails.entries[0].percentOfTotal - 14.285714285714286) < "
        "0.0001\n"
        "  && ctx.self.damageDetails.entries[1].sourceKind === 'attack'\n"
        "  && ctx.self.damageDetails.entries[1].sourceName === 'hit'\n"
        "  && ctx.self.damageDetails.entries[1].instanceCount === 2\n"
        "  && ctx.self.damageDetails.entries[1].totalDamage === 30\n"
        "  && ctx.self.damageDetails.entries[1].largestDamage === 20\n"
        "  && ctx.self.damageDetails.entries[1].averageDamage === 15\n"
        "  && Math.abs(ctx.self.damageDetails.entries[1].percentOfTotal - 85.71428571428571) < "
        "0.0001\n"
        "  && ctx.self.skills.length === 2\n"
        "  && ctx.self.skills[0].id === 1\n"
        "  && ctx.self.skills[0].name === 'Slashing'\n"
        "  && ctx.self.skills[0].profession === 'warrior'\n"
        "  && ctx.self.skills[0].level === 0\n"
        "  && ctx.self.skills[0].practice === 3\n"
        "  && ctx.self.skills[0].minimumPosition === 7\n"
        "  && ctx.self.skills[0].manaCost === 0\n"
        "  && ctx.self.skills[0].beats === 0\n"
        "  && ctx.self.skills[0].targets === 16\n"
        "  && ctx.self.skills[0].learnDifficulty === 30\n"
        "  && ctx.self.skills[0].learnType === 1\n"
        "  && ctx.self.skills[0].isFast === false\n"
        "  && ctx.self.skills[0].specialization === 0\n"
        "  && ctx.self.skills[1].name === 'Swimming'\n"
        "  && ctx.self.skills[1].profession === 'ranger'\n"
        "  && ctx.self.skills[1].practice === 2\n"
        "  && ctx.self.knowledge.length === 2\n"
        "  && ctx.self.knowledge[0].id === 1\n"
        "  && ctx.self.knowledge[0].name === 'Slashing'\n"
        "  && ctx.self.knowledge[0].profession === 'warrior'\n"
        "  && ctx.self.knowledge[0].level === 0\n"
        "  && ctx.self.knowledge[0].knowledge === 40\n"
        "  && ctx.self.knowledge[0].minimumPosition === 7\n"
        "  && ctx.self.knowledge[0].manaCost === 0\n"
        "  && ctx.self.knowledge[0].beats === 0\n"
        "  && ctx.self.knowledge[0].targets === 16\n"
        "  && ctx.self.knowledge[0].learnDifficulty === 30\n"
        "  && ctx.self.knowledge[0].learnType === 1\n"
        "  && ctx.self.knowledge[0].isFast === false\n"
        "  && ctx.self.knowledge[0].specialization === 0\n"
        "  && ctx.self.knowledge[1].name === 'Swimming'\n"
        "  && ctx.self.knowledge[1].profession === 'ranger'\n"
        "  && ctx.self.knowledge[1].knowledge === 55\n"
        "  && ctx.self.affects.length === 2\n"
        "  && ctx.self.affects[0].type === 56\n"
        "  && ctx.self.affects[0].name === 'Sanctuary'\n"
        "  && ctx.self.affects[0].duration === 8\n"
        "  && ctx.self.affects[0].timePhase === 1\n"
        "  && ctx.self.affects[0].modifier === 5\n"
        "  && ctx.self.affects[0].location === 2\n"
        "  && ctx.self.affects[0].locationName === 'DEX'\n"
        "  && ctx.self.affects[0].bitvector === 128\n"
        "  && ctx.self.affects[0].bitvectorNames.length === 1\n"
        "  && ctx.self.affects[0].bitvectorNames[0] === 'SANCT'\n"
        "  && ctx.self.equipment.length === 22\n"
        "  && ctx.self.equipment[0].slotIndex === 0\n"
        "  && ctx.self.equipment[0].slotName === 'light'\n"
        "  && ctx.self.equipment[0].object === null\n"
        "  && ctx.self.equipment[6].slotIndex === 6\n"
        "  && ctx.self.equipment[6].slotName === 'head'\n"
        "  && ctx.self.equipment[6].object.id === 'object:2001'\n"
        "  && ctx.self.equipment[6].object.name === 'silver helm'\n"
        "  && ctx.self.equipment[6].object.shortDescription === 'a silver helm'\n"
        "  && ctx.self.equipment[6].object.actionDescription === 'The helm glints in the light.'\n"
        "  && ctx.self.equipment[6].object.vnum === 2001\n"
        "  && ctx.self.equipment[6].object.flags.itemType === 'armor'\n"
        "  && ctx.self.equipment[6].object.flags.wearFlags[1] === 'head'\n"
        "  && ctx.self.equipment[6].object.flags.level === 12\n"
        "  && ctx.self.equipment[6].object.touched === true\n"
        "  && ctx.self.equipment[6].object.room === null\n"
        "  && ctx.self.equipment[6].object.carriedBy === null\n"
        "  && ctx.self.equipment[6].object.wornBy === null\n"
        "  && ctx.self.equipment[6].object.isValid() === true\n"
        "  && ctx.self.equipment[16].slotIndex === 16\n"
        "  && ctx.self.equipment[16].slotName === 'wield'\n"
        "  && ctx.self.equipment[16].object === null\n"
        "  && ctx.self.inventory.length === 2\n"
        "  && ctx.self.inventory[0].id === 'object:3001'\n"
        "  && ctx.self.inventory[0].name === 'oak torch'\n"
        "  && ctx.self.inventory[0].vnum === 3001\n"
        "  && ctx.self.inventory[0].flags.itemType === 'light'\n"
        "  && ctx.self.inventory[0].flags.wearFlags[0] === 'take'\n"
        "  && ctx.self.inventory[0].touched === true\n"
        "  && ctx.self.inventory[0].room === null\n"
        "  && ctx.self.inventory[0].carriedBy === null\n"
        "  && ctx.self.inventory[0].wornBy === null\n"
        "  && ctx.self.inventory[0].isValid() === true\n"
        "  && ctx.self.inventory[1].name === 'small key'\n"
        "  && ctx.self.followers.length === 1\n"
        "  && ctx.self.followers[0].id === 'mob:4101'\n"
        "  && ctx.self.followers[0].name === 'orc guard'\n"
        "  && ctx.self.followers[0].vnum === 4101\n"
        "  && ctx.self.followers[0].prototypeVnum === 4101\n"
        "  && ctx.self.followers[0].isNpc === true\n"
        "  && ctx.self.followers[0].isPlayer === false\n"
        "  && ctx.self.followers[0].isValid() === true\n"
        "  && ctx.self.master.id === 'player:leader'\n"
        "  && ctx.self.master.vnum === null\n"
        "  && ctx.self.master.prototypeVnum === null\n"
        "  && ctx.self.master.isPlayer === true\n"
        "  && ctx.self.mount.mount.id === 'mob:4200'\n"
        "  && ctx.self.mount.mount.name === 'warhorse'\n"
        "  && ctx.self.mount.rider.id === 'player:rider'\n"
        "  && ctx.self.mount.nextRider.id === 'mob:4201'\n"
        "  && ctx.self.mount.isRiding === true\n"
        "  && ctx.self.mount.isMounted === true\n"
        "  && ctx.self.affects[0].bitvectorNames.join(',') === 'SANCT'\n"
        "  && ctx.self.affects[0].counter === 6\n"
        "  && ctx.self.affects[1].name === 'Unknown'\n"
        "  && ctx.self.affects[1].locationName === 'Unknown'\n"
        "  && ctx.self.affects[1].bitvectorNames.length === 0\n"
        "  && ctx.self.isValid() === true\n"
        "  && ctx.self.room.vnum === 1204\n"
        "  && ctx.self.room.isSunlit === true\n"
        "  && ctx.self.room.zone.name === 'Old City'\n"
        "  && ctx.actor.race === 'Elf'\n"
        "  && ctx.actor.experience === 31000\n"
        "  && ctx.actor.rank === 4\n"
        "  && ctx.actor.classPoints === 3\n"
        "  && ctx.actor.interruptCount === 1\n"
        "  && ctx.actor.interruptTime === 5\n"
        "  && ctx.actor.specialBusy === false\n"
        "  && ctx.actor.baseAbilities.strength === 10\n"
        "  && ctx.actor.baseAbilities.intelligence === 18\n"
        "  && ctx.actor.baseAbilities.willpower === 13\n"
        "  && ctx.actor.baseAbilities.dexterity === 15\n"
        "  && ctx.actor.baseAbilities.constitution === 11\n"
        "  && ctx.actor.baseAbilities.leadership === 9\n"
        "  && ctx.actor.currentAbilities.strength === 11\n"
        "  && ctx.actor.currentAbilities.intelligence === 19\n"
        "  && ctx.actor.currentAbilities.willpower === 14\n"
        "  && ctx.actor.currentAbilities.dexterity === 16\n"
        "  && ctx.actor.currentAbilities.constitution === 12\n"
        "  && ctx.actor.currentAbilities.leadership === 10\n"
        "  && ctx.actor.rolledAbilities.strength === 9\n"
        "  && ctx.actor.rolledAbilities.intelligence === 17\n"
        "  && ctx.actor.rolledAbilities.willpower === 12\n"
        "  && ctx.actor.rolledAbilities.dexterity === 14\n"
        "  && ctx.actor.rolledAbilities.constitution === 10\n"
        "  && ctx.actor.rolledAbilities.leadership === 8\n"
        "  && ctx.actor.points.bodypartHits.join(',') === '11,10,9,8,7,6,5,4,3,2,1'\n"
        "  && ctx.actor.points.gold === 77\n"
        "  && ctx.actor.points.experience === 31000\n"
        "  && ctx.actor.points.spirit === 12\n"
        "  && ctx.actor.points.manaRegen === 3\n"
        "  && ctx.actor.points.healthRegen === -1\n"
        "  && ctx.actor.points.moveRegen === 2\n"
        "  && ctx.actor.points.offense === 13\n"
        "  && ctx.actor.points.damage === 4\n"
        "  && ctx.actor.points.energyRegen === 5\n"
        "  && ctx.actor.points.parry === 8\n"
        "  && ctx.actor.points.dodge === 10\n"
        "  && ctx.actor.points.encumbrance === 1\n"
        "  && ctx.actor.points.willpower === 12\n"
        "  && ctx.actor.points.spellPenetration === 6\n"
        "  && ctx.actor.points.spellPower === 7\n"
        "  && ctx.actor.specials.position === 'Standing'\n"
        "  && ctx.actor.specials.lastDirection === 'east'\n"
        "  && ctx.actor.specials.tactics === 'normal'\n"
        "  && ctx.actor.specials.energy === 44\n"
        "  && ctx.actor.specials2.actFlags.join(',') === 'isNpc,memory'\n"
        "  && ctx.actor.specials2.conditions.full === 7\n"
        "  && ctx.actor.specials2.casting === 'fast'\n"
        "  && ctx.actor.professions.length === 2\n"
        "  && ctx.actor.professions[0].key === 'mage'\n"
        "  && ctx.actor.professions[0].name === 'Mage'\n"
        "  && ctx.actor.professions[0].level === 1\n"
        "  && ctx.actor.professions[0].points === 25\n"
        "  && ctx.actor.professions[0].coefficient === 25\n"
        "  && ctx.actor.professions[0].experience === 1100\n"
        "  && ctx.actor.professions[1].key === 'warrior'\n"
        "  && ctx.actor.professions[1].name === 'Warrior'\n"
        "  && ctx.actor.professions[1].level === 4\n"
        "  && ctx.actor.professions[1].points === 100\n"
        "  && ctx.actor.professions[1].coefficient === 100\n"
        "  && ctx.actor.professions[1].experience === 4400\n"
        "  && ctx.actor.specializations.selectedKey === 'weaponMastery'\n"
        "  && ctx.actor.specializations.currentName === 'weapon mastery'\n"
        "  && ctx.actor.specializations.isMageSpecialization === false\n"
        "  && ctx.actor.specializations.hasRuntimeState === false\n"
        "  && ctx.actor.damageDetails.elapsedCombatSeconds === 4\n"
        "  && ctx.actor.damageDetails.totalDamage === 12\n"
        "  && ctx.actor.damageDetails.damagePerSecond === 3\n"
        "  && ctx.actor.damageDetails.entries[0].sourceName === 'Rescue'\n"
        "  && ctx.actor.damageDetails.entries[0].percentOfTotal === 100\n"
        "  && ctx.actor.skills.length === 1\n"
        "  && ctx.actor.skills[0].name === 'Rescue'\n"
        "  && ctx.actor.skills[0].practice === 5\n"
        "  && ctx.actor.knowledge.length === 1\n"
        "  && ctx.actor.knowledge[0].name === 'Rescue'\n"
        "  && ctx.actor.knowledge[0].knowledge === 66\n"
        "  && ctx.actor.affects.length === 1\n"
        "  && ctx.actor.affects[0].name === 'Rescue'\n"
        "  && ctx.actor.affects[0].counter === 9\n"
        "  && ctx.object.vnum === 300\n"
        "  && ctx.object.room.name === 'Northern Gate'\n"
        "  && ctx.object.room.isSunlit === true\n"
        "  && ctx.object.room.zone.vnum === 12\n"
        "  && ctx.object.isValid() === true\n"
        "  && ctx.room.name === 'Northern Gate'\n"
        "  && ctx.room.isSunlit === true\n"
        "  && ctx.room.zone.vnum === 12\n"
        "  && ctx.room.isValid() === true\n"
        "  && ctx.zone.vnum === 12\n"
        "  && ctx.trigger.name === 'onPull'\n"
        "  && ctx.trigger.handlerName === 'onPull'\n"
        "  && ctx.trigger.kind === 'legacy'\n"
        "  && ctx.trigger.blocksGameplay === true;",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, SetterSurfaceMatchesStructMappingCatalog) {
    JsGameTriggerContextFixture context = make_context();
    context.actor.has_room = true;
    context.actor.room = context.room;
    context.has_speaker = true;
    context.speaker = context.actor;
    context.speaker.id = "speaker";
    context.has_attacker = true;
    context.attacker = context.self;
    context.attacker.id = "attacker";
    context.has_victim = true;
    context.victim = context.actor;
    context.victim.id = "victim";
    context.has_killer = true;
    context.killer = context.self;
    context.killer.id = "killer";
    context.has_weapon = true;
    context.weapon = context.object;
    context.weapon.has_carried_by = true;
    context.weapon.carried_by = context.actor;
    context.weapon.carried_by.id = "weapon-carrier";
    context.weapon.has_worn_by = true;
    context.weapon.worn_by = context.self;
    context.weapon.worn_by.id = "weapon-wearer";
    context.has_target = true;
    set_character_target(context.target, context.actor, "target-character");
    context.has_targ1 = true;
    set_character_target(context.targ1, context.self, "targ1-character");
    context.has_targ2 = true;
    set_character_target(context.targ2, context.victim, "targ2-character");
    context.has_dying = true;
    context.dying = context.self;
    context.dying.id = "dying";
    compact_context_for_surface_check(context);

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), context);

    expect_ok_allows(result);

    JsGameTriggerContextFixture object_context = context;
    set_object_target(object_context.target, object_context.object, "target-object");
    set_object_target(object_context.targ1, object_context.weapon, "targ1-object");
    set_object_target(object_context.targ2, object_context.object, "targ2-object");

    JsRuntimeEvalResult object_result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), object_context);

    expect_ok_allows(object_result);

    JsGameTriggerContextFixture room_context = context;
    set_room_target(room_context.target, room_context.room, "target-room");
    set_room_target(room_context.targ1, room_context.room, "targ1-room");
    set_room_target(room_context.targ2, room_context.room, "targ2-room");

    JsRuntimeEvalResult room_result =
        runtime.evaluate_trigger_body(generated_setter_surface_script(), room_context);

    expect_ok_allows(room_result);
}

TEST(JsGameRuntime, ExposesMobPrototypeVnumWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.self.is_npc = true;
    context.self.vnum = 5100;
    context.self.prototype_vnum = 5100;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.isNpc === true\n"
                                      "  && ctx.self.isPlayer === false\n"
                                      "  && ctx.self.vnum === 5100\n"
                                      "  && ctx.self.prototypeVnum === 5100;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsUnresolvedMobPrototypeVnumAsNull) {
    JsGameTriggerContextFixture context = make_context();
    context.self.is_npc = true;
    context.self.vnum = -1;
    context.self.prototype_vnum = -1;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.isNpc === true\n"
                                      "  && ctx.self.vnum === null\n"
                                      "  && ctx.self.prototypeVnum === null;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsObjectRoomAsNullWhenMissing) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.room === null\n"
                                      "  && ctx.object.carriedBy === null\n"
                                      "  && ctx.object.wornBy === null;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesPromotedStructGetterSnapshots) {
    JsGameTriggerContextFixture context = make_context();

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.description === 'A silver lever is bolted to the wall.'\n"
        "  && ctx.object.shortDescription === 'a silver lever'\n"
        "  && ctx.object.actionDescription === 'The lever clicks under your hand.'\n"
        "  && ctx.object.flags.itemType === 'weapon'\n"
        "  && ctx.object.flags.wearFlags.join(',') === 'take,wield'\n"
        "  && ctx.object.flags.extraFlags.join(',') === 'glow,magic'\n"
        "  && ['values', 'rawValues', 'value', 'value0', 'value1', 'value2', 'value3', 'value4',\n"
        "      'typeFlag', 'type_flag', 'wearBits', 'wear_flags', 'extraBits', 'extra_flags',\n"
        "      'bitvector', 'butcherItem', 'butcher_item', 'progNumber', 'prog_number',\n"
        "      'scriptNumber', 'script_number', 'scriptInfo', 'script_info', 'poisoned',\n"
        "      'poisonData', 'poisondata', 'poison_data', 'rawMaterial']\n"
        "      .every((field) => typeof ctx.object.flags[field] === 'undefined')\n"
        "  && ctx.object.flags.level === 12\n"
        "  && ctx.object.flags.weight === 7\n"
        "  && ctx.object.flags.cost === 450\n"
        "  && ctx.object.flags.costPerDay === 15\n"
        "  && ctx.object.flags.timer === 30\n"
        "  && ctx.object.flags.rarity === 2\n"
        "  && ctx.object.flags.material === 'metal'\n"
        "  && ctx.object.affects.length === 2\n"
        "  && ctx.object.affects[0].slotIndex === 0\n"
        "  && ctx.object.affects[0].location === 1\n"
        "  && ctx.object.affects[0].locationName === 'STR'\n"
        "  && ctx.object.affects[0].modifier === 2\n"
        "  && ctx.object.affects[1].slotIndex === 1\n"
        "  && ctx.object.affects[1].locationName === 'DEX'\n"
        "  && ctx.object.affects[1].modifier === -1\n"
        "  && ctx.object.extraDescriptions.length === 2\n"
        "  && ctx.object.extraDescriptions[0].keyword === 'runes'\n"
        "  && ctx.object.extraDescriptions[0].description === 'Faint runes circle the lever.'\n"
        "  && ctx.object.extraDescriptions[1].keyword === 'hinge'\n"
        "  && ctx.object.container.name === 'oak chest'\n"
        "  && ctx.object.container.flags.itemType === 'container'\n"
        "  && ctx.object.container.extraDescriptions[0].keyword === 'lid'\n"
        "  && ctx.object.container.room === null\n"
        "  && typeof ctx.object.container.container === 'undefined'\n"
        "  && typeof ctx.object.container.setName === 'undefined'\n"
        "  && ctx.object.contents.length === 1\n"
        "  && ctx.object.contents[0].name === 'small gear'\n"
        "  && ctx.object.contents[0].extraDescriptions[0].keyword === 'teeth'\n"
        "  && ctx.object.touched === true\n"
        "  && ctx.object.container.touched === false\n"
        "  && ctx.object.contents[0].touched === true\n"
        "  && ctx.object.contents[0].room === null\n"
        "  && typeof ctx.object.contents[0].contents === 'undefined'\n"
        "  && typeof ctx.object.contents[0].setName === 'undefined'\n"
        "  && ctx.self.equipment[6].object.affects.length === 1\n"
        "  && ctx.self.equipment[6].object.affects[0].locationName === 'DEX'\n"
        "  && ctx.self.equipment[6].object.affects[0].modifier === 1\n"
        "  && ctx.self.equipment[6].object.extraDescriptions.length === 1\n"
        "  && ctx.self.equipment[6].object.extraDescriptions[0].keyword === 'crest'\n"
        "  && ctx.self.inventory[0].affects.length === 1\n"
        "  && ctx.self.inventory[0].affects[0].slotIndex === 1\n"
        "  && ctx.self.inventory[0].affects[0].locationName === 'SPEED'\n"
        "  && ctx.self.inventory[0].affects[0].modifier === 3\n"
        "  && ctx.self.inventory[0].extraDescriptions.length === 1\n"
        "  && ctx.self.inventory[0].extraDescriptions[0].keyword === 'grain'\n"
        "  && ctx.self.profile.name === 'Aldren'\n"
        "  && ctx.self.profile.shortDescription === 'Aldren the builder'\n"
        "  && ctx.self.profile.longDescription === 'Aldren is reviewing the old gate.'\n"
        "  && ctx.self.profile.description === 'A builder with a weathered notebook.'\n"
        "  && ctx.self.profile.title === 'the careful mapper'\n"
        "  && ctx.self.profile.deathCry === 'Aldren drops the notebook!'\n"
        "  && ctx.self.profile.deathCry2 === 'A startled shout echoes nearby.'\n"
        "  && ctx.self.profile.corpseNumber === 6100\n"
        "  && ctx.self.profile.raceId === 1\n"
        "  && ctx.self.profile.sex === 1\n"
        "  && ctx.self.profile.bodyType === 2\n"
        "  && ctx.self.profile.profession === 4\n"
        "  && ctx.self.profile.level === 42\n"
        "  && ctx.self.profile.language === 17\n"
        "  && ctx.self.profile.hometown === 12\n"
        "  && ctx.self.profile.birthEpochSeconds === 1700000000\n"
        "  && ctx.self.profile.logonEpochSeconds === 1700003600\n"
        "  && ctx.self.profile.playedSeconds === 7200\n"
        "  && ctx.self.profile.weight === 180\n"
        "  && ctx.self.profile.height === 72\n"
        "  && ctx.self.profile.ranking === 9\n"
        "  && ctx.self.profile.talks.join(',') === '10,20,30'\n"
        "  && typeof ctx.self.profile.host === 'undefined'\n"
        "  && typeof ctx.self.profile.password === 'undefined'\n"
        "  && typeof ctx.self.profile.email === 'undefined'\n"
        "  && typeof ctx.self.profile.account === 'undefined'\n"
        "  && ctx.room.description === 'A gatehouse opens toward the old road.'\n"
        "  && ctx.room.level === 7\n"
        "  && ctx.room.sectorType === 'City'\n"
        "  && Array.isArray(ctx.room.flags)\n"
        "  && ctx.room.flags.join(',') === 'dark,indoors'\n"
        "  && ctx.room.extraDescriptions.length === 1\n"
        "  && ctx.room.extraDescriptions[0].keyword === 'arch'\n"
        "  && ctx.room.extraDescriptions[0].description === 'Ancient stonework frames the gate.'\n"
        "  && ctx.room.exits.length === 2\n"
        "  && ctx.room.exits[0].directionIndex === 0\n"
        "  && ctx.room.exits[0].direction === 'north'\n"
        "  && ctx.room.exits[0].toRoomVnum === 1205\n"
        "  && ctx.room.exits[0].keyword === 'gate'\n"
        "  && ctx.room.exits[0].description === 'A raised portcullis opens onto the road.'\n"
        "  && ctx.room.exits[0].keyVnum === 3001\n"
        "  && ctx.room.exits[0].width === 2\n"
        "  && ctx.room.exits[0].flags.join(',') === 'door,closed,locked,pickproof'\n"
        "  && ctx.room.exits[1].direction === 'down'\n"
        "  && ctx.room.exits[1].toRoomVnum === null\n"
        "  && ctx.room.exits[1].flags.join(',') === 'noLook,hidden'\n"
        "  && ctx.room.contents.length === 1\n"
        "  && ctx.room.contents[0].name === 'polished orb'\n"
        "  && ctx.room.contents[0].extraDescriptions[0].keyword === 'orb'\n"
        "  && ctx.room.contents[0].room === null\n"
        "  && typeof ctx.room.contents[0].container === 'undefined'\n"
        "  && typeof ctx.room.contents[0].contents === 'undefined'\n"
        "  && typeof ctx.room.contents[0].setName === 'undefined'\n"
        "  && ctx.room.characters.length === 1\n"
        "  && ctx.room.characters[0].id === 'mob:4101'\n"
        "  && ctx.room.characters[0].name === 'orc guard'\n"
        "  && ctx.room.characters[0].race === 'Orc'\n"
        "  && ctx.room.characters[0].vnum === 4101\n"
        "  && ctx.room.characters[0].prototypeVnum === 4101\n"
        "  && ctx.room.characters[0].level === 12\n"
        "  && ctx.room.characters[0].isNpc === true\n"
        "  && ctx.room.characters[0].isPlayer === false\n"
        "  && ctx.room.characters[0].isValid() === true\n"
        "  && typeof ctx.room.characters[0].followers === 'undefined'\n"
        "  && typeof ctx.room.characters[0].setName === 'undefined'\n"
        "  && ctx.room.affects.length === 2\n"
        "  && ctx.room.affects[0].name === 'Sanctuary'\n"
        "  && ctx.room.affects[0].duration === 8\n"
        "  && ctx.room.affects[0].locationName === 'DEX'\n"
        "  && ctx.room.affects[0].bitvectorNames.join(',') === 'SANCT'\n"
        "  && ctx.room.affects[1].name === 'Blindness'\n"
        "  && ctx.object.room.contents[0].name === 'polished orb'\n"
        "  && ctx.object.room.contents[0].room === null\n"
        "  && typeof ctx.object.room.contents[0].container === 'undefined'\n"
        "  && typeof ctx.object.room.contents[0].contents === 'undefined'\n"
        "  && ctx.object.room.characters[0].id === 'mob:4101'\n"
        "  && ctx.object.room.affects[0].name === 'Sanctuary'\n"
        "  && ctx.self.room.contents[0].name === 'polished orb'\n"
        "  && ctx.self.room.contents[0].room === null\n"
        "  && typeof ctx.self.room.contents[0].container === 'undefined'\n"
        "  && typeof ctx.self.room.contents[0].contents === 'undefined'\n"
        "  && ctx.self.room.characters[0].id === 'mob:4101'\n"
        "  && ctx.self.room.affects[0].name === 'Sanctuary'\n"
        "  && ctx.room.alignment === -2\n"
        "  && ctx.room.light === 1\n"
        "  && ctx.zone.level === 6\n"
        "  && ctx.zone.description === 'The old city zone.'\n"
        "  && ctx.zone.map === 'N-G-S'\n"
        "  && ctx.zone.lifespan === 30\n"
        "  && ctx.zone.age === 4\n"
        "  && ctx.zone.topRoomVnum === 1299\n"
        "  && ctx.zone.x === 8\n"
        "  && ctx.zone.y === -3\n"
        "  && ctx.zone.symbol === 'C'\n"
        "  && ctx.zone.whitePower === 120\n"
        "  && ctx.zone.darkPower === 80\n"
        "  && ctx.zone.magiPower === 35\n"
        "  && ctx.zone.minimumLookLevel === 2\n"
        "  && ctx.zone.resetMode === 1\n"
        "  && ctx.object.room.description === ctx.room.description\n"
        "  && ctx.object.room.sectorType === ctx.room.sectorType\n"
        "  && ctx.object.room.flags.join(',') === ctx.room.flags.join(',')\n"
        "  && ctx.object.room.extraDescriptions[0].keyword === "
        "ctx.room.extraDescriptions[0].keyword\n"
        "  && ctx.object.room.exits[0].toRoomVnum === ctx.room.exits[0].toRoomVnum\n"
        "  && Object.isFrozen(ctx.object.room.exits)\n"
        "  && Object.isFrozen(ctx.object.room.exits[0])\n"
        "  && Object.isFrozen(ctx.object.room.exits[0].flags)\n"
        "  && ctx.object.room.light === ctx.room.light\n"
        "  && ctx.object.room.zone.description === ctx.zone.description\n"
        "  && ctx.object.room.zone.map === ctx.zone.map\n"
        "  && ctx.object.room.zone.level === ctx.zone.level\n"
        "  && ctx.object.room.zone.whitePower === ctx.zone.whitePower\n"
        "  && ctx.room.zone.darkPower === ctx.zone.darkPower\n"
        "  && ctx.self.room.zone.magiPower === ctx.zone.magiPower\n"
        "  && ctx.self.room.exits[1].direction === ctx.room.exits[1].direction;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, OmitsRawObjectFlagDomainsFromNestedObjectSnapshots) {
    JsGameTriggerContextFixture context = make_context();

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "const forbidden = ['values', 'rawValues', 'value', 'value0', 'value1', 'value2',\n"
        "  'value3', 'value4', 'typeFlag', 'type_flag', 'wearBits', 'wear_flags',\n"
        "  'extraBits', 'extra_flags', 'bitvector', 'butcherItem', 'butcher_item',\n"
        "  'progNumber', 'prog_number', 'scriptNumber', 'script_number', 'scriptInfo',\n"
        "  'script_info', 'poisoned', 'poisonData', 'poisondata', 'poison_data',\n"
        "  'rawMaterial'];\n"
        "const noRaw = (flags) => forbidden.every((field) => typeof flags[field] === "
        "'undefined');\n"
        "return noRaw(ctx.object.flags)\n"
        "  && noRaw(ctx.object.container.flags)\n"
        "  && noRaw(ctx.object.contents[0].flags)\n"
        "  && noRaw(ctx.self.equipment[6].object.flags)\n"
        "  && noRaw(ctx.self.inventory[0].flags)\n"
        "  && (!ctx.weapon || noRaw(ctx.weapon.flags))\n"
        "  && (!ctx.targ2 || !ctx.targ2.flags || noRaw(ctx.targ2.flags));",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsObjectFlagArraysFrozenAndConstructorSafe) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let extraPushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "let lengthBlocked = false;\n"
        "try { ctx.object.flags.wearFlags.push('hold'); } catch (error) { pushBlocked = true; }\n"
        "try { ctx.object.flags.extraFlags.push('dark'); } catch (error) { extraPushBlocked = "
        "true; }\n"
        "try { ctx.object.flags.wearFlags[0] = 'hold'; } catch (error) { indexBlocked = true; }\n"
        "try { ctx.object.flags.extraFlags.length = 0; } catch (error) { lengthBlocked = true; }\n"
        "return ctx.object.flags.wearFlags.join(',') === 'take,wield'\n"
        "  && ctx.object.flags.extraFlags.join(',') === 'glow,magic'\n"
        "  && ['values', 'rawValues', 'value', 'value0', 'value1', 'value2', 'value3', 'value4',\n"
        "      'typeFlag', 'type_flag', 'wearBits', 'wear_flags', 'extraBits', 'extra_flags',\n"
        "      'bitvector', 'butcherItem', 'butcher_item', 'progNumber', 'prog_number',\n"
        "      'scriptNumber', 'script_number', 'scriptInfo', 'script_info', 'poisoned',\n"
        "      'poisonData', 'poisondata', 'poison_data', 'rawMaterial']\n"
        "      .every((field) => typeof ctx.object.flags[field] === 'undefined')\n"
        "  && Object.isFrozen(ctx.object.flags)\n"
        "  && Object.isFrozen(ctx.object.flags.wearFlags)\n"
        "  && Object.isFrozen(ctx.object.flags.extraFlags)\n"
        "  && pushBlocked && extraPushBlocked && indexBlocked && lengthBlocked\n"
        "  && typeof ctx.object.flags.wearFlags.join.constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsRoomFlagArraysFrozenAndConstructorSafe) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "let lengthBlocked = false;\n"
        "try { ctx.room.flags.push('death'); } catch (error) { pushBlocked = true; }\n"
        "try { ctx.room.flags[0] = 'death'; } catch (error) { indexBlocked = true; }\n"
        "try { ctx.room.flags.length = 0; } catch (error) { lengthBlocked = true; }\n"
        "return Array.isArray(ctx.room.flags)\n"
        "  && ctx.room.flags.includes('dark')\n"
        "  && ctx.room.flags.join(',') === 'dark,indoors'\n"
        "  && Object.isFrozen(ctx.room.flags)\n"
        "  && pushBlocked && indexBlocked && lengthBlocked\n"
        "  && typeof ctx.room.flags.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.join.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.filter.constructor === 'undefined'\n"
        "  && typeof ctx.room.flags.__proto__.constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsRoomExtraDescriptionsFrozenAndConstructorSafe) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "try { ctx.room.extraDescriptions.push({ keyword: 'unsafe' }); } catch (error) { "
        "pushBlocked = true; }\n"
        "try { ctx.room.extraDescriptions[0].keyword = 'unsafe'; } catch (error) { indexBlocked = "
        "true; }\n"
        "return Array.isArray(ctx.room.extraDescriptions)\n"
        "  && ctx.room.extraDescriptions.length === 1\n"
        "  && ctx.room.extraDescriptions[0].keyword === 'arch'\n"
        "  && ctx.room.extraDescriptions[0].description === 'Ancient stonework frames the gate.'\n"
        "  && Object.isFrozen(ctx.room.extraDescriptions)\n"
        "  && Object.isFrozen(ctx.room.extraDescriptions[0])\n"
        "  && pushBlocked && indexBlocked\n"
        "  && typeof ctx.room.extraDescriptions.constructor === 'undefined'\n"
        "  && typeof ctx.room.extraDescriptions[0].constructor === 'undefined'\n"
        "  && Object.getPrototypeOf(ctx.room.extraDescriptions[0]) === null;",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, KeepsRoomExitsFrozenAndConstructorSafe) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let pushBlocked = false;\n"
        "let indexBlocked = false;\n"
        "let flagPushBlocked = false;\n"
        "try { ctx.room.exits.push({ direction: 'east' }); } catch (error) { pushBlocked = true; "
        "}\n"
        "try { ctx.room.exits[0].toRoomVnum = 9999; } catch (error) { indexBlocked = true; }\n"
        "try { ctx.room.exits[0].flags.push('hidden'); } catch (error) { flagPushBlocked = true; "
        "}\n"
        "return Array.isArray(ctx.room.exits)\n"
        "  && ctx.room.exits.length === 2\n"
        "  && ctx.room.exits[0].direction === 'north'\n"
        "  && ctx.room.exits[0].toRoomVnum === 1205\n"
        "  && ctx.room.exits[0].flags.join(',') === 'door,closed,locked,pickproof'\n"
        "  && ctx.room.exits[1].toRoomVnum === null\n"
        "  && Object.isFrozen(ctx.room.exits)\n"
        "  && Object.isFrozen(ctx.room.exits[0])\n"
        "  && Object.isFrozen(ctx.room.exits[0].flags)\n"
        "  && pushBlocked && indexBlocked && flagPushBlocked\n"
        "  && typeof ctx.room.exits.constructor === 'undefined'\n"
        "  && typeof ctx.room.exits[0].constructor === 'undefined'\n"
        "  && typeof ctx.room.exits[0].flags.constructor === 'undefined'\n"
        "  && Object.getPrototypeOf(ctx.room.exits[0]) === null;",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsNullablePromotedStructGetterSnapshotsAsNull) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_action_description = false;
    context.self.profile.has_long_description = false;
    context.self.profile.has_description = false;
    context.self.profile.has_title = false;
    context.self.profile.has_death_cry = false;
    context.self.profile.has_death_cry2 = false;
    context.zone.has_description = false;
    context.zone.has_map = false;
    context.room.zone.has_description = false;
    context.room.zone.has_map = false;
    context.object.room.zone.has_description = false;
    context.object.room.zone.has_map = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.actionDescription === null\n"
                                      "  && ctx.self.profile.longDescription === null\n"
                                      "  && ctx.self.profile.description === null\n"
                                      "  && ctx.self.profile.title === null\n"
                                      "  && ctx.self.profile.deathCry === null\n"
                                      "  && ctx.self.profile.deathCry2 === null\n"
                                      "  && ctx.zone.description === null\n"
                                      "  && ctx.zone.map === null\n"
                                      "  && ctx.room.zone.description === null\n"
                                      "  && ctx.object.room.zone.map === null;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, PreservesEmptyNullablePromotedStructGetterStrings) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_action_description = true;
    context.object.action_description = "";
    context.self.profile.has_long_description = true;
    context.self.profile.long_description = "";
    context.self.profile.has_description = true;
    context.self.profile.description = "";
    context.self.profile.has_title = true;
    context.self.profile.title = "";
    context.self.profile.has_death_cry = true;
    context.self.profile.death_cry = "";
    context.self.profile.has_death_cry2 = true;
    context.self.profile.death_cry2 = "";
    context.zone.has_description = true;
    context.zone.description = "";
    context.zone.has_map = true;
    context.zone.map = "";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.actionDescription === ''\n"
                                      "  && ctx.self.profile.longDescription === ''\n"
                                      "  && ctx.self.profile.description === ''\n"
                                      "  && ctx.self.profile.title === ''\n"
                                      "  && ctx.self.profile.deathCry === ''\n"
                                      "  && ctx.self.profile.deathCry2 === ''\n"
                                      "  && ctx.zone.description === ''\n"
                                      "  && ctx.zone.map === '';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsMutationOfCharacterProfileSnapshot) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let nameBlocked = false;\n"
        "let talkBlocked = false;\n"
        "let pushBlocked = false;\n"
        "try { ctx.self.profile.name = 'Changed'; } catch (error) { nameBlocked = true; }\n"
        "try { ctx.self.profile.talks[0] = 99; } catch (error) { talkBlocked = true; }\n"
        "try { ctx.self.profile.talks.push(99); } catch (error) { pushBlocked = true; }\n"
        "return ctx.self.profile.name === 'Aldren'\n"
        "  && ctx.self.profile.talks.join(',') === '10,20,30'\n"
        "  && Object.isFrozen(ctx.self.profile)\n"
        "  && Object.isFrozen(ctx.self.profile.talks)\n"
        "  && nameBlocked && talkBlocked && pushBlocked;",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesObjectCarriedByWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;
    context.object.has_carried_by = true;
    context.object.carried_by = context.actor;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.room === null\n"
                                      "  && ctx.object.carriedBy !== null\n"
                                      "  && ctx.object.carriedBy.name === 'Builder'\n"
                                      "  && ctx.object.carriedBy.rank === 4\n"
                                      "  && ctx.object.carriedBy.isValid() === true\n"
                                      "  && ctx.object.wornBy === null;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesObjectWornByWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_room = false;
    context.object.has_worn_by = true;
    context.object.worn_by = context.self;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.room === null\n"
                                      "  && ctx.object.carriedBy === null\n"
                                      "  && ctx.object.wornBy !== null\n"
                                      "  && ctx.object.wornBy.name === 'Aldren'\n"
                                      "  && ctx.object.wornBy.room.zone.vnum === 12\n"
                                      "  && ctx.object.wornBy.isValid() === true;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsMutationOfObjectOwnerSnapshots) {
    JsGameTriggerContextFixture context = make_context();
    context.object.has_carried_by = true;
    context.object.carried_by = context.actor;
    context.object.has_worn_by = true;
    context.object.worn_by = context.self;

    JsGameRuntime runtime;
    JsRuntimeEvalResult assign_result =
        runtime.evaluate_trigger_body("ctx.object.carriedBy.name = 'changed';\n"
                                      "return true;",
                                      context);
    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(assign_result.diagnostic.find("read-only") != std::string::npos ||
                assign_result.diagnostic.find("no setter") != std::string::npos)
        << assign_result.diagnostic;

    JsRuntimeEvalResult proto_result = runtime.evaluate_trigger_body(
        "Object.setPrototypeOf(ctx.object.wornBy, { injected: true });\n"
        "return true;",
        context);
    EXPECT_EQ(proto_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ExposesTriggerSpecificCharacterRoleSnapshots) {
    JsGameTriggerContextFixture context = make_context();
    context.has_speaker = true;
    context.speaker = context.actor;
    context.has_attacker = true;
    context.attacker = context.actor;
    context.has_victim = true;
    context.victim = context.self;
    context.has_killer = true;
    context.killer = context.actor;
    context.trigger.host_type = "character";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.hostType === 'character'\n"
        "  && ctx.speaker.id === 'player:7'\n"
        "  && ctx.attacker.name === 'Builder'\n"
        "  && ctx.victim.name === 'Aldren'\n"
        "  && ctx.killer.name === 'Builder'\n"
        "  && [ctx.self, ctx.speaker, ctx.attacker, ctx.victim, "
        "ctx.killer].every(function(character) {\n"
        "    return typeof character.setProfile === 'undefined'\n"
        "      && typeof character.setBaseAbilities === 'undefined'\n"
        "      && typeof character.setCurrentAbilities === 'undefined'\n"
        "      && typeof character.setRolledAbilities === 'undefined'\n"
        "      && typeof character.setPoints === 'undefined'\n"
        "      && typeof character.setSpecials === 'undefined'\n"
        "      && typeof character.setSpecials2 === 'undefined'\n"
        "      && typeof character.setProfessions === 'undefined'\n"
        "      && typeof character.setSpecializations === 'undefined'\n"
        "      && typeof character.setDamageDetails === 'undefined'\n"
        "      && typeof character.setSkill === 'undefined'\n"
        "      && typeof character.setKnowledge === 'undefined'\n"
        "      && typeof character.setRoom === 'undefined'\n"
        "      && typeof character.setAffects === 'undefined'\n"
        "      && typeof character.setEquipmentSlot === 'undefined'\n"
        "      && typeof character.setInventory === 'undefined'\n"
        "      && typeof character.setFollowers === 'undefined'\n"
        "      && typeof character.setMaster === 'undefined'\n"
        "      && typeof character.setMount === 'undefined'\n"
        "      && typeof character.group === 'undefined'\n"
        "      && typeof character.setGroup === 'undefined'\n"
        "      && typeof character.setClassPoints === 'undefined'\n"
        "      && typeof character.setInterruptCount === 'undefined'\n"
        "      && typeof character.setInterruptTime === 'undefined'\n"
        "      && typeof character.setSpecialBusy === 'undefined';\n"
        "  })\n"
        "  && ctx.trigger.hostType === ctx.hostType;",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesDamageWeaponWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.has_weapon = true;
    context.weapon = context.object;
    context.trigger.name = "onDamage";
    context.trigger.legacy_name = "ON_DAMAGE";
    context.trigger.legacy_value = 18;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.weapon !== null\n"
        "  && ctx.weapon.name === 'silver lever'\n"
        "  && ctx.weapon.affects.length === 2\n"
        "  && ctx.weapon.affects[0].locationName === 'STR'\n"
        "  && ctx.weapon.affects[1].modifier === -1\n"
        "  && ctx.weapon.extraDescriptions[0].keyword === 'runes'\n"
        "  && ctx.weapon.extraDescriptions[1].description === 'The hinge is polished by use.'\n"
        "  && ctx.weapon.container.name === 'oak chest'\n"
        "  && ctx.weapon.container.room === null\n"
        "  && typeof ctx.weapon.container.container === 'undefined'\n"
        "  && typeof ctx.weapon.container.setName === 'undefined'\n"
        "  && ctx.weapon.contents[0].name === 'small gear'\n"
        "  && ctx.weapon.touched === true\n"
        "  && ctx.weapon.contents[0].touched === true\n"
        "  && typeof ctx.weapon.contents[0].contents === 'undefined'\n"
        "  && typeof ctx.weapon.contents[0].setName === 'undefined'\n"
        "  && ctx.weapon.room.vnum === 1204\n"
        "  && ctx.weapon.isValid();",
        context);

    expect_ok_allows(result);

    JsRuntimeEvalResult assign_result =
        runtime.evaluate_trigger_body("ctx.weapon.name = 'changed';\n"
                                      "return true;",
                                      context);
    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(assign_result.diagnostic.find("read-only") != std::string::npos ||
                assign_result.diagnostic.find("no setter") != std::string::npos)
        << assign_result.diagnostic;

    JsRuntimeEvalResult proto_result =
        runtime.evaluate_trigger_body("Object.setPrototypeOf(ctx.weapon, { injected: true });\n"
                                      "return true;",
                                      context);
    EXPECT_EQ(proto_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, SerializesFalseRoomSunlitState) {
    JsGameTriggerContextFixture context = make_context();
    context.room.is_sunlit = false;
    context.self.room.is_sunlit = false;
    context.object.room.is_sunlit = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.room.isSunlit === false\n"
                                      "  && ctx.self.room.isSunlit === false\n"
                                      "  && ctx.object.room.isSunlit === false;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, PreservesBlockingReturnSemantics) {
    JsGameRuntime runtime;

    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("return false;", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, ExposesScriptResultHelpers) {
    JsGameRuntime runtime;

    JsRuntimeEvalResult allow_result =
        runtime.evaluate_trigger_body("return RotS.ScriptResult.allow();", make_context());
    JsRuntimeEvalResult block_result =
        runtime.evaluate_trigger_body("return RotS.ScriptResult.block();", make_context());

    expect_ok_allows(allow_result);
    EXPECT_EQ(block_result.status, JsRuntimeStatus::Ok) << block_result.diagnostic;
    EXPECT_EQ(block_result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, KeepsScriptResultHelpersImmutable) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "try { RotS.ScriptResult.allow = function() { return false; }; } catch (error) {}\n"
        "try { RotS.ScriptResult.extra = true; } catch (error) {}\n"
        "return RotS.ScriptResult.allow() === true && typeof RotS.ScriptResult.extra === "
        "'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExecutesFirstTextSettersThroughMutationResults) {
    JsGameTriggerContextFixture context = make_context();
    context.actor.has_room = true;
    context.actor.room = context.room;
    context.has_weapon = true;
    context.weapon.id = "object:weapon";
    context.weapon.name = "iron sword";
    context.weapon.description = "An iron sword rests here.";
    context.weapon.short_description = "an iron sword";
    context.weapon.vnum = 301;
    context.weapon.has_room = true;
    context.weapon.room = context.room;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "let assignBlocked = false;\n"
        "let defineBlocked = false;\n"
        "try { ctx.object.name = 'unsafe'; } catch (error) { assignBlocked = true; }\n"
        "try { Object.defineProperty(ctx.object, 'name', { value: 'unsafe' }); } catch (error) {\n"
        "  defineBlocked = true;\n"
        "}\n"
        "const objectName = ctx.object.setName('bronze lever');\n"
        "const objectDescription = ctx.object.setDescription('A bronze lever is bolted here.');\n"
        "const objectShort = ctx.object.setShortDescription('a bronze lever');\n"
        "const objectAction = ctx.object.setActionDescription(null);\n"
        "const objectLevelLower = ctx.object.setLevel(0);\n"
        "const objectLevel = ctx.object.setLevel(100);\n"
        "const objectRarityLower = ctx.object.setRarity(0);\n"
        "const objectRarity = ctx.object.setRarity(255);\n"
        "const roomName = ctx.room.setName('Southern Gate');\n"
        "const roomDescription = ctx.room.setDescription('A second gatehouse faces the road.');\n"
        "const roomLevelLower = ctx.room.setLevel(0);\n"
        "const roomLevel = ctx.room.setLevel(100);\n"
        "const roomSector = ctx.room.setSectorType('Water_noswim');\n"
        "const badRoomSectorType = ctx.room.setSectorType(7);\n"
        "const badRoomSectorNull = ctx.room.setSectorType(null);\n"
        "const badRoomSectorUnknown = ctx.room.setSectorType('Unknown');\n"
        "const badRoomSectorLower = ctx.room.setSectorType('water_noswim');\n"
        "const badRoomSectorWhitespace = ctx.room.setSectorType(' Water_noswim');\n"
        "const badRoomSectorTrailing = ctx.room.setSectorType('Water_noswim ');\n"
        "const badRoomSectorDisplay = ctx.room.setSectorType('Water No Swim');\n"
        "const badRoomSectorHyphen = ctx.room.setSectorType('Water-noswim');\n"
        "const badRoomSectorDenseDisplay = ctx.room.setSectorType('Dense Forest');\n"
        "const badRoomSectorDenseCaps = ctx.room.setSectorType('Dense_Forest');\n"
        "const badRoomSectorNumericText = ctx.room.setSectorType('7');\n"
        "const badRoomSectorEmpty = ctx.room.setSectorType('');\n"
        "const zoneName = ctx.zone.setName('New City');\n"
        "const zoneDescription = ctx.zone.setDescription(null);\n"
        "const zoneMap = ctx.zone.setMap('N-G-S-E');\n"
        "const zoneSymbol = ctx.zone.setSymbol('*');\n"
        "const badType = ctx.object.setName(42);\n"
        "const badRange = ctx.room.setName('x'.repeat(300));\n"
        "const badRoomLevelType = ctx.room.setLevel('2');\n"
        "const badRoomLevelFraction = ctx.room.setLevel(1.5);\n"
        "const badRoomLevelNegative = ctx.room.setLevel(-1);\n"
        "const badRoomLevelRange = ctx.room.setLevel(101);\n"
        "const badRoomLevelNaN = ctx.room.setLevel(NaN);\n"
        "const badObjectLevelType = ctx.object.setLevel('2');\n"
        "const badObjectLevelFraction = ctx.object.setLevel(1.5);\n"
        "const badObjectLevelNegative = ctx.object.setLevel(-1);\n"
        "const badObjectLevelRange = ctx.object.setLevel(101);\n"
        "const badObjectLevelNaN = ctx.object.setLevel(NaN);\n"
        "const badObjectRarityType = ctx.object.setRarity('2');\n"
        "const badObjectRarityFraction = ctx.object.setRarity(1.5);\n"
        "const badObjectRarityNegative = ctx.object.setRarity(-1);\n"
        "const badObjectRarityRange = ctx.object.setRarity(256);\n"
        "const badObjectRarityNaN = ctx.object.setRarity(NaN);\n"
        "const badObjectRarityNull = ctx.object.setRarity(null);\n"
        "const blankName = ctx.zone.setName('   ');\n"
        "const badNul = ctx.object.setDescription('bad\\u0000text');\n"
        "const badMapType = ctx.zone.setMap(42);\n"
        "const badMapNul = ctx.zone.setMap('bad\\u0000map');\n"
        "const badMapRange = ctx.zone.setMap('x'.repeat(8193));\n"
        "const badMapTilde = ctx.zone.setMap('bad~map');\n"
        "const badMapHash = ctx.zone.setMap('ok\\n  #31');\n"
        "const badSymbolType = ctx.zone.setSymbol(7);\n"
        "const badSymbolEmpty = ctx.zone.setSymbol('');\n"
        "const badSymbolSpace = ctx.zone.setSymbol(' ');\n"
        "const badSymbolLong = ctx.zone.setSymbol('**');\n"
        "const badSymbolNonAscii = ctx.zone.setSymbol(String.fromCharCode(0xe9));\n"
        "try { Number.isInteger = function() { return false; }; } catch (error) {}\n"
        "try { globalThis.String = function() { return '25'; }; } catch (error) {}\n"
        "const zoneXLower = ctx.zone.setX(0);\n"
        "const zoneX = ctx.zone.setX(25);\n"
        "const zoneYLower = ctx.zone.setY(0);\n"
        "const zoneY = ctx.zone.setY(25);\n"
        "const zoneResetModeLower = ctx.zone.setResetMode(0);\n"
        "const zoneResetMode = ctx.zone.setResetMode(3);\n"
        "const zoneLifespanLower = ctx.zone.setLifespan(1);\n"
        "const zoneLifespan = ctx.zone.setLifespan(10080);\n"
        "const zoneLevelLower = ctx.zone.setLevel(0);\n"
        "const zoneLevel = ctx.zone.setLevel(100);\n"
        "const badXType = ctx.zone.setX('7');\n"
        "const badXFraction = ctx.zone.setX(1.5);\n"
        "const badXNegative = ctx.zone.setX(-1);\n"
        "const badXRange = ctx.zone.setX(26);\n"
        "const badXNaN = ctx.zone.setX(NaN);\n"
        "const badYType = ctx.zone.setY('7');\n"
        "const badYFraction = ctx.zone.setY(1.5);\n"
        "const badYNegative = ctx.zone.setY(-1);\n"
        "const badYRange = ctx.zone.setY(26);\n"
        "const badYNaN = ctx.zone.setY(NaN);\n"
        "const badResetModeType = ctx.zone.setResetMode('2');\n"
        "const badResetModeFraction = ctx.zone.setResetMode(1.5);\n"
        "const badResetModeNegative = ctx.zone.setResetMode(-1);\n"
        "const badResetModeRange = ctx.zone.setResetMode(4);\n"
        "const badResetModeNaN = ctx.zone.setResetMode(NaN);\n"
        "const badLifespanType = ctx.zone.setLifespan('2');\n"
        "const badLifespanFraction = ctx.zone.setLifespan(1.5);\n"
        "const badLifespanZero = ctx.zone.setLifespan(0);\n"
        "const badLifespanNegative = ctx.zone.setLifespan(-1);\n"
        "const badLifespanRange = ctx.zone.setLifespan(10081);\n"
        "const badLifespanNaN = ctx.zone.setLifespan(NaN);\n"
        "const badLevelType = ctx.zone.setLevel('2');\n"
        "const badLevelFraction = ctx.zone.setLevel(1.5);\n"
        "const badLevelNegative = ctx.zone.setLevel(-1);\n"
        "const badLevelRange = ctx.zone.setLevel(101);\n"
        "const badLevelNaN = ctx.zone.setLevel(NaN);\n"
        "const nestedWeapon = ctx.weapon.setName('tempered sword');\n"
        "const nestedWeaponLevel = ctx.weapon.setLevel(77);\n"
        "const nestedWeaponRarity = ctx.weapon.setRarity(201);\n"
        "const nestedObjectRoom = ctx.object.room.setDescription('Nested object room edit.');\n"
        "const nestedObjectRoomLevel = ctx.object.room.setLevel(12);\n"
        "const nestedObjectRoomSector = ctx.object.room.setSectorType('Underwater');\n"
        "const nestedActorRoom = ctx.actor.room.setName('Actor Room Edit');\n"
        "const nestedZone = ctx.room.zone.setName('Nested Zone Edit');\n"
        "const nestedZoneMap = ctx.room.zone.setMap(null);\n"
        "const nestedZoneY = ctx.room.zone.setY(0);\n"
        "const nestedZoneResetMode = ctx.room.zone.setResetMode(2);\n"
        "const nestedZoneLifespan = ctx.room.zone.setLifespan(60);\n"
        "const nestedZoneLevel = ctx.room.zone.setLevel(45);\n"
        "try { badType.code = 'ok'; } catch (error) {}\n"
        "try { Object.defineProperty(badType, 'extra', { value: true }); } catch (error) {}\n"
        "return typeof RotS.MutationResult === 'undefined'\n"
        "  && typeof MutationResult === 'undefined'\n"
        "  && typeof ctx.object.setName === 'function'\n"
        "  && typeof ctx.object.setLevel === 'function'\n"
        "  && typeof ctx.object.setRarity === 'function'\n"
        "  && typeof ctx.object.setFlags === 'undefined'\n"
        "  && typeof ctx.object.setAffects === 'undefined'\n"
        "  && typeof ctx.object.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.object.setRoom === 'undefined'\n"
        "  && typeof ctx.object.setCarriedBy === 'undefined'\n"
        "  && typeof ctx.object.setContainer === 'undefined'\n"
        "  && typeof ctx.object.setContents === 'undefined'\n"
        "  && typeof ctx.object.setTouched === 'undefined'\n"
        "  && typeof ctx.object.setLoadedBy === 'undefined'\n"
        "  && typeof ctx.weapon.setName === 'function'\n"
        "  && typeof ctx.weapon.setLevel === 'function'\n"
        "  && typeof ctx.weapon.setRarity === 'function'\n"
        "  && typeof ctx.weapon.setFlags === 'undefined'\n"
        "  && typeof ctx.weapon.setAffects === 'undefined'\n"
        "  && typeof ctx.weapon.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.weapon.setRoom === 'undefined'\n"
        "  && typeof ctx.weapon.setCarriedBy === 'undefined'\n"
        "  && typeof ctx.weapon.setContainer === 'undefined'\n"
        "  && typeof ctx.weapon.setContents === 'undefined'\n"
        "  && typeof ctx.weapon.setTouched === 'undefined'\n"
        "  && typeof ctx.weapon.setLoadedBy === 'undefined'\n"
        "  && typeof ctx.actor.setProfile === 'undefined'\n"
        "  && typeof ctx.actor.setBaseAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setCurrentAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setRolledAbilities === 'undefined'\n"
        "  && typeof ctx.actor.setPoints === 'undefined'\n"
        "  && typeof ctx.actor.setSpecials === 'undefined'\n"
        "  && typeof ctx.actor.setSpecials2 === 'undefined'\n"
        "  && typeof ctx.actor.setProfessions === 'undefined'\n"
        "  && typeof ctx.actor.setSpecializations === 'undefined'\n"
        "  && typeof ctx.actor.setDamageDetails === 'undefined'\n"
        "  && typeof ctx.actor.setSkill === 'undefined'\n"
        "  && typeof ctx.actor.setKnowledge === 'undefined'\n"
        "  && typeof ctx.actor.setRoom === 'undefined'\n"
        "  && typeof ctx.actor.setAffects === 'undefined'\n"
        "  && typeof ctx.actor.setEquipmentSlot === 'undefined'\n"
        "  && typeof ctx.actor.setInventory === 'undefined'\n"
        "  && typeof ctx.actor.setFollowers === 'undefined'\n"
        "  && typeof ctx.actor.setMaster === 'undefined'\n"
        "  && typeof ctx.actor.setMount === 'undefined'\n"
        "  && typeof ctx.actor.group === 'undefined'\n"
        "  && typeof ctx.actor.setGroup === 'undefined'\n"
        "  && typeof ctx.actor.setClassPoints === 'undefined'\n"
        "  && typeof ctx.actor.setInterruptCount === 'undefined'\n"
        "  && typeof ctx.actor.setInterruptTime === 'undefined'\n"
        "  && typeof ctx.actor.setSpecialBusy === 'undefined'\n"
        "  && typeof ctx.object.room.setDescription === 'function'\n"
        "  && typeof ctx.object.room.setLevel === 'function'\n"
        "  && typeof ctx.object.room.setSectorType === 'function'\n"
        "  && typeof ctx.object.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.object.room.setExit === 'undefined'\n"
        "  && typeof ctx.object.room.setContents === 'undefined'\n"
        "  && typeof ctx.object.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.object.room.setAffects === 'undefined'\n"
        "  && typeof ctx.actor.room.setName === 'function'\n"
        "  && typeof ctx.actor.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.actor.room.setExit === 'undefined'\n"
        "  && typeof ctx.actor.room.setContents === 'undefined'\n"
        "  && typeof ctx.actor.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.actor.room.setAffects === 'undefined'\n"
        "  && typeof ctx.room.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.room.setExit === 'undefined'\n"
        "  && typeof ctx.room.setContents === 'undefined'\n"
        "  && typeof ctx.room.setCharacters === 'undefined'\n"
        "  && typeof ctx.room.setAffects === 'undefined'\n"
        "  && typeof ctx.room.zone.setName === 'function'\n"
        "  && typeof ctx.room.zone.setMap === 'function'\n"
        "  && typeof ctx.room.zone.setSymbol === 'function'\n"
        "  && typeof ctx.room.zone.setX === 'function'\n"
        "  && typeof ctx.room.zone.setY === 'function'\n"
        "  && typeof ctx.room.zone.setResetMode === 'function'\n"
        "  && typeof ctx.room.zone.setLifespan === 'function'\n"
        "  && typeof ctx.room.zone.setLevel === 'function'\n"
        "  && typeof ctx.room.zone.setMinimumLookLevel === 'undefined'\n"
        "  && typeof ctx.room.zone.setAge === 'undefined'\n"
        "  && typeof ctx.room.zone.setTopRoomVnum === 'undefined'\n"
        "  && typeof ctx.room.zone.setVnum === 'undefined'\n"
        "  && typeof ctx.room.zone.setWhitePower === 'undefined'\n"
        "  && typeof ctx.room.zone.setDarkPower === 'undefined'\n"
        "  && typeof ctx.room.zone.setMagiPower === 'undefined'\n"
        "  && typeof ctx.room.zone.setShortDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setMapDescriptions === 'undefined'\n"
        "  && typeof ctx.room.zone.setOwners === 'undefined'\n"
        "  && typeof ctx.room.zone.setResetCommandCount === 'undefined'\n"
        "  && typeof ctx.room.zone.setResetCommands === 'undefined'\n"
        "  && typeof ctx.zone.setMinimumLookLevel === 'undefined'\n"
        "  && typeof ctx.zone.setAge === 'undefined'\n"
        "  && typeof ctx.zone.setTopRoomVnum === 'undefined'\n"
        "  && typeof ctx.zone.setVnum === 'undefined'\n"
        "  && typeof ctx.zone.setWhitePower === 'undefined'\n"
        "  && typeof ctx.zone.setDarkPower === 'undefined'\n"
        "  && typeof ctx.zone.setMagiPower === 'undefined'\n"
        "  && typeof ctx.zone.setShortDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setExtraDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setMapDescriptions === 'undefined'\n"
        "  && typeof ctx.zone.setOwners === 'undefined'\n"
        "  && typeof ctx.zone.setResetCommandCount === 'undefined'\n"
        "  && typeof ctx.zone.setResetCommands === 'undefined'\n"
        "  && objectName.ok === true && objectName.code === 'ok'\n"
        "  && objectDescription.ok === true && objectShort.ok === true && objectAction.ok === "
        "true\n"
        "  && objectLevelLower.ok === true && objectLevelLower.code === 'ok'\n"
        "  && objectLevelLower.field === 'level'\n"
        "  && objectLevel.ok === true && objectLevel.code === 'ok'\n"
        "  && objectLevel.field === 'level' && objectLevel.message === null\n"
        "  && objectRarityLower.ok === true && objectRarityLower.code === 'ok'\n"
        "  && objectRarityLower.field === 'rarity'\n"
        "  && objectRarity.ok === true && objectRarity.code === 'ok'\n"
        "  && objectRarity.field === 'rarity' && objectRarity.message === null\n"
        "  && roomName.ok === true && roomDescription.ok === true\n"
        "  && roomLevelLower.ok === true && roomLevelLower.code === 'ok'\n"
        "  && roomLevelLower.field === 'level'\n"
        "  && roomLevel.ok === true && roomLevel.code === 'ok'\n"
        "  && roomLevel.field === 'level' && roomLevel.message === null\n"
        "  && roomSector.ok === true && roomSector.code === 'ok'\n"
        "  && roomSector.field === 'sectorType' && roomSector.message === null\n"
        "  && zoneName.ok === true && zoneDescription.ok === true && zoneMap.ok === true\n"
        "  && zoneSymbol.ok === true && zoneSymbol.code === 'ok' && zoneSymbol.field === 'symbol'\n"
        "  && zoneSymbol.message === null\n"
        "  && zoneXLower.ok === true && zoneXLower.code === 'ok' && zoneXLower.field === 'x'\n"
        "  && zoneX.ok === true && zoneX.code === 'ok' && zoneX.field === 'x'\n"
        "  && zoneX.message === null\n"
        "  && zoneYLower.ok === true && zoneYLower.code === 'ok' && zoneYLower.field === 'y'\n"
        "  && zoneY.ok === true && zoneY.code === 'ok' && zoneY.field === 'y'\n"
        "  && zoneY.message === null\n"
        "  && zoneResetModeLower.ok === true && zoneResetModeLower.code === 'ok'\n"
        "  && zoneResetModeLower.field === 'resetMode'\n"
        "  && zoneResetMode.ok === true && zoneResetMode.code === 'ok'\n"
        "  && zoneResetMode.field === 'resetMode' && zoneResetMode.message === null\n"
        "  && zoneLifespanLower.ok === true && zoneLifespanLower.code === 'ok'\n"
        "  && zoneLifespanLower.field === 'lifespan'\n"
        "  && zoneLifespan.ok === true && zoneLifespan.code === 'ok'\n"
        "  && zoneLifespan.field === 'lifespan' && zoneLifespan.message === null\n"
        "  && zoneLevelLower.ok === true && zoneLevelLower.code === 'ok'\n"
        "  && zoneLevelLower.field === 'level'\n"
        "  && zoneLevel.ok === true && zoneLevel.code === 'ok'\n"
        "  && zoneLevel.field === 'level' && zoneLevel.message === null\n"
        "  && badType.ok === false && badType.code === 'invalid-value' && badType.field === "
        "'name'\n"
        "  && badRange.ok === false && badRange.code === 'out-of-range'\n"
        "  && blankName.ok === false && blankName.code === 'invalid-value'\n"
        "  && badNul.ok === false && badNul.code === 'invalid-value'\n"
        "  && badMapType.ok === false && badMapType.field === 'map'\n"
        "  && badMapNul.ok === false && badMapNul.code === 'invalid-value'\n"
        "  && badMapRange.ok === false && badMapRange.code === 'out-of-range'\n"
        "  && badMapTilde.ok === false && badMapTilde.field === 'map'\n"
        "  && badMapHash.ok === false && badMapHash.field === 'map'\n"
        "  && [badSymbolType, badSymbolEmpty, badSymbolSpace, badSymbolLong, badSymbolNonAscii]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'symbol' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomLevelType, badRoomLevelFraction, badRoomLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomLevelNegative, badRoomLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badRoomSectorType, badRoomSectorNull, badRoomSectorUnknown, badRoomSectorLower, "
        "badRoomSectorWhitespace, badRoomSectorTrailing, badRoomSectorDisplay, "
        "badRoomSectorHyphen, badRoomSectorDenseDisplay, badRoomSectorDenseCaps, "
        "badRoomSectorNumericText, badRoomSectorEmpty]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'sectorType' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectLevelType, badObjectLevelFraction, badObjectLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectLevelNegative, badObjectLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectRarityType, badObjectRarityFraction, badObjectRarityNaN, "
        "badObjectRarityNull]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'rarity' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badObjectRarityNegative, badObjectRarityRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'rarity' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badXType, badXFraction, badXNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'x' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badXNegative, badXRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'x' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badYType, badYFraction, badYNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'y' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badYNegative, badYRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'y' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badResetModeType, badResetModeFraction, badResetModeNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'resetMode' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badResetModeNegative, badResetModeRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'resetMode' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLifespanType, badLifespanFraction, badLifespanNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'lifespan' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLifespanZero, badLifespanNegative, badLifespanRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'lifespan' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLevelType, badLevelFraction, badLevelNaN]\n"
        "    .every((result) => result.ok === false && result.code === 'invalid-value'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && [badLevelNegative, badLevelRange]\n"
        "    .every((result) => result.ok === false && result.code === 'out-of-range'\n"
        "      && result.field === 'level' && typeof result.message === 'string'\n"
        "      && result.message.length > 0 && result.message.length <= 120\n"
        "      && result.message.indexOf('\\n') === -1 && result.message.indexOf('\\r') === -1)\n"
        "  && nestedWeapon.ok === true && nestedWeaponLevel.ok === true\n"
        "  && nestedWeaponLevel.code === 'ok' && nestedWeaponLevel.field === 'level'\n"
        "  && nestedWeaponRarity.ok === true && nestedWeaponRarity.code === 'ok'\n"
        "  && nestedWeaponRarity.field === 'rarity'\n"
        "  && nestedObjectRoom.ok === true\n"
        "  && nestedObjectRoomLevel.ok === true && nestedObjectRoomLevel.code === 'ok'\n"
        "  && nestedObjectRoomLevel.field === 'level' && nestedObjectRoomLevel.message === null\n"
        "  && nestedObjectRoomSector.ok === true && nestedObjectRoomSector.code === 'ok'\n"
        "  && nestedObjectRoomSector.field === 'sectorType'\n"
        "  && nestedActorRoom.ok === true && nestedZone.ok === true && nestedZoneMap.ok === true\n"
        "  && nestedZoneY.ok === true && nestedZoneY.code === 'ok'\n"
        "  && nestedZoneY.field === 'y' && nestedZoneY.message === null\n"
        "  && nestedZoneResetMode.ok === true && nestedZoneResetMode.code === 'ok'\n"
        "  && nestedZoneResetMode.field === 'resetMode' && nestedZoneResetMode.message === null\n"
        "  && nestedZoneLifespan.ok === true && nestedZoneLifespan.code === 'ok'\n"
        "  && nestedZoneLifespan.field === 'lifespan' && nestedZoneLifespan.message === null\n"
        "  && nestedZoneLevel.ok === true && nestedZoneLevel.code === 'ok'\n"
        "  && nestedZoneLevel.field === 'level' && nestedZoneLevel.message === null\n"
        "  && Object.isFrozen(objectName) && Object.isFrozen(badType)\n"
        "  && typeof objectName.constructor === 'undefined'\n"
        "  && typeof badType.constructor === 'undefined'\n"
        "  && typeof badType.extra === 'undefined'\n"
        "  && typeof ctx.object.setName.prototype === 'undefined'\n"
        "  && Object.isFrozen(ctx.object.setName)\n"
        "  && ctx.object.name === 'bronze lever'\n"
        "  && ctx.object.description === 'A bronze lever is bolted here.'\n"
        "  && ctx.object.shortDescription === 'a bronze lever'\n"
        "  && ctx.object.actionDescription === null\n"
        "  && ctx.object.flags.level === 100\n"
        "  && ctx.object.flags.rarity === 255\n"
        "  && ctx.room.name === 'Southern Gate'\n"
        "  && ctx.room.description === 'A second gatehouse faces the road.'\n"
        "  && ctx.room.level === 100\n"
        "  && ctx.room.sectorType === 'Water_noswim'\n"
        "  && ctx.zone.name === 'New City'\n"
        "  && ctx.zone.description === null\n"
        "  && ctx.zone.map === 'N-G-S-E'\n"
        "  && ctx.zone.symbol === '*'\n"
        "  && ctx.zone.x === 25\n"
        "  && ctx.zone.resetMode === 3\n"
        "  && ctx.zone.lifespan === 10080\n"
        "  && ctx.zone.level === 100\n"
        "  && ctx.weapon.name === 'tempered sword'\n"
        "  && ctx.weapon.flags.level === 77\n"
        "  && ctx.weapon.flags.rarity === 201\n"
        "  && ctx.object.room.description === 'Nested object room edit.'\n"
        "  && ctx.object.room.level === 12\n"
        "  && ctx.object.room.sectorType === 'Underwater'\n"
        "  && ctx.actor.room.name === 'Actor Room Edit'\n"
        "  && ctx.room.zone.name === 'Nested Zone Edit'\n"
        "  && ctx.room.zone.map === null\n"
        "  && ctx.room.zone.y === 0\n"
        "  && ctx.room.zone.resetMode === 2\n"
        "  && ctx.room.zone.lifespan === 60\n"
        "  && ctx.room.zone.level === 45\n"
        "  && assignBlocked\n"
        "  && defineBlocked;",
        context);

    expect_ok_allows(result);

    expect_ok_allows(
        runtime.evaluate_trigger_body("return ctx.object.name === 'silver lever';", context));
}

TEST(JsGameRuntime, DoesNotExposeInternalMutationEnvelopeToScripts) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult body_result =
        runtime.evaluate_trigger_body("let bodyBlocked = false;\n"
                                      "try { __rotsMutations.push({ targetType: 'object' }); } "
                                      "catch (error) { bodyBlocked = true; }\n"
                                      "return bodyBlocked\n"
                                      "  && typeof __rotsJsonStringify === 'undefined'\n"
                                      "  && typeof __rotsAttachTextSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachSymbolSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachCoordinateSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachResetModeSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachLifespanSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachLevelSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachObjectLevelSetter === 'undefined'\n"
                                      "  && typeof __rotsAttachObjectRaritySetter === 'undefined'\n"
                                      "  && typeof __rotsAttachSectorTypeSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateRaritySetter === 'undefined'\n"
                                      "  && typeof __rotsValidateSectorTypeSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateTextSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateSymbolSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateCoordinateSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateResetModeSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateLifespanSetter === 'undefined'\n"
                                      "  && typeof __rotsValidateLevelSetter === 'undefined';",
                                      make_context());
    JsRuntimeEvalResult package_result = runtime.evaluate_trigger_package_handler(
        "exports.onEnter = function(ctx) {\n"
        "  let packageBlocked = false;\n"
        "  try { __rotsMutations.push({ targetType: 'object' }); } catch (error) { packageBlocked "
        "= true; }\n"
        "  return packageBlocked\n"
        "    && typeof __rotsJsonStringify === 'undefined'\n"
        "    && typeof __rotsAttachTextSetter === 'undefined'\n"
        "    && typeof __rotsAttachSymbolSetter === 'undefined'\n"
        "    && typeof __rotsAttachCoordinateSetter === 'undefined'\n"
        "    && typeof __rotsAttachResetModeSetter === 'undefined'\n"
        "    && typeof __rotsAttachLifespanSetter === 'undefined'\n"
        "    && typeof __rotsAttachLevelSetter === 'undefined'\n"
        "    && typeof __rotsAttachObjectLevelSetter === 'undefined'\n"
        "    && typeof __rotsAttachObjectRaritySetter === 'undefined'\n"
        "    && typeof __rotsAttachSectorTypeSetter === 'undefined'\n"
        "    && typeof __rotsValidateRaritySetter === 'undefined'\n"
        "    && typeof __rotsValidateSectorTypeSetter === 'undefined'\n"
        "    && typeof __rotsValidateTextSetter === 'undefined'\n"
        "    && typeof __rotsValidateSymbolSetter === 'undefined'\n"
        "    && typeof __rotsValidateCoordinateSetter === 'undefined'\n"
        "    && typeof __rotsValidateResetModeSetter === 'undefined'\n"
        "    && typeof __rotsValidateLifespanSetter === 'undefined'\n"
        "    && typeof __rotsValidateLevelSetter === 'undefined';\n"
        "};\n",
        "onEnter", make_context());

    expect_ok_allows(body_result);
    expect_ok_allows(package_result);
}

TEST(JsGameRuntime, RoomSectorTypeSetterAcceptsEveryCanonicalLiveSectorName) {
    ASSERT_NE(sector_types, nullptr);
    ASSERT_GT(num_of_sector_types, 0);

    JsGameRuntime runtime;
    for (int sector = 0; sector < num_of_sector_types; ++sector) {
        ASSERT_NE(sector_types[sector], nullptr) << sector;
        const std::string sector_name = sector_types[sector];
        ASSERT_NE(sector_name, "Unknown") << sector;
        JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
            "const result = ctx.room.setSectorType('" + sector_name +
                "');\n"
                "return result.ok === true && result.code === 'ok' && result.field === "
                "'sectorType'\n"
                "  && result.message === null && ctx.room.sectorType === '" +
                sector_name + "';",
            make_context());
        EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << sector_name << ": " << result.diagnostic;
        EXPECT_EQ(result.value, JsRuntimeValue::Allow) << sector_name;
    }

    JsRuntimeEvalResult unknown_result = runtime.evaluate_trigger_body(
        "const result = ctx.room.setSectorType('Unknown');\n"
        "return result.ok === false && result.code === 'invalid-value'\n"
        "  && result.field === 'sectorType' && ctx.room.sectorType === 'City';",
        make_context());
    expect_ok_allows(unknown_result);
}

TEST(JsGameRuntime, RejectsExcessiveSetterMutationCounts) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "for (let index = 0; index < 65; index += 1) ctx.object.setDescription('edit ' + index);\n"
        "return true;",
        make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_TRUE(result.mutations.empty());
}

TEST(JsGameRuntime, ExposesNoOpConsoleLogForOfflineParity) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "console.log('builder fixture note'); return true;", make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, NormalizesWrapperReturnValues) {
    JsGameRuntime runtime;

    expect_ok_allows(runtime.evaluate_trigger_body("return true;", make_context()));
    expect_ok_allows(runtime.evaluate_trigger_body("return undefined;", make_context()));
    expect_ok_allows(runtime.evaluate_trigger_body("return {};", make_context()));

    EXPECT_EQ(runtime.evaluate_trigger_body("return 0;", make_context()).value,
              JsRuntimeValue::Block);
    EXPECT_EQ(runtime.evaluate_trigger_body("return '';", make_context()).value,
              JsRuntimeValue::Block);
}

TEST(JsGameRuntime, RejectsWrapperBreakoutAttempts) {
    JsGameRuntime runtime;

    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("})(ctx); false; (function(ctx) {", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_NE(result.diagnostic.find("structurally valid"), std::string::npos);
}

TEST(JsGameRuntime, RejectsMutationOfInjectedContext) {
    JsGameRuntime runtime;

    JsRuntimeEvalResult assign_result = runtime.evaluate_trigger_body("ctx.self.name = 'changed';\n"
                                                                      "return true;",
                                                                      make_context());

    EXPECT_EQ(assign_result.status, JsRuntimeStatus::Error);
    EXPECT_NE(assign_result.diagnostic.find("read-only"), std::string::npos)
        << assign_result.diagnostic;

    EXPECT_EQ(
        runtime.evaluate_trigger_body("ctx.added = true; return true;", make_context()).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime.evaluate_trigger_body("delete ctx.trigger.name; return true;", make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body(
                "Object.defineProperty(ctx.trigger, 'name', { value: 'changed' }); return true;",
                make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("Object.setPrototypeOf(ctx.self, {}); return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ContextObjectsHaveNoMutablePrototypeSurface) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "try { Object.prototype.pointer = 'polluted'; } catch (error) {}\n"
        "try { Object.defineProperty(Object.prototype, 'raw', { get() { return 'polluted'; } }); }"
        " catch (error) {}\n"
        "return !('pointer' in ctx.self)\n"
        "  && !('raw' in ctx.room)\n"
        "  && typeof ctx.self.baseAbilities.constructor === 'undefined'\n"
        "  && typeof ctx.self.currentAbilities.constructor === 'undefined'\n"
        "  && typeof ctx.self.rolledAbilities.constructor === 'undefined'\n"
        "  && typeof ctx.self.points.constructor === 'undefined'\n"
        "  && typeof ctx.self.specials.constructor === 'undefined'\n"
        "  && typeof ctx.self.specials2.constructor === 'undefined'\n"
        "  && typeof ctx.self.specials2.conditions.constructor === 'undefined'\n"
        "  && typeof ctx.self.specials2.actFlags.constructor === 'undefined'\n"
        "  && typeof ctx.self.professions.constructor === 'undefined'\n"
        "  && typeof ctx.self.professions[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.specializations.constructor === 'undefined'\n"
        "  && typeof ctx.self.damageDetails.constructor === 'undefined'\n"
        "  && typeof ctx.self.damageDetails.entries.constructor === 'undefined'\n"
        "  && typeof ctx.self.damageDetails.entries[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.skills.constructor === 'undefined'\n"
        "  && typeof ctx.self.skills[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.knowledge.constructor === 'undefined'\n"
        "  && typeof ctx.self.knowledge[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.affects.constructor === 'undefined'\n"
        "  && typeof ctx.self.affects[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.affects[0].bitvectorNames.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.__rotsReadOnlySnapshot === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setName === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setDescription === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setShortDescription === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setActionDescription === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setLevel === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.setRarity === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.flags.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.flags.wearFlags.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.affects.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.affects[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.extraDescriptions.constructor === 'undefined'\n"
        "  && typeof ctx.self.equipment[6].object.extraDescriptions[0].constructor === "
        "'undefined'\n"
        "  && typeof ctx.self.inventory.constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].__rotsReadOnlySnapshot === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setName === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setDescription === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setShortDescription === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setActionDescription === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setLevel === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].setRarity === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].flags.constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].flags.wearFlags.constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].affects.constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].affects[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].extraDescriptions.constructor === 'undefined'\n"
        "  && typeof ctx.self.inventory[0].extraDescriptions[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.followers.constructor === 'undefined'\n"
        "  && typeof ctx.self.followers[0].constructor === 'undefined'\n"
        "  && typeof ctx.self.followers[0].__rotsReadOnlySnapshot === 'undefined'\n"
        "  && typeof ctx.self.followers[0].setName === 'undefined'\n"
        "  && typeof ctx.self.followers[0].setLevel === 'undefined'\n"
        "  && typeof ctx.self.master.constructor === 'undefined'\n"
        "  && typeof ctx.self.master.__rotsReadOnlySnapshot === 'undefined'\n"
        "  && typeof ctx.self.master.setName === 'undefined'\n"
        "  && typeof ctx.self.master.setLevel === 'undefined'\n"
        "  && typeof ctx.self.mount.constructor === 'undefined'\n"
        "  && typeof ctx.self.mount.__rotsReadOnlySnapshot === 'undefined'\n"
        "  && typeof ctx.self.mount.mount.constructor === 'undefined'\n"
        "  && typeof ctx.self.mount.mount.setName === 'undefined'\n"
        "  && typeof ctx.self.mount.rider.constructor === 'undefined'\n"
        "  && typeof ctx.self.mount.nextRider.constructor === 'undefined'\n"
        "  && typeof ctx.self.points.bodypartHits.constructor === 'undefined'\n"
        "  && Object.getPrototypeOf(ctx) === null\n"
        "  && Object.getPrototypeOf(ctx.self) === null\n"
        "  && Object.getPrototypeOf(ctx.self.baseAbilities) === null\n"
        "  && Object.getPrototypeOf(ctx.self.currentAbilities) === null\n"
        "  && Object.getPrototypeOf(ctx.self.rolledAbilities) === null\n"
        "  && Object.getPrototypeOf(ctx.self.points) === null\n"
        "  && Object.getPrototypeOf(ctx.self.specials) === null\n"
        "  && Object.getPrototypeOf(ctx.self.specials2) === null\n"
        "  && Object.getPrototypeOf(ctx.self.specials2.conditions) === null\n"
        "  && Object.getPrototypeOf(ctx.self.professions[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.specializations) === null\n"
        "  && Object.getPrototypeOf(ctx.self.damageDetails) === null\n"
        "  && Object.getPrototypeOf(ctx.self.damageDetails.entries[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.skills[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.knowledge[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.affects[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.equipment[6]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.equipment[6].object) === null\n"
        "  && Object.getPrototypeOf(ctx.self.equipment[6].object.flags) === null\n"
        "  && Object.getPrototypeOf(ctx.self.equipment[6].object.affects[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.equipment[6].object.extraDescriptions[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.inventory[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.inventory[0].flags) === null\n"
        "  && Object.getPrototypeOf(ctx.self.inventory[0].affects[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.inventory[0].extraDescriptions[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.followers[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.self.master) === null\n"
        "  && Object.getPrototypeOf(ctx.trigger) === null\n"
        "  && Object.isFrozen(ctx)\n"
        "  && Object.isFrozen(ctx.self)\n"
        "  && Object.isFrozen(ctx.self.profile)\n"
        "  && Object.isFrozen(ctx.self.profile.talks)\n"
        "  && Object.isFrozen(ctx.self.baseAbilities)\n"
        "  && Object.isFrozen(ctx.self.currentAbilities)\n"
        "  && Object.isFrozen(ctx.self.rolledAbilities)\n"
        "  && Object.isFrozen(ctx.self.points)\n"
        "  && Object.isFrozen(ctx.self.specials)\n"
        "  && Object.isFrozen(ctx.self.specials2)\n"
        "  && Object.isFrozen(ctx.self.specials2.conditions)\n"
        "  && Object.isFrozen(ctx.self.specials2.actFlags)\n"
        "  && Object.isFrozen(ctx.self.specials2.preferenceFlags)\n"
        "  && Object.isFrozen(ctx.self.specials2.hideFlags)\n"
        "  && Object.isFrozen(ctx.self.professions)\n"
        "  && Object.isFrozen(ctx.self.professions[0])\n"
        "  && Object.isFrozen(ctx.self.specializations)\n"
        "  && Object.isFrozen(ctx.self.damageDetails)\n"
        "  && Object.isFrozen(ctx.self.damageDetails.entries)\n"
        "  && Object.isFrozen(ctx.self.damageDetails.entries[0])\n"
        "  && Object.isFrozen(ctx.self.skills)\n"
        "  && Object.isFrozen(ctx.self.skills[0])\n"
        "  && Object.isFrozen(ctx.self.knowledge)\n"
        "  && Object.isFrozen(ctx.self.knowledge[0])\n"
        "  && Object.isFrozen(ctx.self.affects)\n"
        "  && Object.isFrozen(ctx.self.affects[0])\n"
        "  && Object.isFrozen(ctx.self.affects[0].bitvectorNames)\n"
        "  && Object.isFrozen(ctx.self.equipment)\n"
        "  && Object.isFrozen(ctx.self.equipment[6])\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.flags)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.flags.wearFlags)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.flags.extraFlags)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.affects)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.affects[0])\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.extraDescriptions)\n"
        "  && Object.isFrozen(ctx.self.equipment[6].object.extraDescriptions[0])\n"
        "  && Object.isFrozen(ctx.self.inventory)\n"
        "  && Object.isFrozen(ctx.self.inventory[0])\n"
        "  && Object.isFrozen(ctx.self.inventory[0].flags)\n"
        "  && Object.isFrozen(ctx.self.inventory[0].flags.wearFlags)\n"
        "  && Object.isFrozen(ctx.self.inventory[0].flags.extraFlags)\n"
        "  && Object.isFrozen(ctx.self.inventory[0].affects)\n"
        "  && Object.isFrozen(ctx.self.inventory[0].affects[0])\n"
        "  && Object.isFrozen(ctx.self.inventory[0].extraDescriptions)\n"
        "  && Object.isFrozen(ctx.self.inventory[0].extraDescriptions[0])\n"
        "  && typeof ctx.object.affects.constructor === 'undefined'\n"
        "  && typeof ctx.object.affects[0].constructor === 'undefined'\n"
        "  && typeof ctx.object.extraDescriptions.constructor === 'undefined'\n"
        "  && typeof ctx.object.extraDescriptions[0].constructor === 'undefined'\n"
        "  && typeof ctx.object.container.constructor === 'undefined'\n"
        "  && typeof ctx.object.contents.constructor === 'undefined'\n"
        "  && typeof ctx.object.contents[0].constructor === 'undefined'\n"
        "  && typeof ctx.room.contents.constructor === 'undefined'\n"
        "  && typeof ctx.room.contents[0].constructor === 'undefined'\n"
        "  && typeof ctx.room.characters.constructor === 'undefined'\n"
        "  && typeof ctx.room.characters[0].constructor === 'undefined'\n"
        "  && typeof ctx.room.affects.constructor === 'undefined'\n"
        "  && typeof ctx.room.affects[0].constructor === 'undefined'\n"
        "  && typeof ctx.room.affects[0].bitvectorNames.constructor === 'undefined'\n"
        "  && Object.getPrototypeOf(ctx.object.affects[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.object.extraDescriptions[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.object.container) === null\n"
        "  && Object.getPrototypeOf(ctx.object.contents[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.room.contents[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.room.characters[0]) === null\n"
        "  && Object.getPrototypeOf(ctx.room.affects[0]) === null\n"
        "  && Object.isFrozen(ctx.object.affects)\n"
        "  && Object.isFrozen(ctx.object.affects[0])\n"
        "  && Object.isFrozen(ctx.object.extraDescriptions)\n"
        "  && Object.isFrozen(ctx.object.extraDescriptions[0])\n"
        "  && Object.isFrozen(ctx.object.container)\n"
        "  && Object.isFrozen(ctx.object.contents)\n"
        "  && Object.isFrozen(ctx.object.contents[0])\n"
        "  && Object.isFrozen(ctx.room.contents)\n"
        "  && Object.isFrozen(ctx.room.contents[0])\n"
        "  && Object.isFrozen(ctx.room.characters)\n"
        "  && Object.isFrozen(ctx.room.characters[0])\n"
        "  && Object.isFrozen(ctx.room.affects)\n"
        "  && Object.isFrozen(ctx.room.affects[0])\n"
        "  && Object.isFrozen(ctx.room.affects[0].bitvectorNames)\n"
        "  && Object.isFrozen(ctx.self.followers)\n"
        "  && Object.isFrozen(ctx.self.followers[0])\n"
        "  && Object.isFrozen(ctx.self.master)\n"
        "  && Object.isFrozen(ctx.self.mount)\n"
        "  && Object.isFrozen(ctx.self.mount.mount)\n"
        "  && Object.isFrozen(ctx.self.mount.rider)\n"
        "  && Object.isFrozen(ctx.self.mount.nextRider)\n"
        "  && Object.isFrozen(ctx.self.points.bodypartHits)\n"
        "  && Object.isFrozen(ctx.trigger);",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsMutationOfNestedCharacterSnapshots) {
    JsGameRuntime runtime;
    for (const char *property : {"baseAbilities", "currentAbilities", "rolledAbilities"}) {
        JsRuntimeEvalResult result =
            runtime.evaluate_trigger_body(std::string("ctx.self.") + property +
                                              ".strength = 1;\n"
                                              "return true;",
                                          make_context());

        EXPECT_EQ(result.status, JsRuntimeStatus::Error) << property;
    }
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.points.gold = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.specials.energy = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.specials2.alignment = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.specials2.conditions.full = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.specials2.actFlags[0] = 'deleted';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.professions[0].level = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.professions.push({ key: 'test' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.specializations.selectedKey = 'fire';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.damageDetails.totalDamage = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.damageDetails.entries[0].totalDamage = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.skills[0].practice = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.skills.push({ id: 42, name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.knowledge[0].knowledge = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.knowledge.push({ id: 42, name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.affects[0].duration = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.affects.push({ type: 42, name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.affects[0].bitvectorNames[0] = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.affects[0].modifier = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.affects.push({ slotIndex: 1 });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.extraDescriptions[0].keyword = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body("ctx.object.extraDescriptions.push({ keyword: 'unsafe' });\n"
                                   "return true;",
                                   make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.container.name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.contents[0].name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.contents.push({ id: 'object:unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.contents[0].name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.contents.push({ id: 'object:unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.characters[0].name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.characters.push({ name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.affects[0].duration = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.affects.push({ type: 42, name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.room.affects[0].bitvectorNames[0] = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.object.touched = false;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.equipment[6].slotName = 'wield';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.equipment[6].object.name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.equipment[6].object.flags.level = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body("ctx.self.equipment[6].object.flags.wearFlags.push('unsafe');\n"
                                   "return true;",
                                   make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body("ctx.self.equipment[6].object.flags.extraFlags[0] = 'unsafe';\n"
                                   "return true;",
                                   make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.equipment[6].object.affects[0].modifier = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body("ctx.self.equipment[6].object.affects.push({ slotIndex: 1 });\n"
                                   "return true;",
                                   make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime
            .evaluate_trigger_body("ctx.self.equipment[6].object.extraDescriptions[0].keyword = "
                                   "'unsafe';\n"
                                   "return true;",
                                   make_context())
            .status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.equipment.push({ slotName: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].flags.level = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].flags.wearFlags.push('unsafe');\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].affects[0].modifier = 99;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].affects.push({ slotIndex: 1 });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory[0].extraDescriptions.push({ keyword: "
                                         "'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.inventory.push({ name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.followers[0].name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.followers.push({ name: 'unsafe' });\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.master.name = 'unsafe';\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
    EXPECT_EQ(runtime
                  .evaluate_trigger_body("ctx.self.points.bodypartHits[0] = 1;\n"
                                         "return true;",
                                         make_context())
                  .status,
              JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, DefaultsMissingCharacterPointBodypartsToLiveShape) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-points";
    context.self.name = "Default Points";
    context.self.points.bodypart_hits.clear();

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.points.bodypartHits.length === 11\n"
        "  && ctx.self.points.bodypartHits.every(function(hit) { return hit === 0; })\n"
        "  && Object.isFrozen(ctx.self.points.bodypartHits)\n"
        "  && typeof ctx.self.points.bodypartHits.constructor === 'undefined';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterDamageDetailsToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-damage";
    context.self.name = "Default Damage";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self.damageDetails.elapsedCombatSeconds === 0\n"
        "  && ctx.self.damageDetails.totalDamage === 0\n"
        "  && ctx.self.damageDetails.damagePerSecond === 0\n"
        "  && ctx.self.damageDetails.entries.length === 0\n"
        "  && Object.isFrozen(ctx.self.damageDetails)\n"
        "  && Object.isFrozen(ctx.self.damageDetails.entries)\n"
        "  && Object.getPrototypeOf(ctx.self.damageDetails) === null\n"
        "  && typeof ctx.self.damageDetails.constructor === 'undefined';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterSkillsToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-skills";
    context.self.name = "Default Skills";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.skills.length === 0\n"
                                      "  && Object.isFrozen(ctx.self.skills)\n"
                                      "  && typeof ctx.self.skills.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterKnowledgeToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-knowledge";
    context.self.name = "Default Knowledge";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.knowledge.length === 0\n"
                                      "  && Object.isFrozen(ctx.self.knowledge)\n"
                                      "  && typeof ctx.self.knowledge.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterAffectsToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-affects";
    context.self.name = "Default Affects";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.affects.length === 0\n"
                                      "  && Object.isFrozen(ctx.self.affects)\n"
                                      "  && typeof ctx.self.affects.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingObjectAffectsToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_object = true;
    context.object.id = "object:default-affects";
    context.object.name = "Default Object";
    context.object.short_description = "Default Object";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.object.affects.length === 0\n"
                                      "  && Object.isFrozen(ctx.object.affects)\n"
                                      "  && typeof ctx.object.affects.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingObjectExtraDescriptionsToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_object = true;
    context.object.id = "object:default-extra-descriptions";
    context.object.name = "Default Object";
    context.object.short_description = "Default Object";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.object.extraDescriptions.length === 0\n"
        "  && Object.isFrozen(ctx.object.extraDescriptions)\n"
        "  && typeof ctx.object.extraDescriptions.constructor === 'undefined';",
        context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterEquipmentToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-equipment";
    context.self.name = "Default Equipment";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.equipment.length === 22\n"
                                      "  && ctx.self.equipment[0].slotName === 'light'\n"
                                      "  && ctx.self.equipment[0].object === null\n"
                                      "  && ctx.self.equipment[6].slotName === 'head'\n"
                                      "  && ctx.self.equipment[6].object === null\n"
                                      "  && ctx.self.equipment[16].slotName === 'wield'\n"
                                      "  && ctx.self.equipment[16].object === null\n"
                                      "  && Object.isFrozen(ctx.self.equipment)\n"
                                      "  && Object.isFrozen(ctx.self.equipment[6])\n"
                                      "  && typeof ctx.self.equipment.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterInventoryToEmptySnapshot) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-inventory";
    context.self.name = "Default Inventory";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.inventory.length === 0\n"
                                      "  && Object.isFrozen(ctx.self.inventory)\n"
                                      "  && typeof ctx.self.inventory.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DefaultsMissingCharacterRelationshipsToEmptySnapshots) {
    JsGameTriggerContextFixture context;
    context.has_self = true;
    context.self.id = "char:default-follow";
    context.self.name = "Default Follow";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.followers.length === 0\n"
                                      "  && ctx.self.master === null\n"
                                      "  && ctx.self.mount.mount === null\n"
                                      "  && ctx.self.mount.rider === null\n"
                                      "  && ctx.self.mount.nextRider === null\n"
                                      "  && ctx.self.mount.isRiding === false\n"
                                      "  && ctx.self.mount.isMounted === false\n"
                                      "  && Object.isFrozen(ctx.self.followers)\n"
                                      "  && Object.isFrozen(ctx.self.mount)\n"
                                      "  && typeof ctx.self.followers.constructor === 'undefined';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ModelsMissingHandlesAsNull) {
    JsGameTriggerContextFixture context = make_context();
    context.has_self = false;
    context.has_actor = false;
    context.has_speaker = false;
    context.has_attacker = false;
    context.has_victim = false;
    context.has_killer = false;
    context.has_object = false;
    context.has_weapon = false;
    context.has_room = false;
    context.has_zone = false;
    context.has_text = false;
    context.has_wear_slot = false;
    context.has_command = false;
    context.has_args = false;
    context.has_tick = false;
    context.has_direction = false;
    context.has_reverse_direction = false;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.self === null\n"
        "  && ctx.actor === null\n"
        "  && ctx.speaker === null\n"
        "  && ctx.attacker === null\n"
        "  && ctx.victim === null\n"
        "  && ctx.killer === null\n"
        "  && ctx.object === null\n"
        "  && ctx.weapon === null\n"
        "  && ctx.room === null\n"
        "  && ctx.zone === null\n"
        "  && ctx.text === null\n"
        "  && ctx.wearSlot === null\n"
        "  && ctx.command === null\n"
        "  && ctx.args === null\n"
        "  && ctx.target === null\n"
        "  && ctx.tick === null\n"
        "  && ctx.direction === null\n"
        "  && ctx.reverseDirection === null\n"
        "  && ctx.targ1 === null\n"
        "  && ctx.targ2 === null\n"
        "  && Array.isArray(ctx.targetTypes)\n"
        "  && ctx.targetTypes.length === 0\n"
        "  && Object.isFrozen(ctx.targetTypes)\n"
        "  && Object.getPrototypeOf(ctx.targetTypes) === Array.prototype\n"
        "  && typeof ctx.targetTypes.constructor === 'undefined'\n"
        "  && ctx.dying === null;",
        context);

    expect_ok_allows(result);

    JsRuntimeEvalResult target_types_mutation_result =
        runtime.evaluate_trigger_body("ctx.targetTypes[0] = 'char'; return true;", context);
    EXPECT_EQ(target_types_mutation_result.status, JsRuntimeStatus::Error);

    JsRuntimeEvalResult dereference_result =
        runtime.evaluate_trigger_body("return ctx.self.name === 'Aldren';", context);
    EXPECT_EQ(dereference_result.status, JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, ExposesWearSlotWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.has_wear_slot = true;
    context.wear_slot = "head";
    context.trigger.name = "onWear";
    context.trigger.legacy_name = "ON_WEAR";
    context.trigger.legacy_value = 21;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return ctx.wearSlot === 'head' && Object.isFrozen(ctx);", context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesScalarCommandAndMovementPayloadsWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.has_command = true;
    context.command = "open";
    context.has_args = true;
    context.args = "north gate";
    context.has_tick = true;
    context.tick = 42;
    context.has_direction = true;
    context.direction = "north";
    context.has_reverse_direction = true;
    context.reverse_direction = "south";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.command === 'open'\n"
                                      "  && ctx.args === 'north gate'\n"
                                      "  && ctx.tick === 42\n"
                                      "  && ctx.direction === 'north'\n"
                                      "  && ctx.reverseDirection === 'south'\n"
                                      "  && ctx.target === null\n"
                                      "  && ctx.dying === null;",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, ExposesTypedTargetsWhenPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.has_targ1 = true;
    context.targ1.type = "character";
    context.targ1.has_character = true;
    context.targ1.character = context.actor;
    context.targ1.character.id = "targ1";
    context.has_targ2 = true;
    context.targ2.type = "object";
    context.targ2.has_object = true;
    context.targ2.object = context.object;
    context.targ2.object.id = "targ2";
    context.has_target = true;
    context.target = context.targ1;
    context.target.character.id = "target";
    context.target_types = {"character", "object"};
    context.has_dying = true;
    context.dying = context.self;
    context.dying.id = "dying";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "const characterSetterNames = [\n"
        "  'setProfile', 'setBaseAbilities', 'setCurrentAbilities', 'setRolledAbilities',\n"
        "  'setPoints', 'setSpecials', 'setSpecials2', 'setProfessions',\n"
        "  'setSpecializations', 'setDamageDetails', 'setSkill', 'setKnowledge',\n"
        "  'setRoom', 'setAffects', 'setEquipmentSlot', 'setInventory', 'setFollowers',\n"
        "  'setMaster', 'setMount', 'setGroup', 'setClassPoints', 'setInterruptCount',\n"
        "  'setInterruptTime', 'setSpecialBusy'\n"
        "];\n"
        "const objectSetterNames = [\n"
        "  'setFlags', 'setAffects', 'setExtraDescriptions', 'setContents'\n"
        "];\n"
        "const roomSetterNames = [\n"
        "  'setExtraDescriptions', 'setExit', 'setContents', 'setCharacters', 'setAffects'\n"
        "];\n"
        "return ctx.target.name === 'Builder'\n"
        "  && ctx.target.id === 'target'\n"
        "  && ctx.targ1.name === 'Builder'\n"
        "  && ctx.targ1.id === 'targ1'\n"
        "  && ctx.targ2.name === 'silver lever'\n"
        "  && ctx.targ2.id === 'targ2'\n"
        "  && ctx.targ2.contents[0].name === 'small gear'\n"
        "  && ctx.targ2.touched === true\n"
        "  && ctx.targ2.contents[0].touched === true\n"
        "  && typeof ctx.targ2.contents[0].contents === 'undefined'\n"
        "  && typeof ctx.targ2.contents[0].setName === 'undefined'\n"
        "  && ctx.dying.name === 'Aldren'\n"
        "  && ctx.dying.id === 'dying'\n"
        "  && ctx.targetTypes[0] === 'character'\n"
        "  && ctx.targetTypes[1] === 'object'\n"
        "  && Object.isFrozen(ctx.target)\n"
        "  && Object.isFrozen(ctx.targ1)\n"
        "  && Object.isFrozen(ctx.targ2)\n"
        "  && Object.isFrozen(ctx.dying)\n"
        "  && Object.isFrozen(ctx.targetTypes)\n"
        "  && Object.getPrototypeOf(ctx.targ1) === null\n"
        "  && Object.getPrototypeOf(ctx.targetTypes) === Array.prototype\n"
        "  && [ctx.target, ctx.targ1, ctx.dying].every(function(character) {\n"
        "    return characterSetterNames.every(function(name) {\n"
        "      return typeof character[name] === 'undefined';\n"
        "    });\n"
        "  })\n"
        "  && objectSetterNames.every(function(name) {\n"
        "    return typeof ctx.targ2[name] === 'undefined';\n"
        "  })\n"
        "  && roomSetterNames.every(function(name) {\n"
        "    return typeof ctx.targ2.room[name] === 'undefined';\n"
        "  })\n"
        "  && typeof ctx.targ1.constructor === 'undefined';",
        context);

    expect_ok_allows(result);

    JsGameTriggerContextFixture room_context = make_context();
    room_context.has_target = true;
    room_context.target.type = "room";
    room_context.target.has_room = true;
    room_context.target.room = room_context.room;
    room_context.target.room.id = "target-room";
    room_context.has_targ2 = true;
    room_context.targ2.type = "room";
    room_context.targ2.has_room = true;
    room_context.targ2.room = room_context.room;
    room_context.targ2.room.id = "targ2-room";

    JsRuntimeEvalResult room_result = runtime.evaluate_trigger_body(
        "const roomSetterNames = [\n"
        "  'setExtraDescriptions', 'setExit', 'setContents', 'setCharacters', 'setAffects'\n"
        "];\n"
        "return ctx.target.id === 'target-room'\n"
        "  && ctx.targ2.id === 'targ2-room'\n"
        "  && Object.isFrozen(ctx.target)\n"
        "  && Object.isFrozen(ctx.targ2)\n"
        "  && [ctx.target, ctx.targ2].every(function(room) {\n"
        "    return roomSetterNames.every(function(name) {\n"
        "      return typeof room[name] === 'undefined';\n"
        "    });\n"
        "  });",
        room_context);

    expect_ok_allows(room_result);

    EXPECT_EQ(
        runtime.evaluate_trigger_body("ctx.targ1.name = 'mutated'; return true;", context).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime.evaluate_trigger_body("ctx.targetTypes[0] = 'room'; return true;", context).status,
        JsRuntimeStatus::Error);
    EXPECT_EQ(
        runtime.evaluate_trigger_body("ctx.targ1.room.name = 'mutated'; return true;", context)
            .status,
        JsRuntimeStatus::Error);
}

TEST(JsGameRuntime, TreatsZeroTickAsPresent) {
    JsGameTriggerContextFixture context = make_context();
    context.has_tick = true;
    context.tick = 0;

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("return ctx.tick === 0;", context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, EscapesFixtureStringsBeforeEvaluation) {
    JsGameTriggerContextFixture context = make_context();
    context.self.name = "self \"quoted\" \\ name";
    context.actor.race = "race\x01value";
    context.object.name = "object\nname";
    context.room.name = "room\rname";
    context.zone.name = "zone\tname";
    context.trigger.name = "trigger `name`";
    context.trigger.legacy_name = "ON_\"PULL\"";
    context.text = "line one\r\nline \"two\" \\\\ end";
    context.has_command = true;
    context.command = "cmd\nname";
    context.has_args = true;
    context.args = "arg \"quoted\"";
    context.has_direction = true;
    context.direction = "north\r";
    context.has_reverse_direction = true;
    context.reverse_direction = "south\t";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.self.name === 'self \"quoted\" \\\\ name'\n"
                                      "  && ctx.actor.race.charCodeAt(4) === 1\n"
                                      "  && ctx.object.name === 'object\\nname'\n"
                                      "  && ctx.room.name === 'room\\rname'\n"
                                      "  && ctx.zone.name === 'zone\\tname'\n"
                                      "  && ctx.trigger.name === 'trigger `name`'\n"
                                      "  && ctx.trigger.handlerName === 'trigger `name`'\n"
                                      "  && ctx.trigger.kind === 'legacy'\n"
                                      "  && ctx.trigger.legacyName === 'ON_\"PULL\"'\n"
                                      "  && ctx.text.indexOf('line one\\r\\n') === 0\n"
                                      "  && ctx.text.includes('line \"two\"')\n"
                                      "  && ctx.text.includes('\\\\\\\\ end')\n"
                                      "  && ctx.command === 'cmd\\nname'\n"
                                      "  && ctx.args === 'arg \"quoted\"'\n"
                                      "  && ctx.direction === 'north\\r'\n"
                                      "  && ctx.reverseDirection === 'south\\t';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, InheritsRuntimeInstructionLimits) {
    JsRuntimeLimits limits;
    limits.instruction_budget = 64;
    JsGameRuntime runtime(limits);

    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("while (true) {}", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Interrupted);
}

TEST(JsGameRuntime, DoesNotPersistScriptStateAcrossEvaluations) {
    JsGameRuntime runtime;

    JsRuntimeEvalResult first = runtime.evaluate_trigger_body(
        "globalThis.process = { env: {} };\n"
        "try { Object.prototype.pointer = 'polluted'; } catch (error) {}\n"
        "return true;",
        make_context());
    EXPECT_EQ(first.status, JsRuntimeStatus::Ok) << first.diagnostic;

    JsRuntimeEvalResult second = runtime.evaluate_trigger_body(
        "return typeof process === 'undefined' && !('pointer' in ctx.self);", make_context());
    expect_ok_allows(second);
}

TEST(JsGameRuntime, DoesNotExposeRawPointersOrProcessGlobals) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body(
        "return typeof ctx.self.pointer === 'undefined'\n"
        "  && typeof ctx.self.address === 'undefined'\n"
        "  && typeof ctx.room.raw === 'undefined'\n"
        "  && typeof ctx.self.constructor === 'undefined'\n"
        "  && typeof ctx.self.isValid.constructor === 'undefined'\n"
        "  && typeof ctx.trigger.toString === 'undefined'\n"
        "  && typeof RotS.ScriptResult.allow.constructor === 'undefined'\n"
        "  && typeof process === 'undefined'\n"
        "  && typeof require === 'undefined'\n"
        "  && typeof ({}).constructor === 'undefined'\n"
        "  && typeof (function() {}).constructor === 'undefined';",
        make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RedactsThrownActorTextFromDiagnostics) {
    JsGameTriggerContextFixture context = make_context();
    context.text = "private player text\r\nwith newline";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_body("throw ctx.text;", context);

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.diagnostic.find("private player text"), std::string::npos);
    EXPECT_EQ(result.diagnostic.find('\n'), std::string::npos);
    EXPECT_LE(result.diagnostic.size(), 120);
}

TEST(JsGameRuntime, BuildsStableContextLiteral) {
    std::string literal = js_game_trigger_context_literal(make_context());

    EXPECT_NE(literal.find("\"self\":{\"id\":\"char:1001\""), std::string::npos);
    EXPECT_NE(literal.find("\"experience\":42000"), std::string::npos);
    EXPECT_NE(literal.find("\"rank\":9"), std::string::npos);
    EXPECT_NE(literal.find("\"room\":{\"id\":\"room:1204\""), std::string::npos);
    EXPECT_NE(literal.find("\"isSunlit\":true"), std::string::npos);
    EXPECT_NE(literal.find("\"sectorType\":\"City\""), std::string::npos);
    EXPECT_NE(literal.find("\"flags\":[\"dark\",\"indoors\"]"), std::string::npos);
    EXPECT_NE(literal.find("\"extraDescriptions\":[{\"keyword\":\"arch\","
                           "\"description\":\"Ancient stonework frames the gate.\"}]"),
              std::string::npos);
    EXPECT_NE(literal.find("\"exits\":[{\"directionIndex\":0,\"direction\":\"north\","
                           "\"toRoomVnum\":1205,\"keyword\":\"gate\","
                           "\"description\":\"A raised portcullis opens onto the road.\","
                           "\"keyVnum\":3001,\"width\":2,"
                           "\"flags\":[\"door\",\"closed\",\"locked\",\"pickproof\"]},"
                           "{\"directionIndex\":5,\"direction\":\"down\",\"toRoomVnum\":null,"),
              std::string::npos);
    EXPECT_NE(literal.find("\"contents\":[{\"id\":\"object:303\",\"name\":\"polished orb\","
                           "\"description\":\"A polished orb rests on the floor.\","
                           "\"shortDescription\":\"a polished orb\",\"actionDescription\":null,"
                           "\"vnum\":303,\"flags\":{\"itemType\":\"treasure\","
                           "\"wearFlags\":[\"take\"],\"extraFlags\":[],\"level\":0,"),
              std::string::npos);
    EXPECT_NE(literal.find("\"light\":1"), std::string::npos);
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"isValid\":function() { return true; }"), std::string::npos);
    EXPECT_NE(literal.find("\"object\":{\"id\":\"object:300\""), std::string::npos);
    EXPECT_NE(literal.find("\"object\":{\"id\":\"object:300\",\"name\":\"silver lever\","
                           "\"description\":\"A silver lever is bolted to the wall.\","
                           "\"shortDescription\":\"a silver lever\","
                           "\"actionDescription\":\"The lever clicks under your hand.\","
                           "\"vnum\":300,"
                           "\"flags\":{\"itemType\":\"weapon\",\"wearFlags\":[\"take\",\"wield\"],"
                           "\"extraFlags\":[\"glow\",\"magic\"],\"level\":12,\"weight\":7,"
                           "\"cost\":450,\"costPerDay\":15,\"timer\":30,\"rarity\":2,"
                           "\"material\":\"metal\"},"
                           "\"affects\":[{\"slotIndex\":0,\"location\":1,\"locationName\":\"STR\","
                           "\"modifier\":2},{\"slotIndex\":1,\"location\":2,"
                           "\"locationName\":\"DEX\",\"modifier\":-1}],"
                           "\"extraDescriptions\":[{\"keyword\":\"runes\","
                           "\"description\":\"Faint runes circle the lever.\"},"
                           "{\"keyword\":\"hinge\","
                           "\"description\":\"The hinge is polished by use.\"}],"
                           "\"container\":{\"id\":\"object:301\",\"name\":\"oak chest\","
                           "\"description\":\"A stout oak chest rests here.\","
                           "\"shortDescription\":\"an oak chest\",\"actionDescription\":null,"
                           "\"vnum\":301,"
                           "\"flags\":{\"itemType\":\"container\","
                           "\"wearFlags\":[\"take\"],"
                           "\"extraFlags\":[],\"level\":8,"
                           "\"weight\":0,\"cost\":0,\"costPerDay\":0,\"timer\":0,"
                           "\"rarity\":0,\"material\":\"\"},"
                           "\"affects\":[],"
                           "\"extraDescriptions\":[{\"keyword\":\"lid\","
                           "\"description\":\"The lid is reinforced.\"}],"
                           "\"touched\":false,"
                           "\"room\":null,\"carriedBy\":null,\"wornBy\":null,"
                           "\"__rotsReadOnlySnapshot\":true,"
                           "\"isValid\":function() { return true; }},"
                           "\"contents\":[{\"id\":\"object:302\",\"name\":\"small gear\","
                           "\"description\":\"A small gear rests inside.\","
                           "\"shortDescription\":\"a small gear\",\"actionDescription\":null,"
                           "\"vnum\":302,"
                           "\"flags\":{\"itemType\":\"other\","
                           "\"wearFlags\":[\"take\"],"
                           "\"extraFlags\":[],\"level\":0,"
                           "\"weight\":0,\"cost\":0,\"costPerDay\":0,\"timer\":0,"
                           "\"rarity\":0,\"material\":\"\"},"
                           "\"affects\":[],"
                           "\"extraDescriptions\":[{\"keyword\":\"teeth\","
                           "\"description\":\"The gear teeth are sharp.\"}],"
                           "\"touched\":true,"
                           "\"room\":null,\"carriedBy\":null,\"wornBy\":null,"
                           "\"__rotsReadOnlySnapshot\":true,"
                           "\"isValid\":function() { return true; }}],"
                           "\"touched\":true,"
                           "\"room\":{\"id\":\"room:1204\""),
              std::string::npos);
    EXPECT_NE(literal.find("\"description\":\"The old city zone.\""), std::string::npos);
    EXPECT_NE(literal.find("\"map\":\"N-G-S\""), std::string::npos);
    EXPECT_NE(literal.find("\"topRoomVnum\":1299"), std::string::npos);
    EXPECT_NE(literal.find("\"minimumLookLevel\":2"), std::string::npos);
    EXPECT_NE(literal.find("\"resetMode\":1"), std::string::npos);
    EXPECT_NE(literal.find("\"speaker\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"attacker\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"victim\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"killer\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"weapon\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"wearSlot\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"command\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"args\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"target\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"tick\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"direction\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"reverseDirection\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targ1\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targ2\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"targetTypes\":[]"), std::string::npos);
    EXPECT_NE(literal.find("\"dying\":null"), std::string::npos);
    EXPECT_NE(literal.find("\"hostType\":\"object\""), std::string::npos);
    EXPECT_NE(literal.find("\"kind\":\"legacy\""), std::string::npos);
    EXPECT_NE(literal.find("\"handlerName\":\"onPull\""), std::string::npos);
    EXPECT_NE(literal.find("\"zone\":{\"id\":\"zone:12\""), std::string::npos);
    EXPECT_NE(literal.find("\"legacyName\":\"ON_PULL\""), std::string::npos);
    EXPECT_EQ(count_occurrences(literal, "\"command\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"args\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"target\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"tick\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"direction\":"), 7);
    EXPECT_EQ(count_occurrences(literal, "\"reverseDirection\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targ1\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targ2\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"targetTypes\":"), 1);
    EXPECT_EQ(count_occurrences(literal, "\"dying\":"), 1);
    EXPECT_EQ(literal.find("char_data"), std::string::npos);
    EXPECT_EQ(literal.find("obj_data"), std::string::npos);
}

TEST(JsGameRuntime, EmitsMudlleTriggerKindForSpecialCallFlags) {
    JsGameTriggerContextFixture context = make_context();
    context.trigger.name = "onMudlleCommand";
    context.trigger.legacy_name = "SPECIAL_COMMAND";
    context.trigger.host_type = "mudlleMobile";

    JsGameRuntime runtime;
    JsRuntimeEvalResult result =
        runtime.evaluate_trigger_body("return ctx.trigger.kind === 'mudlle'\n"
                                      "  && ctx.trigger.handlerName === 'onMudlleCommand'\n"
                                      "  && ctx.trigger.legacyName === 'SPECIAL_COMMAND';",
                                      context);

    expect_ok_allows(result);
}

TEST(JsGameRuntime, DispatchesCompiledCommonJsExports) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_package_handler(
        "\"use strict\";\n"
        "Object.defineProperty(exports, \"__esModule\", { value: true });\n"
        "exports.onEnter = onEnter;\n"
        "function onEnter(ctx) { return RotS.ScriptResult.block(); }\n",
        "onEnter", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(result.value, JsRuntimeValue::Block);
}

TEST(JsGameRuntime, PrefersCompiledExportOverGlobalHandlerFallback) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_package_handler(
        "exports.onEnter = function(ctx) { return RotS.ScriptResult.allow(); };\n"
        "function onEnter(ctx) { return RotS.ScriptResult.block(); }\n",
        "onEnter", make_context());

    expect_ok_allows(result);
}

TEST(JsGameRuntime, RejectsUnsafePackageHandlerNamesBeforeEvaluation) {
    JsGameRuntime runtime;
    JsRuntimeEvalResult result = runtime.evaluate_trigger_package_handler(
        "throw new Error('should not run');", "onEnter(); evil", make_context());

    EXPECT_EQ(result.status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.diagnostic, "JavaScript game handler name is not a safe identifier");
}
