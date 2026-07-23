#include "js_trigger_dispatch.h"

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "js_api_struct_mapping.h"
#include "json_utils.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

void draw_map();
void perform_give(struct char_data *ch, struct char_data *vict, struct obj_data *obj);
extern struct char_data *waiting_list;
extern char *sector_types[];
extern char num_of_sector_types;

namespace {

bool is_identifier_start(unsigned char ch) { return std::isalpha(ch) || ch == '_' || ch == '$'; }

bool is_identifier_continue(unsigned char ch) { return std::isalnum(ch) || ch == '_' || ch == '$'; }

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

JsGameTriggerFixture make_trigger_fixture(const JsScriptingManifestEntry &entry,
                                          JsScriptPackageHost host) {
    JsGameTriggerFixture trigger;
    trigger.name = entry.javascript_handler_name ? entry.javascript_handler_name : "";
    trigger.legacy_name = entry.legacy_name ? entry.legacy_name : "";
    trigger.host_type = js_script_package_host_name(host);
    trigger.legacy_value = entry.legacy_value;
    trigger.blocks_gameplay = entry.blocks_gameplay;
    return trigger;
}

bool required_host_context_is_present(JsScriptPackageHost host,
                                      const JsGameTriggerContextFixture &context) {
    switch (host) {
    case JsScriptPackageHost::Character:
        return context.has_self;
    case JsScriptPackageHost::MudlleMobile:
        return context.has_self && context.self.is_npc;
    case JsScriptPackageHost::Object:
        return context.has_object;
    case JsScriptPackageHost::Room:
        return context.has_room;
    }
    return false;
}

struct PendingTextMutation {
    char **target = nullptr;
    char *char_target = nullptr;
    int *int_target = nullptr;
    unsigned char *byte_target = nullptr;
    bool has_value = false;
    bool redraw_world_map = false;
    std::string value;
    int int_value = 0;
};

struct RoomFlagHelperFlag {
    const char *name;
    long bit;
    const char *authority_class;
    bool requires_admin_override;
};

struct PendingRoomFlagMutation {
    room_data *room = nullptr;
    int room_vnum = -1;
    int room_zone_index = -1;
    int room_zone_vnum = -1;
    const char *flag_name = nullptr;
    const char *authority_class = nullptr;
    long flag_bit = 0;
    bool add = false;
    bool requires_admin_override = false;
};

struct AppliedRoomFlagMutation {
    room_data *room = nullptr;
    long previous_flags = 0;
};

enum class PendingObjectCommandTarget {
    None,
    Character,
    Room,
};

struct PendingObjectCommand {
    std::string operation;
    char_data *giver = nullptr;
    char_data *recipient = nullptr;
    obj_data *object = nullptr;
    PendingObjectCommandTarget target = PendingObjectCommandTarget::None;
    char_data *target_character = nullptr;
    int target_room = NOWHERE;
    int real_object_index = -1;
};

enum class AppliedObjectCommandKind {
    LoadedObject,
    GaveObject,
};

struct AppliedObjectCommand {
    AppliedObjectCommandKind kind = AppliedObjectCommandKind::LoadedObject;
    obj_data *object = nullptr;
    char_data *giver = nullptr;
    char_data *recipient = nullptr;
};

struct PendingWaitCommand {
    std::string character_id;
    int pulses = 0;
};

struct ScriptCommandArguments {
    std::string text;
    std::string speaker_id;
    std::string target_id;
    std::string room_id;
    std::string giver_id;
    std::string recipient_id;
    std::string object_id;
    std::string load_target_id;
    int vnum = -1;
    int pulses = -1;
    bool saw_text = false;
    bool saw_speaker_id = false;
    bool saw_target_id = false;
    bool saw_room_id = false;
    bool saw_giver_id = false;
    bool saw_recipient_id = false;
    bool saw_object_id = false;
    bool saw_load_target_id = false;
    bool saw_vnum = false;
    bool saw_pulses = false;
};

struct PendingOutputCommand {
    std::string operation;
    char_data *character = nullptr;
    int room = NOWHERE;
    std::string text;
};

bool is_blank_text(const std::string &value) {
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

bool has_zone_map_file_syntax_marker(const std::string &value) {
    bool at_line_start = true;
    for (char ch : value) {
        if (ch == '~')
            return true;
        if (at_line_start && ch == '#')
            return true;
        if (ch == '\n' || ch == '\r') {
            at_line_start = true;
            continue;
        }
        if (at_line_start && std::isspace(static_cast<unsigned char>(ch)))
            continue;
        at_line_start = false;
    }
    return false;
}

bool is_nullable_text_property(const JsRuntimeMutation &mutation) {
    return (mutation.target_type == "object" && mutation.property == "actionDescription") ||
           (mutation.target_type == "zone" &&
            (mutation.property == "description" || mutation.property == "map"));
}

bool validate_text_mutation_value(const JsRuntimeMutation &mutation) {
    if ((mutation.has_value && mutation.value_kind != "string") ||
        (!mutation.has_value && mutation.value_kind != "null"))
        return false;
    if (!mutation.has_value)
        return is_nullable_text_property(mutation);

    const bool is_name = mutation.property == "name";
    const std::size_t max_length = is_name ? 256 : 8192;
    if (mutation.value.size() > max_length)
        return false;
    if (mutation.value.find('\0') != std::string::npos)
        return false;
    if (is_name && is_blank_text(mutation.value))
        return false;
    if (mutation.target_type == "zone" && mutation.property == "map" &&
        has_zone_map_file_syntax_marker(mutation.value))
        return false;
    return true;
}

bool validate_symbol_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "zone" || mutation.property != "symbol" || !mutation.has_value ||
        mutation.value_kind != "string")
        return false;
    if (mutation.value.size() != 1)
        return false;
    const unsigned char ch = static_cast<unsigned char>(mutation.value[0]);
    return ch > 32 && ch < 127;
}

bool parse_coordinate_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty())
        return false;
    if (value.size() > 2 || (value.size() > 1 && value[0] == '0'))
        return false;
    int result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        result = result * 10 + (ch - '0');
        if (result > 25)
            return false;
    }
    *parsed = result;
    return true;
}

bool parse_reset_mode_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty())
        return false;
    if (value.size() != 1)
        return false;
    if (!std::isdigit(static_cast<unsigned char>(value[0])))
        return false;
    const int result = value[0] - '0';
    if (result > 3)
        return false;
    *parsed = result;
    return true;
}

bool parse_lifespan_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty())
        return false;
    if (value == "0" || value.size() > 5 || (value.size() > 1 && value[0] == '0'))
        return false;
    int result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        result = result * 10 + (ch - '0');
        if (result > 10080)
            return false;
    }
    *parsed = result;
    return true;
}

bool parse_level_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty())
        return false;
    if (value.size() > 3 || (value.size() > 1 && value[0] == '0'))
        return false;
    int result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        result = result * 10 + (ch - '0');
        if (result > 100)
            return false;
    }
    *parsed = result;
    return true;
}

bool parse_rarity_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty())
        return false;
    if (value.size() > 3 || (value.size() > 1 && value[0] == '0'))
        return false;
    int result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        result = result * 10 + (ch - '0');
        if (result > 255)
            return false;
    }
    *parsed = result;
    return true;
}

bool parse_sector_type_value(const std::string &value, int *parsed) {
    if (parsed == nullptr || value.empty() || value == "Unknown" || sector_types == nullptr)
        return false;
    for (int index = 0; index < num_of_sector_types; ++index) {
        if (sector_types[index] != nullptr &&
            std::strcmp(sector_types[index], value.c_str()) == 0) {
            *parsed = index;
            return true;
        }
    }
    return false;
}

bool validate_coordinate_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "zone" || (mutation.property != "x" && mutation.property != "y") ||
        !mutation.has_value || mutation.value_kind != "number")
        return false;
    int parsed = 0;
    return parse_coordinate_value(mutation.value, &parsed);
}

bool validate_reset_mode_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "zone" || mutation.property != "resetMode" || !mutation.has_value ||
        mutation.value_kind != "number")
        return false;
    int parsed = 0;
    return parse_reset_mode_value(mutation.value, &parsed);
}

bool validate_lifespan_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "zone" || mutation.property != "lifespan" || !mutation.has_value ||
        mutation.value_kind != "number")
        return false;
    int parsed = 0;
    return parse_lifespan_value(mutation.value, &parsed);
}

bool validate_level_mutation_value(const JsRuntimeMutation &mutation) {
    if ((mutation.target_type != "zone" && mutation.target_type != "room" &&
         mutation.target_type != "object") ||
        mutation.property != "level" || !mutation.has_value || mutation.value_kind != "number")
        return false;
    int parsed = 0;
    return parse_level_value(mutation.value, &parsed);
}

bool validate_rarity_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "object" || mutation.property != "rarity" || !mutation.has_value ||
        mutation.value_kind != "number")
        return false;
    int parsed = 0;
    return parse_rarity_value(mutation.value, &parsed);
}

bool validate_sector_type_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type != "room" || mutation.property != "sectorType" ||
        !mutation.has_value || mutation.value_kind != "string")
        return false;
    int parsed = 0;
    return parse_sector_type_value(mutation.value, &parsed);
}

bool validate_mutation_value(const JsRuntimeMutation &mutation) {
    if (mutation.target_type == "zone" && mutation.property == "symbol")
        return validate_symbol_mutation_value(mutation);
    if (mutation.target_type == "zone" && (mutation.property == "x" || mutation.property == "y"))
        return validate_coordinate_mutation_value(mutation);
    if (mutation.target_type == "zone" && mutation.property == "resetMode")
        return validate_reset_mode_mutation_value(mutation);
    if (mutation.target_type == "zone" && mutation.property == "lifespan")
        return validate_lifespan_mutation_value(mutation);
    if ((mutation.target_type == "zone" || mutation.target_type == "room" ||
         mutation.target_type == "object") &&
        mutation.property == "level")
        return validate_level_mutation_value(mutation);
    if (mutation.target_type == "object" && mutation.property == "rarity")
        return validate_rarity_mutation_value(mutation);
    if (mutation.target_type == "room" && mutation.property == "sectorType")
        return validate_sector_type_mutation_value(mutation);
    return validate_text_mutation_value(mutation);
}

bool runtime_mutation_kind_is_setter(const JsRuntimeMutation &mutation) {
    return mutation.kind == "setter";
}

bool runtime_mutation_kind_is_helper(const JsRuntimeMutation &mutation) {
    return mutation.kind == "helper";
}

bool runtime_mutation_kind_is_command(const JsRuntimeMutation &mutation) {
    return mutation.kind == "command";
}

std::size_t runtime_helper_mutation_count(const std::vector<JsRuntimeMutation> &mutations) {
    std::size_t count = 0;
    for (const JsRuntimeMutation &mutation : mutations) {
        if (runtime_mutation_kind_is_helper(mutation))
            ++count;
    }
    return count;
}

bool has_persistent_setter_authority(const JsTriggerMutationAuthorityContext &authority);
bool has_room_flag_admin_override_authority(const JsTriggerMutationAuthorityContext &authority);
bool parse_id_number(const std::string &id, const char *prefix, int *number);

bool command_text_is_valid(const std::string &value) {
    if (value.empty() || value.size() > 512)
        return false;
    for (char ch : value) {
        if (ch == '\0' || ch == '\r' || ch == '\n')
            return false;
    }
    return true;
}

