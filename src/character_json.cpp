#include "character_json.h"

#include "json_utils.h"
#include "spells.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_map>

extern byte language_number;
extern byte language_skills[];

namespace character_json {
namespace {

    struct FlagDefinition {
        const char* name;
        long bit;
    };

    struct NamedValue {
        std::string key;
        int value = 0;
    };

    constexpr FlagDefinition kPlayerFlags[] = {
        { "is_nchanged", PLR_IS_NCHANGED },
        { "frozen", PLR_FROZEN },
        { "dontset", PLR_DONTSET },
        { "writing", PLR_WRITING },
        { "mailing", PLR_MAILING },
        { "crash", PLR_CRASH },
        { "siteok", PLR_SITEOK },
        { "noshout", PLR_NOSHOUT },
        { "notitle", PLR_NOTITLE },
        { "deleted", PLR_DELETED },
        { "loadroom", PLR_LOADROOM },
        { "nowizlist", PLR_NOWIZLIST },
        { "nodelete", PLR_NODELETE },
        { "invstart", PLR_INVSTART },
        { "retired", PLR_RETIRED },
        { "shaping", PLR_SHAPING },
        { "wr_finish", PLR_WR_FINISH },
        { "isshadow", PLR_ISSHADOW },
        { "isafk", PLR_ISAFK },
        { "incognito", PLR_INCOGNITO },
        { "was_kitted", PLR_WAS_KITTED },
    };

    constexpr FlagDefinition kPreferenceFlags[] = {
        { "brief", PRF_BRIEF },
        { "compact", PRF_COMPACT },
        { "narrate", PRF_NARRATE },
        { "notell", PRF_NOTELL },
        { "mental", PRF_MENTAL },
        { "swim", PRF_SWIM },
        { "prompt", PRF_PROMPT },
        { "disptext", PRF_DISPTEXT },
        { "nohassle", PRF_NOHASSLE },
        { "summonable", PRF_SUMMONABLE },
        { "echo", PRF_ECHO },
        { "holylight", PRF_HOLYLIGHT },
        { "color", PRF_COLOR },
        { "sing", PRF_SING },
        { "wiz", PRF_WIZ },
        { "log1", PRF_LOG1 },
        { "log2", PRF_LOG2 },
        { "log3", PRF_LOG3 },
        { "chat", PRF_CHAT },
        { "roomflags", PRF_ROOMFLAGS },
        { "spam", PRF_SPAM },
        { "msdp", PRF_MSDP },
        { "wrap", PRF_WRAP },
        { "latin1", PRF_LATIN1 },
        { "spinner", PRF_SPINNER },
        { "inv_sort1", PRF_INV_SORT1 },
        { "inv_sort2", PRF_INV_SORT2 },
        { "advanced_view", PRF_ADVANCED_VIEW },
        { "advanced_prompt", PRF_ADVANCED_PROMPT },
    };

    constexpr FlagDefinition kAffectedFlags[] = {
        { "detect_hidden", AFF_DETECT_HIDDEN },
        { "infrared", AFF_INFRARED },
        { "sneak", AFF_SNEAK },
        { "hide", AFF_HIDE },
        { "detect_magic", AFF_DETECT_MAGIC },
        { "charm", AFF_CHARM },
        { "curse", AFF_CURSE },
        { "sanctuary", AFF_SANCTUARY },
        { "twohanded", AFF_TWOHANDED },
        { "invisible", AFF_INVISIBLE },
        { "moonvision", AFF_MOONVISION },
        { "poison", AFF_POISON },
        { "shield", AFF_SHIELD },
        { "breathe", AFF_BREATHE },
        { "confuse", AFF_CONFUSE },
        { "sleep", AFF_SLEEP },
        { "bash", AFF_BASH },
        { "flying", AFF_FLYING },
        { "detect_invisible", AFF_DETECT_INVISIBLE },
        { "fear", AFF_FEAR },
        { "blind", AFF_BLIND },
        { "follow", AFF_FOLLOW },
        { "swim", AFF_SWIM },
        { "hunt", AFF_HUNT },
        { "evasion", AFF_EVASION },
        { "waiting", AFF_WAITING },
        { "waitwheel", AFF_WAITWHEEL },
        { "concentration", AFF_CONCENTRATION },
        { "haze", AFF_HAZE },
        { "hallucinate", AFF_HALLUCINATE },
    };

    constexpr FlagDefinition kHideFlags[] = {
        { "hiding_well", HIDING_WELL },
        { "snuck_in", HIDING_SNUCK_IN },
    };

    constexpr const char* kColorFieldNames[MAX_COLOR_FIELDS] = {
        "narrate",
        "chat",
        "yell",
        "tell",
        "say",
        "roomname",
        "hit",
        "damage",
        "character",
        "object",
        "enemy",
        "description",
        "group",
        "magic",
        "weather",
        "reserved_15",
    };

    ColorValueData default_color_value()
    {
        return ColorValueData {};
    }

    ColorSettingData default_color_setting()
    {
        return ColorSettingData {};
    }

    ColorValueData color_value_from_store(const color_value_data& stored_value)
    {
        ColorValueData value;
        value.mode = stored_value.mode;
        value.value = stored_value.ansi;
        value.red = stored_value.red;
        value.green = stored_value.green;
        value.blue = stored_value.blue;
        return value;
    }

    color_value_data color_value_to_store(const ColorValueData& value)
    {
        color_value_data stored_value {};
        stored_value.mode = static_cast<unsigned char>(value.mode);
        stored_value.ansi = static_cast<unsigned char>(value.value);
        stored_value.red = static_cast<unsigned char>(value.red);
        stored_value.green = static_cast<unsigned char>(value.green);
        stored_value.blue = static_cast<unsigned char>(value.blue);
        return stored_value;
    }

    bool is_default_color_value(const ColorValueData& value)
    {
        return value.mode == COLOR_VALUE_DEFAULT;
    }

    bool is_default_color_setting(const ColorSettingData& setting)
    {
        return is_default_color_value(setting.foreground) && is_default_color_value(setting.background);
    }

    ColorSettingData color_setting_from_store(const char_prof_data& profs, int index)
    {
        ColorSettingData setting = default_color_setting();
        if (index < 0 || index >= MAX_COLOR_FIELDS)
            return setting;

        setting.foreground = color_value_from_store(profs.color_settings[index].foreground);
        setting.background = color_value_from_store(profs.color_settings[index].background);

        if (setting.foreground.mode == COLOR_VALUE_DEFAULT) {
            setting.foreground.mode = COLOR_VALUE_ANSI16;
            setting.foreground.value = profs.colors[index];
        }

        if (setting.foreground.mode == COLOR_VALUE_TRUECOLOR && (setting.foreground.value < CNRM || setting.foreground.value > CBWHT))
            setting.foreground.value = nearest_ansi_color(setting.foreground.red, setting.foreground.green, setting.foreground.blue);

        return setting;
    }

    void normalize_color_setting(ColorSettingData* setting)
    {
        if (setting == nullptr)
            return;

        if (setting->foreground.mode == COLOR_VALUE_TRUECOLOR && (setting->foreground.value < CNRM || setting->foreground.value > CBWHT))
            setting->foreground.value = nearest_ansi_color(setting->foreground.red, setting->foreground.green, setting->foreground.blue);
        else if (setting->foreground.mode == COLOR_VALUE_DEFAULT)
            setting->foreground.value = CNRM;

        if (setting->background.mode == COLOR_VALUE_TRUECOLOR && (setting->background.value < CNRM || setting->background.value > CBWHT))
            setting->background.value = nearest_ansi_color(setting->background.red, setting->background.green, setting->background.blue);
        else if (setting->background.mode == COLOR_VALUE_DEFAULT)
            setting->background.value = CNRM;
    }

    AbilityData ability_from_store(const char_ability_data& ability)
    {
        AbilityData ability_data;
        ability_data.str = ability.str;
        ability_data.lea = ability.lea;
        ability_data.intel = ability.intel;
        ability_data.wil = ability.wil;
        ability_data.dex = ability.dex;
        ability_data.con = ability.con;
        ability_data.hit = ability.hit;
        ability_data.mana = ability.mana;
        ability_data.move = ability.move;
        return ability_data;
    }

    void apply_ability_to_store(const AbilityData& ability_data, char_ability_data* ability)
    {
        if (ability == nullptr)
            return;

        ability->str = static_cast<signed char>(ability_data.str);
        ability->lea = static_cast<signed char>(ability_data.lea);
        ability->intel = static_cast<signed char>(ability_data.intel);
        ability->wil = static_cast<signed char>(ability_data.wil);
        ability->dex = static_cast<signed char>(ability_data.dex);
        ability->con = static_cast<signed char>(ability_data.con);
        ability->hit = ability_data.hit;
        ability->mana = static_cast<sh_int>(ability_data.mana);
        ability->move = static_cast<sh_int>(ability_data.move);
    }

    PointData point_data_from_store(const char_point_data& points)
    {
        PointData point_data;
        point_data.bodypart_hit.reserve(MAX_BODYPARTS);
        for (int index = 0; index < MAX_BODYPARTS; ++index)
            point_data.bodypart_hit.push_back(points.bodypart_hit[index]);
        point_data.gold = points.gold;
        point_data.experience = points.exp;
        point_data.spirit = points.spirit;
        point_data.mana_regen = points.mana_regen;
        point_data.health_regen = points.health_regen;
        point_data.move_regen = points.move_regen;
        point_data.ob = points.OB;
        point_data.damage = points.damage;
        point_data.energy_regen = points.ENE_regen;
        point_data.parry = points.parry;
        point_data.dodge = points.dodge;
        point_data.encumbrance = points.encumb;
        point_data.willpower = points.willpower;
        point_data.spell_pen = points.spell_pen;
        point_data.spell_power = points.spell_power;
        return point_data;
    }

    void apply_point_data_to_store(const PointData& point_data, char_point_data* points)
    {
        if (points == nullptr)
            return;

        for (int index = 0; index < MAX_BODYPARTS; ++index) {
            const int value = (index < static_cast<int>(point_data.bodypart_hit.size())) ? point_data.bodypart_hit[index] : 0;
            points->bodypart_hit[index] = static_cast<ubyte>(value);
        }
        points->gold = point_data.gold;
        points->exp = point_data.experience;
        points->spirit = point_data.spirit;
        points->mana_regen = point_data.mana_regen;
        points->health_regen = point_data.health_regen;
        points->move_regen = point_data.move_regen;
        points->OB = static_cast<sh_int>(point_data.ob);
        points->damage = static_cast<sh_int>(point_data.damage);
        points->ENE_regen = static_cast<sh_int>(point_data.energy_regen);
        points->parry = static_cast<sh_int>(point_data.parry);
        points->dodge = static_cast<sh_int>(point_data.dodge);
        points->encumb = static_cast<sh_int>(point_data.encumbrance);
        points->willpower = static_cast<sh_int>(point_data.willpower);
        points->spell_pen = static_cast<sh_int>(point_data.spell_pen);
        points->spell_power = static_cast<sh_int>(point_data.spell_power);
    }

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
    }

