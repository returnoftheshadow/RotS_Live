#include "js_publish_endpoint_contract.h"

#include "json_utils.h"

#include <algorithm>

namespace {

bool has_activation_code(const JsPublishActivationResult &result,
    JsPublishActivationDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishActivationDiagnostic &diagnostic) {
            return diagnostic.code == code;
        });
}

bool has_authorization_code(const JsPublishActivationResult &result,
    JsPublishDiagnosticCode code)
{
    return std::any_of(result.assembly.authorization_result.diagnostics.begin(),
        result.assembly.authorization_result.diagnostics.end(),
        [code](const JsPublishDiagnostic &diagnostic) {
            return diagnostic.code == code;
        });
}

bool has_non_precondition_authorization_diagnostic(
    const std::vector<JsPublishDiagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const JsPublishDiagnostic &diagnostic) {
            return diagnostic.code != JsPublishDiagnosticCode::PackagePreconditionMismatch;
        });
}

bool has_precondition_authorization_diagnostic(
    const std::vector<JsPublishDiagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const JsPublishDiagnostic &diagnostic) {
            return diagnostic.code == JsPublishDiagnosticCode::PackagePreconditionMismatch;
        });
}

void add_stage_status_fields(JsPublishEndpointResponse &response,
    const JsPublishStagedPackageStatus &status)
{
    response.package_id = status.package_id;
    response.package_version_id = status.package_version_id;
    response.staged_digest = status.staged_digest;
    response.live_checksum = status.current_live_checksum.empty() ? status.base_live_checksum
                                                                   : status.current_live_checksum;
    response.audit_id = status.audit_id;
}

void add_stage_record_fields(JsPublishEndpointResponse &response,
    const JsStagedPackageRecord &record)
{
    response.package_id = record.identity.package_id;
    response.package_version_id = record.identity.package_version_id;
    response.staged_digest = record.identity.canonical_digest;
    response.live_checksum = record.identity.base_live_checksum;
    response.audit_id = record.audit.audit_id;
}

void add_diagnostics(JsPublishEndpointResponse &response,
    const std::vector<JsStagedPackageRepositoryDiagnostic> &diagnostics)
{
    for (const JsStagedPackageRepositoryDiagnostic &diagnostic : diagnostics) {
        switch (diagnostic.code) {
        case JsStagedPackageRepositoryDiagnosticCode::InvalidRequest:
        case JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed:
            response.diagnostics.push_back("stage request is invalid");
            break;
        case JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict:
            response.diagnostics.push_back("staged package version already exists");
            break;
        case JsStagedPackageRepositoryDiagnosticCode::RecordLimitExceeded:
            response.diagnostics.push_back("staged package repository is full");
            break;
        case JsStagedPackageRepositoryDiagnosticCode::NotFound:
            response.diagnostics.push_back("staged package was not found");
            break;
        }
    }
}

void add_diagnostics(JsPublishEndpointResponse &response,
    const std::vector<JsPublishStagingDiagnostic> &diagnostics)
{
    for (const JsPublishStagingDiagnostic &diagnostic : diagnostics) {
        switch (diagnostic.code) {
        case JsPublishStagingDiagnosticCode::InvalidRequest:
            response.diagnostics.push_back("status request is invalid");
            break;
        case JsPublishStagingDiagnosticCode::StagedPackageNotFound:
            response.diagnostics.push_back("staged package was not found");
            break;
        }
    }
}

