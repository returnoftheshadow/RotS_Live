#ifndef JS_PUBLISH_STAGING_H
#define JS_PUBLISH_STAGING_H

#include "js_staged_package_repository.h"

#include <string>
#include <vector>

enum class JsPublishStagingDiagnosticCode {
    InvalidRequest,
    StagedPackageNotFound,
};

struct JsPublishStagedPackageStatus {
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string digest_algorithm;
    int canonical_format_version = 0;
    int package_format_version = 0;
    long long staged_at_epoch_seconds = 0;
    std::string base_live_checksum;
    std::string manifest_checksum;
    std::string compiled_javascript_checksum;
    std::string runtime_name;
    std::string runtime_version;
    std::string generated_typings_version;
};

struct JsPublishStagingDiagnostic {
    JsPublishStagingDiagnosticCode code = JsPublishStagingDiagnosticCode::InvalidRequest;
    std::string message;
};

struct JsPublishStagedPackageStatusResult {
    bool ok = false;
    JsPublishStagedPackageStatus status;
    std::vector<JsPublishStagingDiagnostic> diagnostics;
};

struct JsPublishStagedRequestAssemblyInput {
    JsPublishOperation operation = JsPublishOperation::PackageActivate;
    std::string request_id;
    std::string actor_id;
    std::string builder_account_id;
    std::string package_id;
    std::string package_version_id;
    std::string expected_live_checksum;
    JsPublishTokenMetadata token;
    JsPublishTransportMetadata transport;
};

struct JsPublishStagedRequestAssemblyOptions {
    long long now_epoch_seconds = 0;
    bool allow_mutating_operations = false;
    bool rate_limited = false;
    bool allow_rollback_any = false;
    std::string expected_server_audience;
    std::string expected_workspace_id;
    std::string current_live_checksum;
};

struct JsPublishStagedRequestAssemblyResult {
    bool assembled = false;
    JsPublishRequest request;
    JsPublishAuthorizationOptions authorization_options;
    JsPublishAuthorizationResult authorization_result;
    JsPublishStagedPackageStatus status;
    std::vector<JsPublishStagingDiagnostic> diagnostics;
};

// Internal post-authorization metadata helpers. Do not expose these directly from a status
// endpoint without a caller/owner/workspace authorization check at the endpoint boundary.
JsPublishStagedPackageStatusResult
js_publish_staged_package_status(const JsStagedPackageRepository& repository,
    const std::string& package_id, const std::string& package_version_id);

JsPublishStagedPackageStatusResult
js_publish_latest_staged_package_status(const JsStagedPackageRepository& repository,
    const std::string& package_id);

JsPublishStagedRequestAssemblyResult
js_publish_assemble_staged_package_request(const JsStagedPackageRepository& repository,
    const JsPublishStagedRequestAssemblyInput& input,
    const JsPublishStagedRequestAssemblyOptions& options = {});

const char* js_publish_staging_diagnostic_code_name(JsPublishStagingDiagnosticCode code);

#endif
