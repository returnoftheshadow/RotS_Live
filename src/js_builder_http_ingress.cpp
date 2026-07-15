#include "js_builder_http_ingress.h"

#include "json_utils.h"

#include <algorithm>
#include <cctype>

namespace {

std::string lower_ascii(const std::string &value)
{
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value)
        lower.push_back(static_cast<char>(std::tolower(ch)));
    return lower;
}

bool starts_with_case_insensitive(const std::string &value, const char *prefix)
{
    std::size_t index = 0;
    while (prefix[index] != '\0') {
        if (index >= value.size()
            || std::tolower(static_cast<unsigned char>(value[index]))
                != std::tolower(static_cast<unsigned char>(prefix[index])))
            return false;
        ++index;
    }
    return true;
}

std::string trim_copy(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size()
        && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;
    std::size_t end = value.size();
    while (end > start
        && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}

JsBuilderHttpIngressResult ingress_error(
    int http_status, const char *reason_code, const char *message)
{
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

std::vector<std::string> header_values(
    const JsBuilderHttpIngressRequest &request, const std::string &name)
{
    std::vector<std::string> values;
    const std::string expected = lower_ascii(name);
    for (const auto &header : request.headers) {
        if (lower_ascii(trim_copy(header.first)) == expected)
            values.push_back(header.second);
    }
    return values;
}

bool has_valid_proxy_secret(const JsBuilderHttpIngressRequest &request,
    const JsBuilderHttpIngressOptions &options)
{
    if (options.expected_proxy_secret.empty())
        return false;
    const std::vector<std::string> values =
        header_values(request, options.proxy_secret_header);
    return values.size() == 1 && trim_copy(values[0]) == options.expected_proxy_secret;
}

std::string single_header_or_empty(
    const JsBuilderHttpIngressRequest &request, const std::string &name,
    bool *duplicate_header)
{
    const std::vector<std::string> values = header_values(request, name);
    if (values.size() > 1) {
        if (duplicate_header)
            *duplicate_header = true;
        return "";
    }
    return values.empty() ? "" : trim_copy(values[0]);
}

std::string bearer_token_from_authorization(const std::string &authorization)
{
    const std::string trimmed = trim_copy(authorization);
    if (!starts_with_case_insensitive(trimmed, "Bearer "))
        return "";
    return trim_copy(trimmed.substr(7));
}

JsBuilderHttpIngressResult from_manifest(const JsBuilderManifestEndpointResult &source)
{
    return { source.ok, source.http_status, source.reason_code, source.json };
}

JsBuilderHttpIngressResult from_session(const JsBuilderSessionEndpointResult &source)
{
    return { source.ok, source.http_status, source.reason_code, source.json };
}

JsBuilderHttpIngressResult from_publish(const JsPublishEndpointTransportResult &source)
{
    return { source.ok, source.http_status, source.reason_code, source.json };
}

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

bool is_valid_absolute_path(const std::string &path)
{
    return !path.empty() && path[0] == '/' && path.find('?') == std::string::npos;
}

bool is_known_publish_path(const std::string &path, const std::string &route_prefix)
{
    if (route_prefix.empty() || !starts_with(path, route_prefix))
        return false;
    const std::string operation = path.substr(route_prefix.size());
    return operation == "status" || operation == "stage" || operation == "activate"
        || operation == "rollback";
}

bool has_route_configuration_conflict(const JsBuilderHttpIngressOptions &options)
{
    const std::string &manifest = options.manifest_options.route_path;
    const std::string &login = options.session_options.login_path;
    const std::string &logout = options.session_options.logout_path;
    const std::string &publish_prefix = options.publish_options.route_prefix;
    if (!is_valid_absolute_path(manifest) || !is_valid_absolute_path(login)
        || !is_valid_absolute_path(logout) || !is_valid_absolute_path(publish_prefix))
        return true;
    if (manifest == login || manifest == logout || login == logout)
        return true;
    return starts_with(manifest, publish_prefix) || starts_with(login, publish_prefix)
        || starts_with(logout, publish_prefix);
}

} // namespace

JsBuilderHttpIngressResult js_builder_http_ingress_dispatch(
    const JsBuilderHttpIngressRequest &request,
    const JsBuilderHttpIngressOptions &options)
{
    if (!has_valid_proxy_secret(request, options))
        return ingress_error(403, "builder.ingress.untrusted",
            "Builder request was rejected.");
    if (has_route_configuration_conflict(options))
        return ingress_error(500, "builder.ingress.invalid-route-config",
            "Builder request was rejected.");

    bool duplicate_content_type = false;
    bool duplicate_authorization = false;
    const std::string content_type =
        single_header_or_empty(request, "content-type", &duplicate_content_type);
    const std::string authorization =
        single_header_or_empty(request, "authorization", &duplicate_authorization);
    if (duplicate_content_type || duplicate_authorization)
        return ingress_error(400, "builder.ingress.invalid-headers",
            "Builder request was rejected.");

    if (request.path == options.manifest_options.route_path) {
        JsBuilderManifestEndpointRequest manifest_request;
        manifest_request.method = request.method;
        manifest_request.path = request.path;
        manifest_request.body = request.body;
        manifest_request.trusted_proxy = true;
        return from_manifest(
            js_builder_manifest_endpoint_dispatch(manifest_request,
                options.manifest_options));
    }

    if (request.path == options.session_options.login_path
        || request.path == options.session_options.logout_path) {
        JsBuilderSessionEndpointRequest session_request;
        session_request.method = request.method;
        session_request.path = request.path;
        session_request.content_type = content_type;
        session_request.body = request.body;
        session_request.bearer_token = bearer_token_from_authorization(authorization);
        session_request.trusted_proxy = true;
        return from_session(
            js_builder_session_endpoint_dispatch(session_request,
                options.session_options));
    }

    if (is_known_publish_path(request.path, options.publish_options.route_prefix)) {
        if (options.publish_service == nullptr)
            return ingress_error(503, "builder.ingress.publish-unavailable",
                "Builder request was rejected.");
        JsPublishHttpEndpointRequest publish_request;
        publish_request.method = request.method;
        publish_request.path = request.path;
        publish_request.content_type = content_type;
        publish_request.body = request.body;
        publish_request.bearer_token = bearer_token_from_authorization(authorization);
        publish_request.trusted_proxy = true;
        return from_publish(js_publish_http_endpoint_dispatch(*options.publish_service,
            publish_request, options.publish_context, options.publish_options));
    }

    return ingress_error(404, "builder.ingress.not-found",
        "Builder request was rejected.");
}
