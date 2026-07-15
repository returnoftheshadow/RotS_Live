#ifndef JS_BUILDER_HTTP_SERVER_TRANSPORT_H
#define JS_BUILDER_HTTP_SERVER_TRANSPORT_H

#include "js_builder_http_transport.h"
#include "js_builder_publish_context.h"

#include <string>

struct JsBuilderHttpServerTransportOptions {
    JsBuilderHttpTransportOptions transport_options;
    JsBuilderHttpIngressOptions ingress_options;
    JsBuilderPublishContextOptions publish_context_options;
};

struct JsBuilderHttpServerTransportResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string http_response;
};

JsBuilderHttpServerTransportResult
js_builder_http_server_transport_dispatch(const std::string &raw_request,
                                          const JsBuilderHttpServerTransportOptions &options);

JsBuilderPublishTargetCatalog js_builder_http_server_transport_catalog_from_loaded_world();

#endif
