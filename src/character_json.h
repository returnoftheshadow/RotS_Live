#ifndef CHARACTER_JSON_H
#define CHARACTER_JSON_H

#include "color.h"
#include "json_utils.h"
#include "structs.h"

#include <array>
#include <functional>
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
    int effect_modifier = 0;
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
    int specialization = PLRSPEC_NONE;

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

// Parallel, profileable v2 implementations (v1 above is the untouched baseline). serialize v2a/v2b
// produce byte-identical output to v1; deserialize v2a/v2b produce an identical CharacterData. v2a
// isolates the memoized-lookup / reserve+to_chars win; v2b adds JsonReaderV2 / escape-fastpath+cached
// keys. Profiled head-to-head against v1 via savebench; no live caller uses them in this branch.
std::string serialize_character_to_json_v2a(const CharacterData& character);
std::string serialize_character_to_json_v2b(const CharacterData& character);
bool deserialize_character_from_json_v2a(const std::string& json, CharacterData* character, std::string* error_message = nullptr);
bool deserialize_character_from_json_v2b(const std::string& json, CharacterData* character, std::string* error_message = nullptr);

// Two or more entries in a name-keyed table that produce the SAME JSON key. The character file
// writes skills and talks as objects keyed by name, so a table carrying a duplicate name (today
// skills 125 and 126, both "trash") makes a character holding values in both serialize to a
// duplicate key -- which the reader refuses, for the whole file. That is the one way this writer can
// produce something its own reader will not take back, so it is checked directly rather than by
// re-parsing every save.
struct NamedKeyCollision {
    std::string table; // "skill", "talk" or "color"
    std::string key; // the JSON key the entries share
    std::vector<int> indices; // the slots sharing it, ascending
    // Whether a duplicate of this key makes the READER refuse the whole file. True for skills and
    // talks, which parse through parse_named_integer_object and its duplicate check. False for
    // colours: parse_colors_object resolves each key to a slot and assigns, so a duplicate silently
    // overwrites instead -- a fidelity defect worth reporting at boot, but not a reason to refuse a
    // player's save.
    bool duplicate_refuses_the_file = true;
};

// The scan behind named_key_collisions(), exposed so its behaviour can be tested directly against
// synthetic tables rather than only against whatever consts.cpp happens to contain today.
std::vector<NamedKeyCollision> find_key_collisions(const char* table_name, int slot_count,
    const std::function<std::string(int)>& key_for_index);

// Computed once from the static tables. Empty on a healthy build; boot reports whatever is here.
const std::vector<NamedKeyCollision>& named_key_collisions();

// Empty when this character can be written and read back. Otherwise a sentence naming the clash,
// for the refusal message.
std::string first_unwritable_named_value(const CharacterData& character);

std::vector<std::string> encode_player_flags(long flags);
std::vector<std::string> encode_preference_flags(long flags);
// Every act/pref bit character JSON has a name for. A bit outside the mask cannot be written and
// is silently dropped by encode_player_flags/encode_preference_flags.
long serializable_player_flag_mask();
long serializable_preference_flag_mask();
std::vector<std::string> encode_affected_flags(long flags);
std::vector<std::string> encode_hide_flags(long flags);

bool decode_player_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);
bool decode_preference_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr, bool skip_unknown_names = false);
bool decode_affected_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);
bool decode_hide_flags(const std::vector<std::string>& names, long* flags, std::string* error_message = nullptr);

// Colour-slot JSON, shared with the account-level PPC store so the codebase has one
// colour format. encode returns the object BODY (no surrounding braces), sparse: only
// slots that differ from the default appear. parse resets both arrays to defaults and
// then fills them from one JSON object; both arrays must hold MAX_COLOR_FIELDS entries.
// skip_unknown_keys makes the parse forward-compatible for the account store, whose parse
// failures are fatal at boot: both an unrecognised slot name and an unrecognised colour mode
// are skipped (the affected colour value falls back to the default) instead of failing.
std::string encode_color_slots_object(const char* colors, const color_slot_data* color_settings);
bool parse_color_slots_object(json_utils::JsonReader* reader, char* colors,
    color_slot_data* color_settings, std::string* error_message = nullptr, bool skip_unknown_keys = false);

} // namespace character_json

#endif