void add_diagnostics(JsPublishEndpointResponse &response,
    const std::vector<JsPublishActivationDiagnostic> &diagnostics)
{
    for (const JsPublishActivationDiagnostic &diagnostic : diagnostics) {
        switch (diagnostic.code) {
        case JsPublishActivationDiagnosticCode::InvalidRequest:
            response.diagnostics.push_back("activation request is invalid");
            break;
        case JsPublishActivationDiagnosticCode::AssemblyFailed:
            response.diagnostics.push_back("staged package was not found");
            break;
        case JsPublishActivationDiagnosticCode::AuthorizationFailed:
            response.diagnostics.push_back("publish authorization failed");
            break;
        case JsPublishActivationDiagnosticCode::LiveUpdateDisabled:
            response.diagnostics.push_back("live package updates are disabled");
            break;
        case JsPublishActivationDiagnosticCode::AuditPreconditionFailed:
            response.diagnostics.push_back("publish audit precondition failed");
            break;
        case JsPublishActivationDiagnosticCode::LivePointerConflict:
            response.diagnostics.push_back("refresh status and retry against current live checksum");
            break;
        case JsPublishActivationDiagnosticCode::StoreFailed:
        case JsPublishActivationDiagnosticCode::PointerFailed:
        case JsPublishActivationDiagnosticCode::PersistenceFailed:
            response.diagnostics.push_back("live package update could not be persisted");
            break;
        }
    }
}

void add_authorization_diagnostics(JsPublishEndpointResponse &response,
    const std::vector<JsPublishDiagnostic> &diagnostics)
{
    for (const JsPublishDiagnostic &diagnostic : diagnostics) {
        switch (diagnostic.code) {
        case JsPublishDiagnosticCode::MissingScope:
            response.diagnostics.push_back("required publish scope is missing");
            break;
        case JsPublishDiagnosticCode::PackagePreconditionMismatch:
            response.diagnostics.push_back(
                "refresh status and rebuild against the latest live checksum");
            break;
        case JsPublishDiagnosticCode::PermissionMismatch:
            response.diagnostics.push_back("package permission check failed");
            break;
        case JsPublishDiagnosticCode::TransportRejected:
            response.diagnostics.push_back("publish transport was rejected");
            break;
        case JsPublishDiagnosticCode::TokenExpired:
        case JsPublishDiagnosticCode::TokenRevoked:
        case JsPublishDiagnosticCode::TokenAudienceMismatch:
        case JsPublishDiagnosticCode::TokenServerMismatch:
        case JsPublishDiagnosticCode::TokenActorMismatch:
            response.diagnostics.push_back("publish token is not valid for this request");
            break;
        case JsPublishDiagnosticCode::RateLimited:
            response.diagnostics.push_back("publish request is rate limited");
            break;
        case JsPublishDiagnosticCode::PackageValidationFailed:
            response.diagnostics.push_back("package failed server validation");
            break;
        case JsPublishDiagnosticCode::PublishingDisabled:
            response.diagnostics.push_back("publish operations are disabled");
            break;
        case JsPublishDiagnosticCode::InvalidRequest:
            response.diagnostics.push_back("publish request is invalid");
            break;
        }
    }
}

std::string activation_failure_reason(const JsPublishActivationResult &result,
    const char *operation)
{
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::LivePointerConflict))
        return std::string(operation) + ".stale-live-checksum";
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::AuthorizationFailed)) {
        if (has_authorization_code(result, JsPublishDiagnosticCode::MissingScope))
            return std::string(operation) + ".missing-scope";
        if (has_authorization_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch)
            && !has_non_precondition_authorization_diagnostic(
                result.assembly.authorization_result.diagnostics))
            return std::string(operation) + ".stale-live-checksum";
        return std::string(operation) + ".authorization-failed";
    }
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::AssemblyFailed))
        return std::string(operation) + ".staged-package-not-found";
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::LiveUpdateDisabled))
        return std::string(operation) + ".live-update-disabled";
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::AuditPreconditionFailed))
        return std::string(operation) + ".audit-precondition-failed";
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::PersistenceFailed))
        return std::string(operation) + ".persistence-failed";
    return std::string(operation) + ".rejected";
}

