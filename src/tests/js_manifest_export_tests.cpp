#include "js_manifest_export.h"

#include "js_api_contract.h"
#include "js_api_struct_mapping.h"
#include "js_scripting_manifest.h"
#include "js_scripting_runtime_policy.h"
#include "json_utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

std::size_t count_occurrences(const std::string &text, const std::string &needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void expect_contains_field(const std::string &json, const char *key, const char *value) {
    std::string expected = "\"";
    expected += key;
    expected += "\":\"";
    expected += value;
    expected += "\"";
    EXPECT_NE(json.find(expected), std::string::npos) << expected;
}

void expect_contains_json_string(const std::string &json, const char *value) {
    std::string expected = "\"";
    expected += value;
    expected += "\"";
    EXPECT_NE(json.find(expected), std::string::npos) << expected;
}

void expect_contains_int_field(const std::string &json, const char *key, int value) {
    std::string expected = "\"";
    expected += key;
    expected += "\":";
    expected += std::to_string(value);
    EXPECT_NE(json.find(expected), std::string::npos) << expected;
}

void expect_contains_json_object(const std::string &json, const std::string &object) {
    EXPECT_NE(json.find(object), std::string::npos) << object;
}

bool contains_raw_cpp_type_text(const std::string &json) {
    const char *forbidden[] = {
        "char_data",   "obj_data", "room_data", "zone_data", "script_data",
        "script_head", "void*",    "char*",     "struct ",
    };

    for (const char *text : forbidden) {
        if (json.find(text) != std::string::npos)
            return true;
    }
    return false;
}

bool mapping_is_public(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.getter_status) != "internal-only";
}

std::size_t public_mapping_count() {
    std::size_t count = 0;
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        if (mapping_is_public(js_api_struct_field_mappings()[index]))
            ++count;
    }
    return count;
}

