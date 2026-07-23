#include "../js_game_adapter.h"

#include "../db.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

extern char *sector_types[];
extern char num_of_sector_types;

namespace {

char_data make_character(const char *name, int race, int level, int hit, int max_hit, bool npc) {
    char_data character{};
    character.abs_number = 77;
    character.nr = npc ? 1 : -1;
    character.in_room = 0;
    character.player.name = const_cast<char *>(name);
    character.player.short_descr = const_cast<char *>(name);
    character.player.race = race;
    character.player.level = level;
    character.player.ranking = level + 3;
    character.points.exp = level * 1000;
    character.points.gold = 321;
    character.points.spirit = 44;
    character.points.mana_regen = -2;
    character.points.health_regen = 3;
    character.points.move_regen = 4;
    character.points.OB = 22;
    character.points.damage = 7;
    character.points.ENE_regen = 9;
    character.points.parry = 18;
    character.points.dodge = 20;
    character.points.encumb = 5;
    character.points.willpower = 14;
    character.points.spell_pen = 8;
    character.points.spell_power = 11;
    for (int index = 0; index < MAX_BODYPARTS; ++index)
        character.points.bodypart_hit[index] = static_cast<ubyte>(index + 1);
    character.tmpabilities.hit = hit;
    character.tmpabilities.str = 18;
    character.tmpabilities.intel = 13;
    character.tmpabilities.wil = 15;
    character.tmpabilities.dex = 17;
    character.tmpabilities.con = 16;
    character.tmpabilities.lea = 9;
    character.abilities.hit = max_hit;
    character.abilities.str = 16;
    character.abilities.intel = 12;
    character.abilities.wil = 14;
    character.abilities.dex = 15;
    character.abilities.con = 13;
    character.abilities.lea = 8;
    character.constabilities.str = 15;
    character.constabilities.intel = 11;
    character.constabilities.wil = 13;
    character.constabilities.dex = 14;
    character.constabilities.con = 12;
    character.constabilities.lea = 7;
    character.classpoints = 4;
    character.interrupt_count = 2;
    character.interrupt_time = 9;
    character.spec_busy = true;
    character.specials.position = POSITION_FIGHTING;
    character.specials.default_pos = POSITION_STANDING;
    character.specials.carry_weight = 120;
    character.specials.worn_weight = 40;
    character.specials.encumb_weight = 15;
    character.specials.carry_items = 6;
    character.specials.timer = 2;
    character.specials.was_in_room = 1203;
    character.specials.ENERGY = 77;
    character.specials.current_parry = 12;
    character.specials.last_direction = 0;
    character.specials.attack_type = 5;
    character.specials.script_number = 901;
    character.specials.current_bodypart = 3;
    character.specials.tactics = TACTICS_AGGRESSIVE;
    character.specials.prompt_number = 1;
    character.specials.prompt_value = 42;
    character.specials.homezone = 12;
    character.specials.load_line = 8;
    character.specials2.idnum = npc ? -1 : 1234;
    if (npc)
        character.specials2.act |= MOB_ISNPC | MOB_MEMORY | MOB_GUARDIAN;
    else
        character.specials2.act |= PLR_WRITING | PLR_INCOGNITO;
    character.specials2.load_room = 3001;
    character.specials2.spells_to_learn = 3;
    character.specials2.alignment = 250;
    character.specials2.pref = PRF_BRIEF | PRF_COLOR | PRF_ADVANCED_VIEW;
    character.specials2.wimp_level = 20;
    character.specials2.freeze_level = 5;
    character.specials2.saving_throw = 7;
    character.specials2.rawPerception = 81;
    character.specials2.perception = 87;
    character.specials2.conditions[0] = 1;
    character.specials2.conditions[1] = 15;
    character.specials2.conditions[2] = 19;
    character.specials2.mini_level = 2;
    character.specials2.max_mini_level = 4;
    character.specials2.morale = 33;
    character.specials2.owner = 987;
    character.specials2.rerolls = 2;
    character.specials2.leg_encumb = 8;
    character.specials2.rp_flag = 17;
    character.specials2.retiredon = 1705000000;
    character.specials2.hide_flags = HIDING_WELL | HIDING_SNUCK_IN;
    character.specials2.will_teach = 123456;
    character.specials2.tactics = TACTICS_AGGRESSIVE;
    character.specials2.shooting = SHOOTING_FAST;
    character.specials2.casting = CASTING_SLOW;
    character.specials2.two_handed = 1;
    return character;
}

obj_data make_object(const char *name, int item_number) {
    obj_data object{};
    object.item_number = item_number;
    object.in_room = 0;
    object.name = const_cast<char *>(name);
    object.short_description = const_cast<char *>(name);
    object.description = const_cast<char *>("A detailed object description.");
    object.action_description = const_cast<char *>("A detailed action description.");
    object.obj_flags.type_flag = ITEM_WEAPON;
    object.obj_flags.wear_flags = ITEM_TAKE | ITEM_WIELD;
    object.obj_flags.extra_flags = ITEM_GLOW | ITEM_MAGIC;
    object.obj_flags.level = 12;
    object.obj_flags.weight = 700;
    object.obj_flags.cost = 450;
    object.obj_flags.cost_per_day = 15;
    object.obj_flags.timer = 30;
    object.obj_flags.rarity = 2;
    object.obj_flags.material = 4;
    object.owner = 88;
    object.touched = 1;
    return object;
}

room_data make_room(const char *name, int number, int zone) {
    room_data room{};
    room.name = const_cast<char *>(name);
    room.description = const_cast<char *>("A detailed room description.");
    room.number = number;
    room.zone = zone;
    room.level = 4;
    room.sector_type = SECT_CITY;
    room.room_flags = DARK | INDOORS;
    room.alignment = -3;
    room.light = 2;
    return room;
}

zone_data make_zone(const char *name, int number) {
    zone_data zone{};
    zone.name = const_cast<char *>(name);
    zone.description = const_cast<char *>("A detailed zone description.");
    zone.map = const_cast<char *>("N-G-S");
    zone.number = number;
    zone.level = 5;
    zone.lifespan = 45;
    zone.age = 7;
    zone.top = number + 99;
    zone.x = 11;
    zone.y = -4;
    zone.symbol = 'Z';
    zone.white_power = 21;
    zone.dark_power = 13;
    zone.magi_power = 8;
    zone.min_level_look = 3;
    zone.reset_mode = 2;
    return zone;
}

JsGameTriggerFixture make_trigger() {
    JsGameTriggerFixture trigger;
    trigger.name = "onEnter";
    trigger.legacy_name = "ON_ENTER";
    trigger.host_type = "character";
    trigger.legacy_value = 11;
    return trigger;
}

class ScopedSunlight {
  public:
    explicit ScopedSunlight(int sunlight) : previous_sunlight_(weather_info.sunlight) {
        weather_info.sunlight = sunlight;
    }

    ~ScopedSunlight() { weather_info.sunlight = previous_sunlight_; }

  private:
    int previous_sunlight_;
};

JsGameAdapterOptions make_options(const char_data *const *characters, std::size_t character_count,
                                  const obj_data *const *objects, std::size_t object_count,
                                  room_data *world, int top_of_world, index_data *mob_index,
                                  std::size_t mob_index_count, index_data *obj_index,
                                  std::size_t obj_index_count, zone_data *zones,
                                  std::size_t zone_count, const char *const *race_names,
                                  std::size_t race_name_count) {
    JsGameAdapterOptions options;
    options.live_characters = characters;
    options.live_character_count = character_count;
    options.live_objects = objects;
    options.live_object_count = object_count;
    options.world = world;
    options.world_count = top_of_world >= 0 ? static_cast<std::size_t>(top_of_world + 1) : 0;
    options.top_of_world = top_of_world;
    options.mobile_index = mob_index;
    options.mobile_index_count = mob_index_count;
    options.object_index = obj_index;
    options.object_index_count = obj_index_count;
    options.zones = zones;
    options.zone_count = zone_count;
    options.race_names = race_names;
    options.race_name_count = race_name_count;
    return options;
}

} // namespace

TEST(JsGameAdapter, SnapshotsApprovedCharacterFields) {
    const char *races[] = {"God", "Human", "Dwarf"};
    index_data mobile_index[2]{};
    mobile_index[1].virt = 5100;
    char_data npc = make_character("Gate Guard", 2, 15, 41, 55, true);
    npc.player.short_descr = const_cast<char *>("a vigilant gate guard");
    npc.player.long_descr = const_cast<char *>("A vigilant gate guard watches the road.");
    npc.player.description = const_cast<char *>("The guard studies every traveler.");
    npc.player.title = const_cast<char *>("the northern watcher");
    npc.player.death_cry = const_cast<char *>("The guard sounds a final alarm!");
    npc.player.death_cry2 = const_cast<char *>("An alarm echoes from the gate.");
    npc.player.corpse_num = 6100;
    npc.player.sex = 1;
    npc.player.bodytype = 3;
    npc.player.prof = 4;
    npc.player.language = 17;
    npc.player.hometown = 12;
    npc.player.time.birth = 1700000000;
    npc.player.time.logon = 1700003600;
    npc.player.time.played = 7200;
    npc.player.weight = 180;
    npc.player.height = 72;
    npc.player.talks[0] = 10;
    npc.player.talks[1] = 20;
    npc.player.talks[2] = 30;
    const char_data *live_characters[] = {&npc};
    room_data world[1] = {make_room("Northern Gate", 1204, 0)};
    zone_data zones[1] = {make_zone("Old City", 12)};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, world, 0,
                                                mobile_index, 2, nullptr, 0, zones, 1, races, 3);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&npc, options, &fixture));

    EXPECT_EQ(fixture.id, "mob:5100");
    EXPECT_EQ(fixture.name, "a vigilant gate guard");
    EXPECT_EQ(fixture.race, "Dwarf");
    EXPECT_EQ(fixture.vnum, 5100);
    EXPECT_EQ(fixture.prototype_vnum, 5100);
    EXPECT_EQ(fixture.level, 15);
    EXPECT_EQ(fixture.experience, 15000);
    EXPECT_EQ(fixture.rank, 18);
    EXPECT_EQ(fixture.hit_points, 41);
    EXPECT_EQ(fixture.max_hit_points, 55);
    EXPECT_EQ(fixture.class_points, 4);
    EXPECT_EQ(fixture.interrupt_count, 2);
    EXPECT_EQ(fixture.interrupt_time, 9);
    EXPECT_TRUE(fixture.special_busy);
    EXPECT_EQ(fixture.profile.name, "Gate Guard");
    EXPECT_EQ(fixture.profile.short_description, "a vigilant gate guard");
    EXPECT_TRUE(fixture.profile.has_long_description);
    EXPECT_EQ(fixture.profile.long_description, "A vigilant gate guard watches the road.");
    EXPECT_TRUE(fixture.profile.has_description);
    EXPECT_EQ(fixture.profile.description, "The guard studies every traveler.");
    EXPECT_TRUE(fixture.profile.has_title);
    EXPECT_EQ(fixture.profile.title, "the northern watcher");
    EXPECT_TRUE(fixture.profile.has_death_cry);
    EXPECT_EQ(fixture.profile.death_cry, "The guard sounds a final alarm!");
    EXPECT_TRUE(fixture.profile.has_death_cry2);
    EXPECT_EQ(fixture.profile.death_cry2, "An alarm echoes from the gate.");
    EXPECT_EQ(fixture.profile.corpse_number, 6100);
    EXPECT_EQ(fixture.profile.race_id, 2);
    EXPECT_EQ(fixture.profile.sex, 1);
    EXPECT_EQ(fixture.profile.body_type, 3);
    EXPECT_EQ(fixture.profile.profession, 4);
    EXPECT_EQ(fixture.profile.level, 15);
    EXPECT_EQ(fixture.profile.language, 17);
    EXPECT_EQ(fixture.profile.hometown, 12);
    EXPECT_EQ(fixture.profile.birth_epoch_seconds, 1700000000);
    EXPECT_EQ(fixture.profile.logon_epoch_seconds, 1700003600);
    EXPECT_EQ(fixture.profile.played_seconds, 7200);
    EXPECT_EQ(fixture.profile.weight, 180);
    EXPECT_EQ(fixture.profile.height, 72);
    EXPECT_EQ(fixture.profile.ranking, 18);
    EXPECT_EQ(fixture.profile.talks, (std::vector<int>{10, 20, 30}));
    EXPECT_EQ(fixture.base_abilities.strength, 16);
    EXPECT_EQ(fixture.base_abilities.intelligence, 12);
    EXPECT_EQ(fixture.base_abilities.willpower, 14);
    EXPECT_EQ(fixture.base_abilities.dexterity, 15);
    EXPECT_EQ(fixture.base_abilities.constitution, 13);
    EXPECT_EQ(fixture.base_abilities.leadership, 8);
    EXPECT_EQ(fixture.current_abilities.strength, 18);
    EXPECT_EQ(fixture.current_abilities.intelligence, 13);
    EXPECT_EQ(fixture.current_abilities.willpower, 15);
    EXPECT_EQ(fixture.current_abilities.dexterity, 17);
    EXPECT_EQ(fixture.current_abilities.constitution, 16);
    EXPECT_EQ(fixture.current_abilities.leadership, 9);
    EXPECT_EQ(fixture.rolled_abilities.strength, 15);
    EXPECT_EQ(fixture.rolled_abilities.intelligence, 11);
    EXPECT_EQ(fixture.rolled_abilities.willpower, 13);
    EXPECT_EQ(fixture.rolled_abilities.dexterity, 14);
    EXPECT_EQ(fixture.rolled_abilities.constitution, 12);
    EXPECT_EQ(fixture.rolled_abilities.leadership, 7);
    ASSERT_EQ(fixture.points.bodypart_hits.size(), static_cast<std::size_t>(MAX_BODYPARTS));
    for (int index = 0; index < MAX_BODYPARTS; ++index)
        EXPECT_EQ(fixture.points.bodypart_hits[index], index + 1);
    EXPECT_EQ(fixture.points.gold, 321);
    EXPECT_EQ(fixture.points.experience, 15000);
    EXPECT_EQ(fixture.points.spirit, 44);
    EXPECT_EQ(fixture.points.mana_regen, -2);
    EXPECT_EQ(fixture.points.health_regen, 3);
    EXPECT_EQ(fixture.points.move_regen, 4);
    EXPECT_EQ(fixture.points.offense, 22);
    EXPECT_EQ(fixture.points.damage, 7);
    EXPECT_EQ(fixture.points.energy_regen, 9);
    EXPECT_EQ(fixture.points.parry, 18);
    EXPECT_EQ(fixture.points.dodge, 20);
    EXPECT_EQ(fixture.points.encumbrance, 5);
    EXPECT_EQ(fixture.points.willpower, 14);
    EXPECT_EQ(fixture.points.spell_penetration, 8);
    EXPECT_EQ(fixture.points.spell_power, 11);
    EXPECT_FALSE(fixture.specials.is_fighting);
    EXPECT_FALSE(fixture.specials.is_hunting);
    EXPECT_FALSE(fixture.specials.has_memory);
    EXPECT_EQ(fixture.specials.position, "Fighting");
    EXPECT_EQ(fixture.specials.default_position, "Standing");
    EXPECT_EQ(fixture.specials.carry_weight, 120);
    EXPECT_EQ(fixture.specials.worn_weight, 40);
    EXPECT_EQ(fixture.specials.encumbrance_weight, 15);
    EXPECT_EQ(fixture.specials.carry_items, 6);
    EXPECT_EQ(fixture.specials.timer, 2);
    EXPECT_EQ(fixture.specials.was_in_room, 1203);
    EXPECT_EQ(fixture.specials.energy, 77);
    EXPECT_EQ(fixture.specials.current_parry, 12);
    EXPECT_EQ(fixture.specials.last_direction, "north");
    EXPECT_EQ(fixture.specials.attack_type, 5);
    EXPECT_EQ(fixture.specials.script_number, 901);
    EXPECT_EQ(fixture.specials.current_bodypart, 3);
    EXPECT_EQ(fixture.specials.tactics, "");
    EXPECT_EQ(fixture.specials.prompt_number, 1);
    EXPECT_EQ(fixture.specials.prompt_value, 42);
    EXPECT_EQ(fixture.specials.home_zone, 12);
    EXPECT_EQ(fixture.specials.load_line, 8);
    EXPECT_EQ(fixture.specials2.load_room, 3001);
    EXPECT_EQ(fixture.specials2.spells_to_learn, 3);
    EXPECT_EQ(fixture.specials2.alignment, 250);
    EXPECT_EQ(fixture.specials2.act_flags,
              (std::vector<std::string>{"isNpc", "memory", "guardian"}));
    EXPECT_EQ(fixture.specials2.preference_flags,
              (std::vector<std::string>{"brief", "color", "advancedView"}));
    EXPECT_EQ(fixture.specials2.wimp_level, 20);
    EXPECT_EQ(fixture.specials2.freeze_level, 5);
    EXPECT_EQ(fixture.specials2.saving_throw, 7);
    EXPECT_EQ(fixture.specials2.raw_perception, 81);
    EXPECT_EQ(fixture.specials2.perception, 87);
    EXPECT_EQ(fixture.specials2.conditions.drunk, 1);
    EXPECT_EQ(fixture.specials2.conditions.full, 15);
    EXPECT_EQ(fixture.specials2.conditions.thirst, 19);
    EXPECT_EQ(fixture.specials2.mini_level, 2);
    EXPECT_EQ(fixture.specials2.max_mini_level, 4);
    EXPECT_EQ(fixture.specials2.morale, 33);
    EXPECT_EQ(fixture.specials2.rerolls, 2);
    EXPECT_EQ(fixture.specials2.leg_encumbrance, 8);
    EXPECT_EQ(fixture.specials2.retired_on, 1705000000);
    EXPECT_EQ(fixture.specials2.hide_flags, (std::vector<std::string>{"hidingWell", "snuckIn"}));
    EXPECT_EQ(fixture.specials2.tactics, "aggressive");
    EXPECT_EQ(fixture.specials2.shooting, "fast");
    EXPECT_EQ(fixture.specials2.casting, "slow");
    EXPECT_TRUE(fixture.specials2.two_handed);
    EXPECT_TRUE(fixture.is_npc);
    EXPECT_TRUE(fixture.professions.empty());
    ASSERT_TRUE(fixture.has_room);
    EXPECT_EQ(fixture.room.vnum, 1204);
    ASSERT_TRUE(fixture.room.has_zone);
    EXPECT_EQ(fixture.room.zone.vnum, 12);
}

