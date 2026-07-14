#ifndef JS_PUBLISH_ENDPOINT_TRANSPORT_H
#define JS_PUBLISH_ENDPOINT_TRANSPORT_H

#include "js_publish_endpoint_service.h"

#include <cstddef>
#include <string>

struct JsPublishEndpointTransportOptions {
    std::size_t maximum_request_bytes = 64 * 1024;
};

struct JsPublishEndpointTransportContext {
    std::string request_id;
    std::string actor_id;
    std::string builder_account_id;
    JsPublishTokenMetadata token;
    JsPublishTransportMetadata transport;
    long long now_epoch_seconds = 0;
    std::string expected_server_audience;
    std::string expected_workspace_id;
};

struct JsPublishEndpointTransportResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string json;
};

JsPublishEndpointTransportResult js_publish_endpoint_dispatch_json(
    JsPublishEndpointService &service, const std::string &request_json,
    const JsPublishEndpointTransportContext &context,
    const JsPublishEndpointTransportOptions &options = {});

#endif