const char *public_owner_name(JsApiStructOwner owner) {
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

std::string trim_ascii_space(const std::string &value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split_context_fields(const char *fields) {
    std::vector<std::string> names;
    std::string text = fields ? fields : "";
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        const std::string name = trim_ascii_space(text.substr(start, end - start));
        if (!name.empty())
            names.push_back(name);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return names;
}

void expect_valid_json_object(const std::string &json) {
    json_utils::JsonReader reader(json);
    std::string error_message;
    EXPECT_TRUE(reader.parse_root_object(
        [](const std::string &, json_utils::JsonReader *nested_reader,
           std::string *nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
}

} // namespace

TEST(JsManifestExport, EmitsValidJsonForDefaultAndCompactExports) {
    expect_valid_json_object(js_export_trigger_manifest_json());
    expect_valid_json_object(js_export_api_contract_json());
    expect_valid_json_object(js_export_builder_manifest_json());

    JsManifestExportOptions options;
    options.include_documentation = false;
    expect_valid_json_object(js_export_trigger_manifest_json(options));
    expect_valid_json_object(js_export_api_contract_json(options));
    expect_valid_json_object(js_export_builder_manifest_json(options));
}

TEST(JsManifestExport, ExportsTriggerManifestMetadataAndEveryTrigger) {
    const std::string json = js_export_trigger_manifest_json();
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();

    expect_contains_field(json, "exportKind", "triggerManifest");
    expect_contains_int_field(json, "schemaVersion", 1);
    expect_contains_int_field(json, "triggerCatalogRevision", metadata.trigger_catalog_revision);
    expect_contains_field(json, "manifestChecksum", metadata.manifest_checksum);
    expect_contains_field(json, "selectedRuntimeName", metadata.selected_runtime_name);
    expect_contains_field(json, "selectedRuntimeVersion", metadata.selected_runtime_version);
    expect_contains_field(json, "runtimeFeatureFlags", metadata.runtime_feature_flags);
    EXPECT_EQ(count_occurrences(json, "\"legacyName\":\""),
              js_scripting_manifest_entry_count() + js_scripting_api_permission_entry_count());
    EXPECT_EQ(count_occurrences(json, "\"contextFields\":["), js_scripting_manifest_entry_count());
    EXPECT_EQ(count_occurrences(json, "\"contextFieldsText\":\""),
              js_scripting_manifest_entry_count());

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        expect_contains_field(json, "legacyName", entry.legacy_name);
        expect_contains_field(json, "handlerName", entry.javascript_handler_name);
        expect_contains_field(json, "kind", js_scripting_manifest_kind_name(entry.kind));
        expect_contains_field(json, "supportStatus",
                              js_scripting_support_status_name(entry.support_status));
        expect_contains_field(json, "builderStatus",
                              js_scripting_builder_status_name(entry.builder_status));
        for (const std::string &field_name : split_context_fields(entry.context_fields))
            expect_contains_json_string(json, field_name.c_str());
    }
}

TEST(JsManifestExport, ExportsDeniedLegacyApiPermissions) {
    const std::string json = js_export_trigger_manifest_json();

    EXPECT_NE(json.find("\"deniedLegacyApis\":["), std::string::npos);
    expect_contains_int_field(json, "deniedLegacyApiCount",
                              static_cast<int>(js_scripting_api_permission_entry_count()));

    for (std::size_t index = 0; index < js_scripting_api_permission_entry_count(); ++index) {
        const JsScriptingApiPermissionEntry &entry = js_scripting_api_permission_entries()[index];
        expect_contains_field(json, "legacyName", entry.legacy_name);
        expect_contains_field(json, "apiName", entry.javascript_api_name);
        expect_contains_field(json, "status",
                              js_scripting_api_permission_status_name(entry.status));
    }
}

TEST(JsManifestExport, ExportsApiContractMetadataAndEveryTypeMember) {
    const std::string json = js_export_api_contract_json();
    const JsApiContractMetadata &metadata = js_api_contract_metadata();

    expect_contains_field(json, "exportKind", "apiContract");
    expect_contains_int_field(json, "schemaVersion", 1);
    expect_contains_int_field(json, "apiRevision", metadata.api_revision);
    expect_contains_field(json, "contractChecksum", metadata.contract_checksum);
    expect_contains_field(json, "generatedTypingsVersion", metadata.generated_typings_version);
    expect_contains_field(json, "documentationVersion", metadata.documentation_version);
    expect_contains_int_field(json, "typeCount", static_cast<int>(js_api_contract_type_count()));
    expect_contains_int_field(json, "structFieldMappingCount",
                              static_cast<int>(public_mapping_count()));

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        expect_contains_field(json, "name", type.name);
        expect_contains_field(json, "kind", js_api_type_kind_name(type.kind));
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            expect_contains_field(json, "name", member.name);
            expect_contains_field(json, "kind", js_api_member_kind_name(member.kind));
            expect_contains_field(json, "sideEffect", js_api_side_effect_name(member.side_effect));
            expect_contains_field(json, "status", js_api_member_status_name(member.status));
            expect_contains_field(json, "permission", member.permission);
        }
    }
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (!mapping_is_public(mapping))
            continue;
        expect_contains_field(json, "owner", public_owner_name(mapping.owner));
        expect_contains_field(
            json, "fieldId",
            (std::string(public_owner_name(mapping.owner)) + "." + mapping.js_property).c_str());
        expect_contains_field(json, "property", mapping.js_property);
        expect_contains_field(json, "getterName", mapping.getter_name);
        expect_contains_field(json, "setterName", mapping.setter_name);
        expect_contains_field(json, "typeName", mapping.type_name);
        expect_contains_field(json, "getterStatus", mapping.getter_status);
        expect_contains_field(json, "setterStatus", mapping.setter_status);
        expect_contains_field(json, "sideEffect", mapping.side_effect);
    }
    expect_contains_field(json, "owner", "Character");
    expect_contains_field(json, "owner", "GameObject");
    expect_contains_field(json, "owner", "Room");
    expect_contains_field(json, "owner", "Zone");
    expect_contains_field(json, "fieldId", "GameObject.vnum");
    expect_contains_field(json, "property", "vnum");
    expect_contains_field(json, "getterStatus", "implemented-read-only-getter");
    EXPECT_EQ(json.find("\"getterStatus\":\"internal-only\""), std::string::npos);
    expect_contains_field(json, "setterStatus", "planned-validated-setter");
    expect_contains_field(json, "setterStatus", "unsupported");
    EXPECT_EQ(json.find("\"property\":\"ownerId\""), std::string::npos);
    EXPECT_EQ(json.find("\"getterName\":\"getOwners\""), std::string::npos);
    EXPECT_EQ(json.find("\"getterStatus\":\"internal-only\""), std::string::npos);
    expect_contains_json_object(
        json,
        "{\"owner\":\"GameObject\",\"fieldId\":\"GameObject.vnum\",\"property\":\"vnum\","
        "\"getterName\":\"getVnum\",\"setterName\":\"setVnum\",\"typeName\":\"number | null\","
        "\"nullable\":true,\"getterStatus\":\"implemented-read-only-getter\","
        "\"setterStatus\":\"unsupported\",\"sideEffect\":\"none\",\"getterCallable\":true,"
        "\"setterCallable\":false,\"documentationOnly\":true,\"getterDocs\":\"Returns the object "
        "prototype vnum when the prototype can be resolved; otherwise null.\",\"setterDocs\":"
        "\"Changing a live object's prototype from JavaScript is "
        "unsupported.\",\"notes\":\"Already "
        "exposed as GameObject.vnum for implemented snapshots.\"}");
    expect_contains_json_object(
        json,
        "{\"owner\":\"Zone\",\"fieldId\":\"Zone.name\",\"property\":\"name\","
        "\"getterName\":\"getName\",\"setterName\":\"setName\",\"typeName\":\"string\","
        "\"nullable\":false,\"getterStatus\":\"implemented-read-only-getter\","
        "\"setterStatus\":\"planned-validated-setter\",\"sideEffect\":\"mutation\","
        "\"getterCallable\":true,\"setterCallable\":false,\"documentationOnly\":true,"
        "\"getterDocs\":\"Returns the zone display name.\",\"setterDocs\":\"Sets the zone display "
        "name after ownership, length, and sanitization checks.\",\"notes\":\"Already exposed as "
        "Zone.name for reads.\"}");
    expect_contains_json_object(
        json,
        "{\"owner\":\"GameObject\",\"fieldId\":\"GameObject.description\","
        "\"property\":\"description\",\"getterName\":\"getDescription\","
        "\"setterName\":\"setDescription\",\"typeName\":\"string\",\"nullable\":false,"
        "\"getterStatus\":\"deferred\",\"setterStatus\":\"planned-validated-setter\","
        "\"sideEffect\":\"mutation\",\"getterCallable\":false,\"setterCallable\":false,"
        "\"documentationOnly\":true,\"getterDocs\":\"Planned getter for the room-visible object "
        "description.\",\"setterDocs\":\"Sets the room-visible object description after length, "
        "ownership, and sanitization checks.\",\"notes\":\"String ownership must be explicit.\"}");
}

