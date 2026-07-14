#ifndef JS_PUBLISH_HTTP_ENDPOINT_H
#define JS_PUBLISH_HTTP_ENDPOINT_H

#include "js_publish_endpoint_transport.h"

#include <string>

struct JsPublishHttpEndpointRequest {
    std::string method;
    std::string path;
    std::string content_type;
    std::string body;
};

struct JsPublishHttpEndpointOptions {
    std::string route_prefix = "/api/js-scripts/";
    JsPublishEndpointTransportOptions transport_options;
};

JsPublishEndpointTransportResult js_publish_http_endpoint_dispatch(
    JsPublishEndpointService &service, const JsPublishHttpEndpointRequest &request,
    const JsPublishEndpointTransportContext &context,
    const JsPublishHttpEndpointOptions &options = {});

#endif