TEST(JsGameAdapter, SnapshotsCharacterProfessionsWhenPresent) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    char_prof_data professions{};
    professions.prof_level[PROF_MAGE] = 8;
    professions.prof_coof[PROF_MAGE] = 121;
    professions.prof_exp[PROF_MAGE] = 8100;
    professions.prof_level[PROF_CLERIC] = 5;
    professions.prof_coof[PROF_CLERIC] = 64;
    professions.prof_exp[PROF_CLERIC] = 5200;
    professions.prof_level[PROF_RANGER] = 3;
    professions.prof_coof[PROF_RANGER] = 25;
    professions.prof_exp[PROF_RANGER] = 3300;
    professions.prof_level[PROF_WARRIOR] = 2;
    professions.prof_coof[PROF_WARRIOR] = 16;
    professions.prof_exp[PROF_WARRIOR] = 2400;
    professions.colors[0] = 7;
    professions.specialization = 2;
    player.profs = &professions;
    player.extra_specialization_data.set(player);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.professions.size(), 4u);
    EXPECT_EQ(fixture.professions[0].key, "mage");
    EXPECT_EQ(fixture.professions[0].name, "Mage");
    EXPECT_EQ(fixture.professions[0].level, 8);
    EXPECT_EQ(fixture.professions[0].points, 121);
    EXPECT_EQ(fixture.professions[0].coefficient, 121);
    EXPECT_EQ(fixture.professions[0].experience, 8100);
    EXPECT_EQ(fixture.professions[1].key, "mystic");
    EXPECT_EQ(fixture.professions[1].name, "Mystic");
    EXPECT_EQ(fixture.professions[1].level, 5);
    EXPECT_EQ(fixture.professions[1].points, 64);
    EXPECT_EQ(fixture.professions[1].coefficient, 64);
    EXPECT_EQ(fixture.professions[1].experience, 5200);
    EXPECT_EQ(fixture.professions[2].key, "ranger");
    EXPECT_EQ(fixture.professions[2].name, "Ranger");
    EXPECT_EQ(fixture.professions[2].level, 3);
    EXPECT_EQ(fixture.professions[2].points, 25);
    EXPECT_EQ(fixture.professions[2].coefficient, 25);
    EXPECT_EQ(fixture.professions[2].experience, 3300);
    EXPECT_EQ(fixture.professions[3].key, "warrior");
    EXPECT_EQ(fixture.professions[3].name, "Warrior");
    EXPECT_EQ(fixture.professions[3].level, 2);
    EXPECT_EQ(fixture.professions[3].points, 16);
    EXPECT_EQ(fixture.professions[3].coefficient, 16);
    EXPECT_EQ(fixture.professions[3].experience, 2400);
    EXPECT_EQ(fixture.specializations.selected_id, game_types::PS_Cold);
    EXPECT_EQ(fixture.specializations.selected_key, "cold");
    EXPECT_EQ(fixture.specializations.selected_name, "cold");
    EXPECT_EQ(fixture.specializations.current_id, game_types::PS_Cold);
    EXPECT_EQ(fixture.specializations.current_key, "cold");
    EXPECT_EQ(fixture.specializations.current_name, "cold");
    EXPECT_TRUE(fixture.specializations.is_mage_specialization);
    EXPECT_TRUE(fixture.specializations.has_runtime_state);
}

TEST(JsGameAdapter, SnapshotsCharacterSkillsWhenPresent) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    byte skill_values[MAX_SKILLS]{};
    skill_values[1] = 3;
    skill_values[8] = 2;
    skill_values[255] = 9;
    player.skills = skill_values;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.skills.size(), 2u);
    EXPECT_EQ(fixture.skills[0].id, 1);
    EXPECT_EQ(fixture.skills[0].name, get_skill_array()[1].name);
    EXPECT_EQ(fixture.skills[0].profession, "warrior");
    EXPECT_EQ(fixture.skills[0].level, get_skill_array()[1].level);
    EXPECT_EQ(fixture.skills[0].practice, 3);
    EXPECT_EQ(fixture.skills[0].minimum_position, get_skill_array()[1].minimum_position);
    EXPECT_EQ(fixture.skills[0].mana_cost, get_skill_array()[1].min_usesmana);
    EXPECT_EQ(fixture.skills[0].beats, get_skill_array()[1].beats);
    EXPECT_EQ(fixture.skills[0].targets, get_skill_array()[1].targets);
    EXPECT_EQ(fixture.skills[0].learn_difficulty, get_skill_array()[1].learn_diff);
    EXPECT_EQ(fixture.skills[0].learn_type, get_skill_array()[1].learn_type);
    EXPECT_EQ(fixture.skills[0].is_fast, get_skill_array()[1].is_fast != 0);
    EXPECT_EQ(fixture.skills[0].specialization, get_skill_array()[1].skill_spec);

    EXPECT_EQ(fixture.skills[1].id, 8);
    EXPECT_EQ(fixture.skills[1].name, get_skill_array()[8].name);
    EXPECT_EQ(fixture.skills[1].profession, "ranger");
    EXPECT_EQ(fixture.skills[1].practice, 2);
}

TEST(JsGameAdapter, DefaultsMissingCharacterSkillsToEmptySnapshot) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    player.skills = nullptr;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.skills.empty());
}

TEST(JsGameAdapter, SnapshotsCharacterKnowledgeWhenPresent) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    byte knowledge_values[MAX_SKILLS]{};
    knowledge_values[1] = 40;
    knowledge_values[8] = 55;
    knowledge_values[255] = 80;
    player.knowledge = knowledge_values;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.knowledge.size(), 2u);
    EXPECT_EQ(fixture.knowledge[0].id, 1);
    EXPECT_EQ(fixture.knowledge[0].name, get_skill_array()[1].name);
    EXPECT_EQ(fixture.knowledge[0].profession, "warrior");
    EXPECT_EQ(fixture.knowledge[0].level, get_skill_array()[1].level);
    EXPECT_EQ(fixture.knowledge[0].knowledge, 40);
    EXPECT_EQ(fixture.knowledge[0].minimum_position, get_skill_array()[1].minimum_position);
    EXPECT_EQ(fixture.knowledge[0].mana_cost, get_skill_array()[1].min_usesmana);
    EXPECT_EQ(fixture.knowledge[0].beats, get_skill_array()[1].beats);
    EXPECT_EQ(fixture.knowledge[0].targets, get_skill_array()[1].targets);
    EXPECT_EQ(fixture.knowledge[0].learn_difficulty, get_skill_array()[1].learn_diff);
    EXPECT_EQ(fixture.knowledge[0].learn_type, get_skill_array()[1].learn_type);
    EXPECT_EQ(fixture.knowledge[0].is_fast, get_skill_array()[1].is_fast != 0);
    EXPECT_EQ(fixture.knowledge[0].specialization, get_skill_array()[1].skill_spec);

    EXPECT_EQ(fixture.knowledge[1].id, 8);
    EXPECT_EQ(fixture.knowledge[1].name, get_skill_array()[8].name);
    EXPECT_EQ(fixture.knowledge[1].profession, "ranger");
    EXPECT_EQ(fixture.knowledge[1].knowledge, 55);
}

TEST(JsGameAdapter, DefaultsMissingCharacterKnowledgeToEmptySnapshot) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    player.knowledge = nullptr;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.knowledge.empty());
}

TEST(JsGameAdapter, SnapshotsCharacterAffectsWhenPresent) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    affected_type unknown_affect{};
    unknown_affect.type = MAX_SKILLS + 1;
    unknown_affect.duration = 2;
    unknown_affect.time_phase = 3;
    unknown_affect.modifier = -1;
    unknown_affect.location = -1;
    unknown_affect.bitvector = 0;
    unknown_affect.counter = 4;

    affected_type sanctuary_affect{};
    sanctuary_affect.type = SPELL_SANCTUARY;
    sanctuary_affect.duration = 8;
    sanctuary_affect.time_phase = 1;
    sanctuary_affect.modifier = 5;
    sanctuary_affect.location = APPLY_DEX;
    sanctuary_affect.bitvector = AFF_SANCTUARY;
    sanctuary_affect.counter = 6;
    sanctuary_affect.next = &unknown_affect;
    player.affected = &sanctuary_affect;

    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.affects.size(), 2u);
    EXPECT_EQ(fixture.affects[0].type, SPELL_SANCTUARY);
    EXPECT_EQ(fixture.affects[0].name, get_skill_array()[SPELL_SANCTUARY].name);
    EXPECT_EQ(fixture.affects[0].duration, 8);
    EXPECT_EQ(fixture.affects[0].time_phase, 1);
    EXPECT_EQ(fixture.affects[0].modifier, 5);
    EXPECT_EQ(fixture.affects[0].location, APPLY_DEX);
    EXPECT_EQ(fixture.affects[0].location_name, "DEX");
    EXPECT_EQ(fixture.affects[0].bitvector, AFF_SANCTUARY);
    ASSERT_EQ(fixture.affects[0].bitvector_names.size(), 1u);
    EXPECT_EQ(fixture.affects[0].bitvector_names[0], "SANCT");
    EXPECT_EQ(fixture.affects[0].counter, 6);

    EXPECT_EQ(fixture.affects[1].type, MAX_SKILLS + 1);
    EXPECT_EQ(fixture.affects[1].name, "Unknown");
    EXPECT_EQ(fixture.affects[1].location_name, "Unknown");
    EXPECT_TRUE(fixture.affects[1].bitvector_names.empty());
}

TEST(JsGameAdapter, DefaultsMissingCharacterAffectsToEmptySnapshot) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    player.affected = nullptr;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.affects.empty());
}