bool command_numeric_id_with_prefix_is_valid(const std::string &value, const char *prefix) {
    int ignored = 0;
    return parse_id_number(value, prefix, &ignored);
}

bool command_id_is_valid(const std::string &value, const char *prefix) {
    if (value.empty() || value.size() > 128 || prefix == nullptr)
        return false;
    if (std::strcmp(prefix, "character:") == 0) {
        return command_numeric_id_with_prefix_is_valid(value, "character:") ||
               command_numeric_id_with_prefix_is_valid(value, "char:") ||
               command_numeric_id_with_prefix_is_valid(value, "player:") ||
               command_numeric_id_with_prefix_is_valid(value, "mob:") || value == "self" ||
               value == "actor" || value == "speaker" || value == "attacker" || value == "victim" ||
               value == "killer" || value == "dying" || value == "target" || value == "targ1" ||
               value == "targ2";
    }
    if (std::strcmp(prefix, "room:") == 0) {
        return command_numeric_id_with_prefix_is_valid(value, "room:") || value == "room" ||
               value == "target" || value == "targ1" || value == "targ2";
    }
    if (std::strcmp(prefix, "object:") == 0) {
        return command_numeric_id_with_prefix_is_valid(value, "object:") || value == "object" ||
               value == "weapon" || value == "target" || value == "targ1" || value == "targ2";
    }
    return command_numeric_id_with_prefix_is_valid(value, prefix);
}

bool command_vnum_is_valid(int vnum) { return vnum >= 0 && vnum <= 999999; }

bool command_pulses_are_valid(int pulses) { return pulses >= 1 && pulses <= 10000; }

bool parse_script_command_arguments(const JsRuntimeMutation &mutation,
                                    ScriptCommandArguments *parsed = nullptr) {
    if (mutation.arguments_json.empty() || mutation.arguments_json.size() > 1024)
        return false;

    json_utils::JsonReader reader(mutation.arguments_json);
    ScriptCommandArguments local;
    std::string error;
    if (!reader.parse_root_object(
            [&](const std::string &name, json_utils::JsonReader *nested_reader,
                std::string *nested_error) {
                if (name == "text" && !local.saw_text) {
                    local.saw_text = true;
                    return nested_reader->parse_string(&local.text, nested_error);
                }
                if (name == "speakerId" && !local.saw_speaker_id) {
                    local.saw_speaker_id = true;
                    return nested_reader->parse_string(&local.speaker_id, nested_error);
                }
                if (name == "targetId" && !local.saw_target_id) {
                    local.saw_target_id = true;
                    return nested_reader->parse_string(&local.target_id, nested_error);
                }
                if (name == "roomId" && !local.saw_room_id) {
                    local.saw_room_id = true;
                    return nested_reader->parse_string(&local.room_id, nested_error);
                }
                if (name == "giverId" && !local.saw_giver_id) {
                    local.saw_giver_id = true;
                    return nested_reader->parse_string(&local.giver_id, nested_error);
                }
                if (name == "recipientId" && !local.saw_recipient_id) {
                    local.saw_recipient_id = true;
                    return nested_reader->parse_string(&local.recipient_id, nested_error);
                }
                if (name == "objectId" && !local.saw_object_id) {
                    local.saw_object_id = true;
                    return nested_reader->parse_string(&local.object_id, nested_error);
                }
                if (name == "loadTargetId" && !local.saw_load_target_id) {
                    local.saw_load_target_id = true;
                    return nested_reader->parse_string(&local.load_target_id, nested_error);
                }
                if (name == "vnum" && !local.saw_vnum) {
                    local.saw_vnum = true;
                    return nested_reader->parse_integer(&local.vnum, nested_error);
                }
                if (name == "pulses" && !local.saw_pulses) {
                    local.saw_pulses = true;
                    return nested_reader->parse_integer(&local.pulses, nested_error);
                }
                return false;
            },
            &error))
        return false;

    bool valid = false;
    if (mutation.operation == "script.do_wait")
        valid = local.saw_pulses && command_pulses_are_valid(local.pulses);
    else if (mutation.operation == "script.load_obj")
        valid =
            local.saw_vnum && command_vnum_is_valid(local.vnum) &&
            (!local.saw_load_target_id || command_id_is_valid(local.load_target_id, "character:") ||
             command_id_is_valid(local.load_target_id, "room:"));
    else if (mutation.operation == "script.do_say")
        valid = local.saw_speaker_id && local.saw_text &&
                command_id_is_valid(local.speaker_id, "character:") &&
                command_text_is_valid(local.text);
    else if (mutation.operation == "script.send_to_char")
        valid = local.saw_target_id && local.saw_text &&
                command_id_is_valid(local.target_id, "character:") &&
                command_text_is_valid(local.text);
    else if (mutation.operation == "script.send_to_room")
        valid = local.saw_room_id && local.saw_text &&
                command_id_is_valid(local.room_id, "room:") && command_text_is_valid(local.text);
    else if (mutation.operation == "script.do_give")
        valid = local.saw_giver_id && local.saw_recipient_id && local.saw_object_id &&
                command_id_is_valid(local.giver_id, "character:") &&
                command_id_is_valid(local.recipient_id, "character:") &&
                command_id_is_valid(local.object_id, "object:");
    if (parsed != nullptr)
        *parsed = local;
    return valid;
}

bool validate_script_command_mutations(const std::vector<JsRuntimeMutation> &mutations) {
    for (const JsRuntimeMutation &mutation : mutations) {
        if (!runtime_mutation_kind_is_command(mutation))
            return false;
        if (!mutation.target_type.empty() || !mutation.target_id.empty() ||
            !mutation.property.empty() || !mutation.value_kind.empty() || mutation.has_value ||
            !mutation.value.empty() || !mutation.target_token.empty())
            return false;
        if (!parse_script_command_arguments(mutation))
            return false;
    }
    return true;
}

bool script_command_mutation_is_output(const JsRuntimeMutation &mutation) {
    return mutation.operation == "script.do_say" || mutation.operation == "script.send_to_char" ||
           mutation.operation == "script.send_to_room";
}

bool runtime_mutation_requires_persistent_authority(const JsRuntimeMutation &mutation) {
    if (runtime_mutation_kind_is_setter(mutation) || runtime_mutation_kind_is_helper(mutation))
        return true;
    return runtime_mutation_kind_is_command(mutation) &&
           !script_command_mutation_is_output(mutation);
}

bool runtime_mutations_require_persistent_authority(
    const std::vector<JsRuntimeMutation> &mutations) {
    for (const JsRuntimeMutation &mutation : mutations) {
        if (runtime_mutation_requires_persistent_authority(mutation))
            return true;
    }
    return false;
}

bool room_matches_mutation_authority(const room_data &room, const JsGameAdapterOptions &options,
                                     const JsTriggerMutationAuthorityContext &authority);
bool object_matches_mutation_authority(const obj_data &object, const JsGameAdapterOptions &options,
                                       const JsTriggerMutationAuthorityContext &authority);
JsTriggerHelperMutationTransactionResult prepare_helper_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations,
    const JsTriggerHelperMutationTransactionOptions &options,
    std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations);

bool registry_contains_operation(const JsTriggerHelperMutationOperationRegistry &registry,
                                 const std::string &operation) {
    if (operation.empty() || registry.operation_names == nullptr)
        return false;
    for (std::size_t index = 0; index < registry.operation_count; ++index) {
        if (registry.operation_names[index] != nullptr &&
            operation == registry.operation_names[index])
            return true;
    }
    return false;
}

constexpr RoomFlagHelperFlag RoomFlagHelperAllowedFlags[] = {
    {"dark", DARK, "builder-zone", false},
    {"death", DEATH, "admin-only", true},
    {"noMob", NO_MOB, "builder-zone", false},
    {"indoors", INDOORS, "builder-zone", false},
    {"noRide", NORIDE, "builder-zone", false},
    {"shadowy", SHADOWY, "builder-zone", false},
    {"noMagic", NO_MAGIC, "builder-zone", false},
    {"tunnel", TUNNEL, "builder-zone", false},
    {"private", PRIVATE, "admin-only", true},
    {"godRoom", GODROOM, "admin-only", true},
    {"drinkWater", DRINK_WATER, "builder-zone", false},
    {"drinkPoison", DRINK_POISON, "builder-zone", false},
    {"securityRoom", SECURITYROOM, "admin-only", true},
    {"peaceRoom", PEACEROOM, "builder-zone", false},
    {"noTeleport", NO_TELEPORT, "admin-only", true},
    {"hideVnum", HIDE_VNUM, "builder-zone", false},
};

std::size_t room_flag_helper_allowed_flag_count() {
    return sizeof(RoomFlagHelperAllowedFlags) / sizeof(RoomFlagHelperAllowedFlags[0]);
}

bool room_flag_helper_flag_is_allowed(const std::string &flag_name) {
    for (std::size_t index = 0; index < room_flag_helper_allowed_flag_count(); ++index) {
        if (flag_name == RoomFlagHelperAllowedFlags[index].name)
            return true;
    }
    return false;
}

const RoomFlagHelperFlag *find_room_flag_helper_flag(const std::string &flag_name) {
    for (std::size_t index = 0; index < room_flag_helper_allowed_flag_count(); ++index) {
        if (flag_name == RoomFlagHelperAllowedFlags[index].name)
            return &RoomFlagHelperAllowedFlags[index];
    }
    return nullptr;
}

bool parse_room_flag_helper_arguments(const std::string &arguments_json,
                                      const RoomFlagHelperFlag **flag) {
    if (flag == nullptr || arguments_json.empty() || arguments_json.size() > 512)
        return false;

    bool saw_flag = false;
    std::string parsed_flag;
    std::string error;
    json_utils::JsonReader reader(arguments_json);
    if (!reader.parse_root_object(
            [&](const std::string &key, json_utils::JsonReader *nested_reader,
                std::string *nested_error) {
                if (key != "flag" || saw_flag)
                    return false;
                saw_flag = true;
                return nested_reader->parse_string(&parsed_flag, nested_error);
            },
            &error))
        return false;

    const RoomFlagHelperFlag *found_flag =
        saw_flag ? find_room_flag_helper_flag(parsed_flag) : nullptr;
    if (found_flag == nullptr)
        return false;

    *flag = found_flag;
    return true;
}

bool parse_room_flag_helper_target_token(const std::string &token, int *zone_vnum, int *room_vnum,
                                         std::string *secret) {
    if (zone_vnum == nullptr || room_vnum == nullptr || secret == nullptr)
        return false;

    constexpr const char *Prefix = "room-token:v1:";
    const std::string prefix(Prefix);
    if (token.find(prefix) != 0)
        return false;

    const std::string suffix = token.substr(prefix.size());
    const std::size_t first_separator = suffix.find(':');
    if (first_separator == std::string::npos || first_separator == 0 ||
        first_separator == suffix.size() - 1)
        return false;
    const std::size_t second_separator = suffix.find(':', first_separator + 1);
    if (second_separator == std::string::npos || second_separator == first_separator + 1 ||
        second_separator == suffix.size() - 1)
        return false;
    if (suffix.find(':', second_separator + 1) != std::string::npos)
        return false;

    auto parse_nonnegative_int = [](const std::string &value, int *parsed_value) {
        if (parsed_value == nullptr || value.empty() || value.size() > 10)
            return false;
        int parsed = 0;
        for (char ch : value) {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return false;
            const int digit = ch - '0';
            if (parsed > (std::numeric_limits<int>::max() - digit) / 10)
                return false;
            parsed = parsed * 10 + digit;
        }
        *parsed_value = parsed;
        return true;
    };

    const std::string zone_part = suffix.substr(0, first_separator);
    const std::string room_part =
        suffix.substr(first_separator + 1, second_separator - first_separator - 1);
    const std::string secret_part = suffix.substr(second_separator + 1);
    if (!parse_nonnegative_int(zone_part, zone_vnum) ||
        !parse_nonnegative_int(room_part, room_vnum))
        return false;
    if (secret_part.empty() || secret_part.size() > 128)
        return false;

    *secret = secret_part;
    return true;
}