TEST(JsManifestExport, ExportsCombinedBuilderCompatibilityBlock) {
    const std::string json = js_export_builder_manifest_json();
    const JsScriptingManifestMetadata &trigger_metadata = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api_metadata = js_api_contract_metadata();
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();

    expect_contains_field(json, "exportKind", "builderManifest");
    EXPECT_NE(json.find("\"compatibility\":{"), std::string::npos);
    EXPECT_NE(json.find("\"runtimeSafety\":{"), std::string::npos);
    EXPECT_NE(json.find("\"triggerManifest\":{"), std::string::npos);
    EXPECT_NE(json.find("\"apiContract\":{"), std::string::npos);
    expect_contains_int_field(json, "packageFormatVersion",
                              trigger_metadata.package_format_version);
    expect_contains_field(json, "triggerManifestChecksum", trigger_metadata.manifest_checksum);
    expect_contains_int_field(json, "apiRevision", api_metadata.api_revision);
    expect_contains_field(json, "apiContractChecksum", api_metadata.contract_checksum);
    expect_contains_field(json, "runtimeName", trigger_metadata.selected_runtime_name);
    expect_contains_field(json, "runtimeVersion", trigger_metadata.selected_runtime_version);
    expect_contains_int_field(json, "memoryLimitBytes",
                              static_cast<int>(policy.runtime_limits.memory_limit_bytes));
    expect_contains_int_field(json, "stackLimitBytes",
                              static_cast<int>(policy.runtime_limits.stack_limit_bytes));
    expect_contains_int_field(json, "instructionBudget",
                              static_cast<int>(policy.runtime_limits.instruction_budget));
    expect_contains_int_field(json, "maxInvocationsPerPulse",
                              static_cast<int>(policy.budget_limits.max_invocations_per_pulse));
    expect_contains_int_field(
        json, "maxInvocationsPerPackagePerPulse",
        static_cast<int>(policy.budget_limits.max_invocations_per_package_per_pulse));
    expect_contains_int_field(json, "maxDispatchDepth",
                              static_cast<int>(policy.depth_limits.max_dispatch_depth));
    expect_contains_int_field(json, "maxDispatchFailureLogsPerPulse",
                              static_cast<int>(policy.max_dispatch_failure_logs_per_pulse));
    expect_contains_json_string(json, "budget-exceeded");
    expect_contains_json_string(json, "depth-exceeded");
    expect_contains_json_string(json, "registry-not-ready");
}

