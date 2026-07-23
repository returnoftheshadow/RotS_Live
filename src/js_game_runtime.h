#ifndef JS_GAME_RUNTIME_H
#define JS_GAME_RUNTIME_H

#include "js_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

struct JsGameZoneFixture {
    std::string id;
    std::string name;
    bool has_description = false;
    std::string description;
    bool has_map = false;
    std::string map;
    int vnum = 0;
    int level = 0;
    int lifespan = 0;
    int age = 0;
    int top_room_vnum = 0;
    int x = 0;
    int y = 0;
    std::string symbol;
    int white_power = 0;
    int dark_power = 0;
    int magi_power = 0;
    int minimum_look_level = 0;
    int reset_mode = 0;
};

struct JsGameExtraDescriptionFixture {
    std::string keyword;
    std::string description;
};

struct JsGameRoomExitFixture {
    int direction_index = 0;
    std::string direction;
    bool has_to_room_vnum = false;
    int to_room_vnum = 0;
    std::string keyword;
    std::string description;
    int key_vnum = -1;
    int width = 0;
    std::vector<std::string> flags;
};

struct JsGameObjectFlagsFixture {
    std::string item_type;
    std::vector<std::string> wear_flags;
    std::vector<std::string> extra_flags;
    int level = 0;
    int weight = 0;
    int cost = 0;
    int cost_per_day = 0;
    int timer = 0;
    int rarity = 0;
    std::string material;
};

struct JsGameObjectAffectFixture {
    int slot_index = 0;
    int location = 0;
    std::string location_name;
    int modifier = 0;
};

struct JsGameRoomContentObjectFixture {
    std::string id;
    std::string name;
    std::string description;
    std::string short_description;
    bool has_action_description = false;
    std::string action_description;
    int vnum = 0;
    JsGameObjectFlagsFixture flags;
    std::vector<JsGameObjectAffectFixture> affects;
    std::vector<JsGameExtraDescriptionFixture> extra_descriptions;
    bool touched = false;
};

struct JsGameAffectFixture {
    int type = 0;
    std::string name = "Unknown";
    int duration = 0;
    int time_phase = 0;
    int modifier = 0;
    int location = 0;
    std::string location_name = "Unknown";
    long bitvector = 0;
    std::vector<std::string> bitvector_names;
    int counter = 0;
};

struct JsGameCharacterReferenceFixture {
    std::string id;
    std::string name;
    std::string race;
    int vnum = -1;
    int prototype_vnum = -1;
    int level = 0;
    bool is_npc = false;
};

struct JsGameRoomFixture {
    std::string id;
    std::string name;
    std::string description;
    int vnum = 0;
    int level = 0;
    std::string sector_type;
    std::vector<std::string> flags;
    std::vector<JsGameExtraDescriptionFixture> extra_descriptions;
    std::vector<JsGameRoomExitFixture> exits;
    std::vector<JsGameRoomContentObjectFixture> contents;
    std::vector<JsGameCharacterReferenceFixture> characters;
    std::vector<JsGameAffectFixture> affects;
    int alignment = 0;
    int light = 0;
    bool is_sunlit = false;

    bool has_zone = false;
    JsGameZoneFixture zone;
};

struct JsGameAbilityScoresFixture {
    int strength = 0;
    int intelligence = 0;
    int willpower = 0;
    int dexterity = 0;
    int constitution = 0;
    int leadership = 0;
};

struct JsGameCharacterPointsFixture {
    std::vector<int> bodypart_hits = std::vector<int>(11, 0);
    int gold = 0;
    int experience = 0;
    int spirit = 0;
    int mana_regen = 0;
    int health_regen = 0;
    int move_regen = 0;
    int offense = 0;
    int damage = 0;
    int energy_regen = 0;
    int parry = 0;
    int dodge = 0;
    int encumbrance = 0;
    int willpower = 0;
    int spell_penetration = 0;
    int spell_power = 0;
};

struct JsGameCharacterSpecialsFixture {
    bool is_fighting = false;
    bool is_hunting = false;
    bool has_memory = false;
    std::string position = "Unknown";
    std::string default_position = "Unknown";
    int carry_weight = 0;
    int worn_weight = 0;
    int encumbrance_weight = 0;
    int carry_items = 0;
    int timer = 0;
    int was_in_room = -1;
    int energy = 0;
    int current_parry = 0;
    std::string last_direction;
    int attack_type = 0;
    int script_number = 0;
    int current_bodypart = 0;
    std::string tactics;
    int prompt_number = 0;
    int prompt_value = 0;
    int home_zone = 0;
    int load_line = 0;
};

struct JsGameCharacterConditionsFixture {
    int drunk = 0;
    int full = 0;
    int thirst = 0;
};

