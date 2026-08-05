#ifndef JS_BUILDER_MANIFEST_ENDPOINT_H
#define JS_BUILDER_MANIFEST_ENDPOINT_H

#include "js_manifest_export.h"

#include <cstddef>
#include <string>

struct JsBuilderManifestEndpointRequest {
    std::string method;
    std::string path;
    std::string body;
    // Derived by the server transport from the validated proxy trust marker.
    // This must never be filled from BuilderClient-controlled JSON.
    bool trusted_proxy = false;
};

struct JsBuilderManifestEndpointOptions {
    std::string route_path = "/api/builder/js/manifest";
    JsManifestExportOptions export_options;
    std::size_t maximum_response_bytes = 2 * 1024 * 1024;
};

struct JsBuilderManifestEndpointResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string json;
};

JsBuilderManifestEndpointResult js_builder_manifest_endpoint_dispatch(
    const JsBuilderManifestEndpointRequest &request,
    const JsBuilderManifestEndpointOptions &options = {});

#endif