room_data *mutable_live_room_for_vnum(int room_vnum, const JsGameAdapterOptions &options) {
    if (options.world == nullptr || room_vnum < 0)
        return nullptr;

    const std::size_t room_count = options.world_count > 0
                                       ? options.world_count
                                       : static_cast<std::size_t>(options.top_of_world + 1);
    for (std::size_t index = 0; index < room_count; ++index) {
        if (options.world[index].number == room_vnum)
            return const_cast<room_data *>(&options.world[index]);
    }
    return nullptr;
}

room_data *
resolve_room_flag_helper_target(const JsRuntimeMutation &mutation,
                                const JsTriggerHelperMutationValidationContext &context) {
    if (context.request == nullptr || context.adapter_options == nullptr ||
        context.authority == nullptr)
        return nullptr;
    if (!has_persistent_setter_authority(*context.authority))
        return nullptr;

    int token_zone_vnum = -1;
    int room_vnum = -1;
    std::string token_secret;
    if (!parse_room_flag_helper_target_token(mutation.target_token, &token_zone_vnum, &room_vnum,
                                             &token_secret))
        return nullptr;
    if (token_zone_vnum != context.authority->target_zone)
        return nullptr;
    if (context.authority->target_token_secret.empty() ||
        token_secret != context.authority->target_token_secret)
        return nullptr;

    room_data *room = mutable_live_room_for_vnum(room_vnum, *context.adapter_options);
    if (room == nullptr ||
        !room_matches_mutation_authority(*room, *context.adapter_options, *context.authority))
        return nullptr;
    return room;
}

int room_zone_vnum_for_index(int zone_index, const JsGameAdapterOptions &options) {
    if (zone_index < 0 || options.zones == nullptr ||
        static_cast<std::size_t>(zone_index) >= options.zone_count)
        return -1;
    return options.zones[zone_index].number;
}

bool prepare_room_flag_helper_mutation(
    const JsRuntimeMutation &mutation, const JsTriggerHelperMutationValidationContext &context,
    std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations,
    JsTriggerHelperMutationTransactionStatus *status, std::string *diagnostic) {
    room_data *room = resolve_room_flag_helper_target(mutation, context);
    if (room == nullptr) {
        if (status != nullptr)
            *status = JsTriggerHelperMutationTransactionStatus::InvalidTarget;
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript helper mutation target rejected";
        return false;
    }

    const RoomFlagHelperFlag *flag = nullptr;
    if (!parse_room_flag_helper_arguments(mutation.arguments_json, &flag)) {
        if (status != nullptr)
            *status = JsTriggerHelperMutationTransactionStatus::InvalidArguments;
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript helper mutation arguments rejected";
        return false;
    }
    if (flag->requires_admin_override &&
        !has_room_flag_admin_override_authority(*context.authority)) {
        if (status != nullptr)
            *status = JsTriggerHelperMutationTransactionStatus::AuthorityRejected;
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript helper mutation authority rejected";
        return false;
    }

    if (pending_room_flag_mutations != nullptr) {
        PendingRoomFlagMutation pending;
        pending.room = room;
        pending.room_vnum = room->number;
        pending.room_zone_index = room->zone;
        pending.room_zone_vnum = room_zone_vnum_for_index(room->zone, *context.adapter_options);
        pending.flag_name = flag->name;
        pending.authority_class = flag->authority_class;
        pending.flag_bit = flag->bit;
        pending.add = mutation.operation == "room.flags.add";
        pending.requires_admin_override = flag->requires_admin_override;
        pending_room_flag_mutations->push_back(pending);
    }

    return true;
}

bool validate_helper_mutation_with_context(
    const JsRuntimeMutation &mutation, const JsTriggerHelperMutationTransactionOptions &options,
    std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations,
    JsTriggerHelperMutationTransactionStatus *status, std::string *diagnostic) {
    if (mutation.operation == "room.flags.add" || mutation.operation == "room.flags.remove") {
        if (options.validation_context == nullptr) {
            if (status != nullptr)
                *status = JsTriggerHelperMutationTransactionStatus::InvalidTarget;
            if (diagnostic != nullptr)
                *diagnostic = "JavaScript helper mutation target rejected";
            return false;
        }
        return prepare_room_flag_helper_mutation(mutation, *options.validation_context,
                                                 pending_room_flag_mutations, status, diagnostic);
    }
    return true;
}

std::string helper_operations_summary(const std::vector<JsRuntimeMutation> &helper_mutations) {
    std::set<std::string> operations;
    for (const JsRuntimeMutation &mutation : helper_mutations) {
        if (!mutation.operation.empty())
            operations.insert(mutation.operation);
    }

    std::ostringstream summary;
    bool first = true;
    for (const std::string &operation : operations) {
        if (!first)
            summary << ",";
        first = false;
        summary << operation;
    }
    return summary.str();
}

std::string command_operations_summary(const std::vector<JsRuntimeMutation> &command_mutations) {
    std::set<std::string> operations;
    for (const JsRuntimeMutation &mutation : command_mutations) {
        if (!mutation.operation.empty())
            operations.insert(mutation.operation);
    }

    std::ostringstream summary;
    bool first = true;
    for (const std::string &operation : operations) {
        if (!first)
            summary << ",";
        first = false;
        summary << operation;
    }
    return summary.str();
}

bool pending_room_flag_mutations_require_admin_override(
    const std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations) {
    if (pending_room_flag_mutations == nullptr)
        return false;
    for (const PendingRoomFlagMutation &mutation : *pending_room_flag_mutations) {
        if (mutation.requires_admin_override)
            return true;
    }
    return false;
}

std::string room_flag_authority_summary(
    const std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations) {
    if (pending_room_flag_mutations == nullptr || pending_room_flag_mutations->empty())
        return "";
    std::set<std::string> authority_classes;
    for (const PendingRoomFlagMutation &mutation : *pending_room_flag_mutations) {
        if (mutation.authority_class != nullptr)
            authority_classes.insert(mutation.authority_class);
    }

    std::ostringstream summary;
    bool first = true;
    for (const std::string &authority_class : authority_classes) {
        if (!first)
            summary << ",";
        first = false;
        summary << authority_class;
    }
    return summary.str();
}

std::string room_flag_admin_override_evidence(
    const JsTriggerMutationAuthorityContext *authority,
    const std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations) {
    if (authority == nullptr ||
        !pending_room_flag_mutations_require_admin_override(pending_room_flag_mutations))
        return "";
    return authority->room_flag_admin_override_evidence;
}

std::string
room_flag_audit_summary(const std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations) {
    if (pending_room_flag_mutations == nullptr || pending_room_flag_mutations->empty())
        return "";

    std::unordered_map<room_data *, long> staged_room_flags;
    std::ostringstream summary;
    for (std::size_t index = 0; index < pending_room_flag_mutations->size(); ++index) {
        const PendingRoomFlagMutation &mutation = (*pending_room_flag_mutations)[index];
        if (index != 0)
            summary << ";";
        long current_flags = 0;
        if (mutation.room != nullptr) {
            auto staged = staged_room_flags.find(mutation.room);
            if (staged == staged_room_flags.end()) {
                current_flags = mutation.room->room_flags;
            } else {
                current_flags = staged->second;
            }
        }
        const bool previous = IS_SET(current_flags, mutation.flag_bit);
        if (mutation.add) {
            current_flags |= mutation.flag_bit;
        } else {
            current_flags &= ~mutation.flag_bit;
        }
        if (mutation.room != nullptr)
            staged_room_flags[mutation.room] = current_flags;

        summary << (mutation.add ? "add" : "remove") << ":"
                << (mutation.flag_name != nullptr ? mutation.flag_name : "") << ":"
                << (mutation.authority_class != nullptr ? mutation.authority_class : "")
                << ":room=" << mutation.room_vnum << ":zoneVnum=" << mutation.room_zone_vnum
                << ":zoneIndex=" << mutation.room_zone_index
                << ":previous=" << (previous ? "true" : "false")
                << ":new=" << (mutation.add ? "true" : "false");
    }
    return summary.str();
}

bool has_persistent_setter_authority(const JsTriggerMutationAuthorityContext &authority) {
    return authority.allow_persistent_setter_mutations && !authority.builder_account_id.empty() &&
           authority.eligible_character_id > 0 && authority.target_zone >= 0 &&
           !authority.decision_evidence.empty();
}

bool has_room_flag_admin_override_authority(const JsTriggerMutationAuthorityContext &authority) {
    return authority.allow_room_flag_admin_override &&
           !authority.room_flag_admin_override_evidence.empty();
}

