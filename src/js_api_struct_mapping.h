#ifndef JS_API_STRUCT_MAPPING_H
#define JS_API_STRUCT_MAPPING_H

#include <cstddef>

enum class JsApiStructOwner {
    CharData,
    ObjData,
    RoomData,
    ZoneData,
};

struct JsApiStructFieldMapping {
    JsApiStructOwner owner;
    const char *source_struct;
    const char *source_field;
    const char *js_property;
    const char *getter_name;
    const char *setter_name;
    const char *type_name;
    bool nullable;
    const char *getter_status;
    const char *setter_status;
    const char *getter_docs;
    const char *setter_docs;
    const char *side_effect;
    const char *notes;
};

struct JsApiDeferredHelperPlan {
    const char *id;
    int priority;
    const char *title;
    const char *source_fields;
    const char *helper_shape;
    const char *authority_policy;
    const char *offline_parity;
    const char *test_focus;
    const char *notes;
};

struct JsApiRawSetterGuardrail {
    JsApiStructOwner owner;
    const char *source_field;
    const char *setter_name;
    const char *helper_plan_id;
    const char *reason;
};

struct JsApiHelperMutationGateRequirement {
    const char *id;
    const char *title;
    const char *server_policy;
    const char *offline_policy;
    const char *test_policy;
};

struct JsApiRoomFlagHelperOperation {
    const char *operation_name;
    const char *helper_name;
    const char *allowed_flags;
    const char *excluded_flags;
    const char *builder_zone_flags;
    const char *admin_only_flags;
    const char *blocked_flags;
    const char *authority_policy;
    const char *side_effect_policy;
    const char *audit_policy;
    const char *diagnostic_policy;
    const char *rollback_policy;
    const char *offline_policy;
    const char *test_focus;
};

struct JsApiCharacterMovementHelperOperation {
    const char *operation_name;
    const char *helper_name;
    const char *legacy_commands;
    const char *target_policy;
    const char *authority_policy;
    const char *side_effect_policy;
    const char *audit_policy;
    const char *diagnostic_policy;
    const char *rollback_policy;
    const char *offline_policy;
    const char *test_focus;
};

struct JsApiCombatEffectHelperOperation {
    const char *operation_name;
    const char *helper_name;
    const char *legacy_commands;
    const char *target_policy;
    const char *authority_policy;
    const char *side_effect_policy;
    const char *audit_policy;
    const char *diagnostic_policy;
    const char *rollback_policy;
    const char *offline_policy;
    const char *test_focus;
};

struct JsApiEquipmentHelperOperation {
    const char *operation_name;
    const char *helper_name;
    const char *legacy_commands;
    const char *target_policy;
    const char *authority_policy;
    const char *side_effect_policy;
    const char *audit_policy;
    const char *diagnostic_policy;
    const char *rollback_policy;
    const char *offline_policy;
    const char *test_focus;
};

const JsApiStructFieldMapping *js_api_struct_field_mappings();
std::size_t js_api_struct_field_mapping_count();
const char *js_api_struct_owner_name(JsApiStructOwner owner);
std::size_t js_api_struct_field_mapping_count_for_owner(JsApiStructOwner owner);
const JsApiStructFieldMapping *find_js_api_struct_field_mapping(JsApiStructOwner owner,
                                                                const char *source_field);
const JsApiDeferredHelperPlan *js_api_deferred_helper_plans();
std::size_t js_api_deferred_helper_plan_count();
const JsApiRawSetterGuardrail *js_api_raw_setter_guardrails();
std::size_t js_api_raw_setter_guardrail_count();
const JsApiHelperMutationGateRequirement *js_api_helper_mutation_gate_requirements();
std::size_t js_api_helper_mutation_gate_requirement_count();
const JsApiRoomFlagHelperOperation *js_api_room_flag_helper_operations();
std::size_t js_api_room_flag_helper_operation_count();
const JsApiCharacterMovementHelperOperation *js_api_character_movement_helper_operations();
std::size_t js_api_character_movement_helper_operation_count();
const JsApiCombatEffectHelperOperation *js_api_combat_effect_helper_operations();
std::size_t js_api_combat_effect_helper_operation_count();
const JsApiEquipmentHelperOperation *js_api_equipment_helper_operations();
std::size_t js_api_equipment_helper_operation_count();

#endif
