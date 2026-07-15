#include "js_builder_http_server_transport.h"

#include "db.h"
#include "json_utils.h"
#include "zone.h"

#include <algorithm>
#include <cctype>
#include <string>

extern room_data world;
extern int top_of_world;
extern index_data *mob_index;
extern int top_of_mobt;
extern index_data *obj_index;
extern int top_of_objt;

namespace {

std::string lower_ascii(const std::string &value) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value)
        lower.push_back(static_cast<char>(std::tolower(ch)));
    return lower;
}

std::string trim_copy(const std::string &value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}

bool starts_with(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool is_known_publish_path(const std::string &path, const std::string &route_prefix) {
    if (route_prefix.empty() || !starts_with(path, route_prefix))
        return false;
    const std::string operation = path.substr(route_prefix.size());
    return operation == "status" || operation == "stage" || operation == "activate" ||
           operation == "rollback";
}

std::vector<std::string> header_values(const JsBuilderHttpIngressRequest &request,
                                       const std::string &name) {
    std::vector<std::string> values;
    const std::string expected = lower_ascii(name);
    for (const auto &header : request.headers) {
        if (lower_ascii(trim_copy(header.first)) == expected)
            values.push_back(header.second);
    }
    return values;
}

bool has_valid_proxy_secret(const JsBuilderHttpIngressRequest &request,
                            const JsBuilderHttpIngressOptions &options) {
    if (options.expected_proxy_secret.empty())
        return false;
    const std::vector<std::string> values = header_values(request, options.proxy_secret_header);
    return values.size() == 1 && trim_copy(values[0]) == options.expected_proxy_secret;
}

std::string single_header_or_empty(const JsBuilderHttpIngressRequest &request,
                                   const std::string &name, bool *duplicate_header) {
    const std::vector<std::string> values = header_values(request, name);
    if (values.size() > 1) {
        if (duplicate_header)
            *duplicate_header = true;
        return "";
    }
    return values.empty() ? "" : trim_copy(values[0]);
}

bool starts_with_case_insensitive(const std::string &value, const char *prefix) {
    std::size_t index = 0;
    while (prefix[index] != '\0') {
        if (index >= value.size() || std::tolower(static_cast<unsigned char>(value[index])) !=
                                         std::tolower(static_cast<unsigned char>(prefix[index])))
            return false;
        ++index;
    }
    return true;
}

std::string bearer_token_from_authorization(const std::string &authorization) {
    const std::string trimmed = trim_copy(authorization);
    if (!starts_with_case_insensitive(trimmed, "Bearer "))
        return "";
    return trim_copy(trimmed.substr(7));
}

bool is_json_content_type(const std::string &content_type) {
    const std::string trimmed = lower_ascii(trim_copy(content_type));
    return trimmed == "application/json" || starts_with(trimmed, "application/json;");
}

bool publish_request_allows_context_resolution(const JsBuilderHttpIngressRequest &request,
                                               const JsBuilderHttpIngressOptions &options) {
    if (request.method != "POST" || options.publish_options.session_store == nullptr)
        return false;

    bool duplicate_content_type = false;
    bool duplicate_authorization = false;
    const std::string content_type =
        single_header_or_empty(request, "content-type", &duplicate_content_type);
    const std::string authorization =
        single_header_or_empty(request, "authorization", &duplicate_authorization);
    if (duplicate_content_type || duplicate_authorization || !is_json_content_type(content_type))
        return false;

    const std::string bearer_token = bearer_token_from_authorization(authorization);
    if (bearer_token.empty())
        return false;

    return options.publish_options.session_store
        ->lookup(bearer_token, options.publish_options.session_store_options)
        .ok;
}

std::string publish_operation_from_path(const std::string &path, const std::string &route_prefix) {
    if (!is_known_publish_path(path, route_prefix))
        return "";
    return path.substr(route_prefix.size());
}

JsBuilderHttpIngressResult ingress_error(int http_status, const char *reason_code,
                                         const char *message) {
    JsBuilderHttpIngressResult result;
    result.ok = false;
    result.http_status = http_status;
    result.reason_code = reason_code;
    result.json = "{\"ok\":false,\"reasonCode\":\"";
    result.json += reason_code;
    result.json += "\",\"message\":\"";
    result.json += json_utils::escape_json_string(message);
    result.json += "\"}";
    return result;
}

JsBuilderHttpIngressResult parse_error_response(const JsBuilderHttpTransportParseResult &parse) {
    return ingress_error(parse.http_status, parse.reason_code.c_str(),
                         parse.message.empty() ? "Builder request was rejected."
                                               : parse.message.c_str());
}

int status_for_context_reason(const std::string &reason_code) {
    if (reason_code == "builder.context.catalog-unavailable" ||
        reason_code == "builder.context.live-store-unavailable")
        return 503;
    if (reason_code == "builder.context.catalog-invalid")
        return 500;
    if (reason_code == "builder.context.invalid-request" ||
        reason_code == "builder.context.invalid-target")
        return 400;
    return 403;
}

JsBuilderHttpServerTransportResult from_ingress_result(const JsBuilderHttpIngressResult &ingress) {
    JsBuilderHttpServerTransportResult result;
    result.ok = ingress.ok;
    result.http_status = ingress.http_status;
    result.reason_code = ingress.reason_code;
    result.http_response = js_builder_http_transport_render_response(ingress);
    return result;
}

void push_unique(std::vector<int> *values, int value) {
    if (value <= 0)
        return;
    if (std::find(values->begin(), values->end(), value) == values->end())
        values->push_back(value);
}

std::vector<int> owner_ids(const owner_list *owners) {
    std::vector<int> ids;
    for (const owner_list *owner = owners; owner != nullptr; owner = owner->next) {
        if (owner->owner == 0) {
            if (ids.empty())
                ids.push_back(0);
            break;
        }
        push_unique(&ids, owner->owner);
    }
    return ids;
}

} // namespace

