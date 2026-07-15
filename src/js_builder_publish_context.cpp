#include "js_builder_publish_context.h"

#include "js_script_package_loader.h"
#include "js_staged_package_identity.h"
#include "json_utils.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

struct TargetReference {
    std::string operation;
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
};

struct RequestTargetEnvelope {
    std::string operation;
    std::string package_id;
    bool saw_operation = false;
    bool saw_package_id = false;
    bool saw_package = false;
    JsScriptPackage package;
};

bool is_digits_only(const std::string &value) {
    if (value.empty())
        return false;
    for (char ch : value) {
        if (ch < '0' || ch > '9')
            return false;
    }
    return true;
}

bool parse_host_name(const std::string &value, JsScriptPackageHost *host) {
    if (value == js_script_package_host_name(JsScriptPackageHost::Character)) {
        *host = JsScriptPackageHost::Character;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Object)) {
        *host = JsScriptPackageHost::Object;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Room)) {
        *host = JsScriptPackageHost::Room;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::MudlleMobile)) {
        *host = JsScriptPackageHost::MudlleMobile;
        return true;
    }
    return false;
}

bool parse_logical_package_id(const std::string &package_id, int *zone, JsScriptPackageHost *host,
                              int *vnum) {
    const std::string prefix = "js:";
    if (package_id.compare(0, prefix.size(), prefix) != 0)
        return false;
    const std::string::size_type first = package_id.find(':', prefix.size());
    if (first == std::string::npos)
        return false;
    const std::string::size_type second = package_id.find(':', first + 1);
    if (second == std::string::npos)
        return false;
    if (package_id.find(':', second + 1) != std::string::npos)
        return false;

    const std::string zone_text = package_id.substr(prefix.size(), first - prefix.size());
    const std::string host_text = package_id.substr(first + 1, second - first - 1);
    const std::string vnum_text = package_id.substr(second + 1);
    if (!is_digits_only(zone_text) || !is_digits_only(vnum_text))
        return false;

    char *end = nullptr;
    const long parsed_zone = std::strtol(zone_text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed_zone <= 0 || parsed_zone > std::numeric_limits<int>::max())
        return false;
    end = nullptr;
    const long parsed_vnum = std::strtol(vnum_text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed_vnum <= 0 || parsed_vnum > std::numeric_limits<int>::max())
        return false;

    JsScriptPackageHost parsed_host;
    if (!parse_host_name(host_text, &parsed_host))
        return false;

    const int canonical_zone = static_cast<int>(parsed_zone);
    const int canonical_vnum = static_cast<int>(parsed_vnum);
    if (package_id !=
        js_staged_package_logical_package_id(canonical_zone, parsed_host, canonical_vnum))
        return false;

    *zone = canonical_zone;
    *host = parsed_host;
    *vnum = canonical_vnum;
    return true;
}

bool mark_seen(std::vector<std::string> &seen_fields, const std::string &key,
               std::string *error_message) {
    if (std::find(seen_fields.begin(), seen_fields.end(), key) != seen_fields.end()) {
        *error_message = "duplicate JSON field";
        return false;
    }
    seen_fields.push_back(key);
    return true;
}

bool parse_target_envelope(const std::string &request_json, RequestTargetEnvelope *envelope) {
    std::vector<std::string> seen_fields;
    JsScriptPackageBundleLoadOptions package_options;
    std::string error_message;
    json_utils::JsonReader reader(request_json);
    const bool parsed = reader.parse_root_object(
        [envelope, &seen_fields, &package_options](const std::string &key,
                                                   json_utils::JsonReader *nested_reader,
                                                   std::string *nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "operation") {
                envelope->saw_operation = true;
                return nested_reader->parse_string(&envelope->operation, nested_error_message);
            }
            if (key == "packageId") {
                envelope->saw_package_id = true;
                return nested_reader->parse_string(&envelope->package_id, nested_error_message);
            }
            if (key == "package") {
                envelope->saw_package = true;
                return js_script_package_parse_json_object(
                    nested_reader, package_options, &envelope->package, nested_error_message);
            }
            if (key == "baseLiveChecksum" || key == "stagedDigest" || key == "targetLiveChecksum" ||
                key == "reason")
                return nested_reader->skip_value(nested_error_message);
            *nested_error_message = "unknown publish context field";
            return false;
        },
        &error_message);
    return parsed;
}

