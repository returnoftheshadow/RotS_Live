#include "js_publish_authorization.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 220;

std::string diagnostic_identifier(const std::string& value, const char* label)
{
    return value.empty() ? std::string() : std::string(label) + ":redacted";
}

std::string bounded_single_line(std::string message)
{
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

void add_diagnostic(JsPublishAuthorizationResult& result, JsPublishDiagnosticCode code,
    const JsPublishRequest& request, const std::string& message)
{
    JsPublishDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.request_id = diagnostic_identifier(request.request_id, "request");
    diagnostic.package_id = diagnostic_identifier(request.package_id, "package");
    diagnostic.message = bounded_single_line(message);
    result.diagnostics.push_back(diagnostic);
}

bool is_blank(const std::string& value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    });
}

bool has_scope(const JsPublishTokenMetadata& token, unsigned scope)
{
    return (token.scopes & scope) == scope;
}

void validate_request_shape(const JsPublishRequest& request,
    JsPublishAuthorizationResult& result)
{
    if (is_blank(request.request_id))
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "request id is required");
    if (is_blank(request.actor_id))
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "actor id is required");
    if (is_blank(request.builder_account_id))
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "builder account id is required");
    if (request.zone < 0)
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "zone must not be negative");
    if (request.vnum <= 0)
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "vnum must be positive");
    if (is_blank(request.package_id)
        && (request.operation == JsPublishOperation::PackageStage
            || request.operation == JsPublishOperation::PackageActivate
            || request.operation == JsPublishOperation::PackageRollbackOwn
            || request.operation == JsPublishOperation::PackageRollbackAny))
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "package id is required for package mutation operations");
}

void validate_transport(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options, JsPublishAuthorizationResult& result)
{
    if (options.expected_server_audience.empty())
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "expected server audience is required");
    if (!request.transport.secure_channel && !request.transport.localhost_development)
        add_diagnostic(result, JsPublishDiagnosticCode::TransportRejected, request,
            "publish transport must be secure or explicit localhost development mode");
    if (!request.transport.server_identity_verified)
        add_diagnostic(result, JsPublishDiagnosticCode::TransportRejected, request,
            "server identity was not verified");
    if (request.transport.downgrade_detected)
        add_diagnostic(result, JsPublishDiagnosticCode::TransportRejected, request,
            "transport downgrade was detected");
    if (request.transport.redirected_with_authorization)
        add_diagnostic(result, JsPublishDiagnosticCode::TransportRejected, request,
            "authorization metadata must not be forwarded across redirects");
    if (request.transport.server_audience.empty()
        || request.transport.server_audience != options.expected_server_audience)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenServerMismatch, request,
            "transport server audience does not match the expected server");
}

void validate_token(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options, JsPublishAuthorizationResult& result)
{
    if (!request.token.claims_verified)
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "token claims must come from a verified server-side token lookup");
    if (is_blank(request.token.token_id))
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "token id is required");
    if (request.token.revoked)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenRevoked, request,
            "token has been revoked");
    if (options.now_epoch_seconds <= 0)
        add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
            "authorization clock is required");
    if (request.token.issued_at_epoch_seconds <= 0
        || request.token.expires_at_epoch_seconds <= request.token.issued_at_epoch_seconds)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenExpired, request,
            "token lifetime is invalid");
    if (options.now_epoch_seconds > 0
        && request.token.issued_at_epoch_seconds > options.now_epoch_seconds)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenExpired, request,
            "token was issued in the future");
    if (options.now_epoch_seconds > 0
        && request.token.expires_at_epoch_seconds <= options.now_epoch_seconds)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenExpired, request,
            "token has expired");
    if (request.token.actor_id != request.actor_id
        || request.token.builder_account_id != request.builder_account_id)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenActorMismatch, request,
            "token actor does not match request actor");
    if (request.token.server_audience.empty()
        || request.token.server_audience != options.expected_server_audience
        || request.token.server_audience != request.transport.server_audience)
        add_diagnostic(result, JsPublishDiagnosticCode::TokenAudienceMismatch, request,
            "token audience does not match expected server");
    if (options.expected_workspace_id.empty()
        || request.token.workspace_id != options.expected_workspace_id)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "token workspace does not match expected workspace");

    const unsigned required_scope = js_publish_scope_for_operation(request.operation);
    if (required_scope != 0 && !has_scope(request.token, required_scope))
        add_diagnostic(result, JsPublishDiagnosticCode::MissingScope, request,
            std::string("token lacks required scope for ")
                + js_publish_operation_name(request.operation));
}

bool operation_requires_package_authority(JsPublishOperation operation)
{
    switch (operation) {
    case JsPublishOperation::SourceView:
    case JsPublishOperation::AdminSourceView:
    case JsPublishOperation::PackageStage:
    case JsPublishOperation::PackageActivate:
    case JsPublishOperation::PackageRollbackOwn:
    case JsPublishOperation::PackageRollbackAny:
    case JsPublishOperation::AdminRevoke:
    case JsPublishOperation::PackageGarbageCollect:
        return true;
    case JsPublishOperation::ManifestRead:
    case JsPublishOperation::StatusRead:
    case JsPublishOperation::DiffRead:
        return false;
    }
    return true;
}