JsBuilderHttpServerTransportResult
js_builder_http_server_transport_dispatch(const std::string &raw_request,
                                          const JsBuilderHttpServerTransportOptions &options) {
    const JsBuilderHttpTransportParseResult parse =
        js_builder_http_transport_parse_request(raw_request, options.transport_options);
    if (!parse.ok)
        return from_ingress_result(parse_error_response(parse));

    JsBuilderHttpIngressOptions ingress_options = options.ingress_options;
    const std::string publish_operation = publish_operation_from_path(
        parse.request.path, ingress_options.publish_options.route_prefix);
    if (!publish_operation.empty() && has_valid_proxy_secret(parse.request, ingress_options) &&
        ingress_options.publish_service != nullptr &&
        publish_request_allows_context_resolution(parse.request, ingress_options)) {
        const JsBuilderPublishContextResult context =
            js_builder_publish_context_resolve_for_operation(publish_operation, parse.request.body,
                                                             ingress_options.publish_context,
                                                             options.publish_context_options);
        if (!context.ok) {
            return from_ingress_result(ingress_error(status_for_context_reason(context.reason_code),
                                                     context.reason_code.c_str(),
                                                     "Builder request was rejected."));
        }
        ingress_options.publish_context = context.context;
    }

    return from_ingress_result(js_builder_http_ingress_dispatch(parse.request, ingress_options));
}

JsBuilderPublishTargetCatalog js_builder_http_server_transport_catalog_from_loaded_world() {
    JsBuilderPublishTargetCatalog catalog;

    for (int index = 0; mob_index != nullptr && index <= top_of_mobt; ++index)
        push_unique(&catalog.mobile_vnums, mob_index[index].virt);

    for (int index = 0; obj_index != nullptr && index <= top_of_objt; ++index)
        push_unique(&catalog.object_vnums, obj_index[index].virt);

    for (int index = 0; index <= top_of_world; ++index)
        push_unique(&catalog.room_vnums, world[index].number);

    for (int index = 0; zone_table != nullptr && index <= top_of_zone_table; ++index) {
        JsBuilderPublishZoneRecord zone;
        zone.number = zone_table[index].number;
        zone.top = zone_table[index].top;
        zone.owner_character_ids = owner_ids(zone_table[index].owners);
        catalog.zones.push_back(zone);
    }

    return catalog;
}
