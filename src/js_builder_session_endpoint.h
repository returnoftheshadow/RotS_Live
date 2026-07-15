#ifndef JS_BUILDER_SESSION_ENDPOINT_H
#define JS_BUILDER_SESSION_ENDPOINT_H

#include "js_builder_session.h"

#include <cstddef>
#include <functional>
#include <string>

struct JsBuilderSessionEndpointRequest {
    std::string method;
    std::string path;
    std::string content_type;
    std::string body;
    std::string bearer_token;
    // Derived by the server transport from the validated proxy trust marker.
    // This must never be filled from BuilderClient-controlled JSON.
    bool trusted_proxy = false;
};

using JsBuilderSessionLogoutHandler =
    std::function<bool(const std::string &session_token, std::string *error_message)>;

struct JsBuilderSessionEndpointOptions {
    std::string login_path = "/api/builder/login";
    std::string logout_path = "/api/builder/logout";
    std::size_t maximum_request_bytes = 16 * 1024;
    JsBuilderSessionOptions session_options;
    JsBuilderSessionLogoutHandler logout_handler;
};

struct JsBuilderSessionEndpointResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string json;
    JsBuilderSessionLoginResult login_result;
};

JsBuilderSessionEndpointResult js_builder_session_endpoint_dispatch(
    const JsBuilderSessionEndpointRequest &request,
    const JsBuilderSessionEndpointOptions &options = {});

#endif