TEST(JsGameAdapter, SnapshotsCharacterEquipmentSlotsWithShallowObjects) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    obj_data helm = make_object("silver helm", 0);
    extra_descr_data helm_extra{};
    helm_extra.keyword = const_cast<char *>("crest");
    helm_extra.description = const_cast<char *>("A tiny crest is etched inside.");
    helm.ex_description = &helm_extra;
    helm.affected[0].location = APPLY_DEX;
    helm.affected[0].modifier = 2;
    helm.in_room = -1;
    helm.carried_by = &player;
    player.equipment[WEAR_HEAD] = &helm;
    obj_data stale = make_object("stale ring", 1);
    stale.in_room = -1;
    player.equipment[WEAR_FINGER_L] = &stale;
    char_data other_wearer = make_character("OtherOne", 1, 20, 50, 60, false);
    obj_data foreign = make_object("foreign shield", 2);
    foreign.in_room = -1;
    foreign.carried_by = &other_wearer;
    player.equipment[WEAR_SHIELD] = &foreign;
    obj_data room_object = make_object("room boots", 3);
    room_object.carried_by = &player;
    room_object.in_room = 0;
    player.equipment[WEAR_FEET] = &room_object;
    obj_data container = make_object("container", 4);
    obj_data contained = make_object("contained gloves", 5);
    contained.carried_by = &player;
    contained.in_room = -1;
    contained.in_obj = &container;
    player.equipment[WEAR_HANDS] = &contained;

    const char_data *live_characters[] = {&player};
    const obj_data *live_objects[] = {&helm, &foreign, &room_object, &container, &contained};
    index_data object_index[1]{};
    object_index[0].virt = 4301;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, nullptr, -1, nullptr, 0, object_index, 1,
                     nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.equipment.size(), static_cast<std::size_t>(MAX_WEAR));
    EXPECT_EQ(fixture.equipment[0].slot_index, 0);
    EXPECT_EQ(fixture.equipment[0].slot_name, "light");
    EXPECT_FALSE(fixture.equipment[0].has_object);

    ASSERT_LT(WEAR_HEAD, static_cast<int>(fixture.equipment.size()));
    const JsGameEquipmentSlotFixture &head = fixture.equipment[WEAR_HEAD];
    EXPECT_EQ(head.slot_index, WEAR_HEAD);
    EXPECT_EQ(head.slot_name, "head");
    ASSERT_TRUE(head.has_object);
    EXPECT_EQ(head.object.id, "object:4301");
    EXPECT_EQ(head.object.name, "silver helm");
    EXPECT_EQ(head.object.short_description, "silver helm");
    EXPECT_EQ(head.object.description, "A detailed object description.");
    EXPECT_TRUE(head.object.has_action_description);
    EXPECT_EQ(head.object.action_description, "A detailed action description.");
    EXPECT_EQ(head.object.vnum, 4301);
    EXPECT_EQ(head.object.flags.item_type, "weapon");
    EXPECT_EQ(head.object.flags.level, 12);
    ASSERT_EQ(head.object.affects.size(), 1U);
    EXPECT_EQ(head.object.affects[0].slot_index, 0);
    EXPECT_EQ(head.object.affects[0].location, APPLY_DEX);
    EXPECT_EQ(head.object.affects[0].location_name, "DEX");
    EXPECT_EQ(head.object.affects[0].modifier, 2);
    ASSERT_EQ(head.object.extra_descriptions.size(), 1U);
    EXPECT_EQ(head.object.extra_descriptions[0].keyword, "crest");
    EXPECT_EQ(head.object.extra_descriptions[0].description, "A tiny crest is etched inside.");
    EXPECT_FALSE(head.object.has_room);

    ASSERT_LT(WEAR_FINGER_L, static_cast<int>(fixture.equipment.size()));
    EXPECT_EQ(fixture.equipment[WEAR_FINGER_L].slot_name, "fingerLeft");
    EXPECT_FALSE(fixture.equipment[WEAR_FINGER_L].has_object);
    EXPECT_FALSE(fixture.equipment[WEAR_SHIELD].has_object);
    EXPECT_FALSE(fixture.equipment[WEAR_FEET].has_object);
    EXPECT_FALSE(fixture.equipment[WEAR_HANDS].has_object);
}

TEST(JsGameAdapter, DefaultsMissingCharacterEquipmentToEmptySlots) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.equipment.size(), static_cast<std::size_t>(MAX_WEAR));
    for (const JsGameEquipmentSlotFixture &slot : fixture.equipment)
        EXPECT_FALSE(slot.has_object) << slot.slot_name;
}

TEST(JsGameAdapter, SnapshotsCharacterInventoryWithShallowObjects) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    obj_data torch = make_object("oak torch", 0);
    extra_descr_data torch_extra{};
    torch_extra.keyword = const_cast<char *>("grain");
    torch_extra.description = const_cast<char *>("The wood grain is dark and even.");
    torch.ex_description = &torch_extra;
    torch.affected[1].location = APPLY_NONE;
    torch.affected[1].modifier = 4;
    torch.in_room = -1;
    torch.carried_by = &player;
    obj_data key = make_object("small key", 1);
    key.in_room = -1;
    key.carried_by = &player;
    torch.next_content = &key;
    player.carrying = &torch;

    char_data other_carrier = make_character("OtherOne", 1, 20, 50, 60, false);
    obj_data foreign = make_object("foreign bag", 2);
    foreign.in_room = -1;
    foreign.carried_by = &other_carrier;
    key.next_content = &foreign;

    obj_data room_object = make_object("room coin", 3);
    room_object.carried_by = &player;
    room_object.in_room = 0;
    foreign.next_content = &room_object;

    obj_data container = make_object("container", 4);
    obj_data contained = make_object("contained gem", 5);
    contained.carried_by = &player;
    contained.in_room = -1;
    contained.in_obj = &container;
    room_object.next_content = &contained;

    obj_data worn = make_object("worn glove", 6);
    worn.in_room = -1;
    worn.carried_by = &player;
    player.equipment[WEAR_HANDS] = &worn;
    contained.next_content = &worn;

    const char_data *live_characters[] = {&player};
    const obj_data *live_objects[] = {&torch,     &key,       &foreign, &room_object,
                                      &container, &contained, &worn};
    index_data object_index[7]{};
    for (int index = 0; index < 7; ++index)
        object_index[index].virt = 5001 + index;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 7, nullptr, -1, nullptr, 0, object_index, 7,
                     nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.inventory.size(), 2U);
    EXPECT_EQ(fixture.inventory[0].id, "object:5001");
    EXPECT_EQ(fixture.inventory[0].name, "oak torch");
    EXPECT_EQ(fixture.inventory[0].vnum, 5001);
    ASSERT_EQ(fixture.inventory[0].affects.size(), 1U);
    EXPECT_EQ(fixture.inventory[0].affects[0].slot_index, 1);
    EXPECT_EQ(fixture.inventory[0].affects[0].location, APPLY_NONE);
    EXPECT_EQ(fixture.inventory[0].affects[0].location_name, "NONE");
    EXPECT_EQ(fixture.inventory[0].affects[0].modifier, 4);
    ASSERT_EQ(fixture.inventory[0].extra_descriptions.size(), 1U);
    EXPECT_EQ(fixture.inventory[0].extra_descriptions[0].keyword, "grain");
    EXPECT_EQ(fixture.inventory[0].extra_descriptions[0].description,
              "The wood grain is dark and even.");
    EXPECT_FALSE(fixture.inventory[0].has_room);
    EXPECT_EQ(fixture.inventory[1].id, "object:5002");
    EXPECT_EQ(fixture.inventory[1].name, "small key");
}

TEST(JsGameAdapter, DefaultsMissingCharacterInventoryToEmptySnapshot) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.inventory.empty());
}

TEST(JsGameAdapter, BoundsCharacterInventoryTraversalByVisitedNodes) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    char_data other_carrier = make_character("OtherOne", 1, 20, 50, 60, false);
    std::vector<obj_data> invalid_objects;
    invalid_objects.reserve(101);
    for (int index = 0; index < 101; ++index) {
        invalid_objects.push_back(make_object("foreign item", index));
        invalid_objects[index].in_room = -1;
        invalid_objects[index].carried_by = &other_carrier;
    }
    obj_data valid = make_object("valid torch", 101);
    valid.in_room = -1;
    valid.carried_by = &player;
    for (int index = 0; index < 100; ++index)
        invalid_objects[index].next_content = &invalid_objects[index + 1];
    invalid_objects[100].next_content = &valid;
    player.carrying = &invalid_objects[0];

    std::vector<const obj_data *> live_objects;
    live_objects.reserve(102);
    for (const obj_data &object : invalid_objects)
        live_objects.push_back(&object);
    live_objects.push_back(&valid);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects.data(), live_objects.size(), nullptr, -1,
                     nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.inventory.empty());
}

TEST(JsGameAdapter, IncludesExactlyOneHundredCharacterInventoryNodes) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    std::vector<obj_data> objects;
    objects.reserve(100);
    for (int index = 0; index < 100; ++index) {
        objects.push_back(make_object("carried item", index));
        objects[index].in_room = -1;
        objects[index].carried_by = &player;
    }
    for (int index = 0; index < 99; ++index)
        objects[index].next_content = &objects[index + 1];
    player.carrying = &objects[0];

    std::vector<const obj_data *> live_objects;
    live_objects.reserve(objects.size());
    for (const obj_data &object : objects)
        live_objects.push_back(&object);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects.data(), live_objects.size(), nullptr, -1,
                     nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.inventory.size(), 100U);
    EXPECT_EQ(fixture.inventory.front().name, "carried item");
    EXPECT_EQ(fixture.inventory.back().name, "carried item");
}

TEST(JsGameAdapter, StopsCharacterInventoryBeforeHundredFirstNode) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    std::vector<obj_data> objects;
    objects.reserve(101);
    for (int index = 0; index < 101; ++index) {
        objects.push_back(make_object(index == 100 ? "hundred first item" : "carried item", index));
        objects[index].in_room = -1;
        objects[index].carried_by = &player;
    }
    for (int index = 0; index < 100; ++index)
        objects[index].next_content = &objects[index + 1];
    player.carrying = &objects[0];

    std::vector<const obj_data *> live_objects;
    live_objects.reserve(objects.size());
    for (const obj_data &object : objects)
        live_objects.push_back(&object);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects.data(), live_objects.size(), nullptr, -1,
                     nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    ASSERT_EQ(fixture.inventory.size(), 100U);
    for (const JsGameEquipmentObjectFixture &object : fixture.inventory)
        EXPECT_NE(object.name, "hundred first item");
}

TEST(JsGameAdapter, BreaksCharacterInventoryTraversalCycles) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    obj_data loop = make_object("loop item", 0);
    loop.in_room = 0;
    loop.carried_by = &player;
    loop.next_content = &loop;
    player.carrying = &loop;
    const char_data *live_characters[] = {&player};
    const obj_data *live_objects[] = {&loop};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.inventory.empty());
}

TEST(JsGameAdapter, SnapshotsCharacterFollowersAndMasterWithShallowCharacters) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    follower.abs_number = 1234;
    follower.master = &leader;
    follow_type node{};
    node.follower = &follower;
    node.fol_number = follower.abs_number;
    leader.followers = &node;

    const char_data *live_characters[] = {&leader, &follower};
    index_data mobile_index[3]{};
    mobile_index[1].virt = 6202;
    JsGameAdapterOptions options =
        make_options(live_characters, 2, nullptr, 0, nullptr, -1, mobile_index, 3, nullptr, 0,
                     nullptr, 0, nullptr, 0);

    JsGameCharacterFixture leader_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &leader_fixture));
    ASSERT_EQ(leader_fixture.followers.size(), 1U);
    EXPECT_EQ(leader_fixture.followers[0].name, "Follower");
    EXPECT_EQ(leader_fixture.followers[0].vnum, 6202);
    EXPECT_TRUE(leader_fixture.followers[0].is_npc);
    EXPECT_FALSE(leader_fixture.has_master);

    JsGameCharacterFixture follower_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&follower, options, &follower_fixture));
    ASSERT_TRUE(follower_fixture.has_master);
    EXPECT_EQ(follower_fixture.master.name, "Leader");
    EXPECT_FALSE(follower_fixture.master.is_npc);
}

TEST(JsGameAdapter, RejectsInvalidFollowerAndMasterRelationships) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    char_data other_leader = make_character("OtherLeader", 1, 91, 90, 120, false);
    follower.abs_number = 1234;
    follow_type wrong_number{};
    wrong_number.follower = &follower;
    wrong_number.fol_number = follower.abs_number + 1;
    leader.followers = &wrong_number;
    follower.master = &leader;

    const char_data *live_characters[] = {&leader, &follower};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture leader_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &leader_fixture));
    EXPECT_TRUE(leader_fixture.followers.empty());

    JsGameCharacterFixture follower_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&follower, options, &follower_fixture));
    EXPECT_FALSE(follower_fixture.has_master);

    follow_type missing_back_pointer{};
    missing_back_pointer.follower = &follower;
    missing_back_pointer.fol_number = follower.abs_number;
    leader.followers = &missing_back_pointer;
    follower.master = nullptr;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &leader_fixture));
    EXPECT_TRUE(leader_fixture.followers.empty());

    follower.master = &other_leader;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &leader_fixture));
    EXPECT_TRUE(leader_fixture.followers.empty());
}

TEST(JsGameAdapter, BoundsAndBreaksCharacterFollowerTraversal) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    follower.abs_number = 1234;
    follower.master = &leader;
    std::vector<follow_type> nodes(101);
    for (int index = 0; index < 101; ++index) {
        nodes[index].follower = &follower;
        nodes[index].fol_number = follower.abs_number + 1;
        if (index < 100)
            nodes[index].next = &nodes[index + 1];
    }
    nodes[100].fol_number = follower.abs_number;
    leader.followers = &nodes[0];

    const char_data *live_characters[] = {&leader, &follower};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &fixture));
    EXPECT_TRUE(fixture.followers.empty());

    nodes[0].next = &nodes[0];
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &fixture));
    EXPECT_TRUE(fixture.followers.empty());
}

TEST(JsGameAdapter, DeduplicatesDuplicateCharacterFollowerNodes) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    follower.abs_number = 1234;
    follower.master = &leader;
    follow_type first{};
    first.follower = &follower;
    first.fol_number = follower.abs_number;
    follow_type duplicate{};
    duplicate.follower = &follower;
    duplicate.fol_number = follower.abs_number;
    first.next = &duplicate;
    leader.followers = &first;

    const char_data *live_characters[] = {&leader, &follower};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &fixture));
    ASSERT_EQ(fixture.followers.size(), 1U);
    EXPECT_EQ(fixture.followers[0].name, "Follower");
}

TEST(JsGameAdapter, RejectsStaleFollowerPointersBeforeDereferencing) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    follower.abs_number = 1234;
    follower.master = &leader;
    auto *stale_character = reinterpret_cast<char_data *>(static_cast<std::uintptr_t>(0x1));
    follow_type stale_node{};
    stale_node.follower = stale_character;
    stale_node.fol_number = 1234;
    leader.followers = &stale_node;

    const char_data *live_characters[] = {&leader, &follower};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture leader_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&leader, options, &leader_fixture));
    EXPECT_TRUE(leader_fixture.followers.empty());

    follow_type stale_master_node{};
    stale_master_node.follower = stale_character;
    stale_master_node.fol_number = 1234;
    leader.followers = &stale_master_node;
    follower.master = &leader;

    JsGameCharacterFixture follower_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&follower, options, &follower_fixture));
    EXPECT_FALSE(follower_fixture.has_master);
}