bool target_from_envelope(const RequestTargetEnvelope &envelope, TargetReference *target) {
    target->operation = envelope.operation;
    if (envelope.operation == "stage") {
        if (!envelope.saw_package || envelope.saw_package_id)
            return false;
        target->vnum = envelope.package.vnum;
        target->host = envelope.package.host;
        return target->vnum > 0;
    }

    if (envelope.operation == "status" || envelope.operation == "activate" ||
        envelope.operation == "rollback") {
        if (!envelope.saw_package_id || envelope.saw_package)
            return false;
        return parse_logical_package_id(envelope.package_id, &target->zone, &target->host,
                                        &target->vnum);
    }

    return false;
}

bool contains_vnum(const std::vector<int> &vnums, int vnum) {
    return std::find(vnums.begin(), vnums.end(), vnum) != vnums.end();
}

bool has_duplicate_vnum(const std::vector<int> &vnums) {
    for (std::vector<int>::size_type index = 0; index < vnums.size(); ++index) {
        if (vnums[index] <= 0)
            return true;
        if (std::find(vnums.begin() + index + 1, vnums.end(), vnums[index]) != vnums.end())
            return true;
    }
    return false;
}

bool target_exists(const JsBuilderPublishTargetCatalog &catalog, const TargetReference &target) {
    if (target.host == JsScriptPackageHost::Character ||
        target.host == JsScriptPackageHost::MudlleMobile)
        return contains_vnum(catalog.mobile_vnums, target.vnum);
    if (target.host == JsScriptPackageHost::Object)
        return contains_vnum(catalog.object_vnums, target.vnum);
    if (target.host == JsScriptPackageHost::Room)
        return contains_vnum(catalog.room_vnums, target.vnum);
    return false;
}

const JsBuilderPublishZoneRecord *
find_zone_by_number(const std::vector<JsBuilderPublishZoneRecord> &zones, int zone_number) {
    for (const JsBuilderPublishZoneRecord &zone : zones) {
        if (zone.number == zone_number)
            return &zone;
    }
    return nullptr;
}

const JsBuilderPublishZoneRecord *
find_zone_for_vnum(const std::vector<JsBuilderPublishZoneRecord> &zones, int vnum) {
    int previous_top = -1;
    const JsBuilderPublishZoneRecord *match = nullptr;
    for (const JsBuilderPublishZoneRecord &zone : zones) {
        if (zone.number <= 0 || zone.top <= previous_top)
            return nullptr;
        if (vnum > previous_top && vnum <= zone.top) {
            if (match)
                return nullptr;
            match = &zone;
        }
        previous_top = zone.top;
    }
    return match;
}

bool owners_valid(const std::vector<int> &owners) {
    if (owners.empty())
        return true;
    if (std::find(owners.begin(), owners.end(), 0) != owners.end())
        return owners.size() == 1;
    for (int owner : owners) {
        if (owner <= 0)
            return false;
    }
    for (std::vector<int>::size_type index = 0; index < owners.size(); ++index) {
        if (std::find(owners.begin() + index + 1, owners.end(), owners[index]) != owners.end())
            return false;
    }
    return true;
}

bool catalog_valid(const JsBuilderPublishTargetCatalog &catalog) {
    if (has_duplicate_vnum(catalog.mobile_vnums) || has_duplicate_vnum(catalog.object_vnums) ||
        has_duplicate_vnum(catalog.room_vnums))
        return false;

    int previous_top = -1;
    std::vector<int> zone_numbers;
    for (const JsBuilderPublishZoneRecord &zone : catalog.zones) {
        if (zone.number <= 0 || zone.top <= previous_top || !owners_valid(zone.owner_character_ids))
            return false;
        if (std::find(zone_numbers.begin(), zone_numbers.end(), zone.number) != zone_numbers.end())
            return false;
        zone_numbers.push_back(zone.number);
        previous_top = zone.top;
    }
    return true;
}

