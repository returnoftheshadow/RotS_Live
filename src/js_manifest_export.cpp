#include "js_manifest_export.h"

#include "js_api_contract.h"
#include "js_api_struct_mapping.h"
#include "js_scripting_manifest.h"
#include "js_scripting_runtime_policy.h"
#include "json_utils.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char *safe_string(const char *value) { return value ? value : ""; }

void append_json_string(std::ostringstream &out, const char *value, std::size_t max_bytes) {
    out << '"';
    const char *text = safe_string(value);
    std::string bounded;
    for (std::size_t index = 0; text[index] != '\0' && index < max_bytes; ++index)
        bounded += text[index];
    std::string escaped;
    json_utils::append_escaped_json_string(escaped, bounded);
    out << escaped;
    out << '"';
}

void append_json_string(std::ostringstream &out, const char *value) {
    append_json_string(out, value, std::string::npos);
}

void append_key(std::ostringstream &out, const char *key) {
    append_json_string(out, key);
    out << ':';
}

void append_string_field(std::ostringstream &out, const char *key, const char *value) {
    append_key(out, key);
    append_json_string(out, value);
}

void append_documentation_field(std::ostringstream &out, const char *key, const char *value) {
    append_key(out, key);
    append_json_string(out, value, JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES);
}

void append_int_field(std::ostringstream &out, const char *key, int value) {
    append_key(out, key);
    out << value;
}

void append_size_field(std::ostringstream &out, const char *key, std::size_t value) {
    append_key(out, key);
    out << value;
}

void append_bool_field(std::ostringstream &out, const char *key, bool value) {
    append_key(out, key);
    out << (value ? "true" : "false");
}

void append_host_flags(std::ostringstream &out, unsigned flags) {
    out << '[';
    bool first = true;
    auto append_host = [&](unsigned flag, const char *name) {
        if ((flags & flag) == 0)
            return;
        if (!first)
            out << ',';
        append_json_string(out, name);
        first = false;
    };

    append_host(JS_SCRIPTING_HOST_CHARACTER, "character");
    append_host(JS_SCRIPTING_HOST_OBJECT, "object");
    append_host(JS_SCRIPTING_HOST_ROOM, "room");
    append_host(JS_SCRIPTING_HOST_MUDLLE_MOBILE, "mudlleMobile");
    out << ']';
}

std::string trim_ascii_space(const std::string &value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split_context_fields(const char *fields) {
    std::vector<std::string> names;
    std::string text = safe_string(fields);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        std::string name = trim_ascii_space(text.substr(start, end - start));
        if (!name.empty())
            names.push_back(name);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return names;
}

void append_context_field_names(std::ostringstream &out, const char *fields) {
    const std::vector<std::string> names = split_context_fields(fields);
    out << '[';
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0)
            out << ',';
        append_json_string(out, names[index].c_str());
    }
    out << ']';
}

void append_trigger_metadata(std::ostringstream &out) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    out << '{';
    append_int_field(out, "schemaVersion", metadata.schema_version);
    out << ',';
    append_int_field(out, "packageFormatVersion", metadata.package_format_version);
    out << ',';
    append_int_field(out, "triggerCatalogRevision", metadata.trigger_catalog_revision);
    out << ',';
    append_string_field(out, "manifestChecksum", metadata.manifest_checksum);
    out << ',';
    append_string_field(out, "apiVersion", metadata.api_version);
    out << ',';
    append_string_field(out, "selectedRuntimeName", metadata.selected_runtime_name);
    out << ',';
    append_string_field(out, "selectedRuntimeVersion", metadata.selected_runtime_version);
    out << ',';
    append_string_field(out, "minimumSupportedRuntimeVersion",
                        metadata.minimum_supported_runtime_version);
    out << ',';
    append_string_field(out, "runtimeFeatureFlags", metadata.runtime_feature_flags);
    out << ',';
    append_string_field(out, "generatedTypingsVersion", metadata.generated_typings_version);
    out << '}';
}