TEST(JsGameAdapter, BoundsAndBreaksCharacterMasterTraversal) {
    char_data leader = make_character("Leader", 1, 92, 90, 120, false);
    char_data follower = make_character("Follower", 2, 20, 50, 60, true);
    follower.abs_number = 1234;
    follower.master = &leader;
    std::vector<follow_type> nodes(101);
    for (int index = 0; index < 101; ++index) {
        nodes[index].follower = &follower;
        nodes[index].fol_number = follower.abs_number + 1;
        if (index < 100)
            nodes[index].next = &nodes[index + 1];
    }
    nodes[100].fol_number = follower.abs_number;
    leader.followers = &nodes[0];

    const char_data *live_characters[] = {&leader, &follower};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture follower_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&follower, options, &follower_fixture));
    EXPECT_FALSE(follower_fixture.has_master);

    nodes[0].next = &nodes[0];
    ASSERT_TRUE(js_game_adapter_character_fixture(&follower, options, &follower_fixture));
    EXPECT_FALSE(follower_fixture.has_master);
}

TEST(JsGameAdapter, SnapshotsCharacterMountRelationships) {
    char_data mount = make_character("Warhorse", 1, 20, 90, 120, true);
    char_data rider = make_character("Rider", 1, 92, 90, 120, false);
    char_data next_rider = make_character("PackRider", 1, 25, 90, 120, true);
    mount.abs_number = 2000;
    rider.abs_number = 2001;
    next_rider.abs_number = 2002;
    rider.mount_data.mount = &mount;
    rider.mount_data.mount_number = mount.abs_number;
    rider.mount_data.next_rider = &next_rider;
    rider.mount_data.next_rider_number = next_rider.abs_number;
    next_rider.mount_data.mount = &mount;
    next_rider.mount_data.mount_number = mount.abs_number;
    mount.mount_data.rider = &rider;
    mount.mount_data.rider_number = rider.abs_number;

    const char_data *live_characters[] = {&mount, &rider, &next_rider};
    index_data mobile_index[3]{};
    mobile_index[1].virt = 6202;
    JsGameAdapterOptions options =
        make_options(live_characters, 3, nullptr, 0, nullptr, -1, mobile_index, 3, nullptr, 0,
                     nullptr, 0, nullptr, 0);

    JsGameCharacterFixture rider_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&rider, options, &rider_fixture));
    EXPECT_TRUE(rider_fixture.mount.is_riding);
    ASSERT_TRUE(rider_fixture.mount.has_mount);
    EXPECT_EQ(rider_fixture.mount.mount.name, "Warhorse");
    ASSERT_TRUE(rider_fixture.mount.has_next_rider);
    EXPECT_EQ(rider_fixture.mount.next_rider.name, "PackRider");

    JsGameCharacterFixture mount_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&mount, options, &mount_fixture));
    EXPECT_TRUE(mount_fixture.mount.is_mounted);
    ASSERT_TRUE(mount_fixture.mount.has_rider);
    EXPECT_EQ(mount_fixture.mount.rider.name, "Rider");
}

TEST(JsGameAdapter, RejectsInvalidCharacterMountRelationships) {
    char_data mount = make_character("Warhorse", 1, 20, 90, 120, true);
    char_data rider = make_character("Rider", 1, 92, 90, 120, false);
    char_data next_rider = make_character("PackRider", 1, 25, 90, 120, true);
    mount.abs_number = 2000;
    rider.abs_number = 2001;
    next_rider.abs_number = 2002;
    rider.mount_data.mount = &mount;
    rider.mount_data.mount_number = mount.abs_number + 1;
    rider.mount_data.next_rider = &next_rider;
    rider.mount_data.next_rider_number = next_rider.abs_number + 1;
    next_rider.mount_data.mount = &mount;
    next_rider.mount_data.mount_number = mount.abs_number;
    mount.mount_data.rider = &rider;
    mount.mount_data.rider_number = rider.abs_number + 1;

    const char_data *live_characters[] = {&mount, &rider, &next_rider};
    JsGameAdapterOptions options = make_options(live_characters, 3, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture rider_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&rider, options, &rider_fixture));
    EXPECT_FALSE(rider_fixture.mount.is_riding);
    EXPECT_FALSE(rider_fixture.mount.has_mount);
    EXPECT_FALSE(rider_fixture.mount.has_next_rider);

    JsGameCharacterFixture mount_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&mount, options, &mount_fixture));
    EXPECT_FALSE(mount_fixture.mount.is_mounted);
    EXPECT_FALSE(mount_fixture.mount.has_rider);

    mount.mount_data.rider_number = rider.abs_number;
    rider.mount_data.mount = nullptr;
    ASSERT_TRUE(js_game_adapter_character_fixture(&mount, options, &mount_fixture));
    EXPECT_FALSE(mount_fixture.mount.has_rider);
}

TEST(JsGameAdapter, RejectsStaleRootCharacterMountPointersBeforeDereferencing) {
    char_data character = make_character("Rider", 1, 92, 90, 120, false);
    char_data mount = make_character("Warhorse", 1, 20, 90, 120, true);
    char_data rider = make_character("Passenger", 1, 25, 90, 120, true);
    char_data next_rider = make_character("PackRider", 1, 25, 90, 120, true);
    character.abs_number = 2001;
    mount.abs_number = 2000;
    rider.abs_number = 2002;
    next_rider.abs_number = 2003;

    const char_data *live_characters[] = {&character, &mount, &rider, &next_rider};
    JsGameAdapterOptions options = make_options(live_characters, 4, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    auto *stale_character = reinterpret_cast<char_data *>(static_cast<std::uintptr_t>(0x1));

    character.mount_data.mount = stale_character;
    character.mount_data.mount_number = mount.abs_number;
    character.mount_data.rider = stale_character;
    character.mount_data.rider_number = rider.abs_number;
    character.mount_data.next_rider = stale_character;
    character.mount_data.next_rider_number = next_rider.abs_number;

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));
    EXPECT_FALSE(fixture.mount.is_riding);
    EXPECT_FALSE(fixture.mount.has_mount);
    EXPECT_FALSE(fixture.mount.is_mounted);
    EXPECT_FALSE(fixture.mount.has_rider);
    EXPECT_FALSE(fixture.mount.has_next_rider);

    character.mount_data.mount = &mount;
    character.mount_data.mount_number = mount.abs_number;
    mount.mount_data.rider = &character;
    mount.mount_data.rider_number = character.abs_number;
    character.mount_data.next_rider = stale_character;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));
    EXPECT_TRUE(fixture.mount.is_riding);
    EXPECT_TRUE(fixture.mount.has_mount);
    EXPECT_FALSE(fixture.mount.has_next_rider);
}

TEST(JsGameAdapter, BoundsAndBreaksCharacterMountTraversal) {
    char_data mount = make_character("Warhorse", 1, 20, 90, 120, true);
    mount.abs_number = 2000;
    std::vector<char_data> riders;
    riders.reserve(101);
    for (int index = 0; index < 101; ++index) {
        riders.push_back(make_character("Rider", 1, 92, 90, 120, false));
        riders[index].abs_number = 3000 + index;
        riders[index].mount_data.mount = &mount;
        riders[index].mount_data.mount_number = mount.abs_number;
        if (index > 0) {
            riders[index - 1].mount_data.next_rider = &riders[index];
            riders[index - 1].mount_data.next_rider_number = riders[index].abs_number;
        }
    }
    mount.mount_data.rider = &riders[0];
    mount.mount_data.rider_number = riders[0].abs_number;

    std::vector<const char_data *> live_characters;
    live_characters.push_back(&mount);
    for (const char_data &rider : riders)
        live_characters.push_back(&rider);
    JsGameAdapterOptions options =
        make_options(live_characters.data(), live_characters.size(), nullptr, 0, nullptr, -1,
                     nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture beyond_cap_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&riders[100], options, &beyond_cap_fixture));
    EXPECT_FALSE(beyond_cap_fixture.mount.is_riding);
    EXPECT_FALSE(beyond_cap_fixture.mount.has_mount);

    riders[0].mount_data.next_rider = &riders[0];
    riders[0].mount_data.next_rider_number = riders[0].abs_number;
    JsGameCharacterFixture cycle_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&riders[1], options, &cycle_fixture));
    EXPECT_FALSE(cycle_fixture.mount.is_riding);
    EXPECT_FALSE(cycle_fixture.mount.has_mount);

    auto *stale_next_rider = reinterpret_cast<char_data *>(static_cast<std::uintptr_t>(0x1));
    riders[0].mount_data.next_rider = stale_next_rider;
    riders[0].mount_data.next_rider_number = riders[1].abs_number;
    ASSERT_TRUE(js_game_adapter_character_fixture(&riders[1], options, &cycle_fixture));
    EXPECT_FALSE(cycle_fixture.mount.is_riding);
    EXPECT_FALSE(cycle_fixture.mount.has_mount);
}

TEST(JsGameAdapter, SnapshotsCharacterDamageDetails) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    player.damage_details.add_damage(1, 5);
    player.damage_details.add_damage(TYPE_HIT, 10);
    player.damage_details.add_damage(TYPE_HIT, 20);
    player.damage_details.tick(2.0f);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_DOUBLE_EQ(fixture.damage_details.elapsed_combat_seconds, 2.0);
    EXPECT_EQ(fixture.damage_details.total_damage, 35);
    EXPECT_DOUBLE_EQ(fixture.damage_details.damage_per_second, 17.5);
    ASSERT_EQ(fixture.damage_details.entries.size(), 2u);

    const JsGameDamageEntryFixture &skill_entry = fixture.damage_details.entries[0];
    EXPECT_EQ(skill_entry.source_id, 1);
    EXPECT_EQ(skill_entry.source_kind, "skill");
    EXPECT_EQ(skill_entry.source_name, get_skill_array()[1].name);
    EXPECT_EQ(skill_entry.instance_count, 1);
    EXPECT_EQ(skill_entry.total_damage, 5);
    EXPECT_EQ(skill_entry.largest_damage, 5);
    EXPECT_DOUBLE_EQ(skill_entry.average_damage, 5.0);
    EXPECT_NEAR(skill_entry.percent_of_total, 14.2857, 0.0001);

    const JsGameDamageEntryFixture &attack_entry = fixture.damage_details.entries[1];
    EXPECT_EQ(attack_entry.source_id, TYPE_HIT);
    EXPECT_EQ(attack_entry.source_kind, "attack");
    EXPECT_EQ(attack_entry.source_name, get_hit_text(TYPE_HIT).singular);
    EXPECT_EQ(attack_entry.instance_count, 2);
    EXPECT_EQ(attack_entry.total_damage, 30);
    EXPECT_EQ(attack_entry.largest_damage, 20);
    EXPECT_DOUBLE_EQ(attack_entry.average_damage, 15.0);
    EXPECT_NEAR(attack_entry.percent_of_total, 85.7142, 0.0001);
}

TEST(JsGameAdapter, SnapshotsEmptyAndUnknownCharacterDamageDetails) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));
    EXPECT_DOUBLE_EQ(fixture.damage_details.elapsed_combat_seconds, 0.0);
    EXPECT_EQ(fixture.damage_details.total_damage, 0);
    EXPECT_DOUBLE_EQ(fixture.damage_details.damage_per_second, 0.0);
    EXPECT_TRUE(fixture.damage_details.entries.empty());

    player.damage_details.add_damage(MAX_SKILLS + 10, 10);
    player.damage_details.tick(0.25f);
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));
    EXPECT_DOUBLE_EQ(fixture.damage_details.elapsed_combat_seconds, 0.25);
    EXPECT_EQ(fixture.damage_details.total_damage, 10);
    EXPECT_DOUBLE_EQ(fixture.damage_details.damage_per_second, 20.0);
    ASSERT_EQ(fixture.damage_details.entries.size(), 1u);
    EXPECT_EQ(fixture.damage_details.entries[0].source_id, MAX_SKILLS + 10);
    EXPECT_EQ(fixture.damage_details.entries[0].source_kind, "unknown");
    EXPECT_EQ(fixture.damage_details.entries[0].source_name, "Unknown");
    EXPECT_EQ(fixture.damage_details.entries[0].percent_of_total, 100.0);
}

TEST(JsGameAdapter, SnapshotsMissingAndInvalidSpecializationSafely) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));
    EXPECT_EQ(fixture.specializations.selected_key, "nothing");
    EXPECT_EQ(fixture.specializations.selected_id, game_types::PS_None);
    EXPECT_EQ(fixture.specializations.selected_name, "nothing");
    EXPECT_EQ(fixture.specializations.current_key, "nothing");
    EXPECT_EQ(fixture.specializations.current_id, game_types::PS_None);
    EXPECT_EQ(fixture.specializations.current_name, "nothing");
    EXPECT_FALSE(fixture.specializations.is_mage_specialization);
    EXPECT_FALSE(fixture.specializations.has_runtime_state);

    char_prof_data professions{};
    professions.specialization = game_types::PS_Count;
    player.profs = &professions;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));
    EXPECT_EQ(fixture.specializations.selected_id, -1);
    EXPECT_EQ(fixture.specializations.selected_key, "Unknown");
    EXPECT_EQ(fixture.specializations.selected_name, "Unknown");
    EXPECT_EQ(fixture.specializations.current_id, game_types::PS_None);
    EXPECT_EQ(fixture.specializations.current_key, "nothing");
    EXPECT_EQ(fixture.specializations.current_name, "nothing");
    EXPECT_FALSE(fixture.specializations.is_mage_specialization);
    EXPECT_FALSE(fixture.specializations.has_runtime_state);
}