bool live_pointer_not_found(const JsLivePackagePointerResult &pointer) {
    if (pointer.diagnostics.empty())
        return false;
    for (const JsLivePackageStoreDiagnostic &diagnostic : pointer.diagnostics) {
        if (diagnostic.code != JsLivePackageStoreDiagnosticCode::LivePointerNotFound)
            return false;
    }
    return true;
}

JsBuilderPublishContextResult context_error(const std::string &reason_code) {
    JsBuilderPublishContextResult result;
    result.ok = false;
    result.reason_code = reason_code;
    return result;
}

} // namespace

JsBuilderPublishContextResult
js_builder_publish_context_resolve(const std::string &request_json,
                                   const JsPublishEndpointTransportContext &base_context,
                                   const JsBuilderPublishContextOptions &options) {
    return js_builder_publish_context_resolve_for_operation("", request_json, base_context,
                                                            options);
}

JsBuilderPublishContextResult js_builder_publish_context_resolve_for_operation(
    const std::string &operation, const std::string &request_json,
    const JsPublishEndpointTransportContext &base_context,
    const JsBuilderPublishContextOptions &options) {
    if (!options.target_catalog)
        return context_error("builder.context.catalog-unavailable");
    if (!options.live_store)
        return context_error("builder.context.live-store-unavailable");
    if (!catalog_valid(*options.target_catalog))
        return context_error("builder.context.catalog-invalid");
    if (request_json.empty() || request_json.size() > options.maximum_request_bytes)
        return context_error("builder.context.invalid-request");

    RequestTargetEnvelope envelope;
    if (!parse_target_envelope(request_json, &envelope))
        return context_error("builder.context.invalid-request");
    if (!operation.empty()) {
        if (envelope.saw_operation && envelope.operation != operation)
            return context_error("builder.context.invalid-target");
        envelope.operation = operation;
        envelope.saw_operation = true;
    }
    if (!envelope.saw_operation)
        return context_error("builder.context.invalid-request");

    TargetReference target;
    if (!target_from_envelope(envelope, &target))
        return context_error("builder.context.invalid-target");
    if (!target_exists(*options.target_catalog, target))
        return context_error("builder.context.target-not-found");

    const JsBuilderPublishZoneRecord *zone_from_vnum =
        find_zone_for_vnum(options.target_catalog->zones, target.vnum);
    if (!zone_from_vnum)
        return context_error("builder.context.zone-not-found");
    const JsBuilderPublishZoneRecord *zone = zone_from_vnum;
    if (target.zone > 0) {
        const JsBuilderPublishZoneRecord *zone_from_id =
            find_zone_by_number(options.target_catalog->zones, target.zone);
        if (!zone_from_id)
            return context_error("builder.context.zone-not-found");
        if (zone_from_id->number != zone_from_vnum->number)
            return context_error("builder.context.zone-mismatch");
        zone = zone_from_id;
    }
    if (target.zone > 0 && target.zone != zone->number)
        return context_error("builder.context.zone-mismatch");

    JsBuilderPublishContextResult result;
    result.ok = true;
    result.reason_code = "builder.context.accepted";
    result.context = base_context;
    result.context.zone = zone->number;
    result.context.target_zone_resolved = true;
    result.context.server_resolved_target_zone = zone->number;
    result.context.server_resolved_target_host = target.host;
    result.context.zone_exists = true;
    result.context.zone_owner_character_ids = zone->owner_character_ids;
    result.context.zone_allows_all_builders =
        std::find(zone->owner_character_ids.begin(), zone->owner_character_ids.end(), 0) !=
        zone->owner_character_ids.end();
    result.context.current_live_checksum.clear();

    const JsLivePackagePointerResult pointer =
        options.live_store->find_live_pointer(zone->number, target.host, target.vnum);
    if (pointer.ok)
        result.context.current_live_checksum = pointer.pointer.current_live_checksum;
    else if (!live_pointer_not_found(pointer))
        return context_error("builder.context.live-store-unavailable");

    return result;
}
