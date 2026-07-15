#include "js_builder_manifest_endpoint.h"

#include "json_utils.h"

namespace {

bool is_valid_route_path(const std::string &path)
{
    return !path.empty() && path[0] == '/' && path.find('?') == std::string::npos;
}

JsBuilderManifestEndpointResult endpoint_error(
    int http_status, const char *reason_code, const char *message)
{
    JsBuilderManifestEndpointResult result;
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

} // namespace

JsBuilderManifestEndpointResult js_builder_manifest_endpoint_dispatch(
    const JsBuilderManifestEndpointRequest &request,
    const JsBuilderManifestEndpointOptions &options)
{
    if (!request.trusted_proxy)
        return endpoint_error(403, "builder.manifest.untrusted",
            "Builder manifest request was rejected.");
    if (!is_valid_route_path(options.route_path))
        return endpoint_error(500, "builder.manifest.invalid-route",
            "Builder manifest endpoint is misconfigured.");
    if (request.path != options.route_path || request.path.find('?') != std::string::npos)
        return endpoint_error(404, "builder.manifest.not-found",
            "Builder manifest request was rejected.");
    if (request.method != "GET")
        return endpoint_error(405, "builder.manifest.method-not-allowed",
            "Builder manifest request was rejected.");
    if (!request.body.empty())
        return endpoint_error(400, "builder.manifest.invalid-request",
            "Builder manifest request was rejected.");

    JsBuilderManifestEndpointResult result;
    result.ok = true;
    result.http_status = 200;
    result.reason_code = "builder.manifest.current";
    result.json = js_export_builder_manifest_json(options.export_options);
    if (result.json.size() > options.maximum_response_bytes)
        return endpoint_error(413, "builder.manifest.response-too-large",
            "Builder manifest response exceeds the configured size limit.");
    return result;
}