TEST(JsGameAdapter, MapsEverySpecializationIdToStablePublicNames) {
    struct ExpectedSpecialization {
        int id;
        const char *key;
        const char *name;
        bool mage;
        bool runtime_state;
    };
    const ExpectedSpecialization expected[] = {
        {game_types::PS_None, "nothing", "nothing", false, false},
        {game_types::PS_Fire, "fire", "fire", true, true},
        {game_types::PS_Cold, "cold", "cold", true, true},
        {game_types::PS_Regeneration, "regeneration", "regeneration", false, false},
        {game_types::PS_Protection, "protection", "protection", false, false},
        {game_types::PS_Animals, "animals", "animals", false, false},
        {game_types::PS_Stealth, "stealth", "stealth", false, false},
        {game_types::PS_WildFighting, "wildFighting", "wild fighting", false, true},
        {game_types::PS_Teleportation, "teleportation", "teleportation", false, false},
        {game_types::PS_Illusion, "illusion", "illusion", false, false},
        {game_types::PS_Lightning, "lightning", "lightning", true, true},
        {game_types::PS_Guardian, "guardian", "guardian", false, false},
        {game_types::PS_HeavyFighting, "heavyFighting", "heavy fighting", false, true},
        {game_types::PS_LightFighting, "lightFighting", "light fighting", false, true},
        {game_types::PS_Defender, "defending", "defending", false, true},
        {game_types::PS_Archery, "archery", "archery", false, false},
        {game_types::PS_Darkness, "darkness", "darkness", true, true},
        {game_types::PS_Arcane, "arcane", "arcane", true, true},
        {game_types::PS_WeaponMaster, "weaponMastery", "weapon mastery", false, false},
        {game_types::PS_BattleMage, "battleMagic", "battle magic", true, true},
    };
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    char_prof_data professions{};
    player.profs = &professions;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    for (const ExpectedSpecialization &entry : expected) {
        professions.specialization = entry.id;
        player.extra_specialization_data.set(player);
        JsGameCharacterFixture fixture;
        ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture)) << entry.key;
        EXPECT_EQ(fixture.specializations.selected_id, entry.id) << entry.key;
        EXPECT_EQ(fixture.specializations.selected_key, entry.key) << entry.key;
        EXPECT_EQ(fixture.specializations.selected_name, entry.name) << entry.key;
        EXPECT_EQ(fixture.specializations.current_id, entry.id) << entry.key;
        EXPECT_EQ(fixture.specializations.current_key, entry.key) << entry.key;
        EXPECT_EQ(fixture.specializations.current_name, entry.name) << entry.key;
        EXPECT_EQ(fixture.specializations.is_mage_specialization, entry.mage) << entry.key;
        EXPECT_EQ(fixture.specializations.has_runtime_state, entry.runtime_state) << entry.key;
    }
}

TEST(JsGameAdapter, SnapshotsPlayerWithoutPrototypeVnum) {
    const char *races[] = {"God", "Human"};
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, races, 2);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_EQ(fixture.id, "player");
    EXPECT_EQ(fixture.name, "PlayerOne");
    EXPECT_EQ(fixture.race, "Human");
    EXPECT_EQ(fixture.vnum, -1);
    EXPECT_EQ(fixture.prototype_vnum, -1);
    EXPECT_EQ(fixture.experience, 29000);
    EXPECT_EQ(fixture.rank, 32);
    EXPECT_EQ(fixture.specials.tactics, "aggressive");
    EXPECT_EQ(fixture.specials2.act_flags, (std::vector<std::string>{"writing", "incognito"}));
    EXPECT_FALSE(fixture.is_npc);
}

TEST(JsGameAdapter, MapsCharacterSpecialsFiniteVocabularies) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    player.specials.position = POSITION_SHAPING;
    player.specials.default_pos = 99;
    player.specials.last_direction = 99;
    player.specials.tactics = TACTICS_BERSERK;
    player.specials2.tactics = 99;
    player.specials2.shooting = 99;
    player.specials2.casting = 99;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_EQ(fixture.specials.position, "Shaping");
    EXPECT_EQ(fixture.specials.default_position, "Unknown");
    EXPECT_EQ(fixture.specials.last_direction, "");
    EXPECT_EQ(fixture.specials.tactics, "berserk");
    EXPECT_EQ(fixture.specials2.tactics, "Unknown");
    EXPECT_EQ(fixture.specials2.shooting, "Unknown");
    EXPECT_EQ(fixture.specials2.casting, "Unknown");
}

TEST(JsGameAdapter, MapsCharacterSpecialsPointerPresenceWithoutDereferencing) {
    char_data player = make_character("PlayerOne", 1, 29, 90, 120, false);
    char_data opponent = make_character("Opponent", 1, 12, 40, 60, false);
    memory_rec remembered{};
    player.specials.fighting = &opponent;
    player.specials.hunting = &opponent;
    player.specials.memory = &remembered;
    const char_data *live_characters[] = {&player};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&player, options, &fixture));

    EXPECT_TRUE(fixture.specials.is_fighting);
    EXPECT_TRUE(fixture.specials.is_hunting);
    EXPECT_TRUE(fixture.specials.has_memory);
}

TEST(JsGameAdapter, RejectsNullAndStaleCharacters) {
    char_data live = make_character("Live", 1, 10, 10, 10, false);
    char_data stale = make_character("Stale", 1, 10, 10, 10, false);
    const char_data *live_characters[] = {&live};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    EXPECT_FALSE(js_game_adapter_character_fixture(nullptr, options, &fixture));
    EXPECT_FALSE(js_game_adapter_character_fixture(&stale, options, &fixture));

    JsGameAdapterOptions default_options;
    EXPECT_FALSE(js_game_adapter_character_fixture(&live, default_options, &fixture));

    JsGameAdapterOptions missing_table_options;
    missing_table_options.live_character_count = 1;
    EXPECT_FALSE(js_game_adapter_character_fixture(&live, missing_table_options, &fixture));
}

TEST(JsGameAdapter, SnapshotsObjectRoomAndZoneFields) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);
    object.affected[0].location = APPLY_STR;
    object.affected[0].modifier = 2;
    object.affected[1].location = 41;
    object.affected[1].modifier = -1;
    extra_descr_data hinge_extra{};
    hinge_extra.keyword = const_cast<char *>("hinge");
    hinge_extra.description = const_cast<char *>("The hinge is polished by use.");
    extra_descr_data rune_extra{};
    rune_extra.keyword = const_cast<char *>("runes");
    rune_extra.description = const_cast<char *>("Faint runes circle the lever.");
    rune_extra.next = &hinge_extra;
    object.ex_description = &rune_extra;
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Northern Gate", 1204, 0)};
    zone_data zones[1] = {make_zone("Old City", 12)};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
                                                object_index, 1, zones, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.id, "object:300");
    EXPECT_EQ(object_fixture.name, "silver lever");
    EXPECT_EQ(object_fixture.description, "A detailed object description.");
    EXPECT_EQ(object_fixture.short_description, "silver lever");
    EXPECT_TRUE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "A detailed action description.");
    EXPECT_EQ(object_fixture.vnum, 300);
    EXPECT_EQ(object_fixture.flags.item_type, "weapon");
    EXPECT_EQ(object_fixture.flags.wear_flags, (std::vector<std::string>{"take", "wield"}));
    EXPECT_EQ(object_fixture.flags.extra_flags, (std::vector<std::string>{"glow", "magic"}));
    EXPECT_EQ(object_fixture.flags.level, 12);
    EXPECT_EQ(object_fixture.flags.weight, 700);
    EXPECT_EQ(object_fixture.flags.cost, 450);
    EXPECT_EQ(object_fixture.flags.cost_per_day, 15);
    EXPECT_EQ(object_fixture.flags.timer, 30);
    EXPECT_EQ(object_fixture.flags.rarity, 2);
    EXPECT_EQ(object_fixture.flags.material, "metal");
    EXPECT_TRUE(object_fixture.touched);
    ASSERT_EQ(object_fixture.affects.size(), 2U);
    EXPECT_EQ(object_fixture.affects[0].slot_index, 0);
    EXPECT_EQ(object_fixture.affects[0].location, APPLY_STR);
    EXPECT_EQ(object_fixture.affects[0].location_name, "STR");
    EXPECT_EQ(object_fixture.affects[0].modifier, 2);
    EXPECT_EQ(object_fixture.affects[1].slot_index, 1);
    EXPECT_EQ(object_fixture.affects[1].location, 41);
    EXPECT_EQ(object_fixture.affects[1].location_name, "Unknown");
    EXPECT_EQ(object_fixture.affects[1].modifier, -1);
    ASSERT_EQ(object_fixture.extra_descriptions.size(), 2U);
    EXPECT_EQ(object_fixture.extra_descriptions[0].keyword, "runes");
    EXPECT_EQ(object_fixture.extra_descriptions[0].description, "Faint runes circle the lever.");
    EXPECT_EQ(object_fixture.extra_descriptions[1].keyword, "hinge");
    EXPECT_EQ(object_fixture.extra_descriptions[1].description, "The hinge is polished by use.");
    ASSERT_TRUE(object_fixture.has_room);
    EXPECT_EQ(object_fixture.room.vnum, 1204);
    ASSERT_TRUE(object_fixture.room.has_zone);
    EXPECT_EQ(object_fixture.room.zone.vnum, 12);

    JsGameRoomFixture room_fixture;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.id, "room:1204");
    EXPECT_EQ(room_fixture.name, "Northern Gate");
    EXPECT_EQ(room_fixture.description, "A detailed room description.");
    EXPECT_EQ(room_fixture.vnum, 1204);
    EXPECT_EQ(room_fixture.level, 4);
    EXPECT_EQ(room_fixture.sector_type, "City");
    EXPECT_EQ(room_fixture.flags, (std::vector<std::string>{"dark", "indoors"}));
    EXPECT_EQ(room_fixture.alignment, -3);
    EXPECT_EQ(room_fixture.light, 2);
    EXPECT_FALSE(room_fixture.is_sunlit);

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_EQ(zone_fixture.id, "zone:12");
    EXPECT_EQ(zone_fixture.name, "Old City");
    EXPECT_TRUE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "A detailed zone description.");
    EXPECT_TRUE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "N-G-S");
    EXPECT_EQ(zone_fixture.vnum, 12);
    EXPECT_EQ(zone_fixture.level, 5);
    EXPECT_EQ(zone_fixture.lifespan, 45);
    EXPECT_EQ(zone_fixture.age, 7);
    EXPECT_EQ(zone_fixture.top_room_vnum, 111);
    EXPECT_EQ(zone_fixture.x, 11);
    EXPECT_EQ(zone_fixture.y, -4);
    EXPECT_EQ(zone_fixture.symbol, "Z");
    EXPECT_EQ(zone_fixture.white_power, 21);
    EXPECT_EQ(zone_fixture.dark_power, 13);
    EXPECT_EQ(zone_fixture.magi_power, 8);
    EXPECT_EQ(zone_fixture.minimum_look_level, 3);
    EXPECT_EQ(zone_fixture.reset_mode, 2);
}

TEST(JsGameAdapter, SnapshotsObjectContainerWhenReciprocalAndLive) {
    index_data object_index[2]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    obj_data object = make_object("silver lever", 0);
    obj_data container = make_object("oak chest", 1);
    container.touched = 0;
    object.in_room = NOWHERE;
    object.in_obj = &container;
    container.contains = &object;
    const obj_data *live_objects[] = {&object, &container};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 2, nullptr, -1, nullptr,
                                                0, object_index, 2, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    EXPECT_FALSE(object_fixture.has_room);
    ASSERT_TRUE(object_fixture.has_container);
    EXPECT_EQ(object_fixture.container.id, "object:301");
    EXPECT_EQ(object_fixture.container.name, "oak chest");
    EXPECT_FALSE(object_fixture.container.touched);
    EXPECT_FALSE(object_fixture.container.has_room);
}

TEST(JsGameAdapter, NormalizesUntouchedObjectsToFalse) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);
    object.touched = 0;
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    EXPECT_FALSE(object_fixture.touched);
}

TEST(JsGameAdapter, OmitsObjectContainerWhenNotReciprocalOrNotLive) {
    index_data object_index[2]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    obj_data object = make_object("silver lever", 0);
    obj_data container = make_object("oak chest", 1);
    object.in_room = NOWHERE;
    object.in_obj = &container;
    const obj_data *live_objects[] = {&object, &container};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 2, nullptr, -1, nullptr,
                                                0, object_index, 2, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_container);

    container.contains = &container;
    container.next_content = &container;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_container);

    object.in_obj = &container;
    container.contains = &object;
    room_data world[1] = {make_room("Northern Gate", 1204, 0)};
    object.in_room = 0;
    JsGameAdapterOptions inconsistent_room_options = make_options(
        nullptr, 0, live_objects, 2, world, 0, nullptr, 0, object_index, 2, nullptr, 0, nullptr,
        0);
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, inconsistent_room_options, &object_fixture));
    EXPECT_TRUE(object_fixture.has_room);
    EXPECT_FALSE(object_fixture.has_container);

    object.in_room = NOWHERE;
    const obj_data *only_object_live[] = {&object};
    JsGameAdapterOptions stale_container_options = make_options(
        nullptr, 0, only_object_live, 1, nullptr, -1, nullptr, 0, object_index, 1, nullptr, 0,
        nullptr, 0);
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, stale_container_options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_container);
}

TEST(JsGameAdapter, BoundsObjectContainerMembershipTraversal) {
    index_data object_index[102]{};
    for (int index = 0; index < 102; ++index)
        object_index[index].virt = 300 + index;
    obj_data container = make_object("oak chest", 1);
    std::vector<std::string> names;
    names.reserve(101);
    for (int index = 0; index < 101; ++index)
        names.push_back("contained-" + std::to_string(index));
    std::vector<obj_data> contained;
    contained.reserve(101);
    for (int index = 0; index < 101; ++index) {
        contained.push_back(make_object(names[index].c_str(), index + 1));
        contained[index].in_room = NOWHERE;
        contained[index].in_obj = &container;
        if (index > 0)
            contained[index - 1].next_content = &contained[index];
    }
    container.contains = &contained[0];
    const obj_data *live_objects[] = {&container, &contained[99], &contained[100]};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 3, nullptr, -1, nullptr,
                                                0, object_index, 102, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&contained[99], options, &object_fixture));
    ASSERT_TRUE(object_fixture.has_container);
    EXPECT_EQ(object_fixture.container.id, "object:301");

    ASSERT_TRUE(js_game_adapter_object_fixture(&contained[100], options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_container);
}

TEST(JsGameAdapter, SnapshotsObjectContentsWhenReciprocalAndLive) {
    index_data object_index[3]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    object_index[2].virt = 302;
    obj_data container = make_object("oak chest", 0);
    obj_data first = make_object("small gear", 1);
    obj_data second = make_object("silver key", 2);
    first.touched = 0;
    second.touched = 42;
    first.in_room = NOWHERE;
    first.in_obj = &container;
    first.next_content = &second;
    second.in_room = NOWHERE;
    second.in_obj = &container;
    container.contains = &first;
    const obj_data *live_objects[] = {&container, &first, &second};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 3, nullptr, -1, nullptr,
                                                0, object_index, 3, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 2U);
    EXPECT_EQ(object_fixture.contents[0].id, "object:301");
    EXPECT_EQ(object_fixture.contents[0].name, "small gear");
    EXPECT_FALSE(object_fixture.contents[0].touched);
    EXPECT_FALSE(object_fixture.contents[0].has_room);
    EXPECT_EQ(object_fixture.contents[1].id, "object:302");
    EXPECT_EQ(object_fixture.contents[1].name, "silver key");
    EXPECT_TRUE(object_fixture.contents[1].touched);
}

