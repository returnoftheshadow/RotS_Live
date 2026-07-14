#ifndef JS_PUBLISH_ENDPOINT_CONTRACT_H
#define JS_PUBLISH_ENDPOINT_CONTRACT_H

#include "js_publish_activation.h"
#include "js_publish_staging.h"

#include <string>
#include <vector>

struct JsPublishEndpointResponse {
    bool ok = false;
    int http_status = 500;
    std::string operation;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string live_checksum;
    std::string reason_code;
    std::string audit_id;
    std::string message;
    std::vector<std::string> diagnostics;
};

struct JsPublishStagePreflightEndpointInput {
    JsPublishAuthorizationResult authorization_result;
    std::string package_id;
    std::string current_live_checksum;
    std::string audit_id;
};

JsPublishEndpointResponse
js_publish_endpoint_stage_response(const JsStagedPackageStageResult &result);

JsPublishEndpointResponse
js_publish_endpoint_stage_preflight_response(
    const JsPublishStagePreflightEndpointInput &input);

JsPublishEndpointResponse
js_publish_endpoint_status_response(const JsPublishStagedPackageStatusResult &result);

JsPublishEndpointResponse
js_publish_endpoint_activation_response(const JsPublishActivationResult &result);

JsPublishEndpointResponse
js_publish_endpoint_rollback_response(const JsPublishActivationResult &result);

std::string js_publish_endpoint_response_json(const JsPublishEndpointResponse &response);
std::string js_publish_endpoint_response_body_json(const JsPublishEndpointResponse &response);

#endif