bool parse_id_number(const std::string &id, const char *prefix, int *number) {
    if (number == nullptr)
        return false;
    const std::string prefix_text(prefix);
    if (id.find(prefix_text) != 0)
        return false;
    const std::string suffix = id.substr(prefix_text.size());
    if (suffix.empty())
        return false;
    int parsed = 0;
    for (char ch : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        const int digit = ch - '0';
        if (parsed > (std::numeric_limits<int>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    *number = parsed;
    return true;
}

bool target_data_character_is_live(const target_data *target, const JsGameAdapterOptions &options) {
    return target != nullptr && target->type == TARGET_CHAR &&
           js_game_adapter_is_live_character(target->ptr.ch, options) &&
           GET_ABS_NUM(target->ptr.ch) == target->ch_num;
}

const char_data *character_from_polymorphic_target_data(const target_data *target,
                                                        const JsGameAdapterOptions &options) {
    return target_data_character_is_live(target, options) ? target->ptr.ch : nullptr;
}

const obj_data *object_from_polymorphic_target_data(const target_data *target,
                                                    const JsGameAdapterOptions &options) {
    if (target == nullptr || target->type != TARGET_OBJ ||
        !js_game_adapter_is_live_object(target->ptr.obj, options))
        return nullptr;
    return target->ptr.obj;
}

int room_index_for_polymorphic_room_pointer(const room_data *room,
                                            const JsGameAdapterOptions &options) {
    if (room == nullptr || options.world == nullptr)
        return NOWHERE;
    const std::size_t room_count = options.world_count > 0
                                       ? options.world_count
                                       : static_cast<std::size_t>(options.top_of_world + 1);
    for (std::size_t index = 0; index < room_count; ++index) {
        if (&options.world[index] == room)
            return static_cast<int>(index);
    }
    return NOWHERE;
}

int room_from_polymorphic_target_data(const target_data *target,
                                      const JsGameAdapterOptions &options) {
    if (target == nullptr || target->type != TARGET_ROOM)
        return NOWHERE;
    return room_index_for_polymorphic_room_pointer(target->ptr.room, options);
}

const char_data *polymorphic_character_for_command_id(const std::string &id,
                                                      const JsTriggerDispatchRequest &request,
                                                      const JsGameAdapterOptions &options) {
    if (id == "target" && request.context_input.target_character != nullptr &&
        js_game_adapter_is_live_character(request.context_input.target_character, options))
        return request.context_input.target_character;
    if (id == "targ1")
        return character_from_polymorphic_target_data(request.context_input.targ1, options);
    if (id == "targ2")
        return character_from_polymorphic_target_data(request.context_input.targ2, options);
    return nullptr;
}

const obj_data *polymorphic_object_for_command_id(const std::string &id,
                                                  const JsTriggerDispatchRequest &request,
                                                  const JsGameAdapterOptions &options) {
    if (id == "target" && request.context_input.target_object != nullptr &&
        js_game_adapter_is_live_object(request.context_input.target_object, options))
        return request.context_input.target_object;
    if (id == "targ1")
        return object_from_polymorphic_target_data(request.context_input.targ1, options);
    if (id == "targ2")
        return object_from_polymorphic_target_data(request.context_input.targ2, options);
    return nullptr;
}

int polymorphic_room_for_command_id(const std::string &id, const JsTriggerDispatchRequest &request,
                                    const JsGameAdapterOptions &options) {
    if (id == "target" && request.context_input.target_room >= 0 &&
        js_game_adapter_room_is_valid(request.context_input.target_room, options))
        return request.context_input.target_room;
    if (id == "targ1")
        return room_from_polymorphic_target_data(request.context_input.targ1, options);
    if (id == "targ2")
        return room_from_polymorphic_target_data(request.context_input.targ2, options);
    return NOWHERE;
}

obj_data *mutable_live_object_for_id(const JsRuntimeMutation &mutation,
                                     const JsTriggerDispatchRequest &request,
                                     const JsGameAdapterOptions &options) {
    if (mutation.target_id == "object" &&
        js_game_adapter_is_live_object(request.context_input.object, options))
        return const_cast<obj_data *>(request.context_input.object);
    if (mutation.target_id == "weapon" &&
        js_game_adapter_is_live_object(request.context_input.weapon, options))
        return const_cast<obj_data *>(request.context_input.weapon);
    return nullptr;
}

room_data *mutable_live_room_for_id(const JsRuntimeMutation &mutation,
                                    const JsTriggerDispatchRequest &request,
                                    const JsGameAdapterOptions &options) {
    if (mutation.target_id == "room" &&
        js_game_adapter_room_is_valid(request.context_input.room, options))
        return const_cast<room_data *>(&options.world[request.context_input.room]);

    int room_vnum = -1;
    if (!parse_id_number(mutation.target_id, "room:", &room_vnum) || options.world == nullptr)
        return nullptr;

    const std::size_t room_count = options.world_count > 0
                                       ? options.world_count
                                       : static_cast<std::size_t>(options.top_of_world + 1);
    for (std::size_t index = 0; index < room_count; ++index) {
        if (options.world[index].number == room_vnum)
            return const_cast<room_data *>(&options.world[index]);
    }
    return nullptr;
}

zone_data *mutable_live_zone_for_id(const JsRuntimeMutation &mutation,
                                    const JsTriggerDispatchRequest &request,
                                    const JsGameAdapterOptions &options) {
    if (mutation.target_id == "zone" &&
        js_game_adapter_room_is_valid(request.context_input.room, options)) {
        const int zone_index = options.world[request.context_input.room].zone;
        if (options.zones != nullptr && zone_index >= 0 &&
            static_cast<std::size_t>(zone_index) < options.zone_count) {
            return const_cast<zone_data *>(&options.zones[zone_index]);
        }
    }

    int zone_vnum = -1;
    if (!parse_id_number(mutation.target_id, "zone:", &zone_vnum) || options.zones == nullptr)
        return nullptr;

    for (std::size_t index = 0; index < options.zone_count; ++index) {
        if (options.zones[index].number == zone_vnum)
            return const_cast<zone_data *>(&options.zones[index]);
    }
    return nullptr;
}

char_data *live_character_role_for_command_id(const std::string &id,
                                              const JsTriggerDispatchRequest &request,
                                              const JsGameAdapterOptions &options) {
    const char_data *candidate = nullptr;
    if (id == "self")
        candidate = request.context_input.self;
    else if (id == "actor")
        candidate = request.context_input.actor;
    else if (id == "speaker")
        candidate = request.context_input.speaker;
    else if (id == "attacker")
        candidate = request.context_input.attacker;
    else if (id == "victim")
        candidate = request.context_input.victim;
    else if (id == "killer")
        candidate = request.context_input.killer;
    else if (id == "dying")
        candidate = request.context_input.dying;
    else if (id == "target" || id == "targ1" || id == "targ2")
        candidate = polymorphic_character_for_command_id(id, request, options);
    if (candidate == nullptr || !js_game_adapter_is_live_character(candidate, options))
        return nullptr;
    return const_cast<char_data *>(candidate);
}

int live_room_role_for_command_id(const std::string &id, const JsTriggerDispatchRequest &request,
                                  const JsGameAdapterOptions &options) {
    if (id == "room" && js_game_adapter_room_is_valid(request.context_input.room, options))
        return request.context_input.room;

    if (id == "target" || id == "targ1" || id == "targ2")
        return polymorphic_room_for_command_id(id, request, options);

    int room_vnum = -1;
    if (parse_id_number(id, "room:", &room_vnum)) {
        room_data *room = mutable_live_room_for_vnum(room_vnum, options);
        if (room == nullptr)
            return NOWHERE;
        const std::size_t room_count = options.world_count > 0
                                           ? options.world_count
                                           : static_cast<std::size_t>(options.top_of_world + 1);
        for (std::size_t index = 0; index < room_count; ++index) {
            if (&options.world[index] == room)
                return static_cast<int>(index);
        }
    }
    return NOWHERE;
}

obj_data *live_object_role_for_command_id(const std::string &id,
                                          const JsTriggerDispatchRequest &request,
                                          const JsGameAdapterOptions &options) {
    const obj_data *candidate = nullptr;
    if (id == "object")
        candidate = request.context_input.object;
    else if (id == "weapon")
        candidate = request.context_input.weapon;
    else if (id == "target" || id == "targ1" || id == "targ2")
        candidate = polymorphic_object_for_command_id(id, request, options);
    if (candidate == nullptr || !js_game_adapter_is_live_object(candidate, options))
        return nullptr;
    return const_cast<obj_data *>(candidate);
}

bool command_target_matches_authority(const char_data *character,
                                      const JsGameAdapterOptions &options,
                                      const JsTriggerMutationAuthorityContext &authority) {
    return character != nullptr && js_game_adapter_is_live_character(character, options) &&
           js_game_adapter_room_is_valid(character->in_room, options) &&
           room_matches_mutation_authority(options.world[character->in_room], options, authority);
}

bool command_target_matches_authority(int room, const JsGameAdapterOptions &options,
                                      const JsTriggerMutationAuthorityContext &authority) {
    return js_game_adapter_room_is_valid(room, options) &&
           room_matches_mutation_authority(options.world[room], options, authority);
}

bool object_is_directly_carried_by(const obj_data *object, const char_data *character) {
    if (object == nullptr || character == nullptr || object->carried_by != character)
        return false;
    for (const obj_data *node = character->carrying; node != nullptr; node = node->next_content) {
        if (node == object)
            return true;
    }
    return false;
}

std::string output_command_line(const std::string &text) { return text + "\n\r"; }

std::string say_command_line(const char_data *speaker, const std::string &text) {
    const char *name =
        (speaker != nullptr && speaker->player.name != nullptr) ? speaker->player.name : "Someone";
    return std::string(name) + " says '" + text + "'\n\r";
}

bool prepare_output_command_mutations(const std::vector<JsRuntimeMutation> &mutations,
                                      const JsTriggerDispatchRequest &request,
                                      const JsGameAdapterOptions &options,
                                      std::vector<PendingOutputCommand> *pending_output) {
    if (pending_output == nullptr)
        return false;
    pending_output->clear();
    for (const JsRuntimeMutation &mutation : mutations) {
        if (!runtime_mutation_kind_is_command(mutation) ||
            !script_command_mutation_is_output(mutation))
            continue;
        ScriptCommandArguments arguments;
        if (!parse_script_command_arguments(mutation, &arguments))
            return false;
        PendingOutputCommand pending;
        pending.operation = mutation.operation;
        pending.text = arguments.text;
        if (mutation.operation == "script.do_say") {
            pending.character =
                live_character_role_for_command_id(arguments.speaker_id, request, options);
            if (pending.character == nullptr ||
                !js_game_adapter_room_is_valid(pending.character->in_room, options))
                return false;
            pending.room = pending.character->in_room;
        } else if (mutation.operation == "script.send_to_char") {
            pending.character =
                live_character_role_for_command_id(arguments.target_id, request, options);
            if (pending.character == nullptr)
                return false;
        } else if (mutation.operation == "script.send_to_room") {
            pending.room = live_room_role_for_command_id(arguments.room_id, request, options);
            if (!js_game_adapter_room_is_valid(pending.room, options))
                return false;
        }
        pending_output->push_back(pending);
    }
    return true;
}

bool prepare_object_command_mutations(const std::vector<JsRuntimeMutation> &mutations,
                                      const JsTriggerDispatchRequest &request,
                                      const JsGameAdapterOptions &options,
                                      const JsTriggerMutationAuthorityContext &authority,
                                      std::vector<PendingObjectCommand> *pending_object_commands) {
    if (pending_object_commands == nullptr)
        return false;
    pending_object_commands->clear();
    for (const JsRuntimeMutation &mutation : mutations) {
        if (!runtime_mutation_kind_is_command(mutation) ||
            script_command_mutation_is_output(mutation) || mutation.operation == "script.do_wait")
            continue;
        ScriptCommandArguments arguments;
        if (!parse_script_command_arguments(mutation, &arguments))
            return false;

        PendingObjectCommand pending;
        pending.operation = mutation.operation;
        if (mutation.operation == "script.load_obj") {
            pending.real_object_index = real_object(arguments.vnum);
            if (pending.real_object_index < 0)
                return false;
            if (arguments.saw_load_target_id) {
                pending.target_character =
                    live_character_role_for_command_id(arguments.load_target_id, request, options);
                if (pending.target_character != nullptr) {
                    if (!command_target_matches_authority(pending.target_character, options,
                                                          authority))
                        return false;
                    pending.target = PendingObjectCommandTarget::Character;
                } else {
                    pending.target_room =
                        live_room_role_for_command_id(arguments.load_target_id, request, options);
                    if (!command_target_matches_authority(pending.target_room, options, authority))
                        return false;
                    pending.target = PendingObjectCommandTarget::Room;
                }
            }
        } else if (mutation.operation == "script.do_give") {
            pending.giver =
                live_character_role_for_command_id(arguments.giver_id, request, options);
            pending.recipient =
                live_character_role_for_command_id(arguments.recipient_id, request, options);
            pending.object = live_object_role_for_command_id(arguments.object_id, request, options);
            if (pending.giver == nullptr || pending.recipient == nullptr ||
                pending.object == nullptr ||
                !object_is_directly_carried_by(pending.object, pending.giver))
                return false;
            if (!command_target_matches_authority(pending.giver, options, authority) ||
                !command_target_matches_authority(pending.recipient, options, authority) ||
                !object_matches_mutation_authority(*pending.object, options, authority))
                return false;
        } else {
            return false;
        }
        pending_object_commands->push_back(pending);
    }
    return true;
}

bool prepare_wait_command_mutations(const std::vector<JsRuntimeMutation> &mutations,
                                    const JsTriggerDispatchRequest &request,
                                    const JsGameAdapterOptions &options,
                                    const JsTriggerMutationAuthorityContext &authority,
                                    std::vector<PendingWaitCommand> *pending_wait_commands) {
    if (pending_wait_commands == nullptr)
        return false;
    pending_wait_commands->clear();
    for (const JsRuntimeMutation &mutation : mutations) {
        if (!runtime_mutation_kind_is_command(mutation) || mutation.operation != "script.do_wait")
            continue;
        ScriptCommandArguments arguments;
        if (!parse_script_command_arguments(mutation, &arguments))
            return false;
        char_data *host = live_character_role_for_command_id("self", request, options);
        if (!command_target_matches_authority(host, options, authority))
            return false;
        pending_wait_commands->push_back({"self", arguments.pulses});
    }
    return true;
}

bool audit_command_mutations(const std::vector<JsRuntimeMutation> &command_mutations,
                             const JsTriggerDispatchRequest &request,
                             const JsTriggerMutationAuthorityContext &authority,
                             const JsTriggerHelperMutationTransactionOptions &helper_options,
                             std::string *diagnostic) {
    if (command_mutations.empty() || helper_options.command_audit_callback == nullptr)
        return true;

    JsTriggerCommandMutationAuditRequest audit_request;
    audit_request.mutation_count = command_mutations.size();
    audit_request.operations_summary = command_operations_summary(command_mutations);
    audit_request.host = request.host;
    audit_request.kind = request.kind;
    audit_request.legacy_value = request.legacy_value;
    audit_request.package_vnum = request.package_vnum;
    audit_request.package_id = request.package_id;
    audit_request.handler_name = request.handler_name;
    audit_request.authority_target_zone = authority.target_zone;
    std::string audit_diagnostic;
    if (!helper_options.command_audit_callback(audit_request, &audit_diagnostic,
                                               helper_options.command_audit_user_data)) {
        if (diagnostic != nullptr) {
            *diagnostic = audit_diagnostic.empty()
                              ? "JavaScript trigger command helper audit rejected"
                              : audit_diagnostic;
        }
        return false;
    }
    return true;
}

bool zone_matches_mutation_authority(const zone_data &zone,
                                     const JsTriggerMutationAuthorityContext &authority) {
    return zone.number == authority.target_zone;
}

bool zone_index_matches_mutation_authority(int zone_index, const JsGameAdapterOptions &options,
                                           const JsTriggerMutationAuthorityContext &authority) {
    if (options.zones == nullptr || zone_index < 0 ||
        static_cast<std::size_t>(zone_index) >= options.zone_count)
        return false;
    return zone_matches_mutation_authority(options.zones[zone_index], authority);
}

bool room_matches_mutation_authority(const room_data &room, const JsGameAdapterOptions &options,
                                     const JsTriggerMutationAuthorityContext &authority) {
    return zone_index_matches_mutation_authority(room.zone, options, authority);
}

bool object_is_worn_by_live_carrier(const obj_data *object, const char_data *carrier) {
    if (object == nullptr || carrier == nullptr)
        return false;
    return std::find(carrier->equipment, carrier->equipment + MAX_WEAR, object) !=
           carrier->equipment + MAX_WEAR;
}

bool object_is_carried_by_live_carrier(const obj_data *object, const char_data *carrier) {
    if (object == nullptr || carrier == nullptr)
        return false;
    for (const obj_data *carried = carrier->carrying; carried != nullptr;
         carried = carried->next_content) {
        if (carried == object)
            return true;
    }
    return false;
}

bool object_is_contained_by_live_container(const obj_data *object, const obj_data *container) {
    if (object == nullptr || container == nullptr)
        return false;
    for (const obj_data *contained = container->contains; contained != nullptr;
         contained = contained->next_content) {
        if (contained == object)
            return true;
    }
    return false;
}

int effective_object_room(const obj_data &object, const JsGameAdapterOptions &options,
                          int depth = 0) {
    if (depth > 8)
        return NOWHERE;
    if (js_game_adapter_room_is_valid(object.in_room, options))
        return object.in_room;
    if (object.in_obj != nullptr && js_game_adapter_is_live_object(object.in_obj, options) &&
        object_is_contained_by_live_container(&object, object.in_obj))
        return effective_object_room(*object.in_obj, options, depth + 1);
    if (object.carried_by != nullptr &&
        js_game_adapter_is_live_character(object.carried_by, options) &&
        (object_is_worn_by_live_carrier(&object, object.carried_by) ||
         object_is_carried_by_live_carrier(&object, object.carried_by)) &&
        js_game_adapter_room_is_valid(object.carried_by->in_room, options))
        return object.carried_by->in_room;
    return NOWHERE;
}

bool object_matches_mutation_authority(const obj_data &object, const JsGameAdapterOptions &options,
                                       const JsTriggerMutationAuthorityContext &authority) {
    const int room = effective_object_room(object, options);
    if (!js_game_adapter_room_is_valid(room, options))
        return false;
    return room_matches_mutation_authority(options.world[room], options, authority);
}

char **resolve_text_mutation_target(const JsRuntimeMutation &mutation,
                                    const JsTriggerDispatchRequest &request,
                                    const JsGameAdapterOptions &options,
                                    const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type == "object") {
        obj_data *object = mutable_live_object_for_id(mutation, request, options);
        if (object == nullptr)
            return nullptr;
        if (!object_matches_mutation_authority(*object, options, authority))
            return nullptr;
        if (mutation.property == "name")
            return &object->name;
        if (mutation.property == "description")
            return &object->description;
        if (mutation.property == "shortDescription")
            return &object->short_description;
        if (mutation.property == "actionDescription")
            return &object->action_description;
        return nullptr;
    }

    if (mutation.target_type == "room") {
        room_data *room = mutable_live_room_for_id(mutation, request, options);
        if (room == nullptr)
            return nullptr;
        if (!room_matches_mutation_authority(*room, options, authority))
            return nullptr;
        if (mutation.property == "name")
            return &room->name;
        if (mutation.property == "description")
            return &room->description;
        return nullptr;
    }

    if (mutation.target_type == "zone") {
        zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
        if (zone == nullptr)
            return nullptr;
        if (!zone_matches_mutation_authority(*zone, authority))
            return nullptr;
        if (mutation.property == "name")
            return &zone->name;
        if (mutation.property == "description")
            return &zone->description;
        if (mutation.property == "map")
            return &zone->map;
        return nullptr;
    }

    return nullptr;
}

struct PendingSymbolTarget {
    char *target = nullptr;
    bool redraw_world_map = false;
};

struct PendingCoordinateTarget {
    int *target = nullptr;
    bool redraw_world_map = false;
};

bool zone_uses_global_world_map(const zone_data *zone) {
    return zone_table != nullptr && top_of_zone_table >= 0 && zone >= zone_table &&
           zone <= zone_table + top_of_zone_table;
}

PendingSymbolTarget resolve_symbol_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "zone" || mutation.property != "symbol")
        return {};
    zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
    if (zone == nullptr || !zone_matches_mutation_authority(*zone, authority))
        return {};
    return {&zone->symbol, zone_uses_global_world_map(zone)};
}