TEST(JsGameAdapter, FiltersInvalidAndStaleObjectContentsAndStopsCycles) {
    index_data object_index[5]{};
    for (int index = 0; index < 5; ++index)
        object_index[index].virt = 300 + index;
    obj_data container = make_object("oak chest", 0);
    obj_data valid = make_object("small gear", 1);
    obj_data stale = make_object("stale coin", 2);
    obj_data wrong_parent = make_object("wrong parent", 3);
    obj_data room_object = make_object("room object", 4);
    valid.in_room = NOWHERE;
    valid.in_obj = &container;
    valid.next_content = &stale;
    stale.in_room = NOWHERE;
    stale.in_obj = &container;
    stale.next_content = &wrong_parent;
    wrong_parent.in_room = NOWHERE;
    wrong_parent.next_content = &room_object;
    room_object.in_room = 0;
    room_object.in_obj = &container;
    room_object.next_content = &valid;
    container.contains = &valid;
    const obj_data *live_objects[] = {&container, &valid, &wrong_parent, &room_object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 4, nullptr, -1, nullptr,
                                                0, object_index, 5, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 1U);
    EXPECT_EQ(object_fixture.contents[0].id, "object:301");
}

TEST(JsGameAdapter, SkipsWrongParentObjectContentsAndContinuesTraversal) {
    index_data object_index[3]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    object_index[2].virt = 302;
    obj_data container = make_object("oak chest", 0);
    obj_data wrong_parent = make_object("wrong parent", 1);
    obj_data valid = make_object("small gear", 2);
    wrong_parent.in_room = NOWHERE;
    wrong_parent.next_content = &valid;
    valid.in_room = NOWHERE;
    valid.in_obj = &container;
    container.contains = &wrong_parent;
    const obj_data *live_objects[] = {&container, &wrong_parent, &valid};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 3, nullptr, -1, nullptr,
                                                0, object_index, 3, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 1U);
    EXPECT_EQ(object_fixture.contents[0].id, "object:302");
}

TEST(JsGameAdapter, SkipsDirectRoomObjectContentsAndContinuesTraversal) {
    index_data object_index[3]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    object_index[2].virt = 302;
    obj_data container = make_object("oak chest", 0);
    obj_data room_object = make_object("room object", 1);
    obj_data valid = make_object("small gear", 2);
    room_object.in_room = 0;
    room_object.in_obj = &container;
    room_object.next_content = &valid;
    valid.in_room = NOWHERE;
    valid.in_obj = &container;
    container.contains = &room_object;
    const obj_data *live_objects[] = {&container, &room_object, &valid};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 3, nullptr, -1, nullptr,
                                                0, object_index, 3, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 1U);
    EXPECT_EQ(object_fixture.contents[0].id, "object:302");
}

TEST(JsGameAdapter, StopsObjectContentsCyclesWithoutDuplicates) {
    index_data object_index[3]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    object_index[2].virt = 302;
    obj_data container = make_object("oak chest", 0);
    obj_data first = make_object("small gear", 1);
    obj_data second = make_object("silver key", 2);
    first.in_room = NOWHERE;
    first.in_obj = &container;
    first.next_content = &second;
    second.in_room = NOWHERE;
    second.in_obj = &container;
    second.next_content = &first;
    container.contains = &first;
    const obj_data *live_objects[] = {&container, &first, &second};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 3, nullptr, -1, nullptr,
                                                0, object_index, 3, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 2U);
    EXPECT_EQ(object_fixture.contents[0].id, "object:301");
    EXPECT_EQ(object_fixture.contents[1].id, "object:302");
}

TEST(JsGameAdapter, DoesNotExposeObjectAsItsOwnContent) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data container = make_object("oak chest", 0);
    container.in_room = NOWHERE;
    container.in_obj = &container;
    container.contains = &container;
    container.next_content = &container;
    const obj_data *live_objects[] = {&container};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    EXPECT_TRUE(object_fixture.contents.empty());
}

TEST(JsGameAdapter, StopsObjectContentsBeforeDereferencingStaleNodes) {
    index_data object_index[3]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    object_index[2].virt = 302;
    obj_data container = make_object("oak chest", 0);
    obj_data stale = make_object("stale coin", 1);
    obj_data valid = make_object("small gear", 2);
    stale.in_room = NOWHERE;
    stale.in_obj = &container;
    stale.next_content = &valid;
    valid.in_room = NOWHERE;
    valid.in_obj = &container;
    container.contains = &stale;
    const obj_data *live_objects[] = {&container, &valid};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 2, nullptr, -1, nullptr,
                                                0, object_index, 3, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    EXPECT_TRUE(object_fixture.contents.empty());
}

TEST(JsGameAdapter, BoundsObjectContentsTraversal) {
    index_data object_index[102]{};
    for (int index = 0; index < 102; ++index)
        object_index[index].virt = 300 + index;
    obj_data container = make_object("oak chest", 0);
    std::vector<std::string> names;
    names.reserve(101);
    for (int index = 0; index < 101; ++index)
        names.push_back("contained-" + std::to_string(index));
    std::vector<obj_data> contained;
    contained.reserve(101);
    std::vector<const obj_data *> live_objects;
    live_objects.push_back(&container);
    for (int index = 0; index < 101; ++index) {
        contained.push_back(make_object(names[index].c_str(), index + 1));
        contained[index].in_room = NOWHERE;
        contained[index].in_obj = &container;
        if (index > 0)
            contained[index - 1].next_content = &contained[index];
        live_objects.push_back(&contained[index]);
    }
    container.contains = &contained[0];
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects.data(),
                                                live_objects.size(), nullptr, -1, nullptr, 0,
                                                object_index, 102, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    ASSERT_EQ(object_fixture.contents.size(), 100U);
    EXPECT_EQ(object_fixture.contents.front().id, "object:301");
    EXPECT_EQ(object_fixture.contents.back().id, "object:400");
}

TEST(JsGameAdapter, BoundsObjectContentsTraversalByVisitedNodes) {
    index_data object_index[102]{};
    for (int index = 0; index < 102; ++index)
        object_index[index].virt = 300 + index;
    obj_data container = make_object("oak chest", 0);
    std::vector<std::string> names;
    names.reserve(101);
    for (int index = 0; index < 101; ++index)
        names.push_back("contained-" + std::to_string(index));
    std::vector<obj_data> contained;
    contained.reserve(101);
    std::vector<const obj_data *> live_objects;
    live_objects.push_back(&container);
    for (int index = 0; index < 101; ++index) {
        contained.push_back(make_object(names[index].c_str(), index + 1));
        contained[index].in_room = NOWHERE;
        if (index == 100)
            contained[index].in_obj = &container;
        if (index > 0)
            contained[index - 1].next_content = &contained[index];
        live_objects.push_back(&contained[index]);
    }
    container.contains = &contained[0];
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects.data(),
                                                live_objects.size(), nullptr, -1, nullptr, 0,
                                                object_index, 102, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&container, options, &object_fixture));

    EXPECT_TRUE(object_fixture.contents.empty());
}

TEST(JsGameAdapter, BoundsObjectExtraDescriptionSnapshotsAndStopsCycles) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);

    std::vector<extra_descr_data> descriptions(33);
    std::vector<std::string> keywords;
    std::vector<std::string> bodies;
    keywords.reserve(descriptions.size());
    bodies.reserve(descriptions.size());
    for (std::size_t index = 0; index < descriptions.size(); ++index) {
        keywords.push_back("keyword-" + std::to_string(index));
        bodies.push_back("description-" + std::to_string(index));
        descriptions[index].keyword = keywords[index].data();
        descriptions[index].description = bodies[index].data();
        descriptions[index].next =
            index + 1 < descriptions.size() ? &descriptions[index + 1] : &descriptions[5];
    }
    object.ex_description = descriptions.data();

    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, 0, nullptr, 0,
                                                object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    ASSERT_EQ(object_fixture.extra_descriptions.size(), 32U);
    EXPECT_EQ(object_fixture.extra_descriptions.front().keyword, "keyword-0");
    EXPECT_EQ(object_fixture.extra_descriptions.front().description, "description-0");
    EXPECT_EQ(object_fixture.extra_descriptions.back().keyword, "keyword-31");
    EXPECT_EQ(object_fixture.extra_descriptions.back().description, "description-31");
}

TEST(JsGameAdapter, StopsObjectExtraDescriptionSnapshotsOnPreCapCycles) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);

    extra_descr_data third{};
    third.keyword = const_cast<char *>("third");
    third.description = const_cast<char *>("third description");
    extra_descr_data second{};
    second.keyword = const_cast<char *>("second");
    second.description = const_cast<char *>("second description");
    second.next = &third;
    extra_descr_data first{};
    first.keyword = const_cast<char *>("first");
    first.description = const_cast<char *>("first description");
    first.next = &second;
    third.next = &second;
    object.ex_description = &first;

    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, 0, nullptr, 0,
                                                object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    ASSERT_EQ(object_fixture.extra_descriptions.size(), 3U);
    EXPECT_EQ(object_fixture.extra_descriptions[0].keyword, "first");
    EXPECT_EQ(object_fixture.extra_descriptions[1].keyword, "second");
    EXPECT_EQ(object_fixture.extra_descriptions[2].keyword, "third");
}

TEST(JsGameAdapter, BoundsObjectExtraDescriptionTextCopies) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("silver lever", 0);
    std::string long_keyword(513, 'k');
    std::string long_description(1025, 'd');
    extra_descr_data description{};
    description.keyword = long_keyword.data();
    description.description = long_description.data();
    object.ex_description = &description;

    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, 0, nullptr, 0,
                                                object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    ASSERT_EQ(object_fixture.extra_descriptions.size(), 1U);
    EXPECT_EQ(object_fixture.extra_descriptions[0].keyword.size(), 512U);
    EXPECT_EQ(object_fixture.extra_descriptions[0].description.size(), 1024U);
    EXPECT_EQ(object_fixture.extra_descriptions[0].keyword, std::string(512, 'k'));
    EXPECT_EQ(object_fixture.extra_descriptions[0].description, std::string(1024, 'd'));
}

TEST(JsGameAdapter, FiltersUnknownObjectFlagDomains) {
    index_data object_index[1]{};
    object_index[0].virt = 301;
    obj_data object = make_object("strange shard", 0);
    object.obj_flags.type_flag = -1;
    object.obj_flags.material = 999;
    object.obj_flags.wear_flags = ITEM_TAKE | (1L << 29);
    object.obj_flags.extra_flags = ITEM_GLOW | (1L << 13) | (1L << 29);
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.flags.item_type, "Unknown");
    EXPECT_EQ(object_fixture.flags.material, "Unknown");
    EXPECT_EQ(object_fixture.flags.wear_flags, (std::vector<std::string>{"take"}));
    EXPECT_EQ(object_fixture.flags.extra_flags, (std::vector<std::string>{"glow"}));

    object.obj_flags.type_flag = 999;
    object.obj_flags.material = -1;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.flags.item_type, "Unknown");
    EXPECT_EQ(object_fixture.flags.material, "Unknown");
}

TEST(JsGameAdapter, ModelsUnknownRoomSectorTypes) {
    room_data world[1] = {make_room("Strange Room", 1205, 0)};
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameRoomFixture room_fixture;

    world[0].sector_type = -1;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.sector_type, "Unknown");

    world[0].sector_type = num_of_sector_types;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture));
    EXPECT_EQ(room_fixture.sector_type, "Unknown");
}

TEST(JsGameAdapter, SnapshotsEveryKnownRoomSectorTypeName) {
    room_data world[1] = {make_room("Sector Room", 1206, 0)};
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0);

    ASSERT_GT(num_of_sector_types, 0);
    for (int sector = 0; sector < num_of_sector_types; ++sector) {
        ASSERT_NE(sector_types[sector], nullptr) << sector;
        ASSERT_STRNE(sector_types[sector], "\n") << "sentinel must not be in live sector range";

        world[0].sector_type = sector;
        JsGameRoomFixture room_fixture;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room_fixture)) << sector;
        EXPECT_EQ(room_fixture.sector_type, sector_types[sector]) << sector;
    }
}

TEST(JsGameAdapter, PreservesNullableTextGetterNulls) {
    index_data object_index[1]{};
    object_index[0].virt = 301;
    obj_data object = make_object("ancient key", 0);
    object.action_description = nullptr;
    const obj_data *live_objects[] = {&object};

    zone_data zone = make_zone("Old City", 12);
    zone.description = nullptr;
    zone.map = nullptr;
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, &zone, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "");

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_FALSE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "");
    EXPECT_FALSE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "");
}

TEST(JsGameAdapter, PreservesPresentEmptyNullableTextGetters) {
    index_data object_index[1]{};
    object_index[0].virt = 302;
    obj_data object = make_object("blank sign", 0);
    object.action_description = const_cast<char *>("");
    const obj_data *live_objects[] = {&object};

    zone_data zone = make_zone("Old City", 12);
    zone.description = const_cast<char *>("");
    zone.map = const_cast<char *>("");
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, &zone, 1, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_TRUE(object_fixture.has_action_description);
    EXPECT_EQ(object_fixture.action_description, "");

    JsGameZoneFixture zone_fixture;
    ASSERT_TRUE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_TRUE(zone_fixture.has_description);
    EXPECT_EQ(zone_fixture.description, "");
    EXPECT_TRUE(zone_fixture.has_map);
    EXPECT_EQ(zone_fixture.map, "");
}

TEST(JsGameAdapter, ObjectShortDescriptionFallsBackToObjectName) {
    index_data object_index[1]{};
    object_index[0].virt = 301;
    obj_data object = make_object("ancient key", 0);
    object.short_description = nullptr;
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.name, "ancient key");
    EXPECT_EQ(object_fixture.short_description, "ancient key");
}