struct JsGameCharacterSpecials2Fixture {
    int load_room = 0;
    int spells_to_learn = 0;
    int alignment = 0;
    std::vector<std::string> act_flags;
    std::vector<std::string> preference_flags;
    int wimp_level = 0;
    int freeze_level = 0;
    int saving_throw = 0;
    int raw_perception = 0;
    int perception = 0;
    JsGameCharacterConditionsFixture conditions;
    int mini_level = 0;
    int max_mini_level = 0;
    int morale = 0;
    int rerolls = 0;
    int leg_encumbrance = 0;
    int retired_on = 0;
    std::vector<std::string> hide_flags;
    std::string tactics = "Unknown";
    std::string shooting = "Unknown";
    std::string casting = "Unknown";
    bool two_handed = false;
};

struct JsGameProfessionFixture {
    std::string key;
    std::string name;
    int level = 0;
    int points = 0;
    int coefficient = 0;
    long experience = 0;
};

struct JsGameSpecializationFixture {
    int selected_id = 0;
    std::string selected_key = "nothing";
    std::string selected_name = "nothing";
    int current_id = 0;
    std::string current_key = "nothing";
    std::string current_name = "nothing";
    bool is_mage_specialization = false;
    bool has_runtime_state = false;
};

struct JsGameDamageEntryFixture {
    int source_id = 0;
    std::string source_kind = "unknown";
    std::string source_name = "Unknown";
    int instance_count = 0;
    int total_damage = 0;
    int largest_damage = 0;
    double average_damage = 0;
    double percent_of_total = 0;
};

struct JsGameDamageDetailsFixture {
    double elapsed_combat_seconds = 0;
    long total_damage = 0;
    double damage_per_second = 0;
    std::vector<JsGameDamageEntryFixture> entries;
};

struct JsGameSkillValueFixture {
    int id = 0;
    std::string name;
    std::string profession = "general";
    int level = 0;
    int practice = 0;
    int minimum_position = 0;
    int mana_cost = 0;
    int beats = 0;
    int targets = 0;
    int learn_difficulty = 0;
    int learn_type = 0;
    bool is_fast = false;
    int specialization = 0;
};

struct JsGameKnowledgeValueFixture {
    int id = 0;
    std::string name;
    std::string profession = "general";
    int level = 0;
    int knowledge = 0;
    int minimum_position = 0;
    int mana_cost = 0;
    int beats = 0;
    int targets = 0;
    int learn_difficulty = 0;
    int learn_type = 0;
    bool is_fast = false;
    int specialization = 0;
};

struct JsGameCharacterProfileFixture {
    std::string name;
    std::string short_description;
    bool has_long_description = false;
    std::string long_description;
    bool has_description = false;
    std::string description;
    bool has_title = false;
    std::string title;
    bool has_death_cry = false;
    std::string death_cry;
    bool has_death_cry2 = false;
    std::string death_cry2;
    int corpse_number = 0;
    int race_id = 0;
    int sex = 0;
    int body_type = 0;
    int profession = 0;
    int level = 0;
    int language = 0;
    int hometown = 0;
    std::int64_t birth_epoch_seconds = 0;
    std::int64_t logon_epoch_seconds = 0;
    int played_seconds = 0;
    int weight = 0;
    int height = 0;
    int ranking = 0;
    std::vector<int> talks;
};

struct JsGameEquipmentObjectFixture {
    std::string id;
    std::string name;
    std::string description;
    std::string short_description;
    bool has_action_description = false;
    std::string action_description;
    int vnum = 0;
    JsGameObjectFlagsFixture flags;
    std::vector<JsGameObjectAffectFixture> affects;
    std::vector<JsGameExtraDescriptionFixture> extra_descriptions;
    bool touched = false;

    bool has_room = false;
    JsGameRoomFixture room;
};

struct JsGameEquipmentSlotFixture {
    int slot_index = 0;
    std::string slot_name;
    bool has_object = false;
    JsGameEquipmentObjectFixture object;
};

struct JsGameMountFixture {
    bool has_mount = false;
    JsGameCharacterReferenceFixture mount;
    bool has_rider = false;
    JsGameCharacterReferenceFixture rider;
    bool has_next_rider = false;
    JsGameCharacterReferenceFixture next_rider;
    bool is_riding = false;
    bool is_mounted = false;
};

struct JsGameCharacterFixture {
    std::string id;
    std::string name;
    std::string race;
    int vnum = -1;
    int prototype_vnum = -1;
    int level = 0;
    int experience = 0;
    int rank = 0;
    int hit_points = 0;
    int max_hit_points = 0;
    int class_points = 0;
    int interrupt_count = 0;
    int interrupt_time = 0;
    bool special_busy = false;
    JsGameCharacterProfileFixture profile;
    JsGameAbilityScoresFixture base_abilities;
    JsGameAbilityScoresFixture current_abilities;
    JsGameAbilityScoresFixture rolled_abilities;
    JsGameCharacterPointsFixture points;
    JsGameCharacterSpecialsFixture specials;
    JsGameCharacterSpecials2Fixture specials2;
    std::vector<JsGameProfessionFixture> professions;
    JsGameSpecializationFixture specializations;
    JsGameDamageDetailsFixture damage_details;
    std::vector<JsGameSkillValueFixture> skills;
    std::vector<JsGameKnowledgeValueFixture> knowledge;
    std::vector<JsGameAffectFixture> affects;
    std::vector<JsGameEquipmentSlotFixture> equipment;
    std::vector<JsGameEquipmentObjectFixture> inventory;
    std::vector<JsGameCharacterReferenceFixture> followers;
    bool has_master = false;
    JsGameCharacterReferenceFixture master;
    JsGameMountFixture mount;
    bool is_npc = false;