PendingCoordinateTarget resolve_coordinate_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "zone" || (mutation.property != "x" && mutation.property != "y"))
        return {};
    zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
    if (zone == nullptr || !zone_matches_mutation_authority(*zone, authority))
        return {};
    return {mutation.property == "x" ? &zone->x : &zone->y, zone_uses_global_world_map(zone)};
}

PendingCoordinateTarget resolve_reset_mode_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "zone" || mutation.property != "resetMode")
        return {};
    zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
    if (zone == nullptr || !zone_matches_mutation_authority(*zone, authority))
        return {};
    return {&zone->reset_mode, false};
}

PendingCoordinateTarget resolve_lifespan_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "zone" || mutation.property != "lifespan")
        return {};
    zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
    if (zone == nullptr || !zone_matches_mutation_authority(*zone, authority))
        return {};
    return {&zone->lifespan, false};
}

PendingCoordinateTarget resolve_level_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.property != "level")
        return {};
    if (mutation.target_type == "zone") {
        zone_data *zone = mutable_live_zone_for_id(mutation, request, options);
        if (zone == nullptr || !zone_matches_mutation_authority(*zone, authority))
            return {};
        return {&zone->level, false};
    }
    if (mutation.target_type == "room") {
        room_data *room = mutable_live_room_for_id(mutation, request, options);
        if (room == nullptr || !room_matches_mutation_authority(*room, options, authority))
            return {};
        return {nullptr, false};
    }
    if (mutation.target_type == "object") {
        obj_data *object = mutable_live_object_for_id(mutation, request, options);
        if (object == nullptr || !object_matches_mutation_authority(*object, options, authority))
            return {};
        return {nullptr, false};
    }
    return {};
}

PendingCoordinateTarget resolve_rarity_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "object" || mutation.property != "rarity")
        return {};
    obj_data *object = mutable_live_object_for_id(mutation, request, options);
    if (object == nullptr || !object_matches_mutation_authority(*object, options, authority))
        return {};
    return {nullptr, false};
}

PendingCoordinateTarget resolve_sector_type_mutation_target(
    const JsRuntimeMutation &mutation, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    if (mutation.target_type != "room" || mutation.property != "sectorType")
        return {};
    room_data *room = mutable_live_room_for_id(mutation, request, options);
    if (room == nullptr || !room_matches_mutation_authority(*room, options, authority))
        return {};
    return {&room->sector_type, false};
}