void append_api_metadata(std::ostringstream &out, const JsManifestExportOptions &options) {
    const JsApiContractMetadata &metadata = js_api_contract_metadata();
    out << '{';
    append_int_field(out, "schemaVersion", metadata.schema_version);
    out << ',';
    append_int_field(out, "apiRevision", metadata.api_revision);
    out << ',';
    append_string_field(out, "apiVersion", metadata.api_version);
    out << ',';
    append_string_field(out, "contractChecksum", metadata.contract_checksum);
    out << ',';
    append_string_field(out, "generatedTypingsVersion", metadata.generated_typings_version);
    out << ',';
    append_string_field(out, "documentationVersion", metadata.documentation_version);
    out << ',';
    append_string_field(out, "minimumTriggerCatalogRevision",
                        metadata.minimum_trigger_catalog_revision);
    if (options.include_documentation) {
        out << ',';
        append_documentation_field(out, "notes", metadata.notes);
    }
    out << '}';
}

void append_dispatch_statuses(std::ostringstream &out) {
    out << '[';
    const JsTriggerDispatchStatus statuses[] = {
        JsTriggerDispatchStatus::NoMatch,        JsTriggerDispatchStatus::Allow,
        JsTriggerDispatchStatus::Block,          JsTriggerDispatchStatus::Error,
        JsTriggerDispatchStatus::BudgetExceeded, JsTriggerDispatchStatus::DepthExceeded,
    };
    for (std::size_t index = 0; index < sizeof(statuses) / sizeof(statuses[0]); ++index) {
        if (index > 0)
            out << ',';
        append_json_string(out, js_trigger_dispatch_status_name(statuses[index]));
    }
    out << ']';
}

void append_runtime_safety_policy(std::ostringstream &out, const JsManifestExportOptions &options) {
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();
    out << '{';
    append_size_field(out, "memoryLimitBytes", policy.runtime_limits.memory_limit_bytes);
    out << ',';
    append_size_field(out, "stackLimitBytes", policy.runtime_limits.stack_limit_bytes);
    out << ',';
    append_size_field(out, "instructionBudget", policy.runtime_limits.instruction_budget);
    out << ',';
    append_size_field(out, "maxInvocationsPerPulse",
                      policy.budget_limits.max_invocations_per_pulse);
    out << ',';
    append_size_field(out, "maxInvocationsPerPackagePerPulse",
                      policy.budget_limits.max_invocations_per_package_per_pulse);
    out << ',';
    append_size_field(out, "maxDispatchDepth", policy.depth_limits.max_dispatch_depth);
    out << ',';
    append_size_field(out, "maxDispatchFailureLogsPerPulse",
                      policy.max_dispatch_failure_logs_per_pulse);
    out << ',';
    append_key(out, "dispatchStatuses");
    append_dispatch_statuses(out);
    out << ',';
    append_key(out, "loggedFailureStatuses");
    out << '[';
    append_json_string(out, "registry-not-ready");
    out << ',';
    append_json_string(out, "stale-registry");
    out << ',';
    append_json_string(out, "error");
    out << ',';
    append_json_string(out, "budget-exceeded");
    out << ',';
    append_json_string(out, "depth-exceeded");
    out << ']';
    out << ',';
    append_documentation_field(out, "failureLoggingPolicy", policy.failure_logging_policy);
    out << '}';
}

const char *public_struct_owner_name(JsApiStructOwner owner) {
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

bool mapping_is_public(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.getter_status) != "internal-only";
}

bool mapping_setter_is_callable(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.setter_status) == "implemented-validated-setter";
}

std::string public_mapping_field_id(const JsApiStructFieldMapping &mapping) {
    return std::string(public_struct_owner_name(mapping.owner)) + "." + mapping.js_property;
}