void validate_authority(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options, JsPublishAuthorizationResult& result)
{
    if (!operation_requires_package_authority(request.operation))
        return;

    const JsPublishAuthorityContext& authority = options.authority;
    if (!authority.has_package_authority) {
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "server package authority context is required");
        return;
    }
    if (authority.zone != request.zone || authority.vnum != request.vnum
        || authority.host != request.host || authority.package_id != request.package_id)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "request package scope does not match server authority context");

    if (request.operation == JsPublishOperation::SourceView
        && authority.package_owner_builder_account_id != request.builder_account_id)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "source-view requires package ownership");
    if (request.operation == JsPublishOperation::AdminSourceView
        && !authority.allow_admin_source_view)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "admin source-view authority is required");
    if (request.operation == JsPublishOperation::PackageActivate
        && authority.package_owner_builder_account_id != request.builder_account_id)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "package activation requires package ownership");
    if (request.operation == JsPublishOperation::PackageRollbackOwn
        && authority.package_owner_builder_account_id != request.builder_account_id)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "own rollback requires package ownership");
    if (request.operation == JsPublishOperation::PackageRollbackAny && !authority.allow_rollback_any)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "rollback-any authority is required");
    if (request.operation == JsPublishOperation::AdminRevoke && !authority.allow_admin_revoke)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "admin revoke authority is required");
    if (request.operation == JsPublishOperation::PackageGarbageCollect && !authority.allow_package_gc)
        add_diagnostic(result, JsPublishDiagnosticCode::PermissionMismatch, request,
            "package garbage-collection authority is required");

    if (request.operation == JsPublishOperation::PackageActivate
        || request.operation == JsPublishOperation::PackageRollbackOwn
        || request.operation == JsPublishOperation::PackageRollbackAny) {
        if (!authority.staged_record_loaded
            || authority.package_version_id != request.package_version_id
            || authority.staged_digest != request.staged_digest)
            add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
                "server staged package record does not match request");
    }
}

void validate_package_preconditions(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options, JsPublishAuthorizationResult& result)
{
    if (request.operation == JsPublishOperation::PackageStage) {
        if (is_blank(request.base_live_checksum))
            add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
                "stage request requires base live checksum");
        if (!options.current_live_checksum.empty()
            && request.base_live_checksum != options.current_live_checksum)
            add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
                "base live checksum does not match current live checksum");
        if (!request.has_package) {
            add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
                "stage request requires a package");
            return;
        }
        if (request.package.vnum != request.vnum || request.package.host != request.host
            || request.package.package_id != request.package_id)
            add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
                "package metadata does not match publish request metadata");
        if (request.package.manifest_checksum != request.manifest_checksum)
            add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
                "request manifest checksum does not match package manifest checksum");
    }

    if (request.operation == JsPublishOperation::PackageActivate
        || request.operation == JsPublishOperation::PackageRollbackOwn
        || request.operation == JsPublishOperation::PackageRollbackAny) {
        if (is_blank(request.package_version_id) || is_blank(request.staged_digest))
            add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
                "activation and rollback require exact package version id and staged digest");
        if (is_blank(request.expected_live_checksum))
            add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
                "activation and rollback require expected live checksum");
        if (is_blank(request.manifest_checksum))
            add_diagnostic(result, JsPublishDiagnosticCode::InvalidRequest, request,
                "activation and rollback require manifest checksum");
        if (!is_blank(request.manifest_checksum)
            && request.manifest_checksum != options.authority.manifest_checksum)
            add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
                "request manifest checksum does not match staged authority manifest checksum");
    }

    if (!options.current_live_checksum.empty()
        && !request.expected_live_checksum.empty()
        && request.expected_live_checksum != options.current_live_checksum)
        add_diagnostic(result, JsPublishDiagnosticCode::PackagePreconditionMismatch, request,
            "expected live checksum does not match current live checksum");
}

void validate_package(const JsPublishRequest& request, JsPublishAuthorizationResult& result)
{
    if (request.operation != JsPublishOperation::PackageStage || !request.has_package)
        return;

    result.package_validation = js_script_package_validate(request.package);
    if (!result.package_validation.ok)
        add_diagnostic(result, JsPublishDiagnosticCode::PackageValidationFailed, request,
            "package failed server publish validation");
}

} // namespace