bool prepare_text_mutations(const std::vector<JsRuntimeMutation> &mutations,
                            const JsTriggerDispatchRequest &request,
                            const JsGameAdapterOptions &options,
                            const JsTriggerMutationAuthorityContext &authority,
                            std::vector<PendingTextMutation> *pending) {
    if (pending == nullptr)
        return false;
    pending->clear();
    for (const JsRuntimeMutation &mutation : mutations) {
        if (!js_trigger_dispatch_supports_runtime_mutation(mutation))
            return false;
        if (!validate_mutation_value(mutation))
            return false;
        if (mutation.target_type == "zone" && mutation.property == "symbol") {
            PendingSymbolTarget target =
                resolve_symbol_mutation_target(mutation, request, options, authority);
            if (target.target == nullptr)
                return false;
            pending->push_back({nullptr, target.target, nullptr, nullptr, true,
                                target.redraw_world_map, mutation.value, 0});
            continue;
        }
        if (mutation.target_type == "zone" &&
            (mutation.property == "x" || mutation.property == "y")) {
            PendingCoordinateTarget target =
                resolve_coordinate_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (target.target == nullptr || !parse_coordinate_value(mutation.value, &parsed))
                return false;
            pending->push_back({nullptr, nullptr, target.target, nullptr, true,
                                target.redraw_world_map, mutation.value, parsed});
            continue;
        }
        if (mutation.target_type == "zone" && mutation.property == "resetMode") {
            PendingCoordinateTarget target =
                resolve_reset_mode_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (target.target == nullptr || !parse_reset_mode_value(mutation.value, &parsed))
                return false;
            pending->push_back(
                {nullptr, nullptr, target.target, nullptr, true, false, mutation.value, parsed});
            continue;
        }
        if (mutation.target_type == "zone" && mutation.property == "lifespan") {
            PendingCoordinateTarget target =
                resolve_lifespan_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (target.target == nullptr || !parse_lifespan_value(mutation.value, &parsed))
                return false;
            pending->push_back(
                {nullptr, nullptr, target.target, nullptr, true, false, mutation.value, parsed});
            continue;
        }
        if ((mutation.target_type == "zone" || mutation.target_type == "room" ||
             mutation.target_type == "object") &&
            mutation.property == "level") {
            PendingCoordinateTarget target =
                resolve_level_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (!parse_level_value(mutation.value, &parsed))
                return false;
            if (mutation.target_type == "room") {
                room_data *room = mutable_live_room_for_id(mutation, request, options);
                if (room == nullptr || !room_matches_mutation_authority(*room, options, authority))
                    return false;
                pending->push_back(
                    {nullptr, nullptr, nullptr, &room->level, true, false, mutation.value, parsed});
            } else if (mutation.target_type == "object") {
                obj_data *object = mutable_live_object_for_id(mutation, request, options);
                if (object == nullptr ||
                    !object_matches_mutation_authority(*object, options, authority))
                    return false;
                pending->push_back({nullptr, nullptr, nullptr, &object->obj_flags.level, true,
                                    false, mutation.value, parsed});
            } else {
                if (target.target == nullptr)
                    return false;
                pending->push_back({nullptr, nullptr, target.target, nullptr, true, false,
                                    mutation.value, parsed});
            }
            continue;
        }
        if (mutation.target_type == "object" && mutation.property == "rarity") {
            PendingCoordinateTarget target =
                resolve_rarity_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (!parse_rarity_value(mutation.value, &parsed))
                return false;
            obj_data *object = mutable_live_object_for_id(mutation, request, options);
            if (target.target != nullptr || object == nullptr ||
                !object_matches_mutation_authority(*object, options, authority))
                return false;
            pending->push_back({nullptr, nullptr, nullptr, &object->obj_flags.rarity, true, false,
                                mutation.value, parsed});
            continue;
        }
        if (mutation.target_type == "room" && mutation.property == "sectorType") {
            PendingCoordinateTarget target =
                resolve_sector_type_mutation_target(mutation, request, options, authority);
            int parsed = 0;
            if (target.target == nullptr || !parse_sector_type_value(mutation.value, &parsed))
                return false;
            pending->push_back(
                {nullptr, nullptr, target.target, nullptr, true, false, mutation.value, parsed});
            continue;
        }
        char **target = resolve_text_mutation_target(mutation, request, options, authority);
        if (target == nullptr)
            return false;
        pending->push_back(
            {target, nullptr, nullptr, nullptr, mutation.has_value, false, mutation.value, 0});
    }
    return true;
}