    bool has_room = false;
    JsGameRoomFixture room;
};

struct JsGameObjectFixture {
    std::string id;
    std::string name;
    std::string description;
    std::string short_description;
    bool has_action_description = false;
    std::string action_description;
    int vnum = 0;
    JsGameObjectFlagsFixture flags;
    std::vector<JsGameObjectAffectFixture> affects;
    std::vector<JsGameExtraDescriptionFixture> extra_descriptions;
    bool has_container = false;
    JsGameEquipmentObjectFixture container;
    std::vector<JsGameEquipmentObjectFixture> contents;
    bool touched = false;

    bool has_room = false;
    JsGameRoomFixture room;

    bool has_carried_by = false;
    JsGameCharacterFixture carried_by;

    bool has_worn_by = false;
    JsGameCharacterFixture worn_by;
};

struct JsGameTriggerFixture {
    std::string name;
    std::string legacy_name;
    std::string host_type;
    int legacy_value = 0;
    bool blocks_gameplay = false;
};

struct JsGameTargetFixture {
    std::string type;

    bool has_character = false;
    JsGameCharacterFixture character;

    bool has_object = false;
    JsGameObjectFixture object;

    bool has_room = false;
    JsGameRoomFixture room;
};

struct JsGameTriggerContextFixture {
    bool has_self = false;
    JsGameCharacterFixture self;

    bool has_actor = false;
    JsGameCharacterFixture actor;

    bool has_speaker = false;
    JsGameCharacterFixture speaker;

    bool has_attacker = false;
    JsGameCharacterFixture attacker;

    bool has_victim = false;
    JsGameCharacterFixture victim;

    bool has_killer = false;
    JsGameCharacterFixture killer;

    bool has_object = false;
    JsGameObjectFixture object;

    bool has_weapon = false;
    JsGameObjectFixture weapon;

    bool has_room = false;
    JsGameRoomFixture room;

    bool has_zone = false;
    JsGameZoneFixture zone;

    bool has_text = false;
    std::string text;

    bool has_wear_slot = false;
    std::string wear_slot;

    bool has_command = false;
    std::string command;

    bool has_args = false;
    std::string args;

    bool has_tick = false;
    int tick = 0;

    bool has_direction = false;
    std::string direction;

    bool has_reverse_direction = false;
    std::string reverse_direction;

    bool has_target = false;
    JsGameTargetFixture target;

    bool has_targ1 = false;
    JsGameTargetFixture targ1;

    bool has_targ2 = false;
    JsGameTargetFixture targ2;

    std::vector<std::string> target_types;

    bool has_dying = false;
    JsGameCharacterFixture dying;

    JsGameTriggerFixture trigger;
};

struct JsGameCommandResultRequest {
    std::string operation;
    std::string arguments_json;
};

using JsGameCommandResultCallback = std::string (*)(const JsGameCommandResultRequest &request,
                                                    void *user_data);

struct JsGameRuntimeEvaluationOptions {
    JsGameCommandResultCallback command_result_callback = nullptr;
    void *command_result_user_data = nullptr;
};

class JsGameRuntime {
  public:
    explicit JsGameRuntime(const JsRuntimeLimits &limits = {});

    JsRuntimeEvalResult evaluate_trigger_body(const std::string &source,
                                              const JsGameTriggerContextFixture &context,
                                              const char *filename = "game-script.js");
    JsRuntimeEvalResult
    evaluate_trigger_body(const std::string &source, const JsGameTriggerContextFixture &context,
                          const JsGameRuntimeEvaluationOptions &evaluation_options,
                          const char *filename = "game-script.js");
    JsRuntimeEvalResult evaluate_trigger_package_handler(const std::string &package_source,
                                                         const std::string &handler_name,
                                                         const JsGameTriggerContextFixture &context,
                                                         const char *filename = "game-script.js");
    JsRuntimeEvalResult
    evaluate_trigger_package_handler(const std::string &package_source,
                                     const std::string &handler_name,
                                     const JsGameTriggerContextFixture &context,
                                     const JsGameRuntimeEvaluationOptions &evaluation_options,
                                     const char *filename = "game-script.js");

  private:
    JsRuntimeLimits m_limits;
};

std::string js_game_trigger_context_literal(const JsGameTriggerContextFixture &context);

#endif