TEST(JsGameAdapter, SnapshotsRoomSunlitStateFromCurrentWeatherAndRoomFlags) {
    struct Case {
        const char *name;
        int sunlight;
        int sector_type;
        long room_flags;
        byte light;
        bool expected_is_sunlit;
    };
    const Case cases[] = {
        {"daylight lit room", SUN_LIGHT, SECT_FIELD, 0, 1, true},
        {"sunrise lit room", SUN_RISE, SECT_FIELD, 0, 1, true},
        {"sunset lit room", SUN_SET, SECT_FIELD, 0, 1, false},
        {"dark outdoor room", SUN_DARK, SECT_FIELD, 0, 0, false},
        {"dark flagged unlit room", SUN_LIGHT, SECT_FIELD, DARK, 0, false},
        {"dark flagged room with light source", SUN_LIGHT, SECT_FIELD, DARK, 1, true},
        {"inside night room", SUN_DARK, SECT_INSIDE, 0, 0, false},
        {"city night room", SUN_DARK, SECT_CITY, 0, 0, false},
    };

    for (const Case &test_case : cases) {
        ScopedSunlight sunlight(test_case.sunlight);
        room_data world[1] = {make_room(test_case.name, 101, 0)};
        world[0].sector_type = test_case.sector_type;
        world[0].room_flags = test_case.room_flags;
        world[0].light = test_case.light;
        JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
                                                    nullptr, 0, nullptr, 0, nullptr, 0);

        JsGameRoomFixture room;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room)) << test_case.name;
        EXPECT_EQ(room.is_sunlit, test_case.expected_is_sunlit) << test_case.name;
    }
}

TEST(JsGameAdapter, FiltersRoomFlagsForBuilderSnapshots) {
    room_data world[1] = {make_room("Northern Gate", 1204, 0)};
    world[0].room_flags = BFS_MARK | (1L << 30);
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameRoomFixture room;
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room));
    EXPECT_TRUE(room.flags.empty());

    struct ExpectedRoomFlag {
        long bit;
        const char *name;
    };
    const ExpectedRoomFlag expected_flags[] = {
        {DARK, "dark"},
        {DEATH, "death"},
        {NO_MOB, "noMob"},
        {INDOORS, "indoors"},
        {NORIDE, "noRide"},
        {PERMAFFECT, "permanentAffect"},
        {SHADOWY, "shadowy"},
        {NO_MAGIC, "noMagic"},
        {TUNNEL, "tunnel"},
        {PRIVATE, "private"},
        {GODROOM, "godRoom"},
        {DRINK_WATER, "drinkWater"},
        {DRINK_POISON, "drinkPoison"},
        {SECURITYROOM, "securityRoom"},
        {PEACEROOM, "peaceRoom"},
        {NO_TELEPORT, "noTeleport"},
        {HIDE_VNUM, "hideVnum"},
    };

    long all_safe_flags = 0;
    std::vector<std::string> all_safe_names;
    for (const ExpectedRoomFlag &expected : expected_flags) {
        world[0].room_flags = expected.bit;
        ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room)) << expected.name;
        EXPECT_EQ(room.flags, (std::vector<std::string>{expected.name})) << expected.name;
        all_safe_flags |= expected.bit;
        all_safe_names.emplace_back(expected.name);
    }

    world[0].room_flags = all_safe_flags | BFS_MARK | (1L << 30);
    ASSERT_TRUE(js_game_adapter_room_fixture(0, options, &room));
    EXPECT_EQ(room.flags, all_safe_names);
    EXPECT_EQ(std::find(room.flags.begin(), room.flags.end(), "BFS_MARK"), room.flags.end());
}

TEST(JsGameAdapter, RejectsStaleObjectsAndInvalidRoomBounds) {
    obj_data live = make_object("live object", 0);
    obj_data stale = make_object("stale object", 0);
    const obj_data *live_objects[] = {&live};
    room_data world[1] = {make_room("Only Room", 1, 0)};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    EXPECT_FALSE(js_game_adapter_object_fixture(&stale, options, &object_fixture));

    JsGameAdapterOptions default_options;
    EXPECT_FALSE(js_game_adapter_object_fixture(&live, default_options, &object_fixture));

    JsGameAdapterOptions missing_table_options;
    missing_table_options.live_object_count = 1;
    EXPECT_FALSE(js_game_adapter_object_fixture(&live, missing_table_options, &object_fixture));

    JsGameRoomFixture room_fixture;
    EXPECT_FALSE(js_game_adapter_room_fixture(-1, options, &room_fixture));
    EXPECT_FALSE(js_game_adapter_room_fixture(1, options, &room_fixture));
}

TEST(JsGameAdapter, ModelsObjectRoomAsMissingWhenObjectIsNotDirectlyInRoom) {
    index_data object_index[1]{};
    object_index[0].virt = 300;
    obj_data object = make_object("carried lever", 0);
    object.in_room = -1;
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Only Room", 1, 0)};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, world, 0, nullptr, 0,
                                                object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture object_fixture;
    object_fixture.has_room = true;
    object_fixture.room.id = "sentinel-room";
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_FALSE(object_fixture.has_room);
    EXPECT_EQ(object_fixture.room.id, "sentinel-room");
}

TEST(JsGameAdapter, SnapshotsObjectCarriedByWhenCarrierIsLive) {
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data object = make_object("carried lever", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    carrier.carrying = &object;
    const char_data *live_characters[] = {&carrier};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Carrier Room", 100, 0)};
    zone_data zones[1] = {make_zone("Carrier Zone", 10)};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
                                                nullptr, 0, object_index, 1, zones, 1, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    ASSERT_TRUE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
    EXPECT_EQ(fixture.carried_by.name, "Carrier");
    EXPECT_EQ(fixture.carried_by.level, 20);
    ASSERT_TRUE(fixture.carried_by.has_room);
    EXPECT_EQ(fixture.carried_by.room.vnum, 100);
    ASSERT_TRUE(fixture.carried_by.room.has_zone);
    EXPECT_EQ(fixture.carried_by.room.zone.vnum, 10);
}

TEST(JsGameAdapter, SnapshotsObjectWornByWhenCarrierHasObjectEquipped) {
    char_data wearer = make_character("Wearer", 1, 21, 31, 41, false);
    obj_data object = make_object("worn amulet", 0);
    object.in_room = -1;
    object.carried_by = &wearer;
    wearer.equipment[WEAR_NECK_1] = &object;
    const char_data *live_characters[] = {&wearer};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 301;
    room_data world[1] = {make_room("Wearer Room", 101, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, object_index, 1,
                     nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    EXPECT_FALSE(fixture.has_carried_by);
    ASSERT_TRUE(fixture.has_worn_by);
    EXPECT_EQ(fixture.worn_by.name, "Wearer");
    EXPECT_EQ(fixture.worn_by.level, 21);
    ASSERT_TRUE(fixture.worn_by.has_room);
    EXPECT_EQ(fixture.worn_by.room.vnum, 101);
}

TEST(JsGameAdapter, DoesNotTrustUnlinkedObjectCarrierBackPointer) {
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data object = make_object("unlinked lever", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    const char_data *live_characters[] = {&carrier};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Only Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, object_index, 1,
                     nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));

    EXPECT_FALSE(fixture.has_room);
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
}

TEST(JsGameAdapter, DoesNotExposeOwnerForRoomOrNestedObjects) {
    char_data carrier = make_character("Carrier", 1, 20, 30, 40, false);
    obj_data room_object = make_object("room lever", 0);
    room_object.in_room = 0;
    room_object.carried_by = &carrier;
    obj_data container = make_object("container", 1);
    obj_data nested_object = make_object("nested lever", 0);
    nested_object.in_room = -1;
    nested_object.in_obj = &container;
    nested_object.carried_by = &carrier;
    const char_data *live_characters[] = {&carrier};
    const obj_data *live_objects[] = {&room_object, &nested_object};
    index_data object_index[2]{};
    object_index[0].virt = 300;
    object_index[1].virt = 301;
    room_data world[1] = {make_room("Only Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 2, world, 0, nullptr, 0, object_index, 2,
                     nullptr, 0, nullptr, 0);

    JsGameObjectFixture room_fixture;
    room_fixture.has_carried_by = true;
    room_fixture.has_worn_by = true;
    ASSERT_TRUE(js_game_adapter_object_fixture(&room_object, options, &room_fixture));
    ASSERT_TRUE(room_fixture.has_room);
    EXPECT_FALSE(room_fixture.has_carried_by);
    EXPECT_FALSE(room_fixture.has_worn_by);

    JsGameObjectFixture nested_fixture;
    nested_fixture.has_carried_by = true;
    nested_fixture.has_worn_by = true;
    ASSERT_TRUE(js_game_adapter_object_fixture(&nested_object, options, &nested_fixture));
    EXPECT_FALSE(nested_fixture.has_room);
    EXPECT_FALSE(nested_fixture.has_carried_by);
    EXPECT_FALSE(nested_fixture.has_worn_by);
}

TEST(JsGameAdapter, RejectionPathsDoNotModifyExistingFixtures) {
    char_data stale_character = make_character("Stale", 1, 1, 1, 1, false);
    obj_data stale_object = make_object("stale", 0);
    room_data world[1] = {make_room("Only Room", 1, 0)};
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 0, nullptr, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    character_fixture.id = "sentinel-character";
    EXPECT_FALSE(js_game_adapter_character_fixture(&stale_character, options, &character_fixture));
    EXPECT_EQ(character_fixture.id, "sentinel-character");

    JsGameObjectFixture object_fixture;
    object_fixture.id = "sentinel-object";
    EXPECT_FALSE(js_game_adapter_object_fixture(&stale_object, options, &object_fixture));
    EXPECT_EQ(object_fixture.id, "sentinel-object");

    JsGameRoomFixture room_fixture;
    room_fixture.id = "sentinel-room";
    EXPECT_FALSE(js_game_adapter_room_fixture(2, options, &room_fixture));
    EXPECT_EQ(room_fixture.id, "sentinel-room");

    JsGameZoneFixture zone_fixture;
    zone_fixture.id = "sentinel-zone";
    EXPECT_FALSE(js_game_adapter_zone_fixture(0, options, &zone_fixture));
    EXPECT_EQ(zone_fixture.id, "sentinel-zone");
}

TEST(JsGameAdapter, BuildsContextFromOnlyLiveValidInputs) {
    const char *races[] = {"God", "Human"};
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data stale_actor = make_character("StaleActor", 1, 44, 55, 66, false);
    obj_data object = make_object("key", -1);
    obj_data stale_weapon = make_object("stale weapon", 0);
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Room", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 10)};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
                                                nullptr, 0, nullptr, 0, zones, 1, races, 2);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &stale_actor;
    input.speaker = &stale_actor;
    input.attacker = &stale_actor;
    input.victim = &stale_actor;
    input.killer = &stale_actor;
    input.object = &object;
    input.weapon = &stale_weapon;
    input.room = 0;
    input.text = "hello";
    input.wear_slot = MAX_WEAR;
    input.command = "say";
    input.args = "open sesame";
    input.has_tick = true;
    input.tick = 0;
    input.direction = "south";
    input.reverse_direction = "north";
    input.trigger = make_trigger();

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_TRUE(context.has_self);
    EXPECT_FALSE(context.has_actor);
    EXPECT_FALSE(context.has_speaker);
    EXPECT_FALSE(context.has_attacker);
    EXPECT_FALSE(context.has_victim);
    EXPECT_FALSE(context.has_killer);
    EXPECT_TRUE(context.has_object);
    EXPECT_FALSE(context.has_weapon);
    EXPECT_EQ(context.object.vnum, -1);
    EXPECT_TRUE(context.has_room);
    EXPECT_TRUE(context.has_zone);
    EXPECT_TRUE(context.has_text);
    EXPECT_EQ(context.text, "hello");
    EXPECT_FALSE(context.has_wear_slot);
    EXPECT_TRUE(context.has_command);
    EXPECT_EQ(context.command, "say");
    EXPECT_TRUE(context.has_args);
    EXPECT_EQ(context.args, "open sesame");
    EXPECT_TRUE(context.has_tick);
    EXPECT_EQ(context.tick, 0);
    EXPECT_TRUE(context.has_direction);
    EXPECT_EQ(context.direction, "south");
    EXPECT_TRUE(context.has_reverse_direction);
    EXPECT_EQ(context.reverse_direction, "north");
    EXPECT_EQ(context.trigger.legacy_name, "ON_ENTER");
}

TEST(JsGameAdapter, ContextUsesInvocationLocalRoleIds) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    self.specials2.idnum = 98765;
    char_data actor = make_character("Actor", 1, 11, 22, 33, false);
    actor.specials2.idnum = 11111;
    obj_data object = make_object("object", 0);
    object.owner = 98765;
    object.touched = 55;
    obj_data weapon = make_object("weapon", 0);
    index_data object_index[1]{};
    object_index[0].virt = 400;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&object, &weapon};
    JsGameAdapterOptions options =
        make_options(live_characters, 2, live_objects, 2, nullptr, -1, nullptr, 0, object_index, 1,
                     nullptr, 0, nullptr, 0);

    JsGameAdapterContextInput input;
    input.self = &self;
    input.actor = &actor;
    input.speaker = &actor;
    input.attacker = &actor;
    input.victim = &self;
    input.killer = &actor;
    input.object = &object;
    input.weapon = &weapon;
    input.wear_slot = WIELD;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_EQ(context.self.id, "self");
    EXPECT_EQ(context.actor.id, "actor");
    EXPECT_EQ(context.speaker.id, "speaker");
    EXPECT_EQ(context.attacker.id, "attacker");
    EXPECT_EQ(context.victim.id, "victim");
    ASSERT_TRUE(context.has_killer);
    EXPECT_EQ(context.killer.id, "killer");
    EXPECT_EQ(context.object.id, "object");
    EXPECT_TRUE(context.object.touched);
    EXPECT_EQ(context.weapon.id, "weapon");
    EXPECT_TRUE(context.weapon.touched);
    ASSERT_TRUE(context.has_wear_slot);
    EXPECT_EQ(context.wear_slot, "wield");
    EXPECT_EQ(context.self.id.find("98765"), std::string::npos);
    EXPECT_EQ(context.actor.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.speaker.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.attacker.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.victim.id.find("98765"), std::string::npos);
    EXPECT_EQ(context.killer.id.find("11111"), std::string::npos);
    EXPECT_EQ(context.object.id.find("55"), std::string::npos);
}