bool prepare_runtime_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority,
    const JsTriggerHelperMutationTransactionOptions &helper_options,
    std::vector<PendingTextMutation> *pending,
    std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations,
    std::vector<PendingObjectCommand> *pending_object_commands,
    std::vector<PendingWaitCommand> *pending_wait_commands,
    std::vector<PendingOutputCommand> *pending_output_commands, std::string *diagnostic,
    JsTriggerHelperMutationTransactionStatus *helper_status = nullptr) {
    std::vector<JsRuntimeMutation> setter_mutations;
    std::vector<JsRuntimeMutation> helper_mutations;
    std::vector<JsRuntimeMutation> command_mutations;
    setter_mutations.reserve(mutations.size());
    helper_mutations.reserve(mutations.size());
    command_mutations.reserve(mutations.size());

    for (const JsRuntimeMutation &mutation : mutations) {
        if (runtime_mutation_kind_is_helper(mutation)) {
            helper_mutations.push_back(mutation);
            continue;
        }
        if (runtime_mutation_kind_is_command(mutation)) {
            command_mutations.push_back(mutation);
            continue;
        }
        setter_mutations.push_back(mutation);
    }

    if (pending_room_flag_mutations != nullptr)
        pending_room_flag_mutations->clear();
    if (pending_object_commands != nullptr)
        pending_object_commands->clear();
    if (pending_wait_commands != nullptr)
        pending_wait_commands->clear();
    if (pending_output_commands != nullptr)
        pending_output_commands->clear();

    if (!prepare_text_mutations(setter_mutations, request, options, authority, pending)) {
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger mutation target rejected";
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::NotEvaluated;
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    if (!validate_script_command_mutations(command_mutations)) {
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger command helper mutation rejected";
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::NotEvaluated;
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    if (!prepare_output_command_mutations(command_mutations, request, options,
                                          pending_output_commands)) {
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger output command target rejected";
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::NotEvaluated;
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    if (!prepare_object_command_mutations(command_mutations, request, options, authority,
                                          pending_object_commands)) {
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger object command target rejected";
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::NotEvaluated;
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    if (!prepare_wait_command_mutations(command_mutations, request, options, authority,
                                        pending_wait_commands)) {
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger wait command target rejected";
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::NotEvaluated;
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    if (!audit_command_mutations(command_mutations, request, authority, helper_options,
                                 diagnostic)) {
        if (helper_status != nullptr)
            *helper_status = JsTriggerHelperMutationTransactionStatus::AuditRejected;
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        return false;
    }

    JsTriggerHelperMutationValidationContext helper_validation_context;
    helper_validation_context.request = &request;
    helper_validation_context.adapter_options = &options;
    helper_validation_context.authority = &authority;
    JsTriggerHelperMutationTransactionOptions runtime_helper_options = helper_options;
    runtime_helper_options.validation_context = &helper_validation_context;

    const JsTriggerHelperMutationTransactionResult helper_result =
        prepare_helper_mutation_transaction(helper_mutations, runtime_helper_options,
                                            pending_room_flag_mutations);
    if (helper_status != nullptr)
        *helper_status = helper_result.status;
    if (helper_result.status != JsTriggerHelperMutationTransactionStatus::Ok) {
        if (pending != nullptr)
            pending->clear();
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        if (pending_object_commands != nullptr)
            pending_object_commands->clear();
        if (pending_wait_commands != nullptr)
            pending_wait_commands->clear();
        if (pending_output_commands != nullptr)
            pending_output_commands->clear();
        if (diagnostic != nullptr)
            *diagnostic = "JavaScript trigger helper mutation rejected";
        return false;
    }

    return true;
}

void apply_text_mutations(const std::vector<PendingTextMutation> &mutations) {
    bool redraw_world_map = false;
    for (const PendingTextMutation &mutation : mutations) {
        if (mutation.char_target != nullptr) {
            *mutation.char_target = mutation.value[0];
            redraw_world_map = redraw_world_map || mutation.redraw_world_map;
            continue;
        }
        if (mutation.int_target != nullptr) {
            *mutation.int_target = mutation.int_value;
            redraw_world_map = redraw_world_map || mutation.redraw_world_map;
            continue;
        }
        if (mutation.byte_target != nullptr) {
            *mutation.byte_target = static_cast<unsigned char>(mutation.int_value);
            continue;
        }
        free(*mutation.target);
        *mutation.target = mutation.has_value ? str_dup(mutation.value.c_str()) : nullptr;
    }
    if (redraw_world_map)
        draw_map();
}

void apply_output_commands(const std::vector<PendingOutputCommand> &commands) {
    for (const PendingOutputCommand &command : commands) {
        if (command.operation == "script.do_say" && command.character != nullptr &&
            command.room != NOWHERE) {
            send_to_room(say_command_line(command.character, command.text).c_str(), command.room);
            continue;
        }
        if (command.operation == "script.send_to_char" && command.character != nullptr) {
            send_to_char(output_command_line(command.text).c_str(), command.character);
            continue;
        }
        if (command.operation == "script.send_to_room" && command.room != NOWHERE) {
            send_to_room(output_command_line(command.text).c_str(), command.room);
            continue;
        }
    }
}

void rollback_applied_object_commands(const std::vector<AppliedObjectCommand> &applied) {
    for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
        const AppliedObjectCommand &command = *it;
        if (command.object == nullptr)
            continue;
        if (command.kind == AppliedObjectCommandKind::LoadedObject) {
            extract_obj(command.object);
            continue;
        }
        if (command.kind == AppliedObjectCommandKind::GaveObject && command.giver != nullptr &&
            command.recipient != nullptr && command.object->carried_by == command.recipient &&
            object_is_directly_carried_by(command.object, command.recipient)) {
            obj_from_char(command.object);
            obj_to_char(command.object, command.giver);
        }
    }
}

bool apply_object_commands(const std::vector<PendingObjectCommand> &commands) {
    std::vector<AppliedObjectCommand> applied;
    auto fail = [&applied]() {
        rollback_applied_object_commands(applied);
        return false;
    };
    for (const PendingObjectCommand &command : commands) {
        if (command.operation == "script.load_obj") {
            if (command.target == PendingObjectCommandTarget::None)
                continue;
            obj_data *object = read_object(command.real_object_index, REAL);
            if (object == nullptr)
                return fail();
            if (command.target == PendingObjectCommandTarget::Character &&
                command.target_character != nullptr) {
                obj_to_char(object, command.target_character);
                applied.push_back(
                    {AppliedObjectCommandKind::LoadedObject, object, nullptr, nullptr});
                continue;
            }
            if (command.target == PendingObjectCommandTarget::Room &&
                command.target_room != NOWHERE) {
                obj_to_room(object, command.target_room);
                applied.push_back(
                    {AppliedObjectCommandKind::LoadedObject, object, nullptr, nullptr});
                continue;
            }
            extract_obj(object);
            return fail();
        }
        if (command.operation == "script.do_give") {
            if (command.giver == nullptr || command.recipient == nullptr ||
                command.object == nullptr)
                return fail();
            perform_give(command.giver, command.recipient, command.object);
            applied.push_back({AppliedObjectCommandKind::GaveObject, command.object, command.giver,
                               command.recipient});
            continue;
        }
        return fail();
    }
    return true;
}

bool object_commands_still_applicable(const std::vector<PendingObjectCommand> &commands,
                                      const JsGameAdapterOptions &options,
                                      const JsTriggerMutationAuthorityContext &authority) {
    for (const PendingObjectCommand &command : commands) {
        if (command.operation == "script.load_obj") {
            if (command.real_object_index < 0 || options.object_index == nullptr ||
                static_cast<std::size_t>(command.real_object_index) >= options.object_index_count)
                return false;
            if (command.target == PendingObjectCommandTarget::None)
                continue;
            if (command.target == PendingObjectCommandTarget::Character) {
                if (!command_target_matches_authority(command.target_character, options, authority))
                    return false;
                continue;
            }
            if (command.target == PendingObjectCommandTarget::Room) {
                if (!command_target_matches_authority(command.target_room, options, authority))
                    return false;
                continue;
            }
            return false;
        }
        if (command.operation == "script.do_give") {
            if (command.giver == nullptr || command.recipient == nullptr ||
                command.object == nullptr ||
                !object_is_directly_carried_by(command.object, command.giver))
                return false;
            if (!command_target_matches_authority(command.giver, options, authority) ||
                !command_target_matches_authority(command.recipient, options, authority) ||
                !object_matches_mutation_authority(*command.object, options, authority))
                return false;
            continue;
        }
        return false;
    }
    return true;
}

bool apply_wait_commands(const std::vector<PendingWaitCommand> &commands,
                         const JsTriggerDispatchRequest &request,
                         const JsGameAdapterOptions &options,
                         const JsTriggerMutationAuthorityContext &authority) {
    for (const PendingWaitCommand &command : commands) {
        char_data *character =
            live_character_role_for_command_id(command.character_id, request, options);
        if (!command_target_matches_authority(character, options, authority))
            return false;
        if (IS_SET(character->specials.affected_by, AFF_WAITING))
            continue;
        WAIT_STATE_BRIEF(character, command.pulses, 0, 0, 50, AFF_WAITING);
    }
    return true;
}

bool wait_commands_still_target_authorized_live_hosts(
    const std::vector<PendingWaitCommand> &commands, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &options, const JsTriggerMutationAuthorityContext &authority) {
    for (const PendingWaitCommand &command : commands) {
        char_data *character =
            live_character_role_for_command_id(command.character_id, request, options);
        if (!command_target_matches_authority(character, options, authority))
            return false;
    }
    return true;
}

bool room_flag_apply_preconditions_hold(const std::vector<PendingRoomFlagMutation> &mutations,
                                        const JsTriggerHelperMutationTransactionOptions &options) {
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const PendingRoomFlagMutation &mutation = mutations[index];
        if (mutation.room == nullptr || mutation.room->number != mutation.room_vnum ||
            mutation.room->zone != mutation.room_zone_index)
            return false;
        if (options.apply_precondition_callback != nullptr &&
            !options.apply_precondition_callback(index, options.apply_precondition_user_data))
            return false;
    }
    return true;
}

std::size_t runtime_object_command_mutation_count(const std::vector<JsRuntimeMutation> &mutations) {
    std::size_t count = 0;
    for (const JsRuntimeMutation &mutation : mutations) {
        if (runtime_mutation_kind_is_command(mutation) &&
            !script_command_mutation_is_output(mutation) && mutation.operation != "script.do_wait")
            ++count;
    }
    return count;
}

std::size_t runtime_wait_command_mutation_count(const std::vector<JsRuntimeMutation> &mutations) {
    std::size_t count = 0;
    for (const JsRuntimeMutation &mutation : mutations) {
        if (runtime_mutation_kind_is_command(mutation) && mutation.operation == "script.do_wait")
            ++count;
    }
    return count;
}

bool apply_room_flag_mutations(const std::vector<PendingRoomFlagMutation> &mutations,
                               const JsTriggerHelperMutationTransactionOptions &options,
                               std::size_t *applied_count) {
    std::vector<AppliedRoomFlagMutation> applied_mutations;
    applied_mutations.reserve(mutations.size());
    if (applied_count != nullptr)
        *applied_count = 0;

    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const PendingRoomFlagMutation &mutation = mutations[index];
        if (mutation.room == nullptr || mutation.room->number != mutation.room_vnum ||
            mutation.room->zone != mutation.room_zone_index) {
            for (std::vector<AppliedRoomFlagMutation>::reverse_iterator applied =
                     applied_mutations.rbegin();
                 applied != applied_mutations.rend(); ++applied) {
                applied->room->room_flags = applied->previous_flags;
            }
            return false;
        }

        applied_mutations.push_back({mutation.room, mutation.room->room_flags});
        if (mutation.add)
            SET_BIT(mutation.room->room_flags, mutation.flag_bit);
        else
            REMOVE_BIT(mutation.room->room_flags, mutation.flag_bit);
        if (applied_count != nullptr)
            *applied_count = index + 1;
    }

    return true;
}

JsTriggerDispatchResult make_error_result(const JsScriptPackage &package,
                                          const JsScriptTriggerBinding &binding,
                                          JsRuntimeStatus runtime_status, std::string diagnostic,
                                          std::size_t matched_package_count) {
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::Error;
    result.runtime_status = runtime_status;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = std::move(diagnostic);
    result.matched_package_count = matched_package_count;
    return result;
}

JsTriggerDispatchResult make_budget_exceeded_result(const JsScriptPackage &package,
                                                    const JsScriptTriggerBinding &binding,
                                                    std::size_t matched_package_count) {
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::BudgetExceeded;
    result.runtime_status = JsRuntimeStatus::Ok;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = "JavaScript trigger execution budget exceeded";
    result.matched_package_count = matched_package_count;
    return result;
}

JsTriggerDispatchResult make_depth_exceeded_result(const JsScriptPackage &package,
                                                   const JsScriptTriggerBinding &binding,
                                                   std::size_t matched_package_count) {
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::DepthExceeded;
    result.runtime_status = JsRuntimeStatus::Ok;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = "JavaScript trigger recursion depth exceeded";
    result.matched_package_count = matched_package_count;
    return result;
}

std::vector<const JsScriptPackage *>
find_requested_packages(const JsScriptPackageRegistry &registry,
                        const JsTriggerDispatchRequest &request) {
    if (request.package_vnum <= 0)
        return registry.find_packages_for_trigger(request.host, request.kind, request.legacy_value);

    const JsScriptPackage *package = registry.find_package_by_vnum(request.package_vnum);
    if (!package)
        return {};
    if (!registry.find_trigger_binding(package->vnum, request.host, request.kind,
                                       request.legacy_value)) {
        return {};
    }
    return {package};
}

class JsTriggerDispatchDepthScope {
  public:
    JsTriggerDispatchDepthScope(JsTriggerDispatchDepthGuard *guard,
                                const JsTriggerDispatchDepthLimits &limits)
        : m_guard(guard) {
        if (m_guard != nullptr)
            m_entered = m_guard->try_enter(limits);
    }

    ~JsTriggerDispatchDepthScope() {
        if (m_guard != nullptr && m_entered)
            m_guard->leave();
    }

    bool exceeded() const { return m_guard != nullptr && !m_entered; }

  private:
    JsTriggerDispatchDepthGuard *m_guard = nullptr;
    bool m_entered = false;
};

} // namespace

const char *js_trigger_dispatch_status_name(JsTriggerDispatchStatus status) {
    switch (status) {
    case JsTriggerDispatchStatus::NoMatch:
        return "no-match";
    case JsTriggerDispatchStatus::Allow:
        return "allow";
    case JsTriggerDispatchStatus::Block:
        return "block";
    case JsTriggerDispatchStatus::Error:
        return "error";
    case JsTriggerDispatchStatus::BudgetExceeded:
        return "budget-exceeded";
    case JsTriggerDispatchStatus::DepthExceeded:
        return "depth-exceeded";
    }
    return "unknown";
}

const char *js_trigger_helper_mutation_transaction_status_name(
    JsTriggerHelperMutationTransactionStatus status) {
    switch (status) {
    case JsTriggerHelperMutationTransactionStatus::NotEvaluated:
        return "not-evaluated";
    case JsTriggerHelperMutationTransactionStatus::Ok:
        return "ok";
    case JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope:
        return "unsupported-envelope";
    case JsTriggerHelperMutationTransactionStatus::UnknownOperation:
        return "unknown-operation";
    case JsTriggerHelperMutationTransactionStatus::InvalidTarget:
        return "invalid-target";
    case JsTriggerHelperMutationTransactionStatus::InvalidArguments:
        return "invalid-arguments";
    case JsTriggerHelperMutationTransactionStatus::AuthorityRejected:
        return "authority-rejected";
    case JsTriggerHelperMutationTransactionStatus::AuditRejected:
        return "audit-rejected";
    case JsTriggerHelperMutationTransactionStatus::ApplyRejected:
        return "apply-rejected";
    }
    return "unknown";
}

bool js_trigger_dispatch_supports_runtime_mutation(const JsRuntimeMutation &mutation) {
    if (runtime_mutation_kind_is_setter(mutation))
        return mutation.operation.empty() && mutation.target_token.empty() &&
               mutation.arguments_json.empty();
    if (runtime_mutation_kind_is_command(mutation))
        return mutation.target_type.empty() && mutation.target_id.empty() &&
               mutation.property.empty() && mutation.value_kind.empty() && !mutation.has_value &&
               mutation.value.empty() && mutation.target_token.empty() &&
               parse_script_command_arguments(mutation);
    return false;
}

namespace {

JsTriggerHelperMutationTransactionResult prepare_helper_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations,
    const JsTriggerHelperMutationTransactionOptions &options,
    std::vector<PendingRoomFlagMutation> *pending_room_flag_mutations) {
    JsTriggerHelperMutationTransactionResult result;
    std::vector<JsRuntimeMutation> helper_mutations;
    helper_mutations.reserve(mutations.size());
    std::vector<PendingRoomFlagMutation> local_pending_room_flag_mutations;
    std::vector<PendingRoomFlagMutation> *pending_for_validation =
        pending_room_flag_mutations != nullptr ? pending_room_flag_mutations
                                               : &local_pending_room_flag_mutations;
    if (pending_room_flag_mutations != nullptr)
        pending_room_flag_mutations->clear();

    for (const JsRuntimeMutation &mutation : mutations) {
        if (!runtime_mutation_kind_is_helper(mutation)) {
            result.status = JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope;
            result.diagnostic = "JavaScript helper mutation envelope rejected";
            if (pending_room_flag_mutations != nullptr)
                pending_room_flag_mutations->clear();
            return result;
        }
        if (mutation.operation.empty() || mutation.target_token.empty() ||
            mutation.arguments_json.empty()) {
            result.status = JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope;
            result.diagnostic = "JavaScript helper mutation envelope rejected";
            if (pending_room_flag_mutations != nullptr)
                pending_room_flag_mutations->clear();
            return result;
        }
        if (!registry_contains_operation(options.registry, mutation.operation)) {
            result.status = JsTriggerHelperMutationTransactionStatus::UnknownOperation;
            result.diagnostic = "JavaScript helper mutation operation is not supported";
            if (pending_room_flag_mutations != nullptr)
                pending_room_flag_mutations->clear();
            return result;
        }
        if (!validate_helper_mutation_with_context(mutation, options, pending_for_validation,
                                                   &result.status, &result.diagnostic)) {
            if (pending_room_flag_mutations != nullptr)
                pending_room_flag_mutations->clear();
            return result;
        }
        helper_mutations.push_back(mutation);
    }

    result.mutation_count = helper_mutations.size();
    if (helper_mutations.empty())
        return result;

    JsTriggerHelperMutationAuditRequest audit_request;
    audit_request.mutation_count = helper_mutations.size();
    audit_request.operations_summary = helper_operations_summary(helper_mutations);
    audit_request.requires_room_flag_admin_override =
        pending_room_flag_mutations_require_admin_override(pending_for_validation);
    audit_request.room_flag_admin_override_evidence = room_flag_admin_override_evidence(
        options.validation_context != nullptr ? options.validation_context->authority : nullptr,
        pending_for_validation);
    audit_request.room_flag_authority_summary = room_flag_authority_summary(pending_for_validation);
    audit_request.room_flag_audit_summary = room_flag_audit_summary(pending_for_validation);
    if (options.audit_callback == nullptr ||
        !options.audit_callback(audit_request, &result.diagnostic, options.audit_user_data)) {
        result.status = JsTriggerHelperMutationTransactionStatus::AuditRejected;
        result.diagnostic = "JavaScript helper mutation audit rejected";
        if (pending_room_flag_mutations != nullptr)
            pending_room_flag_mutations->clear();
        return result;
    }

    return result;
}

} // namespace

JsTriggerHelperMutationTransactionResult js_trigger_dispatch_prepare_helper_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations,
    const JsTriggerHelperMutationTransactionOptions &options) {
    return prepare_helper_mutation_transaction(mutations, options, nullptr);
}

