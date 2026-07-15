#ifndef JS_BUILDER_HTTP_TRANSPORT_H
#define JS_BUILDER_HTTP_TRANSPORT_H

#include "js_builder_http_ingress.h"

#include <cstddef>
#include <string>

struct JsBuilderHttpTransportOptions {
    std::size_t maximum_header_bytes = 16 * 1024;
    std::size_t maximum_body_bytes = 64 * 1024;
};

struct JsBuilderHttpTransportParseResult {
    bool ok = false;
    int http_status = 400;
    std::string reason_code;
    std::string message;
    JsBuilderHttpIngressRequest request;
};

JsBuilderHttpTransportParseResult js_builder_http_transport_parse_request(
    const std::string &raw_request,
    const JsBuilderHttpTransportOptions &options = {});

std::string js_builder_http_transport_render_response(
    const JsBuilderHttpIngressResult &result);

#endif