TEST(JsGameAdapter, MapsTypedTargetsFromLiveInputs) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data target_character = make_character("Target", 1, 44, 55, 66, false);
    obj_data target_object = make_object("target object", 0);
    index_data object_index[1]{};
    object_index[0].virt = 700;
    const char_data *live_characters[] = {&self, &target_character};
    const obj_data *live_objects[] = {&target_object};
    room_data world[2] = {make_room("Room", 100, 0), make_room("Other", 101, 0)};
    zone_data zones[1] = {make_zone("Zone", 10)};
    JsGameAdapterOptions options = make_options(live_characters, 2, live_objects, 1, world, 1,
                                                nullptr, 0, object_index, 1, zones, 1, nullptr, 0);

    target_data targ1;
    targ1.type = TARGET_CHAR;
    targ1.ptr.ch = &target_character;
    targ1.ch_num = target_character.abs_number;
    target_data targ2;
    targ2.type = TARGET_OBJ;
    targ2.ptr.obj = &target_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &targ1;
    input.targ2 = &targ2;
    input.target_room = 1;
    input.dying = &target_character;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ1);
    EXPECT_TRUE(context.targ1.has_character);
    EXPECT_EQ(context.targ1.character.name, "Target");
    EXPECT_EQ(context.targ1.character.id, "targ1");
    ASSERT_TRUE(context.has_targ2);
    EXPECT_TRUE(context.targ2.has_object);
    EXPECT_EQ(context.targ2.object.vnum, 700);
    EXPECT_EQ(context.targ2.object.id, "targ2");
    ASSERT_TRUE(context.has_target);
    EXPECT_TRUE(context.target.has_room);
    EXPECT_EQ(context.target.room.vnum, 101);
    EXPECT_EQ(context.target.room.id, "target");
    ASSERT_TRUE(context.has_dying);
    EXPECT_EQ(context.dying.name, "Target");
    EXPECT_EQ(context.dying.id, "dying");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "character");
    EXPECT_EQ(context.target_types[1], "object");
}

TEST(JsGameAdapter, TargetMappingSkipsStaleAndUnsupportedSlots) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data stale_character = make_character("Stale", 1, 44, 55, 66, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&live_object};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, world, 0,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    target_data stale_targ1;
    stale_targ1.type = TARGET_CHAR;
    stale_targ1.ptr.ch = &stale_character;
    stale_targ1.ch_num = stale_character.abs_number;
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &stale_targ1;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    ASSERT_TRUE(context.has_targ2);
    EXPECT_TRUE(context.targ2.has_object);
    ASSERT_TRUE(context.has_target);
    EXPECT_TRUE(context.target.has_object);
    EXPECT_EQ(context.target.object.id, "target");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "character");
    EXPECT_EQ(context.target_types[1], "object");

    struct UnsupportedTargetType {
        signed char type;
        const char *name;
    };
    const UnsupportedTargetType unsupported_types[] = {
        {TARGET_TEXT, "text"},
        {TARGET_DIR, "direction"},
        {TARGET_GOLD, "gold"},
        {TARGET_IN, "in"},
        {TARGET_ALL, "all"},
        {TARGET_VALUE, "value"},
        {TARGET_OTHER, "other"},
        {TARGET_IGNORE, "ignore"},
        {static_cast<signed char>(99), "unknown"},
    };
    for (const UnsupportedTargetType &unsupported_type : unsupported_types) {
        target_data unsupported;
        unsupported.type = unsupported_type.type;
        unsupported.ptr.other = &stale_object;
        JsGameAdapterContextInput unsupported_input;
        unsupported_input.targ1 = &unsupported;

        JsGameTriggerContextFixture unsupported_context =
            js_game_adapter_context_fixture(unsupported_input, options);

        EXPECT_FALSE(unsupported_context.has_targ1) << static_cast<int>(unsupported_type.type);
        EXPECT_FALSE(unsupported_context.has_target) << static_cast<int>(unsupported_type.type);
        ASSERT_EQ(unsupported_context.target_types.size(), 1u);
        EXPECT_EQ(unsupported_context.target_types[0], unsupported_type.name)
            << static_cast<int>(unsupported_type.type);
    }
}

TEST(JsGameAdapter, RejectsStaleObjectTargetDataAndFallsBackToLiveSecondSlot) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&live_object};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    target_data stale_targ1;
    stale_targ1.type = TARGET_OBJ;
    stale_targ1.ptr.obj = &stale_object;
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;

    JsGameAdapterContextInput input;
    input.self = &self;
    input.targ1 = &stale_targ1;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    ASSERT_TRUE(context.has_targ2);
    ASSERT_TRUE(context.targ2.has_object);
    EXPECT_EQ(context.targ2.object.id, "targ2");
    EXPECT_EQ(context.targ2.object.name, "live object");
    ASSERT_TRUE(context.has_target);
    ASSERT_TRUE(context.target.has_object);
    EXPECT_EQ(context.target.object.id, "target");
    EXPECT_EQ(context.target.object.name, "live object");
    ASSERT_EQ(context.target_types.size(), 2u);
    EXPECT_EQ(context.target_types[0], "object");
    EXPECT_EQ(context.target_types[1], "object");
}

TEST(JsGameAdapter, ExplicitStaleTargetDoesNotFallbackToTargetSlots) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    obj_data live_object = make_object("live object", -1);
    obj_data stale_object = make_object("stale object", 0);
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&live_object};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    target_data live_targ2;
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &live_object;
    JsGameAdapterContextInput input;
    input.target_object = &stale_object;
    input.targ2 = &live_targ2;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ2);
    EXPECT_FALSE(context.has_target);
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "object");
}

TEST(JsGameAdapter, RejectsCharacterTargetDataWhenAbsNumberDoesNotMatch) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    char_data target_character = make_character("Target", 1, 44, 55, 66, false);
    const char_data *live_characters[] = {&self, &target_character};
    JsGameAdapterOptions options = make_options(live_characters, 2, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);
    target_data target;
    target.type = TARGET_CHAR;
    target.ptr.ch = &target_character;
    target.ch_num = target_character.abs_number + 1;
    JsGameAdapterContextInput input;
    input.targ1 = &target;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    EXPECT_FALSE(context.has_targ1);
    EXPECT_FALSE(context.has_target);
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "character");
}

TEST(JsGameAdapter, MapsTargetDataRoomPointerToTypedRoom) {
    room_data world[2] = {make_room("Room", 100, 0), make_room("Target Room", 101, 0)};
    zone_data zones[1] = {make_zone("Zone", 10)};
    JsGameAdapterOptions options = make_options(nullptr, 0, nullptr, 0, world, 1, nullptr, 0,
                                                nullptr, 0, zones, 1, nullptr, 0);
    target_data room_target;
    room_target.type = TARGET_ROOM;
    room_target.ptr.room = &world[1];

    JsGameAdapterContextInput input;
    input.targ1 = &room_target;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

    ASSERT_TRUE(context.has_targ1);
    ASSERT_TRUE(context.targ1.has_room);
    EXPECT_EQ(context.targ1.room.vnum, 101);
    EXPECT_EQ(context.targ1.room.id, "targ1");
    ASSERT_TRUE(context.has_target);
    ASSERT_TRUE(context.target.has_room);
    EXPECT_EQ(context.target.room.vnum, 101);
    EXPECT_EQ(context.target.room.id, "target");
    ASSERT_EQ(context.target_types.size(), 1u);
    EXPECT_EQ(context.target_types[0], "room");

    room_data detached_room = make_room("Detached", 999, 0);
    room_target.ptr.room = &detached_room;
    JsGameTriggerContextFixture detached_context = js_game_adapter_context_fixture(input, options);
    EXPECT_FALSE(detached_context.has_targ1);
    EXPECT_FALSE(detached_context.has_target);
    ASSERT_EQ(detached_context.target_types.size(), 1u);
    EXPECT_EQ(detached_context.target_types[0], "room");
}

TEST(JsGameAdapter, MapsEveryWearSlotName) {
    struct ExpectedSlot {
        int slot;
        const char *name;
    };
    const ExpectedSlot expected_slots[] = {
        {WEAR_LIGHT, "light"},
        {WEAR_FINGER_R, "fingerRight"},
        {WEAR_FINGER_L, "fingerLeft"},
        {WEAR_NECK_1, "neck1"},
        {WEAR_NECK_2, "neck2"},
        {WEAR_BODY, "body"},
        {WEAR_HEAD, "head"},
        {WEAR_LEGS, "legs"},
        {WEAR_FEET, "feet"},
        {WEAR_HANDS, "hands"},
        {WEAR_ARMS, "arms"},
        {WEAR_SHIELD, "shield"},
        {WEAR_ABOUT, "aboutBody"},
        {WEAR_WAISTE, "waist"},
        {WEAR_WRIST_R, "wristRight"},
        {WEAR_WRIST_L, "wristLeft"},
        {WIELD, "wield"},
        {HOLD, "hold"},
        {WEAR_BACK, "back"},
        {WEAR_BELT_1, "belt1"},
        {WEAR_BELT_2, "belt2"},
        {WEAR_BELT_3, "belt3"},
    };
    JsGameAdapterOptions options;

    for (const ExpectedSlot &expected : expected_slots) {
        JsGameAdapterContextInput input;
        input.wear_slot = expected.slot;

        JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);

        ASSERT_TRUE(context.has_wear_slot) << expected.slot;
        EXPECT_EQ(context.wear_slot, expected.name) << expected.slot;
    }

    JsGameAdapterContextInput negative_input;
    negative_input.wear_slot = -1;
    EXPECT_FALSE(js_game_adapter_context_fixture(negative_input, options).has_wear_slot);

    JsGameAdapterContextInput too_large_input;
    too_large_input.wear_slot = MAX_WEAR;
    EXPECT_FALSE(js_game_adapter_context_fixture(too_large_input, options).has_wear_slot);
}

TEST(JsGameAdapter, HandlesInvalidRoomZoneMetadata) {
    char_data self = make_character("Self", 1, 11, 22, 33, false);
    const char_data *live_characters[] = {&self};
    zone_data zones[1] = {make_zone("Zone", 10)};
    room_data negative_zone_world[1] = {make_room("Bad Zone", 100, -1)};
    room_data out_of_range_zone_world[1] = {make_room("Bad Zone", 100, 1)};

    JsGameAdapterContextInput input;
    input.self = &self;
    input.room = 0;

    JsGameAdapterOptions negative_options =
        make_options(live_characters, 1, nullptr, 0, negative_zone_world, 0, nullptr, 0, nullptr, 0,
                     zones, 1, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, negative_options).has_zone);

    JsGameAdapterOptions out_of_range_options =
        make_options(live_characters, 1, nullptr, 0, out_of_range_zone_world, 0, nullptr, 0,
                     nullptr, 0, zones, 1, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, out_of_range_options).has_zone);

    JsGameAdapterOptions missing_zone_options =
        make_options(live_characters, 1, nullptr, 0, out_of_range_zone_world, 0, nullptr, 0,
                     nullptr, 0, nullptr, 0, nullptr, 0);
    EXPECT_FALSE(js_game_adapter_context_fixture(input, missing_zone_options).has_zone);
}

TEST(JsGameAdapter, HandlesUnresolvedVnumIndexesWithoutLeakingIndexes) {
    char_data npc = make_character("Unindexed Mob", 1, 1, 1, 1, true);
    npc.nr = 99;
    obj_data object = make_object("Unindexed Object", 99);
    const char_data *live_characters[] = {&npc};
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&npc, options, &character_fixture));
    EXPECT_EQ(character_fixture.vnum, -1);
    EXPECT_EQ(character_fixture.id, "mob:unresolved");
    EXPECT_EQ(character_fixture.id.find("99"), std::string::npos);

    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));
    EXPECT_EQ(object_fixture.vnum, -1);
    EXPECT_EQ(object_fixture.id, "object:unresolved");
    EXPECT_EQ(object_fixture.id.find("99"), std::string::npos);
}

TEST(JsGameAdapter, DoesNotDereferenceObjectRelationshipPointers) {
    char_data stale_carrier = make_character("Carrier", 1, 1, 1, 1, false);
    obj_data stale_container = make_object("Container", 0);
    obj_data object = make_object("Nested", 0);
    object.carried_by = &stale_carrier;
    object.in_obj = &stale_container;
    object.contains = &stale_container;
    object.in_room = -1;
    index_data object_index[1]{};
    object_index[0].virt = 300;
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(nullptr, 0, live_objects, 1, nullptr, -1, nullptr,
                                                0, object_index, 1, nullptr, 0, nullptr, 0);

    JsGameObjectFixture fixture;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));
    EXPECT_EQ(fixture.id, "object:300");
    EXPECT_EQ(fixture.name, "Nested");
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);

    object.in_obj = nullptr;
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &fixture));
    EXPECT_FALSE(fixture.has_carried_by);
    EXPECT_FALSE(fixture.has_worn_by);
}

TEST(JsGameAdapter, BoundsCopiedStrings) {
    char long_name[700];
    std::fill(std::begin(long_name), std::end(long_name), 'x');
    long_name[699] = '\0';
    char_data character = make_character(long_name, 1, 1, 1, 1, false);
    const char_data *live_characters[] = {&character};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));
    EXPECT_EQ(fixture.name.size(), 512u);

    char long_text[1200];
    std::fill(std::begin(long_text), std::end(long_text), 'y');
    long_text[1199] = '\0';
    JsGameAdapterContextInput input;
    input.self = &character;
    input.text = long_text;

    JsGameTriggerContextFixture context = js_game_adapter_context_fixture(input, options);
    EXPECT_EQ(context.text.size(), 1024u);
}

TEST(JsGameAdapter, SnapshotsStringsWithoutRetainingAliases) {
    char original_name[] = "Original";
    char_data character = make_character(original_name, 99, 1, 2, 3, false);
    const char_data *live_characters[] = {&character};
    JsGameAdapterOptions options = make_options(live_characters, 1, nullptr, 0, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &fixture));

    original_name[0] = 'X';
    character.player.name = const_cast<char *>("Changed");

    EXPECT_EQ(fixture.name, "Original");
    EXPECT_EQ(fixture.race, "race:99");
}

TEST(JsGameAdapter, OpaqueIdsDoNotContainPointerLookingText) {
    char_data character = make_character("NoPointer", 1, 1, 1, 1, false);
    obj_data object = make_object("NoPointerObject", -1);
    const char_data *live_characters[] = {&character};
    const obj_data *live_objects[] = {&object};
    JsGameAdapterOptions options = make_options(live_characters, 1, live_objects, 1, nullptr, -1,
                                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0);

    JsGameCharacterFixture character_fixture;
    JsGameObjectFixture object_fixture;
    ASSERT_TRUE(js_game_adapter_character_fixture(&character, options, &character_fixture));
    ASSERT_TRUE(js_game_adapter_object_fixture(&object, options, &object_fixture));

    EXPECT_EQ(character_fixture.id.find("0x"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("0x"), std::string::npos);
    EXPECT_EQ(character_fixture.id.find("char_data"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("obj_data"), std::string::npos);
    EXPECT_EQ(character_fixture.id.find("1234"), std::string::npos);
    EXPECT_EQ(object_fixture.id.find("88"), std::string::npos);
}
