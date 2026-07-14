#include "js_publish_http_endpoint.h"

#include "js_publish_endpoint_contract.h"
#include "json_utils.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

constexpr std::size_t MaxHttpDiagnosticBytes = 180;

std::string bounded_single_line(std::string message)
{
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxHttpDiagnosticBytes)
        message.resize(MaxHttpDiagnosticBytes);
    return message;
}

JsPublishEndpointTransportResult http_error(int http_status, const char *reason_code,
    const char *message, const char *diagnostic)
{
    JsPublishEndpointResponse response;
    response.operation = "publish";
    response.ok = false;
    response.http_status = http_status;
    response.reason_code = reason_code;
    response.message = message;
    response.diagnostics.push_back(bounded_single_line(diagnostic));

    JsPublishEndpointTransportResult result;
    result.ok = false;
    result.http_status = http_status;
    result.reason_code = reason_code;
    result.json = js_publish_endpoint_response_body_json(response);
    return result;
}

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), value.begin());
}

bool is_supported_operation(const std::string &operation)
{
    return operation == "status" || operation == "stage" || operation == "activate"
        || operation == "rollback";
}

bool is_json_content_type(const std::string &content_type)
{
    if (content_type.empty())
        return true;
    std::string normalized;
    normalized.reserve(content_type.size());
    for (unsigned char ch : content_type)
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    return starts_with(normalized, "application/json")
        && (normalized.size() == 16 || normalized[16] == ';'
            || std::isspace(static_cast<unsigned char>(normalized[16])));
}

std::size_t first_non_space(const std::string &value)
{
    std::size_t index = 0;
    while (index < value.size()
        && std::isspace(static_cast<unsigned char>(value[index])))
        ++index;
    return index;
}

std::string body_with_route_operation(
    const std::string &operation, const std::string &body)
{
    const std::size_t object_start = first_non_space(body);
    if (object_start >= body.size() || body[object_start] != '{')
        return body;

    std::string envelope = "{\"operation\":\"";
    envelope += json_utils::escape_json_string(operation);
    envelope += "\"";

    std::size_t next = object_start + 1;
    while (next < body.size() && std::isspace(static_cast<unsigned char>(body[next])))
        ++next;
    if (next < body.size() && body[next] != '}')
        envelope += ",";
    envelope.append(body, next, std::string::npos);
    return envelope;
}

bool projected_envelope_exceeds_limit(
    const std::string &operation, const std::string &body, std::size_t limit)
{
    const std::size_t object_start = first_non_space(body);
    if (object_start >= body.size() || body[object_start] != '{')
        return body.size() > limit;

    std::size_t next = object_start + 1;
    while (next < body.size() && std::isspace(static_cast<unsigned char>(body[next])))
        ++next;

    const std::size_t prefix_size = std::string("{\"operation\":\"").size()
        + operation.size() + std::string("\"").size();
    const std::size_t comma_size = next < body.size() && body[next] != '}' ? 1 : 0;
    if (prefix_size > limit || comma_size > limit - prefix_size)
        return true;
    const std::size_t body_tail_size = body.size() - next;
    return body_tail_size > limit - prefix_size - comma_size;
}

} // namespace

JsPublishEndpointTransportResult js_publish_http_endpoint_dispatch(
    JsPublishEndpointService &service, const JsPublishHttpEndpointRequest &request,
    const JsPublishEndpointTransportContext &context,
    const JsPublishHttpEndpointOptions &options)
{
    if (request.method != "POST")
        return http_error(405, "publish.method-not-allowed", "Publish request rejected.",
            "publish endpoint requires POST");
    if (!is_json_content_type(request.content_type))
        return http_error(415, "publish.unsupported-media-type",
            "Publish request rejected.", "publish endpoint requires application/json");
    if (request.body.empty())
        return http_error(400, "publish.invalid-request", "Publish request rejected.",
            "publish request body is required");
    if (!starts_with(request.path, options.route_prefix)
        || request.path.find('?') != std::string::npos)
        return http_error(404, "publish.not-found", "Publish request rejected.",
            "publish endpoint path is not supported");

    const std::string operation = request.path.substr(options.route_prefix.size());
    if (!is_supported_operation(operation) || operation.find('/') != std::string::npos)
        return http_error(404, "publish.not-found", "Publish request rejected.",
            "publish endpoint path is not supported");
    if (projected_envelope_exceeds_limit(
            operation, request.body, options.transport_options.maximum_request_bytes))
        return http_error(413, "publish.request-too-large", "Publish request rejected.",
            "publish request body exceeds the configured size limit");

    return js_publish_endpoint_dispatch_json(service,
        body_with_route_operation(operation, request.body), context,
        options.transport_options);
}