JsPublishEndpointResponse activation_response(const JsPublishActivationResult &result,
    const char *operation)
{
    JsPublishEndpointResponse response;
    response.operation = operation;
    const bool authorization_precondition_failed =
        has_activation_code(result, JsPublishActivationDiagnosticCode::AuthorizationFailed)
        && has_authorization_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch)
        && !has_non_precondition_authorization_diagnostic(
            result.assembly.authorization_result.diagnostics);
    if (result.ok || authorization_precondition_failed
        || has_activation_code(result, JsPublishActivationDiagnosticCode::LivePointerConflict))
        add_stage_status_fields(response, result.assembly.status);
    if (authorization_precondition_failed
        && !result.assembly.authorization_options.current_live_checksum.empty())
        response.live_checksum = result.assembly.authorization_options.current_live_checksum;
    if (has_activation_code(result, JsPublishActivationDiagnosticCode::LivePointerConflict)
        && !result.live_pointer_result.pointer.current_live_checksum.empty())
        response.live_checksum = result.live_pointer_result.pointer.current_live_checksum;
    if (!result.live_pointer_result.pointer.load_audit_id.empty())
        response.audit_id = result.live_pointer_result.pointer.load_audit_id;

    if (result.ok) {
        response.ok = true;
        response.http_status = 200;
        response.reason_code = std::string(operation) + ".accepted";
        response.message = std::string(operation) == "rollback"
            ? "Rollback activated prior package."
            : "Package activated.";
        if (!result.live_pointer_result.pointer.current_live_checksum.empty())
            response.live_checksum = result.live_pointer_result.pointer.current_live_checksum;
        return response;
    }

    response.ok = false;
    response.http_status = authorization_precondition_failed
            || has_activation_code(result, JsPublishActivationDiagnosticCode::LivePointerConflict)
        ? 409
        : has_activation_code(result, JsPublishActivationDiagnosticCode::AuditPreconditionFailed)
        ? 500
        : has_activation_code(result, JsPublishActivationDiagnosticCode::AuthorizationFailed)
        ? 403
        : 400;
    response.reason_code = activation_failure_reason(result, operation);
    response.message = std::string("Package ") + operation + " rejected.";
    add_diagnostics(response, result.diagnostics);
    add_authorization_diagnostics(response, result.assembly.authorization_result.diagnostics);
    return response;
}

} // namespace

JsPublishEndpointResponse
js_publish_endpoint_stage_response(const JsStagedPackageStageResult &result)
{
    JsPublishEndpointResponse response;
    response.operation = "stage";
    if (result.ok) {
        response.ok = true;
        response.http_status = 200;
        response.reason_code = result.idempotent ? "stage.idempotent" : "stage.accepted";
        response.message = result.idempotent ? "Package was already staged."
                                             : "Package staged and awaiting activation.";
        add_stage_record_fields(response, result.record);
        return response;
    }

    response.ok = false;
    response.http_status = result.diagnostics.empty() ? 400 : 409;
    response.reason_code = "stage.rejected";
    for (const JsStagedPackageRepositoryDiagnostic &diagnostic : result.diagnostics) {
        if (diagnostic.code == JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict) {
            response.http_status = 409;
            response.reason_code = "stage.version-conflict";
            break;
        }
        if (diagnostic.code == JsStagedPackageRepositoryDiagnosticCode::InvalidRequest
            || diagnostic.code == JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed) {
            response.http_status = 400;
            response.reason_code = "stage.invalid-request";
        }
    }
    response.message = "Package stage rejected.";
    add_diagnostics(response, result.diagnostics);
    return response;
}

