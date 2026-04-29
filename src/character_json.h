#ifndef CHARACTER_JSON_H
#define CHARACTER_JSON_H

#include "structs.h"

#include <array>
#include <string>
#include <vector>

namespace character_json {

static constexpr int CHARACTER_JSON_SCHEMA_VERSION = 1;

struct ProfessionData {
    int level = 0;
    int points = 0;
    int coeff = 0;
    long experience = 0;
};

struct AbilityData {
    int str = 0;
    int lea = 0;
    int intel = 0;
    int wil = 0;
    int dex = 0;
    int con = 0;
    int hit = 0;
    int mana = 0;
    int move = 0;
};

struct PointData {
    std::vector<int> bodypart_hit;
    int gold = 0;
    int experience = 0;
    int spirit = 0;
    int mana_regen = 0;
    int health_regen = 0;
    int move_regen = 0;
    int ob = 0;
    int damage = 0;
    int energy_regen = 0;
    int parry = 0;
    int dodge = 0;
    int encumbrance = 0;
    int willpower = 0;
    int spell_pen = 0;
    int spell_power = 0;
};

struct ConditionData {
    int drunk = 0;
    int full = 0;
    int thirst = 0;
};

struct TimerData {
    long birth = 0;
    long last_logon = 0;
    int played_seconds = 0;
    int retired_on = 0;
};

struct ColorValueData {
    int mode = COLOR_VALUE_DEFAULT;
    int value = CNRM;
    int red = 0;
    int green = 0;
    int blue = 0;
};

struct ColorSettingData {
    ColorValueData foreground;
    ColorValueData background;
};

struct AffectData {
    int type = 0;
    int duration = 0;
    int time_phase = 0;
    int modifier = 0;
    int location = 0;
    long bitvector = 0;
    int counter = 0;
    std::vector<std::string> flags;
};

struct CharacterData {
    int schema_version = CHARACTER_JSON_SCHEMA_VERSION;
    std::string character_name;
    std::string title;
    std::string description;

    long idnum = 0;
    int race = 0;
    int sex = 0;
    int bodytype = 0;
    int language = 0;
    int hometown = 0;
    int weight = 0;
    int height = 0;
    int level = 0;
    int alignment = 0;
    int load_room = 0;
    int spells_to_learn = 0;
    int wimp_level = 0;
    int freeze_level = 0;
    int raw_perception = 0;
    int perception = 0;
    int mini_level = 0;
    int max_mini_level = 0;
    int morale = 0;
    int owner = 0;
    int rerolls = 0;
    int leg_encumbrance = 0;
    int rp_flag = 0;
    long will_teach = 0;
    int tactics = 3;
    int shooting = 2;
    int casting = 2;
    bool two_handed = false;

    ProfessionData mage;
    ProfessionData mystic;
    ProfessionData ranger;
    ProfessionData warrior;

    AbilityData temporary_abilities;
    AbilityData rolled_abilities;
    PointData points;
    ConditionData conditions;
    TimerData timers;
    long color_mask = 0;
    std::vector<int> colors;
    std::vector<ColorSettingData> color_settings;
    std::vector<int> talks;
    std::vector<int> skills;

    std::vector<std::string> player_flags;
    std::vector<std::string> preference_flags;
    std::vector<std::string> affected_flags;
    std::vector<std::string> hide_flags;
    std::vector<AffectData> affects;
};

CharacterData character_data_from_store(const char_file_u& stored_character);
bool apply_character_data_to_store(const CharacterData& json_character, char_file_u* stored_character, std::string* error_message = nullptr);

std::string serialize_character_to_json(const CharacterData& character);
bool deserialize_character_from_json(const std::string& json, CharacterData* character, std::string* error_message = nullptr);

std::vector<std::string> encode_player_flags(long flags);
std::vector<std::string> encode_preference_flags(long flags);
std::vector<std::string> encode_affected_flags(long flags);
std::vector<std::string> encode_hide_flags(long flags);

bool decode_player_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);
bool decode_preference_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);
bool decode_affected_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);
bool decode_hide_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);

} // namespace character_json

#endif
