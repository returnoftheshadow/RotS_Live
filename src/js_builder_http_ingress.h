#ifndef JS_BUILDER_HTTP_INGRESS_H
#define JS_BUILDER_HTTP_INGRESS_H

#include "js_builder_manifest_endpoint.h"
#include "js_builder_session_endpoint.h"
#include "js_publish_http_endpoint.h"

#include <string>
#include <utility>
#include <vector>

struct JsBuilderHttpIngressRequest {
    std::string method;
    std::string path;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct JsBuilderHttpIngressOptions {
    std::string proxy_secret_header = "x-rots-builder-proxy-secret";
    std::string expected_proxy_secret;
    JsBuilderManifestEndpointOptions manifest_options;
    JsBuilderSessionEndpointOptions session_options;
    JsPublishEndpointService *publish_service = nullptr;
    JsPublishEndpointTransportContext publish_context;
    JsPublishHttpEndpointOptions publish_options;
};

struct JsBuilderHttpIngressResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string json;
};

JsBuilderHttpIngressResult js_builder_http_ingress_dispatch(
    const JsBuilderHttpIngressRequest &request,
    const JsBuilderHttpIngressOptions &options = {});

#endif