void append_struct_field_mappings(std::ostringstream &out, const JsManifestExportOptions &options) {
    out << '[';
    bool first = true;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (!mapping_is_public(mapping))
            continue;
        if (!first)
            out << ',';
        first = false;
        out << '{';
        append_string_field(out, "owner", public_struct_owner_name(mapping.owner));
        out << ',';
        append_string_field(out, "fieldId", public_mapping_field_id(mapping).c_str());
        out << ',';
        append_string_field(out, "property", mapping.js_property);
        out << ',';
        append_string_field(out, "getterName", mapping.getter_name);
        out << ',';
        append_string_field(out, "setterName", mapping.setter_name);
        out << ',';
        append_string_field(out, "typeName", mapping.type_name);
        out << ',';
        append_bool_field(out, "nullable", mapping.nullable);
        out << ',';
        append_string_field(out, "getterStatus", mapping.getter_status);
        out << ',';
        append_string_field(out, "setterStatus", mapping.setter_status);
        out << ',';
        append_string_field(out, "sideEffect", mapping.side_effect);
        out << ',';
        append_bool_field(out, "getterCallable",
                          std::string(mapping.getter_status) == "implemented-read-only-getter");
        out << ',';
        append_bool_field(out, "setterCallable", mapping_setter_is_callable(mapping));
        out << ',';
        append_bool_field(out, "documentationOnly", !mapping_setter_is_callable(mapping));
        if (options.include_documentation) {
            out << ',';
            append_documentation_field(out, "getterDocs", mapping.getter_docs);
            out << ',';
            append_documentation_field(out, "setterDocs", mapping.setter_docs);
            out << ',';
            append_documentation_field(out, "notes", mapping.notes);
        }
        out << '}';
    }
    out << ']';
}

std::size_t public_struct_field_mapping_count() {
    std::size_t count = 0;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        if (mapping_is_public(js_api_struct_field_mappings()[index]))
            ++count;
    }
    return count;
}

} // namespace

std::string js_export_trigger_manifest_json(const JsManifestExportOptions &options) {
    std::ostringstream out;
    out << '{';
    append_int_field(out, "schemaVersion", 1);
    out << ',';
    append_string_field(out, "exportKind", "triggerManifest");
    out << ',';
    append_key(out, "metadata");
    append_trigger_metadata(out);
    out << ',';
    append_size_field(out, "triggerCount", js_scripting_manifest_entry_count());
    out << ',';
    append_key(out, "triggers");
    out << '[';
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        if (index > 0)
            out << ',';
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        out << '{';
        append_string_field(out, "kind", js_scripting_manifest_kind_name(entry.kind));
        out << ',';
        append_int_field(out, "legacyValue", entry.legacy_value);
        out << ',';
        append_string_field(out, "legacyName", entry.legacy_name);
        out << ',';
        append_string_field(out, "handlerName", entry.javascript_handler_name);
        out << ',';
        append_string_field(out, "supportStatus",
                            js_scripting_support_status_name(entry.support_status));
        out << ',';
        append_string_field(out, "builderStatus",
                            js_scripting_builder_status_name(entry.builder_status));
        out << ',';
        append_key(out, "hostTypes");
        append_host_flags(out, entry.host_flags);
        out << ',';
        append_bool_field(out, "roomOwnedScriptsPublishable", entry.room_owned_scripts_publishable);
        out << ',';
        append_bool_field(out, "mudlleCallMaskRequired", entry.mudlle_call_mask_required);
        out << ',';
        append_bool_field(out, "blocksGameplay", entry.blocks_gameplay);
        out << ',';
        append_bool_field(out, "consumesSpecialResult", entry.consumes_special_result);
        out << ',';
        append_string_field(out, "exceptionPolicy",
                            js_scripting_exception_policy_name(entry.exception_policy));
        out << ',';
        append_documentation_field(out, "dispatchOrder", entry.dispatch_order);
        out << ',';
        append_key(out, "contextFields");
        append_context_field_names(out, entry.context_fields);
        out << ',';
        append_string_field(out, "contextFieldsText", entry.context_fields);
        if (options.include_documentation) {
            out << ',';
            append_documentation_field(out, "notes", entry.notes);
        }
        out << '}';
    }
    out << ']';
    out << ',';
    append_size_field(out, "deniedLegacyApiCount", js_scripting_api_permission_entry_count());
    out << ',';
    append_key(out, "deniedLegacyApis");
    out << '[';
    for (std::size_t index = 0; index < js_scripting_api_permission_entry_count(); ++index) {
        if (index > 0)
            out << ',';
        const JsScriptingApiPermissionEntry &entry = js_scripting_api_permission_entries()[index];
        out << '{';
        append_int_field(out, "legacyCommand", entry.legacy_command);
        out << ',';
        append_string_field(out, "legacyName", entry.legacy_name);
        out << ',';
        append_string_field(out, "apiName", entry.javascript_api_name);
        out << ',';
        append_string_field(out, "status", js_scripting_api_permission_status_name(entry.status));
        if (options.include_documentation) {
            out << ',';
            append_documentation_field(out, "reason", entry.reason);
        }
        out << '}';
    }
    out << ']';
    out << '}';
    return out.str();
}