    bool require_string_length(const std::string& value, size_t max_length, const char* field_name, std::string* error_message)
    {
        if (value.size() > max_length) {
            set_error(error_message, std::string(field_name) + " exceeds the maximum supported length.");
            return false;
        }
        return true;
    }

    bool require_no_embedded_nul(const std::string& value, const char* field_name, std::string* error_message)
    {
        if (value.find('\0') != std::string::npos) {
            set_error(error_message, std::string(field_name) + " must not contain embedded NUL bytes.");
            return false;
        }
        return true;
    }

    bool require_exact_array_size(const std::vector<int>& values, size_t expected_size, const char* field_name, std::string* error_message)
    {
        if (values.size() != expected_size) {
            set_error(error_message, std::string(field_name) + " must contain exactly " + std::to_string(expected_size) + " entries.");
            return false;
        }
        return true;
    }

    bool require_integer_range(long value, long min_value, long max_value, const char* field_name, std::string* error_message)
    {
        if (value < min_value || value > max_value) {
            set_error(error_message, std::string(field_name) + " is out of the supported range.");
            return false;
        }
        return true;
    }

    bool require_signed_char_range(int value, const char* field_name, std::string* error_message)
    {
        return require_integer_range(value, std::numeric_limits<signed char>::min(), std::numeric_limits<signed char>::max(), field_name, error_message);
    }

    bool require_byte_range(int value, const char* field_name, std::string* error_message)
    {
        return require_integer_range(value, std::numeric_limits<byte>::min(), std::numeric_limits<byte>::max(), field_name, error_message);
    }

    bool require_ubyte_range(int value, const char* field_name, std::string* error_message)
    {
        return require_integer_range(value, std::numeric_limits<ubyte>::min(), std::numeric_limits<ubyte>::max(), field_name, error_message);
    }

    bool require_color_value_range(int value, const char* field_name, std::string* error_message)
    {
        return require_integer_range(value, CNRM, CBWHT, field_name, error_message);
    }

    bool require_short_range(int value, const char* field_name, std::string* error_message)
    {
        return require_integer_range(value, std::numeric_limits<sh_int>::min(), std::numeric_limits<sh_int>::max(), field_name, error_message);
    }

    bool validate_character_strings(const CharacterData& character, const char_file_u& stored_character, std::string* error_message)
    {
        return require_string_length(character.character_name, MAX_NAME_LENGTH, "character_name", error_message)
            && require_string_length(character.title, sizeof(stored_character.title) - 1, "title", error_message)
            && require_string_length(character.description, sizeof(stored_character.description) - 1, "description", error_message)
            && require_no_embedded_nul(character.character_name, "character_name", error_message)
            && require_no_embedded_nul(character.title, "title", error_message)
            && require_no_embedded_nul(character.description, "description", error_message);
    }

    bool validate_character_scalar_ranges(const CharacterData& character, std::string* error_message)
    {
        return require_byte_range(character.race, "identity.race", error_message)
            && require_byte_range(character.sex, "identity.sex", error_message)
            && require_byte_range(character.bodytype, "identity.bodytype", error_message)
            && require_byte_range(character.level, "progression.level", error_message)
            && require_byte_range(character.language, "identity.language", error_message)
            && require_short_range(character.hometown, "identity.hometown", error_message)
            && require_byte_range(character.freeze_level, "state.freeze_level", error_message)
            && require_ubyte_range(character.rerolls, "progression.rerolls", error_message)
            && require_integer_range(character.tactics, TACTICS_DEFENSIVE, TACTICS_BERSERK, "state.tactics", error_message)
            && require_integer_range(character.shooting, SHOOTING_SLOW, SHOOTING_FAST, "state.shooting", error_message)
            && require_integer_range(character.casting, CASTING_SLOW, CASTING_FAST, "state.casting", error_message);
    }

    bool validate_ability_data(const AbilityData& ability_data, const char* scope, std::string* error_message)
    {
        return require_signed_char_range(ability_data.str, (std::string(scope) + ".str").c_str(), error_message)
            && require_signed_char_range(ability_data.lea, (std::string(scope) + ".lea").c_str(), error_message)
            && require_signed_char_range(ability_data.intel, (std::string(scope) + ".intel").c_str(), error_message)
            && require_signed_char_range(ability_data.wil, (std::string(scope) + ".wil").c_str(), error_message)
            && require_signed_char_range(ability_data.dex, (std::string(scope) + ".dex").c_str(), error_message)
            && require_signed_char_range(ability_data.con, (std::string(scope) + ".con").c_str(), error_message)
            && require_short_range(ability_data.mana, (std::string(scope) + ".mana").c_str(), error_message)
            && require_short_range(ability_data.move, (std::string(scope) + ".move").c_str(), error_message);
    }

    bool validate_point_data(const PointData& point_data, std::string* error_message)
    {
        if (!require_exact_array_size(point_data.bodypart_hit, MAX_BODYPARTS, "points.bodypart_hit", error_message))
            return false;

        for (size_t index = 0; index < point_data.bodypart_hit.size(); ++index) {
            if (!require_ubyte_range(point_data.bodypart_hit[index], ("points.bodypart_hit[" + std::to_string(index) + "]").c_str(), error_message))
                return false;
        }

        return require_short_range(point_data.ob, "points.ob", error_message)
            && require_short_range(point_data.damage, "points.damage", error_message)
            && require_short_range(point_data.energy_regen, "points.energy_regen", error_message)
            && require_short_range(point_data.parry, "points.parry", error_message)
            && require_short_range(point_data.dodge, "points.dodge", error_message)
            && require_short_range(point_data.encumbrance, "points.encumbrance", error_message)
            && require_short_range(point_data.willpower, "points.willpower", error_message)
            && require_short_range(point_data.spell_pen, "points.spell_pen", error_message)
            && require_short_range(point_data.spell_power, "points.spell_power", error_message);
    }

    bool validate_integer_array_range(const std::vector<int>& values, size_t expected_size, const char* field_name, bool is_byte, std::string* error_message)
    {
        if (!require_exact_array_size(values, expected_size, field_name, error_message))
            return false;

        for (size_t index = 0; index < values.size(); ++index) {
            const std::string indexed_field = std::string(field_name) + "[" + std::to_string(index) + "]";
            if (is_byte) {
                if (!require_byte_range(values[index], indexed_field.c_str(), error_message))
                    return false;
            } else {
                if (!require_ubyte_range(values[index], indexed_field.c_str(), error_message))
                    return false;
            }
        }

        return true;
    }

    bool validate_color_array_range(const std::vector<int>& values, std::string* error_message)
    {
        if (!require_exact_array_size(values, MAX_COLOR_FIELDS, "colors", error_message))
            return false;

        for (size_t index = 0; index < values.size(); ++index) {
            const std::string indexed_field = "colors[" + std::to_string(index) + "]";
            if (!require_color_value_range(values[index], indexed_field.c_str(), error_message))
                return false;
        }

        return true;
    }

    bool validate_color_value_data(const ColorValueData& value, const char* field_name, std::string* error_message)
    {
        if (!require_integer_range(value.mode, COLOR_VALUE_DEFAULT, COLOR_VALUE_TRUECOLOR, field_name, error_message))
            return false;

        if (value.mode == COLOR_VALUE_DEFAULT)
            return true;

        if (value.mode == COLOR_VALUE_ANSI16)
            return require_color_value_range(value.value, field_name, error_message);

        return require_ubyte_range(value.red, (std::string(field_name) + ".red").c_str(), error_message)
            && require_ubyte_range(value.green, (std::string(field_name) + ".green").c_str(), error_message)
            && require_ubyte_range(value.blue, (std::string(field_name) + ".blue").c_str(), error_message);
    }

    bool validate_color_settings(const std::vector<ColorSettingData>& settings, std::string* error_message)
    {
        if (settings.size() != MAX_COLOR_FIELDS) {
            set_error(error_message, "color_settings must contain exactly " + std::to_string(MAX_COLOR_FIELDS) + " entries.");
            return false;
        }

        for (size_t index = 0; index < settings.size(); ++index) {
            const std::string prefix = "colors[" + std::to_string(index) + "]";
            if (!validate_color_value_data(settings[index].foreground, (prefix + ".foreground").c_str(), error_message))
                return false;
            if (!validate_color_value_data(settings[index].background, (prefix + ".background").c_str(), error_message))
                return false;
        }

        return true;
    }

    bool validate_character_collections(const CharacterData& character, std::string* error_message)
    {
        return validate_ability_data(character.temporary_abilities, "abilities.temporary", error_message)
            && validate_ability_data(character.rolled_abilities, "abilities.rolled", error_message)
            && validate_point_data(character.points, error_message)
            && validate_color_array_range(character.colors, error_message)
            && validate_color_settings(character.color_settings, error_message)
            && validate_integer_array_range(character.talks, MAX_TOUNGE, "talks", true, error_message)
            && validate_integer_array_range(character.skills, MAX_SKILLS, "skills", false, error_message);
    }

    bool validate_affect_data(const AffectData& affect, size_t index, std::string* error_message)
    {
        const std::string prefix = "affects[" + std::to_string(index) + "]";
        return require_short_range(affect.type, (prefix + ".type").c_str(), error_message)
            && require_byte_range(affect.time_phase, (prefix + ".time_phase").c_str(), error_message)
            && require_short_range(affect.modifier, (prefix + ".modifier").c_str(), error_message)
            && require_short_range(affect.location, (prefix + ".location").c_str(), error_message)
            && require_short_range(affect.counter, (prefix + ".counter").c_str(), error_message);
    }

    std::vector<std::string> encode_flags(long flags, const FlagDefinition* definitions, size_t definition_count)
    {
        std::vector<std::string> names;
        for (size_t index = 0; index < definition_count; ++index) {
            if ((flags & definitions[index].bit) != 0)
                names.push_back(definitions[index].name);
        }
        return names;
    }

    bool decode_flags(const std::vector<std::string>& names, const FlagDefinition* definitions, size_t definition_count, long* flags, const char* flag_type, std::string* error_message)
    {
        if (flags == nullptr) {
            set_error(error_message, std::string(flag_type) + " flags output parameter must not be null.");
            return false;
        }

        *flags = 0;
        for (const std::string& name : names) {
            auto definition = std::find_if(definitions, definitions + definition_count, [&](const FlagDefinition& candidate) {
                return name == candidate.name;
            });

            if (definition == definitions + definition_count) {
                set_error(error_message, "Unknown " + std::string(flag_type) + " flag '" + name + "'.");
                return false;
            }

            *flags |= definition->bit;
        }

        set_error(error_message, "");
        return true;
    }

    ProfessionData profession_from_store(const char_file_u& stored_character, int profession)
    {
        ProfessionData profession_data;
        profession_data.level = stored_character.profs.prof_level[profession];
        profession_data.points = stored_character.profs.prof_coof[profession];
        profession_data.coeff = stored_character.profs.prof_coof[profession];
        profession_data.experience = stored_character.profs.prof_exp[profession];
        return profession_data;
    }