JsTriggerHelperMutationOperationRegistry js_trigger_dispatch_room_flag_helper_operation_registry() {
    static const char *operation_names[2] = {};
    static bool initialized = false;
    static bool valid = false;
    if (!initialized) {
        const std::size_t operation_count = js_api_room_flag_helper_operation_count();
        if (operation_count == sizeof(operation_names) / sizeof(operation_names[0])) {
            const JsApiRoomFlagHelperOperation *operations = js_api_room_flag_helper_operations();
            valid = operations != nullptr && operations[0].operation_name != nullptr &&
                    operations[1].operation_name != nullptr &&
                    std::strcmp(operations[0].operation_name, "room.flags.add") == 0 &&
                    std::strcmp(operations[1].operation_name, "room.flags.remove") == 0;
            if (valid) {
                for (std::size_t index = 0; index < operation_count; ++index)
                    operation_names[index] = operations[index].operation_name;
            }
        }
        initialized = true;
    }
    if (!valid)
        return {nullptr, 0};
    return {operation_names, sizeof(operation_names) / sizeof(operation_names[0])};
}

JsTriggerRuntimeMutationTransactionProbeResult
js_trigger_dispatch_probe_runtime_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerMutationAuthorityContext &authority,
    const JsTriggerHelperMutationTransactionOptions &helper_options) {
    std::vector<PendingTextMutation> pending_mutations;
    std::vector<PendingRoomFlagMutation> pending_room_flag_mutations;
    std::vector<PendingObjectCommand> pending_object_commands;
    std::vector<PendingWaitCommand> pending_wait_commands;
    std::vector<PendingOutputCommand> pending_output_commands;
    JsTriggerRuntimeMutationTransactionProbeResult result;
    result.ok = prepare_runtime_mutation_transaction(
        mutations, request, adapter_options, authority, helper_options, &pending_mutations,
        &pending_room_flag_mutations, &pending_object_commands, &pending_wait_commands,
        &pending_output_commands, &result.diagnostic, &result.helper_status);
    result.prepared_setter_count = pending_mutations.size();
    result.prepared_helper_count = pending_room_flag_mutations.size();
    return result;
}

JsTriggerRuntimeMutationTransactionApplyResult
js_trigger_dispatch_apply_runtime_mutation_transaction(
    const std::vector<JsRuntimeMutation> &mutations, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerMutationAuthorityContext &authority,
    const JsTriggerHelperMutationTransactionOptions &helper_options) {
    std::vector<PendingTextMutation> pending_mutations;
    std::vector<PendingRoomFlagMutation> pending_room_flag_mutations;
    std::vector<PendingObjectCommand> pending_object_commands;
    std::vector<PendingWaitCommand> pending_wait_commands;
    std::vector<PendingOutputCommand> pending_output_commands;
    JsTriggerRuntimeMutationTransactionApplyResult result;
    result.ok = prepare_runtime_mutation_transaction(
        mutations, request, adapter_options, authority, helper_options, &pending_mutations,
        &pending_room_flag_mutations, &pending_object_commands, &pending_wait_commands,
        &pending_output_commands, &result.diagnostic, &result.helper_status);
    if (!result.ok)
        return result;

    if (runtime_helper_mutation_count(mutations) != pending_room_flag_mutations.size()) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger helper mutation apply rejected";
        return result;
    }

    if (!room_flag_apply_preconditions_hold(pending_room_flag_mutations, helper_options)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger helper mutation apply rejected";
        return result;
    }

    if (runtime_object_command_mutation_count(mutations) != pending_object_commands.size()) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger object command apply rejected";
        return result;
    }

    if (!object_commands_still_applicable(pending_object_commands, adapter_options, authority)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger object command apply rejected";
        return result;
    }

    if (runtime_wait_command_mutation_count(mutations) != pending_wait_commands.size()) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger wait command apply rejected";
        return result;
    }

    if (!wait_commands_still_target_authorized_live_hosts(pending_wait_commands, request,
                                                          adapter_options, authority)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger wait command apply rejected";
        return result;
    }

    if (!apply_object_commands(pending_object_commands)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger object command apply rejected";
        return result;
    }

    if (!apply_room_flag_mutations(pending_room_flag_mutations, helper_options,
                                   &result.applied_helper_count)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger helper mutation apply rejected";
        return result;
    }

    apply_text_mutations(pending_mutations);
    if (!apply_wait_commands(pending_wait_commands, request, adapter_options, authority)) {
        result.ok = false;
        result.helper_status = JsTriggerHelperMutationTransactionStatus::ApplyRejected;
        result.applied_setter_count = 0;
        result.applied_helper_count = 0;
        result.diagnostic = "JavaScript trigger wait command apply rejected";
        return result;
    }
    apply_output_commands(pending_output_commands);
    result.applied_setter_count = pending_mutations.size();
    result.ok = true;
    return result;
}

bool JsTriggerDispatchBudget::try_consume(int pulse, int package_vnum,
                                          const JsTriggerDispatchBudgetLimits &limits) {
    if (!m_has_current_pulse || m_current_pulse != pulse) {
        m_current_pulse = pulse;
        m_has_current_pulse = true;
        m_pulse_invocations = 0;
        m_package_invocations.clear();
    }

    if (limits.max_invocations_per_pulse > 0 &&
        m_pulse_invocations >= limits.max_invocations_per_pulse)
        return false;

    std::size_t &package_invocations = m_package_invocations[package_vnum];
    if (limits.max_invocations_per_package_per_pulse > 0 &&
        package_invocations >= limits.max_invocations_per_package_per_pulse)
        return false;

    ++m_pulse_invocations;
    ++package_invocations;
    return true;
}

void JsTriggerDispatchBudget::reset() {
    m_current_pulse = 0;
    m_has_current_pulse = false;
    m_pulse_invocations = 0;
    m_package_invocations.clear();
}

bool JsTriggerDispatchDepthGuard::would_exceed(const JsTriggerDispatchDepthLimits &limits) const {
    if (limits.max_dispatch_depth > 0 && m_current_depth >= limits.max_dispatch_depth)
        return true;
    return false;
}

bool JsTriggerDispatchDepthGuard::try_enter(const JsTriggerDispatchDepthLimits &limits) {
    if (would_exceed(limits))
        return false;
    ++m_current_depth;
    return true;
}

void JsTriggerDispatchDepthGuard::leave() {
    if (m_current_depth > 0)
        --m_current_depth;
}

void JsTriggerDispatchDepthGuard::reset() { m_current_depth = 0; }

std::size_t JsTriggerDispatchDepthGuard::current_depth() const { return m_current_depth; }

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry &registry,
                                                        const JsTriggerDispatchRequest &request,
                                                        const JsGameAdapterOptions &adapter_options,
                                                        const JsRuntimeLimits &limits) {
    JsTriggerDispatchOptions options;
    options.runtime_limits = limits;
    return js_trigger_dispatch_first_match(registry, request, adapter_options, options);
}

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry &registry,
                                                        const JsTriggerDispatchRequest &request,
                                                        const JsGameAdapterOptions &adapter_options,
                                                        const JsTriggerDispatchOptions &options) {
    const std::vector<const JsScriptPackage *> matches = find_requested_packages(registry, request);
    if (matches.empty())
        return {};

    const JsScriptPackage &package = *matches.front();
    const JsScriptTriggerBinding *binding = registry.find_trigger_binding(
        package.vnum, request.host, request.kind, request.legacy_value);
    if (!binding) {
        JsTriggerDispatchResult result;
        result.status = JsTriggerDispatchStatus::Error;
        result.runtime_status = JsRuntimeStatus::Error;
        result.package_vnum = package.vnum;
        result.package_id = package.package_id;
        result.diagnostic = "JavaScript trigger binding disappeared during dispatch";
        result.matched_package_count = matches.size();
        return result;
    }

    const JsScriptingManifestEntry *entry =
        find_js_scripting_manifest_entry(request.kind, request.legacy_value);
    if (!entry) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
                                 "JavaScript trigger manifest entry missing", matches.size());
    }
    if (!is_safe_handler_identifier(binding->handler_name)) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
                                 "JavaScript trigger handler name is not a safe identifier",
                                 matches.size());
    }

    JsGameAdapterContextInput context_input = request.context_input;
    context_input.trigger = make_trigger_fixture(*entry, request.host);
    const JsGameTriggerContextFixture context =
        js_game_adapter_context_fixture(context_input, adapter_options);
    if (!required_host_context_is_present(request.host, context)) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
                                 "JavaScript trigger context rejected: missing live " +
                                     std::string(js_script_package_host_name(request.host)),
                                 matches.size());
    }

    JsTriggerDispatchDepthScope depth_scope(options.depth_guard, options.depth_limits);
    if (depth_scope.exceeded())
        return make_depth_exceeded_result(package, *binding, matches.size());

    if (options.budget != nullptr &&
        !options.budget->try_consume(options.current_pulse, package.vnum, options.budget_limits)) {
        return make_budget_exceeded_result(package, *binding, matches.size());
    }

    JsGameRuntime runtime(options.runtime_limits);
    const std::string filename = "js-package-" + std::to_string(package.vnum) + ".js";
    const JsRuntimeEvalResult evaluation = runtime.evaluate_trigger_package_handler(
        package.compiled_javascript, binding->handler_name, context, filename.c_str());

    JsTriggerDispatchResult result;
    result.runtime_status = evaluation.status;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding->handler_name;
    result.diagnostic = evaluation.diagnostic;
    result.matched_package_count = matches.size();

    if (evaluation.status != JsRuntimeStatus::Ok) {
        result.status = JsTriggerDispatchStatus::Error;
    } else {
        if (runtime_mutations_require_persistent_authority(evaluation.mutations) &&
            !has_persistent_setter_authority(options.mutation_authority)) {
            result.status = JsTriggerDispatchStatus::Error;
            result.runtime_status = JsRuntimeStatus::Error;
            result.diagnostic =
                "JavaScript trigger persistent mutations require explicit builder authority";
            return result;
        }
        JsTriggerDispatchRequest mutation_request = request;
        mutation_request.package_vnum = package.vnum;
        mutation_request.package_id = package.package_id;
        mutation_request.handler_name = binding->handler_name;
        const JsTriggerRuntimeMutationTransactionApplyResult mutation_result =
            js_trigger_dispatch_apply_runtime_mutation_transaction(
                evaluation.mutations, mutation_request, adapter_options, options.mutation_authority,
                options.helper_mutation_options);
        if (!mutation_result.ok) {
            result.status = JsTriggerDispatchStatus::Error;
            result.runtime_status = JsRuntimeStatus::Error;
            result.diagnostic = mutation_result.diagnostic.empty()
                                    ? "JavaScript trigger mutation target rejected"
                                    : mutation_result.diagnostic;
            return result;
        }
        if (evaluation.value == JsRuntimeValue::Block) {
            result.status = JsTriggerDispatchStatus::Block;
        } else {
            result.status = JsTriggerDispatchStatus::Allow;
        }
    }

    return result;
}

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsRuntimeLimits &limits) {
    JsTriggerDispatchOptions options;
    options.runtime_limits = limits;
    return js_trigger_dispatch_live_first_match(service, request, adapter_options, options);
}

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsTriggerDispatchOptions &options) {
    return js_trigger_dispatch_first_match(service.m_registry, request, adapter_options, options);
}
