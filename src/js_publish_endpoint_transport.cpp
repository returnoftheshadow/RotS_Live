#include "js_publish_endpoint_transport.h"

#include "json_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::size_t MaxTransportDiagnosticBytes = 180;

struct StatusEnvelope {
    std::string operation;
    std::string package_id;
};

std::string bounded_single_line(std::string message)
{
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxTransportDiagnosticBytes)
        message.resize(MaxTransportDiagnosticBytes);
    return message;
}

bool mark_seen(std::vector<std::string> &seen_fields, const std::string &key,
    std::string *error_message)
{
    if (std::find(seen_fields.begin(), seen_fields.end(), key) != seen_fields.end()) {
        *error_message = "duplicate JSON field '" + key + "'";
        return false;
    }
    seen_fields.push_back(key);
    return true;
}

JsPublishEndpointTransportResult transport_response(JsPublishEndpointResponse response)
{
    JsPublishEndpointTransportResult result;
    result.ok = response.ok;
    result.http_status = response.http_status;
    result.reason_code = response.reason_code;
    result.json = js_publish_endpoint_response_body_json(response);
    return result;
}

JsPublishEndpointTransportResult transport_error(int http_status, const char *reason_code,
    const char *message, const char *diagnostic)
{
    JsPublishEndpointResponse response;
    response.operation = "publish";
    response.ok = false;
    response.http_status = http_status;
    response.reason_code = reason_code;
    response.message = message;
    response.diagnostics.push_back(bounded_single_line(diagnostic));
    return transport_response(response);
}

bool parse_host_name(const std::string &value, JsScriptPackageHost *host)
{
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

bool is_digits_only(const std::string &value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch);
    });
}

bool parse_logical_package_id(const std::string &package_id, int *zone,
    JsScriptPackageHost *host, int *vnum)
{
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
    if (!end || *end != '\0' || parsed_zone <= 0
        || parsed_zone > std::numeric_limits<int>::max())
        return false;
    end = nullptr;
    const long parsed_vnum = std::strtol(vnum_text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed_vnum <= 0
        || parsed_vnum > std::numeric_limits<int>::max())
        return false;

    JsScriptPackageHost parsed_host;
    if (!parse_host_name(host_text, &parsed_host))
        return false;

    const int canonical_zone = static_cast<int>(parsed_zone);
    const int canonical_vnum = static_cast<int>(parsed_vnum);
    if (package_id != js_staged_package_logical_package_id(
            canonical_zone, parsed_host, canonical_vnum))
        return false;

    *zone = canonical_zone;
    *host = parsed_host;
    *vnum = canonical_vnum;
    return true;
}

bool parse_status_envelope(const std::string &request_json, StatusEnvelope *envelope,
    std::string *error_message)
{
    bool saw_operation = false;
    bool saw_package_id = false;
    std::vector<std::string> seen_fields;
    json_utils::JsonReader reader(request_json);
    const bool parsed = reader.parse_root_object(
        [envelope, &saw_operation, &saw_package_id, &seen_fields](
            const std::string &key, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "operation") {
                saw_operation = true;
                return nested_reader->parse_string(&envelope->operation, nested_error_message);
            }
            if (key == "packageId") {
                saw_package_id = true;
                return nested_reader->parse_string(&envelope->package_id, nested_error_message);
            }
            *nested_error_message = "unknown publish request field '" + key + "'";
            return false;
        },
        error_message);
    if (!parsed)
        return false;
    if (!saw_operation) {
        *error_message = "missing required publish request field 'operation'";
        return false;
    }
    if (!saw_package_id) {
        *error_message = "missing required publish request field 'packageId'";
        return false;
    }
    return true;
}

JsPublishEndpointStatusInput status_input_from_envelope(
    const StatusEnvelope &envelope, const JsPublishEndpointTransportContext &context)
{
    JsPublishEndpointStatusInput input;
    input.package_id = envelope.package_id;
    input.authorization_request.operation = JsPublishOperation::StatusRead;
    input.authorization_request.request_id = context.request_id;
    input.authorization_request.actor_id = context.actor_id;
    input.authorization_request.builder_account_id = context.builder_account_id;
    parse_logical_package_id(envelope.package_id, &input.authorization_request.zone,
        &input.authorization_request.host, &input.authorization_request.vnum);
    input.authorization_request.package_id = envelope.package_id;
    input.authorization_request.token = context.token;
    input.authorization_request.transport = context.transport;

    input.authorization_options.now_epoch_seconds = context.now_epoch_seconds;
    input.authorization_options.expected_server_audience = context.expected_server_audience;
    input.authorization_options.expected_workspace_id = context.expected_workspace_id;
    return input;
}

} // namespace

JsPublishEndpointTransportResult js_publish_endpoint_dispatch_json(
    JsPublishEndpointService &service, const std::string &request_json,
    const JsPublishEndpointTransportContext &context,
    const JsPublishEndpointTransportOptions &options)
{
    if (request_json.empty())
        return transport_error(400, "publish.invalid-request", "Publish request rejected.",
            "publish request body is required");
    if (request_json.size() > options.maximum_request_bytes)
        return transport_error(413, "publish.request-too-large", "Publish request rejected.",
            "publish request body exceeds the configured size limit");

    StatusEnvelope envelope;
    std::string error_message;
    if (!parse_status_envelope(request_json, &envelope, &error_message))
        return transport_error(400, "publish.invalid-json", "Publish request rejected.",
            error_message.empty() ? "publish request JSON could not be parsed"
                                  : error_message.c_str());

    if (envelope.operation != "status")
        return transport_error(400, "publish.unsupported-operation", "Publish request rejected.",
            "publish operation is not supported by this transport adapter");
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    if (!parse_logical_package_id(envelope.package_id, &zone, &host, &vnum))
        return transport_error(400, "status.invalid-request", "Package status rejected.",
            "status request package id is invalid");

    return transport_response(service.status(status_input_from_envelope(envelope, context)).response);
}