TEST(JsManifestExport, CanOmitDocumentationFieldsForCompactConsumers) {
    JsManifestExportOptions options;
    options.include_documentation = false;

    const std::string trigger_json = js_export_trigger_manifest_json(options);
    const std::string api_json = js_export_api_contract_json(options);
    const std::string builder_json = js_export_builder_manifest_json(options);

    EXPECT_EQ(trigger_json.find("\"notes\":"), std::string::npos);
    EXPECT_EQ(trigger_json.find("\"reason\":"), std::string::npos);
    EXPECT_EQ(api_json.find("\"docs\":"), std::string::npos);
    EXPECT_EQ(api_json.find("\"getterDocs\":"), std::string::npos);
    EXPECT_EQ(api_json.find("\"setterDocs\":"), std::string::npos);
    EXPECT_EQ(api_json.find("\"notes\":"), std::string::npos);
    EXPECT_EQ(builder_json.find("\"docs\":"), std::string::npos);
    EXPECT_EQ(builder_json.find("\"getterDocs\":"), std::string::npos);
    EXPECT_EQ(builder_json.find("\"setterDocs\":"), std::string::npos);
    EXPECT_EQ(builder_json.find("\"notes\":"), std::string::npos);
    EXPECT_EQ(builder_json.find("\"reason\":"), std::string::npos);
    expect_contains_int_field(api_json, "structFieldMappingCount",
                              static_cast<int>(public_mapping_count()));
    EXPECT_NE(api_json.find("\"structFieldMappings\":["), std::string::npos);
    expect_contains_json_object(
        api_json,
        "{\"owner\":\"GameObject\",\"fieldId\":\"GameObject.vnum\",\"property\":\"vnum\","
        "\"getterName\":\"getVnum\",\"setterName\":\"setVnum\",\"typeName\":\"number | null\","
        "\"nullable\":true,\"getterStatus\":\"implemented-read-only-getter\","
        "\"setterStatus\":\"unsupported\",\"sideEffect\":\"none\",\"getterCallable\":true,"
        "\"setterCallable\":false,\"documentationOnly\":true}");
    EXPECT_EQ(api_json.find("\"sourceField\":"), std::string::npos);
    EXPECT_NE(builder_json.find("\"failureLoggingPolicy\":"), std::string::npos);
    EXPECT_NE(builder_json.find("\"runtimeSafety\":{"), std::string::npos);
    EXPECT_NE(builder_json.find("\"dispatchStatuses\":[\"no-match\",\"allow\",\"block\",\"error\","
                                "\"budget-exceeded\",\"depth-exceeded\"]"),
              std::string::npos);
    EXPECT_NE(builder_json.find("\"loggedFailureStatuses\":[\"registry-not-ready\","
                                "\"stale-registry\",\"error\",\"budget-exceeded\","
                                "\"depth-exceeded\"]"),
              std::string::npos);
}

TEST(JsManifestExport, BoundsDocumentationAndAvoidsRawCppTypes) {
    const std::string json = js_export_builder_manifest_json();

    EXPECT_FALSE(contains_raw_cpp_type_text(json));
    EXPECT_EQ(json.find('\n'), std::string::npos);
    EXPECT_EQ(json.find('\r'), std::string::npos);

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        EXPECT_LE(std::strlen(entry.dispatch_order), JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES);
        EXPECT_LE(std::strlen(entry.notes), JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES);
    }
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        EXPECT_LE(std::strlen(type.docs), JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES);
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            EXPECT_LE(std::strlen(type.members[member_index].docs),
                      JS_MANIFEST_EXPORT_MAX_DOCUMENTATION_BYTES);
        }
    }
}