JsPublishEndpointResponse
js_publish_endpoint_stage_preflight_response(
    const JsPublishStagePreflightEndpointInput &input)
{
    JsPublishEndpointResponse response;
    response.operation = "stage";
    if (input.authorization_result.ok) {
        response.ok = true;
        response.http_status = 200;
        response.reason_code = "stage.preflight-accepted";
        response.message = "Package stage preflight accepted.";
        response.package_id = input.package_id;
        response.live_checksum = input.current_live_checksum;
        response.audit_id = input.audit_id;
        return response;
    }

    response.ok = false;
    const bool precondition_only =
        has_precondition_authorization_diagnostic(input.authorization_result.diagnostics)
        && !has_non_precondition_authorization_diagnostic(input.authorization_result.diagnostics);
    if (precondition_only) {
        response.http_status = 409;
        response.reason_code = "stage.stale-live-checksum";
        response.message = "Live package changed before staging.";
        response.package_id = input.package_id;
        response.live_checksum = input.current_live_checksum;
        response.audit_id = input.audit_id;
    } else if (std::any_of(input.authorization_result.diagnostics.begin(),
                   input.authorization_result.diagnostics.end(),
                   [](const JsPublishDiagnostic &diagnostic) {
                       return diagnostic.code == JsPublishDiagnosticCode::MissingScope
                           || diagnostic.code == JsPublishDiagnosticCode::PermissionMismatch;
                   })) {
        response.http_status = 403;
        response.reason_code = "stage.authorization-failed";
        response.message = "Package stage was not authorized.";
    } else {
        response.http_status = 400;
        response.reason_code = "stage.invalid-request";
        response.message = "Package stage preflight rejected.";
    }
    add_authorization_diagnostics(response, input.authorization_result.diagnostics);
    return response;
}

JsPublishEndpointResponse
js_publish_endpoint_status_response(const JsPublishStagedPackageStatusResult &result)
{
    JsPublishEndpointResponse response;
    response.operation = "status";
    if (result.ok) {
        response.ok = true;
        response.http_status = 200;
        response.reason_code = "status.current";
        response.message = "Current staged and live metadata returned.";
        response.diagnostics.push_back("status is metadata-only; source is not included");
        add_stage_status_fields(response, result.status);
        return response;
    }

    response.ok = false;
    response.http_status = 404;
    response.reason_code = "status.not-found";
    for (const JsPublishStagingDiagnostic &diagnostic : result.diagnostics) {
        if (diagnostic.code == JsPublishStagingDiagnosticCode::InvalidRequest) {
            response.http_status = 400;
            response.reason_code = "status.invalid-request";
            break;
        }
    }
    response.message = "Staged package metadata was not found.";
    add_diagnostics(response, result.diagnostics);
    return response;
}

JsPublishEndpointResponse
js_publish_endpoint_activation_response(const JsPublishActivationResult &result)
{
    return activation_response(result, "activate");
}

JsPublishEndpointResponse
js_publish_endpoint_rollback_response(const JsPublishActivationResult &result)
{
    return activation_response(result, "rollback");
}

std::string js_publish_endpoint_response_json(const JsPublishEndpointResponse &response)
{
    std::string json;
    json.reserve(256);
    json += "{\"httpStatus\":";
    json += std::to_string(response.http_status);
    json += ",\"body\":";
    json += js_publish_endpoint_response_body_json(response);
    json += "}";
    return json;
}

std::string js_publish_endpoint_response_body_json(const JsPublishEndpointResponse &response)
{
    std::string json;
    json.reserve(220);
    json += "{";
    json += "\"ok\":";
    json += (response.ok ? "true" : "false");
    const auto add_string_field = [&json](const char *name, const std::string &value) {
        if (value.empty())
            return;
        json += ",\"";
        json += name;
        json += "\":\"";
        json_utils::append_escaped_json_string(json, value);
        json += "\"";
    };
    add_string_field("operation", response.operation);
    add_string_field("packageId", response.package_id);
    add_string_field("packageVersionId", response.package_version_id);
    add_string_field("stagedDigest", response.staged_digest);
    add_string_field("liveChecksum", response.live_checksum);
    add_string_field("reasonCode", response.reason_code);
    add_string_field("auditId", response.audit_id);
    add_string_field("message", response.message);
    json += ",\"diagnostics\":[";
    for (std::size_t index = 0; index < response.diagnostics.size(); ++index) {
        if (index != 0)
            json += ",";
        json += "\"";
        json_utils::append_escaped_json_string(json, response.diagnostics[index]);
        json += "\"";
    }
    json += "]}";
    return json;
}