    bool apply_profession_to_store(const ProfessionData& profession_data, int profession, char_file_u* stored_character, std::string* error_message)
    {
        if (stored_character == nullptr) {
            set_error(error_message, "Stored character output parameter must not be null.");
            return false;
        }

        if (profession_data.points != 0 && profession_data.coeff != 0 && profession_data.points != profession_data.coeff) {
            set_error(error_message, "Profession points and coeff must match when both are present.");
            return false;
        }

        stored_character->profs.prof_level[profession] = profession_data.level;
        stored_character->profs.prof_coof[profession] = (profession_data.coeff != 0) ? profession_data.coeff : profession_data.points;
        stored_character->profs.prof_exp[profession] = profession_data.experience;
        return true;
    }

    int normalize_tactics_value(int value)
    {
        return (value >= TACTICS_DEFENSIVE && value <= TACTICS_BERSERK) ? value : TACTICS_NORMAL;
    }

    int normalize_shooting_value(int value)
    {
        return (value >= SHOOTING_SLOW && value <= SHOOTING_FAST) ? value : SHOOTING_NORMAL;
    }

    int normalize_casting_value(int value)
    {
        return (value >= CASTING_SLOW && value <= CASTING_FAST) ? value : CASTING_NORMAL;
    }

    void write_string_array(std::ostringstream& output, const std::vector<std::string>& values)
    {
        output << "[";
        for (size_t index = 0; index < values.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << "\"" << json_utils::escape_json_string(values[index]) << "\"";
        }
        output << "]";
    }

    void write_integer_array(std::ostringstream& output, const std::vector<int>& values)
    {
        output << "[";
        for (size_t index = 0; index < values.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << values[index];
        }
        output << "]";
    }

    std::string slugify_key(const char* raw_name, const char* fallback_prefix, int index)
    {
        std::string slug;
        if (raw_name != nullptr) {
            for (const char* current = raw_name; *current != '\0'; ++current) {
                const unsigned char ch = static_cast<unsigned char>(*current);
                if (std::isalnum(ch))
                    slug.push_back(static_cast<char>(std::tolower(ch)));
                else if (!slug.empty() && slug.back() != '_')
                    slug.push_back('_');
            }
        }

        while (!slug.empty() && slug.back() == '_')
            slug.pop_back();

        if (slug.empty())
            slug = std::string(fallback_prefix) + "_" + std::to_string(index);

        return slug;
    }

    std::string talk_key_for_index(int index)
    {
        if (index >= 0 && index < language_number)
            return slugify_key(get_skill_array()[language_skills[index]].name, "talk", index);
        return "talk_" + std::to_string(index);
    }

    std::string skill_key_for_index(int index)
    {
        return slugify_key(get_skill_array()[index].name, "skill", index);
    }

    std::string color_key_for_index(int index)
    {
        if (index >= 0 && index < MAX_COLOR_FIELDS)
            return kColorFieldNames[index];
        return "color_" + std::to_string(index);
    }

    int talk_index_for_key(const std::string& key)
    {
        for (int index = 0; index < MAX_TOUNGE; ++index) {
            if (talk_key_for_index(index) == key)
                return index;
        }
        return -1;
    }

    int skill_index_for_key(const std::string& key)
    {
        for (int index = 0; index < MAX_SKILLS; ++index) {
            if (skill_key_for_index(index) == key)
                return index;
        }
        return -1;
    }

