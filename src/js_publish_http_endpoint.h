#ifndef JS_PUBLISH_HTTP_ENDPOINT_H
#define JS_PUBLISH_HTTP_ENDPOINT_H

#include "js_publish_endpoint_transport.h"

#include <string>

struct JsPublishHttpEndpointRequest {
    std::string method;
    std::string path;
    std::string content_type;
    std::string body;
    std::string bearer_token;
    // Derived by the server transport from the validated proxy trust marker.
    // This must never be filled from BuilderClient-controlled JSON.
    bool trusted_proxy = false;
};

struct JsPublishHttpEndpointOptions {
    std::string route_prefix = "/api/js-scripts/";
    JsPublishEndpointTransportOptions transport_options;
    const JsBuilderSessionStore *session_store = nullptr;
    JsBuilderSessionStoreOptions session_store_options;
    bool allow_prederived_context_for_tests = false;
};

JsPublishEndpointTransportResult js_publish_http_endpoint_dispatch(
    JsPublishEndpointService &service, const JsPublishHttpEndpointRequest &request,
    const JsPublishEndpointTransportContext &context,
    const JsPublishHttpEndpointOptions &options = {});

#endif
