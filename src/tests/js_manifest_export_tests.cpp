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

bool mapping_setter_is_callable(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.setter_status) == "implemented-validated-setter";
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

std::string expected_mapping_json_object(const JsApiStructFieldMapping &mapping) {
    const std::string owner = public_owner_name(mapping.owner);
    return "{\"owner\":\"" + json_utils::escape_json_string(owner) + "\",\"fieldId\":\"" +
        json_utils::escape_json_string(owner + "." + mapping.js_property) +
        "\",\"property\":\"" + json_utils::escape_json_string(mapping.js_property) +
        "\",\"getterName\":\"" + json_utils::escape_json_string(mapping.getter_name) +
        "\",\"setterName\":\"" + json_utils::escape_json_string(mapping.setter_name) +
        "\",\"typeName\":\"" + json_utils::escape_json_string(mapping.type_name) +
        "\",\"nullable\":" + (mapping.nullable ? "true" : "false") +
        ",\"getterStatus\":\"" + json_utils::escape_json_string(mapping.getter_status) +
        "\",\"setterStatus\":\"" + json_utils::escape_json_string(mapping.setter_status) +
        "\",\"sideEffect\":\"" + json_utils::escape_json_string(mapping.side_effect) +
        "\",\"getterCallable\":" +
        (std::string(mapping.getter_status) == "implemented-read-only-getter" ? "true" : "false") +
        ",\"setterCallable\":" + (mapping_setter_is_callable(mapping) ? "true" : "false") +
        ",\"documentationOnly\":" + (mapping_setter_is_callable(mapping) ? "false" : "true") +
        ",\"getterDocs\":\"" +
        json_utils::escape_json_string(mapping.getter_docs) +
        "\",\"setterDocs\":\"" + json_utils::escape_json_string(mapping.setter_docs) +
        "\",\"notes\":\"" + json_utils::escape_json_string(mapping.notes) + "\"}";
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
    expect_contains_field(json, "setterStatus", "implemented-validated-setter");
    expect_contains_field(json, "setterStatus", "deferred");
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
    const struct {
        JsApiStructOwner owner;
        const char *source_field;
    } implemented_setters[] = {
        {JsApiStructOwner::ZoneData, "name"},
        {JsApiStructOwner::ZoneData, "description"},
        {JsApiStructOwner::ZoneData, "map"},
        {JsApiStructOwner::ZoneData, "x"},
        {JsApiStructOwner::ZoneData, "y"},
        {JsApiStructOwner::ZoneData, "symbol"},
        {JsApiStructOwner::ZoneData, "reset_mode"},
        {JsApiStructOwner::ZoneData, "lifespan"},
        {JsApiStructOwner::ZoneData, "level"},
        {JsApiStructOwner::RoomData, "level"},
        {JsApiStructOwner::ObjData, "name"},
        {JsApiStructOwner::ObjData, "description"},
        {JsApiStructOwner::ObjData, "short_description"},
        {JsApiStructOwner::ObjData, "action_description"},
        {JsApiStructOwner::RoomData, "name"},
        {JsApiStructOwner::RoomData, "description"},
    };
    for (const auto &entry : implemented_setters) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(entry.owner, entry.source_field);
        ASSERT_NE(mapping, nullptr) << entry.source_field;
        EXPECT_STREQ(mapping->setter_status, "implemented-validated-setter")
            << entry.source_field;
        expect_contains_json_object(json, expected_mapping_json_object(*mapping));
    }
    const JsApiStructFieldMapping *zone_y =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "y");
    ASSERT_NE(zone_y, nullptr);
    const std::string zone_y_object = expected_mapping_json_object(*zone_y);
    EXPECT_NE(zone_y_object.find("\"setterCallable\":true"), std::string::npos);
    EXPECT_NE(zone_y_object.find("\"documentationOnly\":false"), std::string::npos);
    for (const char *fragment :
        {"0 through 25", "target-scoped persistent setter authority", "redraw the map"}) {
        EXPECT_NE(zone_y_object.find(fragment), std::string::npos) << fragment;
    }
    const JsApiStructFieldMapping *zone_reset_mode =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "reset_mode");
    ASSERT_NE(zone_reset_mode, nullptr);
    const std::string zone_reset_mode_object = expected_mapping_json_object(*zone_reset_mode);
    EXPECT_NE(zone_reset_mode_object.find("\"setterCallable\":true"), std::string::npos);
    EXPECT_NE(zone_reset_mode_object.find("\"documentationOnly\":false"), std::string::npos);
    for (const char *fragment :
        {"0 through 3", "target-scoped persistent setter authority", "legacy mixed"}) {
        EXPECT_NE(zone_reset_mode_object.find(fragment), std::string::npos) << fragment;
    }
    const JsApiStructFieldMapping *zone_lifespan =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "lifespan");
    ASSERT_NE(zone_lifespan, nullptr);
    const std::string zone_lifespan_object = expected_mapping_json_object(*zone_lifespan);
    EXPECT_NE(zone_lifespan_object.find("\"setterCallable\":true"), std::string::npos);
    EXPECT_NE(zone_lifespan_object.find("\"documentationOnly\":false"), std::string::npos);
    for (const char *fragment :
        {"1 through 10080", "target-scoped persistent setter authority", "reset scheduling"}) {
        EXPECT_NE(zone_lifespan_object.find(fragment), std::string::npos) << fragment;
    }
    const JsApiStructFieldMapping *zone_level =
        find_js_api_struct_field_mapping(JsApiStructOwner::ZoneData, "level");
    ASSERT_NE(zone_level, nullptr);
    const std::string zone_level_object = expected_mapping_json_object(*zone_level);
    EXPECT_NE(zone_level_object.find("\"setterCallable\":true"), std::string::npos);
    EXPECT_NE(zone_level_object.find("\"documentationOnly\":false"), std::string::npos);
    for (const char *fragment :
        {"0 through 100", "target-scoped persistent setter authority",
            "builder-facing zone metadata"}) {
        EXPECT_NE(zone_level_object.find(fragment), std::string::npos) << fragment;
    }
    const JsApiStructFieldMapping *room_level =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "level");
    ASSERT_NE(room_level, nullptr);
    const std::string room_level_object = expected_mapping_json_object(*room_level);
    EXPECT_NE(room_level_object.find("\"setterCallable\":true"), std::string::npos);
    EXPECT_NE(room_level_object.find("\"documentationOnly\":false"), std::string::npos);
    for (const char *fragment :
        {"0 through 100", "target-scoped persistent setter authority",
            "same-level room filtering"}) {
        EXPECT_NE(room_level_object.find(fragment), std::string::npos) << fragment;
    }
    expect_contains_json_object(
        json,
        "{\"owner\":\"Room\",\"fieldId\":\"Room.level\",\"property\":\"level\","
        "\"getterName\":\"getLevel\",\"setterName\":\"setLevel\",\"typeName\":\"number\","
        "\"nullable\":false,\"getterStatus\":\"implemented-read-only-getter\","
        "\"setterStatus\":\"implemented-validated-setter\",\"sideEffect\":\"mutation\","
        "\"getterCallable\":true,\"setterCallable\":true,\"documentationOnly\":false,"
        "\"getterDocs\":\"Returns the room level value.\","
        "\"setterDocs\":\"Updates the invocation snapshot room level after integer and 0 through "
        "100 inclusive bounds checks, rejects negative values, values above 100, and fractional "
        "or other non-integer values, and applies to live owned memory only when dispatch "
        "provides target-scoped persistent setter authority. This changes the persisted "
        "room-file scalar value used by legacy same-level room filtering.\",\"notes\":\"Persistent "
        "application requires target-scoped dispatch mutation authority context.\"}");
    const JsApiStructFieldMapping *room_alignment =
        find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, "alignment");
    ASSERT_NE(room_alignment, nullptr);
    const std::string room_alignment_object = expected_mapping_json_object(*room_alignment);
    EXPECT_NE(room_alignment_object.find("\"setterCallable\":false"), std::string::npos);
    EXPECT_NE(room_alignment_object.find("\"documentationOnly\":true"), std::string::npos);
    for (const char *fragment :
        {"room file writer", "does not copy alignment", "persistence/editing semantics"}) {
        EXPECT_NE(room_alignment_object.find(fragment), std::string::npos) << fragment;
    }
    expect_contains_json_object(json, room_alignment_object);
    expect_contains_json_object(
        json,
        "{\"owner\":\"Zone\",\"fieldId\":\"Zone.level\",\"property\":\"level\","
        "\"getterName\":\"getLevel\",\"setterName\":\"setLevel\",\"typeName\":\"number\","
        "\"nullable\":false,\"getterStatus\":\"implemented-read-only-getter\","
        "\"setterStatus\":\"implemented-validated-setter\",\"sideEffect\":\"mutation\","
        "\"getterCallable\":true,\"setterCallable\":true,\"documentationOnly\":false,"
        "\"getterDocs\":\"Returns the zone level value.\","
        "\"setterDocs\":\"Updates the invocation snapshot zone level after integer and 0 through "
        "100 inclusive bounds checks, rejects negative values, values above 100, and fractional "
        "or other non-integer values, and applies to live owned memory only when dispatch "
        "provides target-scoped persistent setter authority. This changes the persisted "
        "builder-facing zone metadata value shown by legacy zone inspection and shaping "
        "paths.\",\"notes\":\"Persistent application requires target-scoped dispatch mutation "
        "authority context.\"}");
    const struct {
        JsApiStructOwner owner;
        const char *source_field;
    } promoted_second_group[] = {
        {JsApiStructOwner::ObjData, "action_description"},
        {JsApiStructOwner::ZoneData, "description"},
        {JsApiStructOwner::ZoneData, "map"},
        {JsApiStructOwner::ZoneData, "lifespan"},
        {JsApiStructOwner::ZoneData, "age"},
        {JsApiStructOwner::ZoneData, "top"},
        {JsApiStructOwner::ZoneData, "x"},
        {JsApiStructOwner::ZoneData, "y"},
        {JsApiStructOwner::ZoneData, "symbol"},
        {JsApiStructOwner::ZoneData, "min_level_look"},
        {JsApiStructOwner::ZoneData, "reset_mode"},
    };

    for (const auto &entry : promoted_second_group) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(entry.owner, entry.source_field);
        ASSERT_NE(mapping, nullptr) << entry.source_field;
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter") << entry.source_field;
        expect_contains_json_object(json, expected_mapping_json_object(*mapping));
    }

    const char *room_value_domain_fields[] = { "sector_type", "room_flags", "light" };
    for (const char *source_field : room_value_domain_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::RoomData, source_field);
        ASSERT_NE(mapping, nullptr) << source_field;
        EXPECT_STREQ(mapping->getter_status, "implemented-read-only-getter") << source_field;
        expect_contains_json_object(json, expected_mapping_json_object(*mapping));
    }

    const JsApiStructFieldMapping *object_flags =
        find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, "obj_flags");
    ASSERT_NE(object_flags, nullptr);
    EXPECT_STREQ(object_flags->getter_status, "implemented-read-only-getter");
    expect_contains_json_object(json, expected_mapping_json_object(*object_flags));

    const char *deferred_object_fields[] = {
        "affected",
        "ex_description",
        "in_obj",
        "contains",
        "touched",
    };
    for (const char *source_field : deferred_object_fields) {
        const JsApiStructFieldMapping *mapping =
            find_js_api_struct_field_mapping(JsApiStructOwner::ObjData, source_field);
        ASSERT_NE(mapping, nullptr) << source_field;
        EXPECT_STREQ(mapping->getter_status, "deferred") << source_field;
        expect_contains_json_object(json, expected_mapping_json_object(*mapping));
        EXPECT_NE(expected_mapping_json_object(*mapping).find("\"getterCallable\":false"),
            std::string::npos)
            << source_field;
        EXPECT_NE(expected_mapping_json_object(*mapping).find("\"setterCallable\":false"),
            std::string::npos)
            << source_field;
    }

    const char *internal_object_properties[] = {
        "ownerId",
        "nextContent",
        "loadedBy",
    };
    for (const char *property : internal_object_properties) {
        EXPECT_EQ(json.find(std::string("\"property\":\"") + property + "\""), std::string::npos)
            << property;
    }
}

TEST(JsManifestExport, MarksOnlyImplementedStructSettersCallable) {
    const std::string json = js_export_api_contract_json();

    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (!mapping_is_public(mapping))
            continue;

        const std::string object = expected_mapping_json_object(mapping);
        expect_contains_json_object(json, object);
        if (mapping_setter_is_callable(mapping)) {
            EXPECT_NE(object.find("\"setterCallable\":true"), std::string::npos)
                << mapping.js_property;
            EXPECT_NE(object.find("\"documentationOnly\":false"), std::string::npos)
                << mapping.js_property;
        } else {
            EXPECT_NE(object.find("\"setterCallable\":false"), std::string::npos)
                << mapping.js_property;
            EXPECT_NE(object.find("\"documentationOnly\":true"), std::string::npos)
                << mapping.js_property;
        }
    }
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