std::string js_export_api_contract_json(const JsManifestExportOptions &options) {
    std::ostringstream out;
    out << '{';
    append_int_field(out, "schemaVersion", 1);
    out << ',';
    append_string_field(out, "exportKind", "apiContract");
    out << ',';
    append_key(out, "metadata");
    append_api_metadata(out, options);
    out << ',';
    append_size_field(out, "typeCount", js_api_contract_type_count());
    out << ',';
    append_size_field(out, "structFieldMappingCount", public_struct_field_mapping_count());
    out << ',';
    append_key(out, "types");
    out << '[';
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        if (type_index > 0)
            out << ',';
        const JsApiType &type = js_api_contract_types()[type_index];
        out << '{';
        append_string_field(out, "name", type.name);
        out << ',';
        append_string_field(out, "kind", js_api_type_kind_name(type.kind));
        out << ',';
        append_string_field(out, "extends", type.extends);
        if (options.include_documentation) {
            out << ',';
            append_documentation_field(out, "docs", type.docs);
        }
        out << ',';
        append_size_field(out, "memberCount", type.member_count);
        out << ',';
        append_key(out, "members");
        out << '[';
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            if (member_index > 0)
                out << ',';
            const JsApiMember &member = type.members[member_index];
            out << '{';
            append_string_field(out, "name", member.name);
            out << ',';
            append_string_field(out, "kind", js_api_member_kind_name(member.kind));
            out << ',';
            append_string_field(out, "typeName", member.type_name);
            out << ',';
            append_string_field(out, "returnType", member.return_type);
            out << ',';
            append_bool_field(out, "nullable", member.nullable);
            out << ',';
            append_bool_field(out, "requiresLiveHandle", member.requires_live_handle);
            out << ',';
            append_string_field(out, "sideEffect", js_api_side_effect_name(member.side_effect));
            out << ',';
            append_string_field(out, "status", js_api_member_status_name(member.status));
            out << ',';
            append_string_field(out, "permission", member.permission);
            if (options.include_documentation) {
                out << ',';
                append_documentation_field(out, "docs", member.docs);
            }
            out << '}';
        }
        out << ']';
        out << '}';
    }
    out << ']';
    out << ',';
    append_key(out, "structFieldMappings");
    append_struct_field_mappings(out, options);
    out << '}';
    return out.str();
}

std::string js_export_builder_manifest_json(const JsManifestExportOptions &options) {
    std::ostringstream out;
    const JsScriptingManifestMetadata &trigger_metadata = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api_metadata = js_api_contract_metadata();

    out << '{';
    append_int_field(out, "schemaVersion", 1);
    out << ',';
    append_string_field(out, "exportKind", "builderManifest");
    out << ',';
    append_key(out, "compatibility");
    out << '{';
    append_int_field(out, "packageFormatVersion", trigger_metadata.package_format_version);
    out << ',';
    append_int_field(out, "triggerCatalogRevision", trigger_metadata.trigger_catalog_revision);
    out << ',';
    append_string_field(out, "triggerManifestChecksum", trigger_metadata.manifest_checksum);
    out << ',';
    append_int_field(out, "apiRevision", api_metadata.api_revision);
    out << ',';
    append_string_field(out, "apiContractChecksum", api_metadata.contract_checksum);
    out << ',';
    append_string_field(out, "runtimeName", trigger_metadata.selected_runtime_name);
    out << ',';
    append_string_field(out, "runtimeVersion", trigger_metadata.selected_runtime_version);
    out << ',';
    append_string_field(out, "runtimeFeatureFlags", trigger_metadata.runtime_feature_flags);
    out << ',';
    append_string_field(out, "generatedTypingsVersion", trigger_metadata.generated_typings_version);
    out << ',';
    append_string_field(out, "documentationVersion", api_metadata.documentation_version);
    out << '}';
    out << ',';
    append_key(out, "runtimeSafety");
    append_runtime_safety_policy(out, options);
    out << ',';
    append_key(out, "triggerManifest");
    out << js_export_trigger_manifest_json(options);
    out << ',';
    append_key(out, "apiContract");
    out << js_export_api_contract_json(options);
    out << '}';
    return out.str();
}