JsPublishAuthorizationResult js_publish_authorization_preflight(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options)
{
    JsPublishAuthorizationResult result;
    result.mutates_server_state = js_publish_operation_mutates_server_state(request.operation);

    validate_request_shape(request, result);
    validate_transport(request, options, result);
    validate_token(request, options, result);
    validate_authority(request, options, result);

    if (options.rate_limited)
        add_diagnostic(result, JsPublishDiagnosticCode::RateLimited, request,
            "operation is rate limited");

    validate_package_preconditions(request, options, result);
    if (result.diagnostics.empty())
        validate_package(request, result);

    if (result.mutates_server_state && !options.allow_mutating_operations)
        add_diagnostic(result, JsPublishDiagnosticCode::PublishingDisabled, request,
            "mutating JavaScript publish operations are not enabled");

    result.ok = result.diagnostics.empty();
    return result;
}

unsigned js_publish_scope_for_operation(JsPublishOperation operation)
{
    switch (operation) {
    case JsPublishOperation::ManifestRead:
        return JS_PUBLISH_SCOPE_MANIFEST_READ;
    case JsPublishOperation::StatusRead:
        return JS_PUBLISH_SCOPE_STATUS_READ;
    case JsPublishOperation::DiffRead:
        return JS_PUBLISH_SCOPE_DIFF_READ;
    case JsPublishOperation::SourceView:
        return JS_PUBLISH_SCOPE_SOURCE_VIEW;
    case JsPublishOperation::AdminSourceView:
        return JS_PUBLISH_SCOPE_ADMIN_SOURCE_VIEW;
    case JsPublishOperation::PackageStage:
        return JS_PUBLISH_SCOPE_PACKAGE_STAGE;
    case JsPublishOperation::PackageActivate:
        return JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE;
    case JsPublishOperation::PackageRollbackOwn:
        return JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN;
    case JsPublishOperation::PackageRollbackAny:
        return JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_ANY;
    case JsPublishOperation::AdminRevoke:
        return JS_PUBLISH_SCOPE_ADMIN_REVOKE;
    case JsPublishOperation::PackageGarbageCollect:
        return JS_PUBLISH_SCOPE_PACKAGE_GC;
    }
    return 0;
}

bool js_publish_operation_mutates_server_state(JsPublishOperation operation)
{
    switch (operation) {
    case JsPublishOperation::ManifestRead:
    case JsPublishOperation::StatusRead:
    case JsPublishOperation::DiffRead:
    case JsPublishOperation::SourceView:
    case JsPublishOperation::AdminSourceView:
        return false;
    case JsPublishOperation::PackageStage:
    case JsPublishOperation::PackageActivate:
    case JsPublishOperation::PackageRollbackOwn:
    case JsPublishOperation::PackageRollbackAny:
    case JsPublishOperation::AdminRevoke:
    case JsPublishOperation::PackageGarbageCollect:
        return true;
    }
    return true;
}

const char* js_publish_operation_name(JsPublishOperation operation)
{
    switch (operation) {
    case JsPublishOperation::ManifestRead:
        return "manifest-read";
    case JsPublishOperation::StatusRead:
        return "status-read";
    case JsPublishOperation::DiffRead:
        return "diff-read";
    case JsPublishOperation::SourceView:
        return "source-view";
    case JsPublishOperation::AdminSourceView:
        return "admin-source-view";
    case JsPublishOperation::PackageStage:
        return "package-stage";
    case JsPublishOperation::PackageActivate:
        return "package-activate";
    case JsPublishOperation::PackageRollbackOwn:
        return "package-rollback-own";
    case JsPublishOperation::PackageRollbackAny:
        return "package-rollback-any";
    case JsPublishOperation::AdminRevoke:
        return "admin-revoke";
    case JsPublishOperation::PackageGarbageCollect:
        return "package-gc";
    }
    return "unknown";
}

const char* js_publish_diagnostic_code_name(JsPublishDiagnosticCode code)
{
    switch (code) {
    case JsPublishDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsPublishDiagnosticCode::MissingScope:
        return "missing-scope";
    case JsPublishDiagnosticCode::TokenExpired:
        return "token-expired";
    case JsPublishDiagnosticCode::TokenRevoked:
        return "token-revoked";
    case JsPublishDiagnosticCode::TokenAudienceMismatch:
        return "token-audience-mismatch";
    case JsPublishDiagnosticCode::TokenServerMismatch:
        return "token-server-mismatch";
    case JsPublishDiagnosticCode::TokenActorMismatch:
        return "token-actor-mismatch";
    case JsPublishDiagnosticCode::TransportRejected:
        return "transport-rejected";
    case JsPublishDiagnosticCode::RateLimited:
        return "rate-limited";
    case JsPublishDiagnosticCode::PermissionMismatch:
        return "permission-mismatch";
    case JsPublishDiagnosticCode::PackagePreconditionMismatch:
        return "package-precondition-mismatch";
    case JsPublishDiagnosticCode::PackageValidationFailed:
        return "package-validation-failed";
    case JsPublishDiagnosticCode::PublishingDisabled:
        return "publishing-disabled";
    }
    return "unknown";
}