    int color_index_for_key(const std::string& key)
    {
        for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
            if (color_key_for_index(index) == key)
                return index;
        }
        return -1;
    }

    int skill_index_for_key_memoized(const std::string& key)
    {
        // Lazy slug->index map for skills; built once, INSERT-IF-ABSENT so the LOWEST index wins on a
        // slug collision -- exactly matching skill_index_for_key's first-match linear scan.
        static const std::unordered_map<std::string, int> index_by_key = [] {
            std::unordered_map<std::string, int> table;
            table.reserve(MAX_SKILLS * 2);
            for (int index = 0; index < MAX_SKILLS; ++index)
                table.emplace(skill_key_for_index(index), index);
            return table;
        }();
        const auto found = index_by_key.find(key);
        return found != index_by_key.end() ? found->second : -1;
    }

    int talk_index_for_key_memoized(const std::string& key)
    {
        // Lazy slug->index map for talks; INSERT-IF-ABSENT (lowest index wins) like talk_index_for_key.
        static const std::unordered_map<std::string, int> index_by_key = [] {
            std::unordered_map<std::string, int> table;
            for (int index = 0; index < MAX_TOUNGE; ++index)
                table.emplace(talk_key_for_index(index), index);
            return table;
        }();
        const auto found = index_by_key.find(key);
        return found != index_by_key.end() ? found->second : -1;
    }

    void write_named_integer_object(std::ostringstream& output, const std::vector<NamedValue>& values)
    {
        output << "{";
        for (size_t index = 0; index < values.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << "\"" << json_utils::escape_json_string(values[index].key) << "\": " << values[index].value;
        }
        output << "}";
    }

    void write_color_value(std::ostringstream& output, const ColorValueData& value)
    {
        output << "{";
        if (value.mode == COLOR_VALUE_DEFAULT) {
            output << "\"mode\": \"default\"";
        } else if (value.mode == COLOR_VALUE_ANSI16) {
            output << "\"mode\": \"ansi16\", \"value\": " << value.value;
        } else {
            output << "\"mode\": \"truecolor\", \"value\": " << value.value << ", \"r\": " << value.red << ", \"g\": " << value.green << ", \"b\": " << value.blue;
        }
        output << "}";
    }

    void write_color_setting(std::ostringstream& output, const ColorSettingData& setting)
    {
        output << "{\"foreground\": ";
        write_color_value(output, setting.foreground);
        output << ", \"background\": ";
        write_color_value(output, setting.background);
        output << "}";
    }

    std::vector<NamedValue> collect_non_default_color_slots(const CharacterData& character)
    {
        std::vector<NamedValue> named_values;
        for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
            ColorSettingData setting = default_color_setting();
            if (index < static_cast<int>(character.color_settings.size()))
                setting = character.color_settings[index];
            if (is_default_color_value(setting.foreground) && index < static_cast<int>(character.colors.size()) && character.colors[index] != CNRM) {
                setting.foreground.mode = COLOR_VALUE_ANSI16;
                setting.foreground.value = character.colors[index];
            }
            normalize_color_setting(&setting);
            if (is_default_color_setting(setting))
                continue;
            named_values.push_back({ color_key_for_index(index), index });
        }
        return named_values;
    }

    template <class Reader>
    bool parse_color_value_object(Reader* reader, ColorValueData* value, std::string* error_message)
    {
        if (reader == nullptr || value == nullptr) {
            set_error(error_message, "Color value parser requires non-null parameters.");
            return false;
        }

        ColorValueData parsed = default_color_value();
        bool saw_mode = false;
        bool saw_value = false;
        bool saw_red = false;
        bool saw_green = false;
        bool saw_blue = false;
        if (!reader->parse_object([&parsed, &saw_mode, &saw_value, &saw_red, &saw_green, &saw_blue](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
                if (key == "mode") {
                    std::string mode;
                    if (!nested_reader->parse_string(&mode, nested_error_message))
                        return false;
                    saw_mode = true;
                    if (mode == "default")
                        parsed.mode = COLOR_VALUE_DEFAULT;
                    else if (mode == "ansi16")
                        parsed.mode = COLOR_VALUE_ANSI16;
                    else if (mode == "truecolor")
                        parsed.mode = COLOR_VALUE_TRUECOLOR;
                    else {
                        set_error(nested_error_message, "Unknown color mode.");
                        return false;
                    }
                    return true;
                }
                if (key == "value")
                    return saw_value = true, nested_reader->parse_integer(&parsed.value, nested_error_message);
                if (key == "r")
                    return saw_red = true, nested_reader->parse_integer(&parsed.red, nested_error_message);
                if (key == "g")
                    return saw_green = true, nested_reader->parse_integer(&parsed.green, nested_error_message);
                if (key == "b")
                    return saw_blue = true, nested_reader->parse_integer(&parsed.blue, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            },
                error_message))
            return false;

        if (!saw_mode) {
            set_error(error_message, "Color value object must include mode.");
            return false;
        }
        if (parsed.mode == COLOR_VALUE_ANSI16 && !saw_value) {
            set_error(error_message, "ANSI16 color value object must include value.");
            return false;
        }
        if (parsed.mode == COLOR_VALUE_TRUECOLOR && (!saw_red || !saw_green || !saw_blue)) {
            set_error(error_message, "Truecolor value object must include r, g, and b.");
            return false;
        }

        if (parsed.mode == COLOR_VALUE_TRUECOLOR) {
            if (!saw_value)
                parsed.value = nearest_ansi_color(parsed.red, parsed.green, parsed.blue);
        } else if (parsed.mode == COLOR_VALUE_DEFAULT)
            parsed.value = CNRM;
        *value = parsed;
        return true;
    }

    template <class Reader>
    bool parse_color_setting_value(Reader* reader, ColorSettingData* setting, std::vector<int>* colors, int index, std::string* error_message)
    {
        if (reader == nullptr || setting == nullptr || colors == nullptr) {
            set_error(error_message, "Color setting parser requires non-null parameters.");
            return false;
        }

        int legacy_color = 0;
        if (reader->parse_integer(&legacy_color, error_message)) {
            setting->foreground.mode = COLOR_VALUE_ANSI16;
            setting->foreground.value = legacy_color;
            setting->background = default_color_value();
            (*colors)[index] = legacy_color;
            return true;
        }

        *setting = default_color_setting();
        bool saw_foreground = false;
        bool saw_background = false;
        if (!reader->parse_object([&saw_foreground, &saw_background, setting](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
                if (key == "foreground")
                    return saw_foreground = true, parse_color_value_object(nested_reader, &setting->foreground, nested_error_message);
                if (key == "background")
                    return saw_background = true, parse_color_value_object(nested_reader, &setting->background, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            },
                error_message))
            return false;

        if (!saw_foreground)
            setting->foreground = default_color_value();
        if (!saw_background)
            setting->background = default_color_value();
        normalize_color_setting(setting);
        (*colors)[index] = (setting->foreground.mode == COLOR_VALUE_ANSI16)
            ? setting->foreground.value
            : (setting->foreground.mode == COLOR_VALUE_TRUECOLOR ? nearest_ansi_color(setting->foreground.red, setting->foreground.green, setting->foreground.blue) : CNRM);
        return true;
    }

    template <class Reader>
    bool parse_colors_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        if (reader == nullptr || character == nullptr) {
            set_error(error_message, "Colors parser requires non-null parameters.");
            return false;
        }

        character->colors.assign(MAX_COLOR_FIELDS, 0);
        character->color_settings.assign(MAX_COLOR_FIELDS, default_color_setting());
        return reader->parse_object([character](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            const int index = color_index_for_key(key);
            if (index < 0) {
                set_error(nested_error_message, "Unknown color key.");
                return false;
            }
            return parse_color_setting_value(nested_reader, &character->color_settings[index], &character->colors, index, nested_error_message);
        },
            error_message);
    }

    std::vector<NamedValue> collect_non_zero_named_values(const std::vector<int>& values, int expected_size, const std::function<std::string(int)>& key_for_index)
    {
        std::vector<NamedValue> named_values;
        for (int index = 0; index < expected_size && index < static_cast<int>(values.size()); ++index) {
            if (values[index] == 0)
                continue;
            named_values.push_back({ key_for_index(index), values[index] });
        }
        return named_values;
    }

    void write_ability(std::ostringstream& output, const AbilityData& ability)
    {
        output << "{\n";
        output << "      \"str\": " << ability.str << ",\n";
        output << "      \"lea\": " << ability.lea << ",\n";
        output << "      \"intel\": " << ability.intel << ",\n";
        output << "      \"wil\": " << ability.wil << ",\n";
        output << "      \"dex\": " << ability.dex << ",\n";
        output << "      \"con\": " << ability.con << ",\n";
        output << "      \"hit\": " << ability.hit << ",\n";
        output << "      \"mana\": " << ability.mana << ",\n";
        output << "      \"move\": " << ability.move << "\n";
        output << "    }";
    }

    void write_profession(std::ostringstream& output, const char* name, const ProfessionData& profession)
    {
        output << "    \"" << name << "\": {\n";
        output << "      \"level\": " << profession.level << ",\n";
        output << "      \"points\": " << profession.points << ",\n";
        output << "      \"coeff\": " << profession.coeff << ",\n";
        output << "      \"experience\": " << profession.experience << "\n";
        output << "    }";
    }

    void write_affect(std::ostringstream& output, const AffectData& affect)
    {
        output << "{";
        output << "\"type\": " << affect.type << ", ";
        output << "\"duration\": " << affect.duration << ", ";
        output << "\"time_phase\": " << affect.time_phase << ", ";
        output << "\"modifier\": " << affect.modifier << ", ";
        output << "\"location\": " << affect.location << ", ";
        output << "\"counter\": " << affect.counter << ", ";
        output << "\"flags\": ";
        write_string_array(output, affect.flags);
        output << "}";
    }

    template <class Reader>
    bool parse_string_array(Reader* reader, std::vector<std::string>* values, std::string* error_message)
    {
        if (reader == nullptr || values == nullptr) {
            set_error(error_message, "String array parser requires reader and output parameters.");
            return false;
        }
        return reader->parse_string_array(values, error_message);
    }

    template <class Reader>
    bool parse_integer_array(Reader* reader, std::vector<int>* values, size_t max_values, const char* field_name, std::string* error_message)
    {
        if (reader == nullptr || values == nullptr) {
            set_error(error_message, "Integer array parser requires reader and output parameters.");
            return false;
        }

        values->clear();
        return reader->parse_array([values, max_values, field_name](Reader* nested_reader, std::string* nested_error_message) {
            if (values->size() >= max_values) {
                set_error(nested_error_message, std::string(field_name) + " exceeds the supported entry count.");
                return false;
            }
            int value = 0;
            if (!nested_reader->parse_integer(&value, nested_error_message))
                return false;
            values->push_back(value);
            return true;
        },
            error_message);
    }

    template <class Reader>
    bool parse_named_integer_object(Reader* reader, std::vector<int>* values, size_t expected_size, const char* field_name, const std::function<int(const std::string&)>& index_for_key, std::string* error_message)
    {
        if (reader == nullptr || values == nullptr) {
            set_error(error_message, "Named integer object parser requires reader and output parameters.");
            return false;
        }

        values->assign(expected_size, 0);
        std::vector<bool> seen(expected_size, false);

        return reader->parse_object([&](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            const int index = index_for_key(key);
            if (index < 0 || index >= static_cast<int>(expected_size)) {
                set_error(nested_error_message, std::string("Unknown ") + field_name + " key '" + key + "'.");
                return false;
            }
            if (seen[index]) {
                set_error(nested_error_message, std::string("Duplicate ") + field_name + " key '" + key + "'.");
                return false;
            }

            int value = 0;
            if (!nested_reader->parse_integer(&value, nested_error_message))
                return false;

            seen[index] = true;
            (*values)[index] = value;
            return true;
        },
            error_message);
    }

    template <class Reader>
    bool parse_ability_object(Reader* reader, AbilityData* ability, std::string* error_message)
    {
        if (reader == nullptr || ability == nullptr) {
            set_error(error_message, "Ability parser requires reader and output parameters.");
            return false;
        }

        bool saw_str = false;
        bool saw_lea = false;
        bool saw_intel = false;
        bool saw_wil = false;
        bool saw_dex = false;
        bool saw_con = false;
        bool saw_hit = false;
        bool saw_mana = false;
        bool saw_move = false;
        if (!reader->parse_object([ability, &saw_str, &saw_lea, &saw_intel, &saw_wil, &saw_dex, &saw_con, &saw_hit, &saw_mana, &saw_move](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "str")
                return saw_str = true, nested_reader->parse_integer(&ability->str, nested_error_message);
            if (key == "lea")
                return saw_lea = true, nested_reader->parse_integer(&ability->lea, nested_error_message);
            if (key == "intel")
                return saw_intel = true, nested_reader->parse_integer(&ability->intel, nested_error_message);
            if (key == "wil")
                return saw_wil = true, nested_reader->parse_integer(&ability->wil, nested_error_message);
            if (key == "dex")
                return saw_dex = true, nested_reader->parse_integer(&ability->dex, nested_error_message);
            if (key == "con")
                return saw_con = true, nested_reader->parse_integer(&ability->con, nested_error_message);
            if (key == "hit")
                return saw_hit = true, nested_reader->parse_integer(&ability->hit, nested_error_message);
            if (key == "mana")
                return saw_mana = true, nested_reader->parse_integer(&ability->mana, nested_error_message);
            if (key == "move")
                return saw_move = true, nested_reader->parse_integer(&ability->move, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_str || !saw_lea || !saw_intel || !saw_wil || !saw_dex || !saw_con || !saw_hit || !saw_mana || !saw_move) {
            set_error(error_message, "Ability object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_points_object(Reader* reader, PointData* points, std::string* error_message)
    {
        if (reader == nullptr || points == nullptr) {
            set_error(error_message, "Points parser requires reader and output parameters.");
            return false;
        }

        bool saw_bodypart_hit = false;
        bool saw_gold = false;
        bool saw_experience = false;
        bool saw_spirit = false;
        bool saw_mana_regen = false;
        bool saw_health_regen = false;
        bool saw_move_regen = false;
        bool saw_ob = false;
        bool saw_damage = false;
        bool saw_energy_regen = false;
        bool saw_parry = false;
        bool saw_dodge = false;
        bool saw_encumbrance = false;
        bool saw_willpower = false;
        bool saw_spell_pen = false;
        bool saw_spell_power = false;
        if (!reader->parse_object([points, &saw_bodypart_hit, &saw_gold, &saw_experience, &saw_spirit, &saw_mana_regen, &saw_health_regen, &saw_move_regen, &saw_ob, &saw_damage, &saw_energy_regen, &saw_parry, &saw_dodge, &saw_encumbrance, &saw_willpower, &saw_spell_pen, &saw_spell_power](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "bodypart_hit")
                return saw_bodypart_hit = true, parse_integer_array(nested_reader, &points->bodypart_hit, MAX_BODYPARTS, "points.bodypart_hit", nested_error_message);
            if (key == "gold")
                return saw_gold = true, nested_reader->parse_integer(&points->gold, nested_error_message);
            if (key == "experience")
                return saw_experience = true, nested_reader->parse_integer(&points->experience, nested_error_message);
            if (key == "spirit")
                return saw_spirit = true, nested_reader->parse_integer(&points->spirit, nested_error_message);
            if (key == "mana_regen")
                return saw_mana_regen = true, nested_reader->parse_integer(&points->mana_regen, nested_error_message);
            if (key == "health_regen")
                return saw_health_regen = true, nested_reader->parse_integer(&points->health_regen, nested_error_message);
            if (key == "move_regen")
                return saw_move_regen = true, nested_reader->parse_integer(&points->move_regen, nested_error_message);
            if (key == "ob")
                return saw_ob = true, nested_reader->parse_integer(&points->ob, nested_error_message);
            if (key == "damage")
                return saw_damage = true, nested_reader->parse_integer(&points->damage, nested_error_message);
            if (key == "energy_regen")
                return saw_energy_regen = true, nested_reader->parse_integer(&points->energy_regen, nested_error_message);
            if (key == "parry")
                return saw_parry = true, nested_reader->parse_integer(&points->parry, nested_error_message);
            if (key == "dodge")
                return saw_dodge = true, nested_reader->parse_integer(&points->dodge, nested_error_message);
            if (key == "encumbrance")
                return saw_encumbrance = true, nested_reader->parse_integer(&points->encumbrance, nested_error_message);
            if (key == "willpower")
                return saw_willpower = true, nested_reader->parse_integer(&points->willpower, nested_error_message);
            if (key == "spell_pen")
                return saw_spell_pen = true, nested_reader->parse_integer(&points->spell_pen, nested_error_message);
            if (key == "spell_power")
                return saw_spell_power = true, nested_reader->parse_integer(&points->spell_power, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_bodypart_hit || !saw_gold || !saw_experience || !saw_spirit || !saw_mana_regen || !saw_health_regen || !saw_move_regen || !saw_ob || !saw_damage || !saw_energy_regen || !saw_parry || !saw_dodge || !saw_encumbrance || !saw_willpower || !saw_spell_pen || !saw_spell_power) {
            set_error(error_message, "Points object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_conditions_object(Reader* reader, ConditionData* conditions, std::string* error_message)
    {
        if (reader == nullptr || conditions == nullptr) {
            set_error(error_message, "Conditions parser requires reader and output parameters.");
            return false;
        }

        bool saw_drunk = false;
        bool saw_full = false;
        bool saw_thirst = false;
        if (!reader->parse_object([conditions, &saw_drunk, &saw_full, &saw_thirst](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "drunk")
                return saw_drunk = true, nested_reader->parse_integer(&conditions->drunk, nested_error_message);
            if (key == "full")
                return saw_full = true, nested_reader->parse_integer(&conditions->full, nested_error_message);
            if (key == "thirst")
                return saw_thirst = true, nested_reader->parse_integer(&conditions->thirst, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_drunk || !saw_full || !saw_thirst) {
            set_error(error_message, "Conditions object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_timers_object(Reader* reader, TimerData* timers, std::string* error_message)
    {
        if (reader == nullptr || timers == nullptr) {
            set_error(error_message, "Timers parser requires reader and output parameters.");
            return false;
        }

        bool saw_birth = false;
        bool saw_last_logon = false;
        bool saw_played_seconds = false;
        bool saw_retired_on = false;
        if (!reader->parse_object([timers, &saw_birth, &saw_last_logon, &saw_played_seconds, &saw_retired_on](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "birth")
                return saw_birth = true, nested_reader->parse_long(&timers->birth, nested_error_message);
            if (key == "last_logon")
                return saw_last_logon = true, nested_reader->parse_long(&timers->last_logon, nested_error_message);
            if (key == "played_seconds")
                return saw_played_seconds = true, nested_reader->parse_integer(&timers->played_seconds, nested_error_message);
            if (key == "retired_on")
                return saw_retired_on = true, nested_reader->parse_integer(&timers->retired_on, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_birth || !saw_last_logon || !saw_played_seconds || !saw_retired_on) {
            set_error(error_message, "Timers object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_profession_object(Reader* reader, ProfessionData* profession, std::string* error_message)
    {
        if (reader == nullptr || profession == nullptr) {
            set_error(error_message, "Profession parser requires reader and output parameters.");
            return false;
        }

        return reader->parse_object([profession](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "level")
                return nested_reader->parse_integer(&profession->level, nested_error_message);
            if (key == "points")
                return nested_reader->parse_integer(&profession->points, nested_error_message);
            if (key == "coeff")
                return nested_reader->parse_integer(&profession->coeff, nested_error_message);
            if (key == "experience")
                return nested_reader->parse_long(&profession->experience, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message);
    }

    template <class Reader>
    bool parse_affect_object(Reader* reader, AffectData* affect, std::string* error_message)
    {
        if (reader == nullptr || affect == nullptr) {
            set_error(error_message, "Affect parser requires reader and output parameters.");
            return false;
        }

        bool saw_type = false;
        bool saw_duration = false;
        bool saw_time_phase = false;
        bool saw_modifier = false;
        bool saw_location = false;
        bool saw_counter = false;
        bool saw_flags = false;
        if (!reader->parse_object([affect, &saw_type, &saw_duration, &saw_time_phase, &saw_modifier, &saw_location, &saw_counter, &saw_flags](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "type")
                return saw_type = true, nested_reader->parse_integer(&affect->type, nested_error_message);
            if (key == "duration")
                return saw_duration = true, nested_reader->parse_integer(&affect->duration, nested_error_message);
            if (key == "time_phase")
                return saw_time_phase = true, nested_reader->parse_integer(&affect->time_phase, nested_error_message);
            if (key == "modifier")
                return saw_modifier = true, nested_reader->parse_integer(&affect->modifier, nested_error_message);
            if (key == "location")
                return saw_location = true, nested_reader->parse_integer(&affect->location, nested_error_message);
            if (key == "counter")
                return saw_counter = true, nested_reader->parse_integer(&affect->counter, nested_error_message);
            if (key == "flags")
                return saw_flags = true, parse_string_array(nested_reader, &affect->flags, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_type || !saw_duration || !saw_time_phase || !saw_modifier || !saw_location || !saw_counter || !saw_flags) {
            set_error(error_message, "Affect record was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_affects_array(Reader* reader, std::vector<AffectData>* affects, std::string* error_message)
    {
        if (reader == nullptr || affects == nullptr) {
            set_error(error_message, "Affects parser requires reader and output parameters.");
            return false;
        }

        affects->clear();
        return reader->parse_array([affects](Reader* nested_reader, std::string* nested_error_message) {
            if (affects->size() >= MAX_AFFECT) {
                set_error(nested_error_message, "affects exceeds the supported entry count.");
                return false;
            }
            AffectData affect;
            if (!parse_affect_object(nested_reader, &affect, nested_error_message))
                return false;
            affects->push_back(std::move(affect));
            return true;
        },
            error_message);
    }

    template <class Reader>
    bool parse_identity_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_idnum = false;
        bool saw_race = false;
        bool saw_sex = false;
        bool saw_bodytype = false;
        bool saw_language = false;
        bool saw_hometown = false;
        bool saw_weight = false;
        bool saw_height = false;
        if (!reader->parse_object([character, &saw_idnum, &saw_race, &saw_sex, &saw_bodytype, &saw_language, &saw_hometown, &saw_weight, &saw_height](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "idnum")
                return saw_idnum = true, nested_reader->parse_long(&character->idnum, nested_error_message);
            if (key == "race")
                return saw_race = true, nested_reader->parse_integer(&character->race, nested_error_message);
            if (key == "sex")
                return saw_sex = true, nested_reader->parse_integer(&character->sex, nested_error_message);
            if (key == "bodytype")
                return saw_bodytype = true, nested_reader->parse_integer(&character->bodytype, nested_error_message);
            if (key == "language")
                return saw_language = true, nested_reader->parse_integer(&character->language, nested_error_message);
            if (key == "hometown")
                return saw_hometown = true, nested_reader->parse_integer(&character->hometown, nested_error_message);
            if (key == "weight")
                return saw_weight = true, nested_reader->parse_integer(&character->weight, nested_error_message);
            if (key == "height")
                return saw_height = true, nested_reader->parse_integer(&character->height, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_idnum || !saw_race || !saw_sex || !saw_bodytype || !saw_language || !saw_hometown || !saw_weight || !saw_height) {
            set_error(error_message, "Identity object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_progression_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_level = false;
        bool saw_alignment = false;
        bool saw_mini_level = false;
        bool saw_max_mini_level = false;
        bool saw_spells_to_learn = false;
        bool saw_rerolls = false;
        if (!reader->parse_object([character, &saw_level, &saw_alignment, &saw_mini_level, &saw_max_mini_level, &saw_spells_to_learn, &saw_rerolls](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "level")
                return saw_level = true, nested_reader->parse_integer(&character->level, nested_error_message);
            if (key == "alignment")
                return saw_alignment = true, nested_reader->parse_integer(&character->alignment, nested_error_message);
            if (key == "mini_level")
                return saw_mini_level = true, nested_reader->parse_integer(&character->mini_level, nested_error_message);
            if (key == "max_mini_level")
                return saw_max_mini_level = true, nested_reader->parse_integer(&character->max_mini_level, nested_error_message);
            if (key == "spells_to_learn")
                return saw_spells_to_learn = true, nested_reader->parse_integer(&character->spells_to_learn, nested_error_message);
            if (key == "rerolls")
                return saw_rerolls = true, nested_reader->parse_integer(&character->rerolls, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_level || !saw_alignment || !saw_mini_level || !saw_max_mini_level || !saw_spells_to_learn || !saw_rerolls) {
            set_error(error_message, "Progression object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_abilities_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_temporary = false;
        bool saw_rolled = false;
        if (!reader->parse_object([character, &saw_temporary, &saw_rolled](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "temporary")
                return saw_temporary = true, parse_ability_object(nested_reader, &character->temporary_abilities, nested_error_message);
            if (key == "rolled")
                return saw_rolled = true, parse_ability_object(nested_reader, &character->rolled_abilities, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_temporary || !saw_rolled) {
            set_error(error_message, "Abilities object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_professions_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_mage = false;
        bool saw_mystic = false;
        bool saw_ranger = false;
        bool saw_warrior = false;
        if (!reader->parse_object([character, &saw_mage, &saw_mystic, &saw_ranger, &saw_warrior](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "mage")
                return saw_mage = true, parse_profession_object(nested_reader, &character->mage, nested_error_message);
            if (key == "mystic")
                return saw_mystic = true, parse_profession_object(nested_reader, &character->mystic, nested_error_message);
            if (key == "ranger")
                return saw_ranger = true, parse_profession_object(nested_reader, &character->ranger, nested_error_message);
            if (key == "warrior")
                return saw_warrior = true, parse_profession_object(nested_reader, &character->warrior, nested_error_message);
            if (key == "cleric") {
                set_error(nested_error_message, "Character JSON uses 'mystic' instead of legacy 'cleric'.");
                return false;
            }
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_mage || !saw_mystic || !saw_ranger || !saw_warrior) {
            set_error(error_message, "Professions object was missing one or more required professions.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_flags_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_player = false;
        bool saw_preferences = false;
        bool saw_affected = false;
        bool saw_hide = false;
        if (!reader->parse_object([character, &saw_player, &saw_preferences, &saw_affected, &saw_hide](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "player")
                return saw_player = true, parse_string_array(nested_reader, &character->player_flags, nested_error_message);
            if (key == "preferences")
                return saw_preferences = true, parse_string_array(nested_reader, &character->preference_flags, nested_error_message);
            if (key == "affected")
                return saw_affected = true, parse_string_array(nested_reader, &character->affected_flags, nested_error_message);
            if (key == "hide")
                return saw_hide = true, parse_string_array(nested_reader, &character->hide_flags, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_player || !saw_preferences || !saw_affected || !saw_hide) {
            set_error(error_message, "Flags object was missing one or more required flag lists.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_perception_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_raw = false;
        bool saw_current = false;
        if (!reader->parse_object([character, &saw_raw, &saw_current](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "raw")
                return saw_raw = true, nested_reader->parse_integer(&character->raw_perception, nested_error_message);
            if (key == "current")
                return saw_current = true, nested_reader->parse_integer(&character->perception, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_raw || !saw_current) {
            set_error(error_message, "Perception object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    template <class Reader>
    bool parse_state_object(Reader* reader, CharacterData* character, std::string* error_message)
    {
        bool saw_load_room = false;
        bool saw_wimp_level = false;
        bool saw_freeze_level = false;
        bool saw_morale = false;
        bool saw_owner = false;
        bool saw_leg_encumbrance = false;
        bool saw_rp_flag = false;
        bool saw_will_teach = false;
        if (!reader->parse_object([character, &saw_load_room, &saw_wimp_level, &saw_freeze_level, &saw_morale, &saw_owner, &saw_leg_encumbrance, &saw_rp_flag, &saw_will_teach](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "load_room")
                return saw_load_room = true, nested_reader->parse_integer(&character->load_room, nested_error_message);
            if (key == "wimp_level")
                return saw_wimp_level = true, nested_reader->parse_integer(&character->wimp_level, nested_error_message);
            if (key == "freeze_level")
                return saw_freeze_level = true, nested_reader->parse_integer(&character->freeze_level, nested_error_message);
            if (key == "morale")
                return saw_morale = true, nested_reader->parse_integer(&character->morale, nested_error_message);
            if (key == "owner")
                return saw_owner = true, nested_reader->parse_integer(&character->owner, nested_error_message);
            if (key == "leg_encumbrance")
                return saw_leg_encumbrance = true, nested_reader->parse_integer(&character->leg_encumbrance, nested_error_message);
            if (key == "rp_flag")
                return saw_rp_flag = true, nested_reader->parse_integer(&character->rp_flag, nested_error_message);
            if (key == "will_teach")
                return saw_will_teach = true, nested_reader->parse_long(&character->will_teach, nested_error_message);
            if (key == "tactics")
                return nested_reader->parse_integer(&character->tactics, nested_error_message);
            if (key == "shooting")
                return nested_reader->parse_integer(&character->shooting, nested_error_message);
            if (key == "casting")
                return nested_reader->parse_integer(&character->casting, nested_error_message);
            if (key == "two_handed")
                return nested_reader->parse_bool(&character->two_handed, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
            return false;

        if (!saw_load_room || !saw_wimp_level || !saw_freeze_level || !saw_morale || !saw_owner || !saw_leg_encumbrance || !saw_rp_flag || !saw_will_teach) {
            set_error(error_message, "State object was missing one or more required fields.");
            return false;
        }

        return true;
    }

    bool has_all_required_character_sections(bool saw_schema_version, bool saw_character_name, bool saw_title, bool saw_description, bool saw_identity, bool saw_progression, bool saw_abilities, bool saw_points, bool saw_professions, bool saw_flags, bool saw_conditions, bool saw_timers, bool saw_perception, bool saw_state, bool saw_talks, bool saw_skills, bool saw_affects)
    {
        return saw_schema_version
            && saw_character_name
            && saw_title
            && saw_description
            && saw_identity
            && saw_progression
            && saw_abilities
            && saw_points
            && saw_professions
            && saw_flags
            && saw_conditions
            && saw_timers
            && saw_perception
            && saw_state
            && saw_talks
            && saw_skills
            && saw_affects;
    }

} // namespace

CharacterData character_data_from_store(const char_file_u& stored_character)
{
    CharacterData character;
    character.character_name = stored_character.name;
    character.title = stored_character.title;
    character.description = stored_character.description;
    character.idnum = stored_character.specials2.idnum;
    character.race = stored_character.race;
    character.sex = stored_character.sex;
    character.bodytype = stored_character.bodytype;
    character.language = stored_character.language;
    character.hometown = stored_character.hometown;
    character.weight = stored_character.weight;
    character.height = stored_character.height;
    character.level = stored_character.level;
    character.alignment = stored_character.specials2.alignment;
    character.load_room = stored_character.specials2.load_room;
    character.spells_to_learn = stored_character.specials2.spells_to_learn;
    character.wimp_level = stored_character.specials2.wimp_level;
    character.freeze_level = stored_character.specials2.freeze_level;
    character.raw_perception = stored_character.specials2.rawPerception;
    character.perception = stored_character.specials2.perception;
    character.mini_level = stored_character.specials2.mini_level;
    character.max_mini_level = stored_character.specials2.max_mini_level;
    character.morale = stored_character.specials2.morale;
    character.owner = stored_character.specials2.owner;
    character.rerolls = stored_character.specials2.rerolls;
    character.leg_encumbrance = stored_character.specials2.leg_encumb;
    character.rp_flag = stored_character.specials2.rp_flag;
    character.will_teach = stored_character.specials2.will_teach;
    character.tactics = normalize_tactics_value(stored_character.specials2.tactics);
    character.shooting = normalize_shooting_value(stored_character.specials2.shooting);
    character.casting = normalize_casting_value(stored_character.specials2.casting);
    character.two_handed = stored_character.specials2.two_handed != 0;

    character.mage = profession_from_store(stored_character, PROF_MAGE);
    character.mystic = profession_from_store(stored_character, PROF_CLERIC);
    character.ranger = profession_from_store(stored_character, PROF_RANGER);
    character.warrior = profession_from_store(stored_character, PROF_WARRIOR);

    character.temporary_abilities = ability_from_store(stored_character.tmpabilities);
    character.rolled_abilities = ability_from_store(stored_character.constabilities);
    character.points = point_data_from_store(stored_character.points);
    character.conditions.drunk = stored_character.specials2.conditions[0];
    character.conditions.full = stored_character.specials2.conditions[1];
    character.conditions.thirst = stored_character.specials2.conditions[2];
    character.timers.birth = stored_character.birth;
    character.timers.last_logon = stored_character.last_logon;
    character.timers.played_seconds = stored_character.played;
    character.timers.retired_on = stored_character.specials2.retiredon;
    character.color_mask = stored_character.profs.color_mask;

    character.colors.reserve(MAX_COLOR_FIELDS);
    character.color_settings.reserve(MAX_COLOR_FIELDS);
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        character.colors.push_back(stored_character.profs.colors[index]);
        character.color_settings.push_back(color_setting_from_store(stored_character.profs, index));
    }

    character.talks.reserve(MAX_TOUNGE);
    for (int index = 0; index < MAX_TOUNGE; ++index)
        character.talks.push_back(stored_character.talks[index]);

    character.skills.reserve(MAX_SKILLS);
    for (int index = 0; index < MAX_SKILLS; ++index)
        character.skills.push_back(stored_character.skills[index]);

    character.player_flags = encode_player_flags(stored_character.specials2.act);
    character.preference_flags = encode_preference_flags(stored_character.specials2.pref);
    character.hide_flags = encode_hide_flags(stored_character.specials2.hide_flags);

    long combined_affected_flags = 0;
    for (int index = 0; index < MAX_AFFECT; ++index) {
        const affected_type& affect = stored_character.affected[index];
        if (affect.type == 0)
            continue;

        AffectData affect_data;
        affect_data.type = affect.type;
        affect_data.duration = affect.duration;
        affect_data.time_phase = affect.time_phase;
        affect_data.modifier = affect.modifier;
        affect_data.location = affect.location;
        affect_data.bitvector = affect.bitvector;
        affect_data.counter = affect.counter;
        affect_data.flags = encode_affected_flags(affect.bitvector);
        character.affects.push_back(std::move(affect_data));
        combined_affected_flags |= affect.bitvector;
    }
    character.affected_flags = encode_affected_flags(combined_affected_flags);
    return character;
}

bool apply_character_data_to_store(const CharacterData& json_character, char_file_u* stored_character, std::string* error_message)
{
    if (stored_character == nullptr) {
        set_error(error_message, "Stored character output parameter must not be null.");
        return false;
    }

    long player_flags = 0;
    if (!decode_player_flags(json_character.player_flags, &player_flags, error_message))
        return false;

    long preference_flags = 0;
    if (!decode_preference_flags(json_character.preference_flags, &preference_flags, error_message))
        return false;

    long affected_flags = 0;
    if (!decode_affected_flags(json_character.affected_flags, &affected_flags, error_message))
        return false;
    long hide_flags = 0;
    if (!decode_hide_flags(json_character.hide_flags, &hide_flags, error_message))
        return false;

    if (json_character.points.bodypart_hit.size() > MAX_BODYPARTS) {
        set_error(error_message, "Bodypart hit array exceeds the supported bodypart count.");
        return false;
    }

    if (!validate_character_strings(json_character, *stored_character, error_message))
        return false;

    if (!validate_character_scalar_ranges(json_character, error_message))
        return false;

    if (!validate_character_collections(json_character, error_message))
        return false;

    *stored_character = char_file_u {};

    if (!apply_profession_to_store(json_character.mage, PROF_MAGE, stored_character, error_message))
        return false;
    if (!apply_profession_to_store(json_character.mystic, PROF_CLERIC, stored_character, error_message))
        return false;
    if (!apply_profession_to_store(json_character.ranger, PROF_RANGER, stored_character, error_message))
        return false;
    if (!apply_profession_to_store(json_character.warrior, PROF_WARRIOR, stored_character, error_message))
        return false;

    std::snprintf(stored_character->name, sizeof(stored_character->name), "%s", json_character.character_name.c_str());
    std::snprintf(stored_character->title, sizeof(stored_character->title), "%s", json_character.title.c_str());
    std::snprintf(stored_character->description, sizeof(stored_character->description), "%s", json_character.description.c_str());
    stored_character->specials2.idnum = json_character.idnum;
    stored_character->race = json_character.race;
    stored_character->sex = json_character.sex;
    stored_character->bodytype = json_character.bodytype;
    stored_character->language = json_character.language;
    stored_character->hometown = json_character.hometown;
    stored_character->weight = json_character.weight;
    stored_character->height = json_character.height;
    stored_character->level = json_character.level;
    stored_character->specials2.alignment = json_character.alignment;
    stored_character->specials2.load_room = json_character.load_room;
    stored_character->specials2.spells_to_learn = json_character.spells_to_learn;
    stored_character->specials2.act = player_flags;
    stored_character->specials2.pref = preference_flags;
    stored_character->specials2.wimp_level = json_character.wimp_level;
    stored_character->specials2.freeze_level = static_cast<byte>(json_character.freeze_level);
    stored_character->specials2.rawPerception = json_character.raw_perception;
    stored_character->specials2.perception = json_character.perception;
    stored_character->specials2.conditions[0] = json_character.conditions.drunk;
    stored_character->specials2.conditions[1] = json_character.conditions.full;
    stored_character->specials2.conditions[2] = json_character.conditions.thirst;
    stored_character->specials2.mini_level = json_character.mini_level;
    stored_character->specials2.max_mini_level = json_character.max_mini_level;
    stored_character->specials2.morale = json_character.morale;
    stored_character->specials2.owner = json_character.owner;
    stored_character->specials2.rerolls = static_cast<ubyte>(json_character.rerolls);
    stored_character->specials2.leg_encumb = json_character.leg_encumbrance;
    stored_character->specials2.rp_flag = json_character.rp_flag;
    stored_character->specials2.retiredon = json_character.timers.retired_on;
    stored_character->specials2.hide_flags = static_cast<int>(hide_flags);
    stored_character->specials2.will_teach = json_character.will_teach;
    stored_character->specials2.tactics = json_character.tactics;
    stored_character->specials2.shooting = json_character.shooting;
    stored_character->specials2.casting = json_character.casting;
    stored_character->specials2.two_handed = json_character.two_handed ? 1 : 0;
    stored_character->birth = json_character.timers.birth;
    stored_character->last_logon = json_character.timers.last_logon;
    stored_character->played = json_character.timers.played_seconds;
    stored_character->profs.color_mask = json_character.color_mask;

    apply_ability_to_store(json_character.temporary_abilities, &stored_character->tmpabilities);
    apply_ability_to_store(json_character.rolled_abilities, &stored_character->constabilities);
    apply_point_data_to_store(json_character.points, &stored_character->points);

    for (int index = 0; index < MAX_COLOR_FIELDS; ++index) {
        const ColorSettingData setting = (index < static_cast<int>(json_character.color_settings.size()))
            ? json_character.color_settings[index]
            : default_color_setting();
        stored_character->profs.color_settings[index].foreground = color_value_to_store(setting.foreground);
        stored_character->profs.color_settings[index].background = color_value_to_store(setting.background);
        stored_character->profs.colors[index] = static_cast<char>((index < static_cast<int>(json_character.colors.size())) ? json_character.colors[index] : 0);
    }

    for (int index = 0; index < MAX_TOUNGE; ++index)
        stored_character->talks[index] = static_cast<byte>((index < static_cast<int>(json_character.talks.size())) ? json_character.talks[index] : 0);

    for (int index = 0; index < MAX_SKILLS; ++index)
        stored_character->skills[index] = static_cast<byte>((index < static_cast<int>(json_character.skills.size())) ? json_character.skills[index] : 0);

    for (int index = 0; index < MAX_AFFECT; ++index) {
        stored_character->affected[index].type = 0;
        stored_character->affected[index].duration = 0;
        stored_character->affected[index].time_phase = 0;
        stored_character->affected[index].modifier = 0;
        stored_character->affected[index].location = 0;
        stored_character->affected[index].bitvector = 0;
        stored_character->affected[index].counter = 0;
        stored_character->affected[index].next = nullptr;
    }

    for (size_t index = 0; index < json_character.affects.size() && index < MAX_AFFECT; ++index) {
        if (!validate_affect_data(json_character.affects[index], index, error_message))
            return false;

        long affect_flag_bits = 0;
        if (!decode_affected_flags(json_character.affects[index].flags, &affect_flag_bits, error_message))
            return false;

        affected_type& affect = stored_character->affected[index];
        affect.type = static_cast<sh_int>(json_character.affects[index].type);
        affect.duration = json_character.affects[index].duration;
        affect.time_phase = static_cast<char>(json_character.affects[index].time_phase);
        affect.modifier = static_cast<sh_int>(json_character.affects[index].modifier);
        affect.location = static_cast<sh_int>(json_character.affects[index].location);
        affect.bitvector = affect_flag_bits;
        affect.counter = static_cast<sh_int>(json_character.affects[index].counter);
    }

    long combined_affect_bits = 0;
    for (int index = 0; index < MAX_AFFECT; ++index)
        combined_affect_bits |= stored_character->affected[index].bitvector;
    if (combined_affect_bits != affected_flags) {
        set_error(error_message, "Affected flag list must match the flags implied by structured affects.");
        return false;
    }

    set_error(error_message, "");
    return true;
}

std::string serialize_character_to_json(const CharacterData& character)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"schema_version\": " << character.schema_version << ",\n";
    output << "  \"character_name\": \"" << json_utils::escape_json_string(character.character_name) << "\",\n";
    output << "  \"title\": \"" << json_utils::escape_json_string(character.title) << "\",\n";
    output << "  \"description\": \"" << json_utils::escape_json_string(character.description) << "\",\n";
    output << "  \"identity\": {\n";
    output << "    \"idnum\": " << character.idnum << ",\n";
    output << "    \"race\": " << character.race << ",\n";
    output << "    \"sex\": " << character.sex << ",\n";
    output << "    \"bodytype\": " << character.bodytype << ",\n";
    output << "    \"language\": " << character.language << ",\n";
    output << "    \"hometown\": " << character.hometown << ",\n";
    output << "    \"weight\": " << character.weight << ",\n";
    output << "    \"height\": " << character.height << "\n";
    output << "  },\n";
    output << "  \"progression\": {\n";
    output << "    \"level\": " << character.level << ",\n";
    output << "    \"alignment\": " << character.alignment << ",\n";
    output << "    \"mini_level\": " << character.mini_level << ",\n";
    output << "    \"max_mini_level\": " << character.max_mini_level << ",\n";
    output << "    \"spells_to_learn\": " << character.spells_to_learn << ",\n";
    output << "    \"rerolls\": " << character.rerolls << "\n";
    output << "  },\n";
    output << "  \"abilities\": {\n";
    output << "    \"temporary\": ";
    write_ability(output, character.temporary_abilities);
    output << ",\n";
    output << "    \"rolled\": ";
    write_ability(output, character.rolled_abilities);
    output << "\n  },\n";
    output << "  \"points\": {\n";
    output << "    \"bodypart_hit\": ";
    write_integer_array(output, character.points.bodypart_hit);
    output << ",\n";
    output << "    \"gold\": " << character.points.gold << ",\n";
    output << "    \"experience\": " << character.points.experience << ",\n";
    output << "    \"spirit\": " << character.points.spirit << ",\n";
    output << "    \"mana_regen\": " << character.points.mana_regen << ",\n";
    output << "    \"health_regen\": " << character.points.health_regen << ",\n";
    output << "    \"move_regen\": " << character.points.move_regen << ",\n";
    output << "    \"ob\": " << character.points.ob << ",\n";
    output << "    \"damage\": " << character.points.damage << ",\n";
    output << "    \"energy_regen\": " << character.points.energy_regen << ",\n";
    output << "    \"parry\": " << character.points.parry << ",\n";
    output << "    \"dodge\": " << character.points.dodge << ",\n";
    output << "    \"encumbrance\": " << character.points.encumbrance << ",\n";
    output << "    \"willpower\": " << character.points.willpower << ",\n";
    output << "    \"spell_pen\": " << character.points.spell_pen << ",\n";
    output << "    \"spell_power\": " << character.points.spell_power << "\n";
    output << "  },\n";
    output << "  \"professions\": {\n";
    write_profession(output, "mage", character.mage);
    output << ",\n";
    write_profession(output, "mystic", character.mystic);
    output << ",\n";
    write_profession(output, "ranger", character.ranger);
    output << ",\n";
    write_profession(output, "warrior", character.warrior);
    output << "\n  },\n";
    output << "  \"flags\": {\n";
    output << "    \"player\": ";
    write_string_array(output, character.player_flags);
    output << ",\n";
    output << "    \"preferences\": ";
    write_string_array(output, character.preference_flags);
    output << ",\n";
    output << "    \"affected\": ";
    write_string_array(output, character.affected_flags);
    output << ",\n";
    output << "    \"hide\": ";
    write_string_array(output, character.hide_flags);
    output << "\n  },\n";
    output << "  \"conditions\": {\n";
    output << "    \"drunk\": " << character.conditions.drunk << ",\n";
    output << "    \"full\": " << character.conditions.full << ",\n";
    output << "    \"thirst\": " << character.conditions.thirst << "\n";
    output << "  },\n";
    output << "  \"color_mask\": " << character.color_mask << ",\n";
    output << "  \"colors\": {";
    const std::vector<NamedValue> color_slots = collect_non_default_color_slots(character);
    for (size_t index = 0; index < color_slots.size(); ++index) {
        if (index > 0)
            output << ", ";
        const int slot_index = color_slots[index].value;
        ColorSettingData setting = (slot_index < static_cast<int>(character.color_settings.size()))
            ? character.color_settings[slot_index]
            : default_color_setting();
        if (is_default_color_value(setting.foreground) && slot_index < static_cast<int>(character.colors.size()) && character.colors[slot_index] != CNRM) {
            setting.foreground.mode = COLOR_VALUE_ANSI16;
            setting.foreground.value = character.colors[slot_index];
        }
        normalize_color_setting(&setting);
        output << "\"" << json_utils::escape_json_string(color_slots[index].key) << "\": ";
        write_color_setting(output, setting);
    }
    output << "},\n";
    output << "  \"timers\": {\n";
    output << "    \"birth\": " << character.timers.birth << ",\n";
    output << "    \"last_logon\": " << character.timers.last_logon << ",\n";
    output << "    \"played_seconds\": " << character.timers.played_seconds << ",\n";
    output << "    \"retired_on\": " << character.timers.retired_on << "\n";
    output << "  },\n";
    output << "  \"perception\": {\n";
    output << "    \"raw\": " << character.raw_perception << ",\n";
    output << "    \"current\": " << character.perception << "\n";
    output << "  },\n";
    output << "  \"state\": {\n";
    output << "    \"load_room\": " << character.load_room << ",\n";
    output << "    \"wimp_level\": " << character.wimp_level << ",\n";
    output << "    \"freeze_level\": " << character.freeze_level << ",\n";
    output << "    \"morale\": " << character.morale << ",\n";
    output << "    \"owner\": " << character.owner << ",\n";
    output << "    \"leg_encumbrance\": " << character.leg_encumbrance << ",\n";
    output << "    \"rp_flag\": " << character.rp_flag << ",\n";
    output << "    \"will_teach\": " << character.will_teach << ",\n";
    output << "    \"tactics\": " << character.tactics << ",\n";
    output << "    \"shooting\": " << character.shooting << ",\n";
    output << "    \"casting\": " << character.casting << ",\n";
    output << "    \"two_handed\": " << (character.two_handed ? "true" : "false") << "\n";
    output << "  },\n";
    output << "  \"talks\": ";
    write_named_integer_object(output, collect_non_zero_named_values(character.talks, MAX_TOUNGE, talk_key_for_index));
    output << ",\n";
    output << "  \"skills\": ";
    write_named_integer_object(output, collect_non_zero_named_values(character.skills, MAX_SKILLS, skill_key_for_index));
    output << ",\n";
    output << "  \"affects\": [";
    for (size_t index = 0; index < character.affects.size(); ++index) {
        if (index > 0)
            output << ", ";
        write_affect(output, character.affects[index]);
    }
    output << "]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_character_from_json(const std::string& json, CharacterData* character, std::string* error_message)
{
    if (character == nullptr) {
        set_error(error_message, "Character output parameter must not be null.");
        return false;
    }

    CharacterData parsed_character;
    json_utils::JsonReader reader(json);
    bool saw_schema_version = false;
    bool saw_character_name = false;
    bool saw_title = false;
    bool saw_description = false;
    bool saw_identity = false;
    bool saw_progression = false;
    bool saw_abilities = false;
    bool saw_points = false;
    bool saw_professions = false;
    bool saw_flags = false;
    bool saw_conditions = false;
    bool saw_color_mask = false;
    bool saw_colors = false;
    bool saw_timers = false;
    bool saw_perception = false;
    bool saw_state = false;
    bool saw_talks = false;
    bool saw_skills = false;
    bool saw_affects = false;
    parsed_character.colors.assign(MAX_COLOR_FIELDS, 0);
    parsed_character.color_settings.assign(MAX_COLOR_FIELDS, default_color_setting());
    if (!reader.parse_root_object([&parsed_character, &saw_schema_version, &saw_character_name, &saw_title, &saw_description, &saw_identity, &saw_progression, &saw_abilities, &saw_points, &saw_professions, &saw_flags, &saw_conditions, &saw_color_mask, &saw_colors, &saw_timers, &saw_perception, &saw_state, &saw_talks, &saw_skills, &saw_affects](const std::string& key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "schema_version")
                return saw_schema_version = true, nested_reader->parse_integer(&parsed_character.schema_version, nested_error_message);
            if (key == "character_name")
                return saw_character_name = true, nested_reader->parse_string(&parsed_character.character_name, nested_error_message);
            if (key == "title")
                return saw_title = true, nested_reader->parse_string(&parsed_character.title, nested_error_message);
            if (key == "description")
                return saw_description = true, nested_reader->parse_string(&parsed_character.description, nested_error_message);
            if (key == "identity")
                return saw_identity = true, parse_identity_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "progression")
                return saw_progression = true, parse_progression_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "abilities")
                return saw_abilities = true, parse_abilities_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "points")
                return saw_points = true, parse_points_object(nested_reader, &parsed_character.points, nested_error_message);
            if (key == "professions")
                return saw_professions = true, parse_professions_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "flags")
                return saw_flags = true, parse_flags_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "conditions")
                return saw_conditions = true, parse_conditions_object(nested_reader, &parsed_character.conditions, nested_error_message);
            if (key == "color_mask")
                return saw_color_mask = true, nested_reader->parse_long(&parsed_character.color_mask, nested_error_message);
            if (key == "colors")
                return saw_colors = true, parse_colors_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "timers")
                return saw_timers = true, parse_timers_object(nested_reader, &parsed_character.timers, nested_error_message);
            if (key == "perception")
                return saw_perception = true, parse_perception_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "state")
                return saw_state = true, parse_state_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "talks")
                return saw_talks = true, parse_named_integer_object(nested_reader, &parsed_character.talks, MAX_TOUNGE, "talk", talk_index_for_key, nested_error_message);
            if (key == "skills")
                return saw_skills = true, parse_named_integer_object(nested_reader, &parsed_character.skills, MAX_SKILLS, "skill", skill_index_for_key, nested_error_message);
            if (key == "affects")
                return saw_affects = true, parse_affects_array(nested_reader, &parsed_character.affects, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
        return false;

    if (!has_all_required_character_sections(saw_schema_version, saw_character_name, saw_title, saw_description, saw_identity, saw_progression, saw_abilities, saw_points, saw_professions, saw_flags, saw_conditions, saw_timers, saw_perception, saw_state, saw_talks, saw_skills, saw_affects)) {
        set_error(error_message, "Character JSON was missing one or more required sections.");
        return false;
    }

    if (parsed_character.schema_version != CHARACTER_JSON_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported character schema version.");
        return false;
    }

    if (!validate_character_scalar_ranges(parsed_character, error_message))
        return false;
    if (!validate_character_collections(parsed_character, error_message))
        return false;

    long ignored_flags = 0;
    if (!decode_player_flags(parsed_character.player_flags, &ignored_flags, error_message))
        return false;
    if (!decode_preference_flags(parsed_character.preference_flags, &ignored_flags, error_message))
        return false;
    if (!decode_affected_flags(parsed_character.affected_flags, &ignored_flags, error_message))
        return false;
    if (!decode_hide_flags(parsed_character.hide_flags, &ignored_flags, error_message))
        return false;
    if (!require_exact_array_size(parsed_character.points.bodypart_hit, MAX_BODYPARTS, "points.bodypart_hit", error_message))
        return false;
    if (!saw_color_mask)
        parsed_character.color_mask = 0;
    if (!saw_colors) {
        parsed_character.colors.assign(MAX_COLOR_FIELDS, 0);
        parsed_character.color_settings.assign(MAX_COLOR_FIELDS, default_color_setting());
    }
    if (!require_exact_array_size(parsed_character.colors, MAX_COLOR_FIELDS, "colors", error_message))
        return false;
    if (!require_exact_array_size(parsed_character.talks, MAX_TOUNGE, "talks", error_message))
        return false;
    if (!require_exact_array_size(parsed_character.skills, MAX_SKILLS, "skills", error_message))
        return false;
    for (const AffectData& affect : parsed_character.affects) {
        if (!decode_affected_flags(affect.flags, &ignored_flags, error_message))
            return false;
    }

    *character = std::move(parsed_character);
    set_error(error_message, "");
    return true;
}

template <class Reader>
bool deserialize_character_v2_dispatch(const std::string& json, CharacterData* character, std::string* error_message)
{
    if (character == nullptr) {
        set_error(error_message, "Character output parameter must not be null.");
        return false;
    }

    CharacterData parsed_character;
    Reader reader(json);
    bool saw_schema_version = false;
    bool saw_character_name = false;
    bool saw_title = false;
    bool saw_description = false;
    bool saw_identity = false;
    bool saw_progression = false;
    bool saw_abilities = false;
    bool saw_points = false;
    bool saw_professions = false;
    bool saw_flags = false;
    bool saw_conditions = false;
    bool saw_color_mask = false;
    bool saw_colors = false;
    bool saw_timers = false;
    bool saw_perception = false;
    bool saw_state = false;
    bool saw_talks = false;
    bool saw_skills = false;
    bool saw_affects = false;
    parsed_character.colors.assign(MAX_COLOR_FIELDS, 0);
    parsed_character.color_settings.assign(MAX_COLOR_FIELDS, default_color_setting());
    if (!reader.parse_root_object([&parsed_character, &saw_schema_version, &saw_character_name, &saw_title, &saw_description, &saw_identity, &saw_progression, &saw_abilities, &saw_points, &saw_professions, &saw_flags, &saw_conditions, &saw_color_mask, &saw_colors, &saw_timers, &saw_perception, &saw_state, &saw_talks, &saw_skills, &saw_affects](const std::string& key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "schema_version")
                return saw_schema_version = true, nested_reader->parse_integer(&parsed_character.schema_version, nested_error_message);
            if (key == "character_name")
                return saw_character_name = true, nested_reader->parse_string(&parsed_character.character_name, nested_error_message);
            if (key == "title")
                return saw_title = true, nested_reader->parse_string(&parsed_character.title, nested_error_message);
            if (key == "description")
                return saw_description = true, nested_reader->parse_string(&parsed_character.description, nested_error_message);
            if (key == "identity")
                return saw_identity = true, parse_identity_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "progression")
                return saw_progression = true, parse_progression_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "abilities")
                return saw_abilities = true, parse_abilities_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "points")
                return saw_points = true, parse_points_object(nested_reader, &parsed_character.points, nested_error_message);
            if (key == "professions")
                return saw_professions = true, parse_professions_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "flags")
                return saw_flags = true, parse_flags_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "conditions")
                return saw_conditions = true, parse_conditions_object(nested_reader, &parsed_character.conditions, nested_error_message);
            if (key == "color_mask")
                return saw_color_mask = true, nested_reader->parse_long(&parsed_character.color_mask, nested_error_message);
            if (key == "colors")
                return saw_colors = true, parse_colors_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "timers")
                return saw_timers = true, parse_timers_object(nested_reader, &parsed_character.timers, nested_error_message);
            if (key == "perception")
                return saw_perception = true, parse_perception_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "state")
                return saw_state = true, parse_state_object(nested_reader, &parsed_character, nested_error_message);
            if (key == "talks")
                return saw_talks = true, parse_named_integer_object(nested_reader, &parsed_character.talks, MAX_TOUNGE, "talk", talk_index_for_key_memoized, nested_error_message);
            if (key == "skills")
                return saw_skills = true, parse_named_integer_object(nested_reader, &parsed_character.skills, MAX_SKILLS, "skill", skill_index_for_key_memoized, nested_error_message);
            if (key == "affects")
                return saw_affects = true, parse_affects_array(nested_reader, &parsed_character.affects, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
            error_message))
        return false;

    if (!has_all_required_character_sections(saw_schema_version, saw_character_name, saw_title, saw_description, saw_identity, saw_progression, saw_abilities, saw_points, saw_professions, saw_flags, saw_conditions, saw_timers, saw_perception, saw_state, saw_talks, saw_skills, saw_affects)) {
        set_error(error_message, "Character JSON was missing one or more required sections.");
        return false;
    }

    if (parsed_character.schema_version != CHARACTER_JSON_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported character schema version.");
        return false;
    }

    if (!validate_character_scalar_ranges(parsed_character, error_message))
        return false;
    if (!validate_character_collections(parsed_character, error_message))
        return false;

    long ignored_flags = 0;
    if (!decode_player_flags(parsed_character.player_flags, &ignored_flags, error_message))
        return false;
    if (!decode_preference_flags(parsed_character.preference_flags, &ignored_flags, error_message))
        return false;
    if (!decode_affected_flags(parsed_character.affected_flags, &ignored_flags, error_message))
        return false;
    if (!decode_hide_flags(parsed_character.hide_flags, &ignored_flags, error_message))
        return false;
    if (!require_exact_array_size(parsed_character.points.bodypart_hit, MAX_BODYPARTS, "points.bodypart_hit", error_message))
        return false;
    if (!saw_color_mask)
        parsed_character.color_mask = 0;
    if (!saw_colors) {
        parsed_character.colors.assign(MAX_COLOR_FIELDS, 0);
        parsed_character.color_settings.assign(MAX_COLOR_FIELDS, default_color_setting());
    }
    if (!require_exact_array_size(parsed_character.colors, MAX_COLOR_FIELDS, "colors", error_message))
        return false;
    if (!require_exact_array_size(parsed_character.talks, MAX_TOUNGE, "talks", error_message))
        return false;
    if (!require_exact_array_size(parsed_character.skills, MAX_SKILLS, "skills", error_message))
        return false;
    for (const AffectData& affect : parsed_character.affects) {
        if (!decode_affected_flags(affect.flags, &ignored_flags, error_message))
            return false;
    }

    *character = std::move(parsed_character);
    set_error(error_message, "");
    return true;
}

bool deserialize_character_from_json_v2a(const std::string& json, CharacterData* character, std::string* error_message)
{
    return deserialize_character_v2_dispatch<json_utils::JsonReader>(json, character, error_message);
}

bool deserialize_character_from_json_v2b(const std::string& json, CharacterData* character, std::string* error_message)
{
    return deserialize_character_v2_dispatch<json_utils::JsonReaderV2>(json, character, error_message);
}

std::vector<std::string> encode_player_flags(long flags)
{
    return encode_flags(flags, kPlayerFlags, sizeof(kPlayerFlags) / sizeof(kPlayerFlags[0]));
}

std::vector<std::string> encode_preference_flags(long flags)
{
    return encode_flags(flags, kPreferenceFlags, sizeof(kPreferenceFlags) / sizeof(kPreferenceFlags[0]));
}

std::vector<std::string> encode_affected_flags(long flags)
{
    return encode_flags(flags, kAffectedFlags, sizeof(kAffectedFlags) / sizeof(kAffectedFlags[0]));
}

std::vector<std::string> encode_hide_flags(long flags)
{
    return encode_flags(flags, kHideFlags, sizeof(kHideFlags) / sizeof(kHideFlags[0]));
}

bool decode_player_flags(const std::vector<std::string>& names, long* flags, std::string* error_message)
{
    return decode_flags(names, kPlayerFlags, sizeof(kPlayerFlags) / sizeof(kPlayerFlags[0]), flags, "player", error_message);
}

bool decode_preference_flags(const std::vector<std::string>& names, long* flags, std::string* error_message)
{
    return decode_flags(names, kPreferenceFlags, sizeof(kPreferenceFlags) / sizeof(kPreferenceFlags[0]), flags, "preference", error_message);
}

bool decode_affected_flags(const std::vector<std::string>& names, long* flags, std::string* error_message)
{
    return decode_flags(names, kAffectedFlags, sizeof(kAffectedFlags) / sizeof(kAffectedFlags[0]), flags, "affected", error_message);
}

bool decode_hide_flags(const std::vector<std::string>& names, long* flags, std::string* error_message)
{
    return decode_flags(names, kHideFlags, sizeof(kHideFlags) / sizeof(kHideFlags[0]), flags, "hide", error_message);
}

} // namespace character_json
