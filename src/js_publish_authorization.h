#ifndef JS_PUBLISH_AUTHORIZATION_H
#define JS_PUBLISH_AUTHORIZATION_H

#include "js_script_package.h"

#include <string>
#include <vector>

enum class JsPublishOperation {
    ManifestRead,
    StatusRead,
    DiffRead,
    SourceView,
    AdminSourceView,
    PackageStage,
    PackageActivate,
    PackageRollbackOwn,
    PackageRollbackAny,
    AdminRevoke,
    PackageGarbageCollect,
};

enum class JsPublishDiagnosticCode {
    InvalidRequest,
    MissingScope,
    TokenExpired,
    TokenRevoked,
    TokenAudienceMismatch,
    TokenServerMismatch,
    TokenActorMismatch,
    TransportRejected,
    RateLimited,
    PermissionMismatch,
    PackagePreconditionMismatch,
    PackageValidationFailed,
    PublishingDisabled,
};

enum JsPublishScope : unsigned {
    JS_PUBLISH_SCOPE_MANIFEST_READ = 1u << 0,
    JS_PUBLISH_SCOPE_STATUS_READ = 1u << 1,
    JS_PUBLISH_SCOPE_DIFF_READ = 1u << 2,
    JS_PUBLISH_SCOPE_SOURCE_VIEW = 1u << 3,
    JS_PUBLISH_SCOPE_PACKAGE_STAGE = 1u << 4,
    JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE = 1u << 5,
    JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN = 1u << 6,
    JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_ANY = 1u << 7,
    JS_PUBLISH_SCOPE_ADMIN_SOURCE_VIEW = 1u << 8,
    JS_PUBLISH_SCOPE_ADMIN_REVOKE = 1u << 9,
    JS_PUBLISH_SCOPE_PACKAGE_GC = 1u << 10,
};

struct JsPublishTokenMetadata {
    bool claims_verified = false;
    std::string token_id;
    std::string actor_id;
    std::string builder_account_id;
    std::string server_audience;
    std::string workspace_id;
    unsigned scopes = 0;
    long long issued_at_epoch_seconds = 0;
    long long expires_at_epoch_seconds = 0;
    bool revoked = false;
};

struct JsPublishTransportMetadata {
    bool secure_channel = false;
    bool localhost_development = false;
    bool server_identity_verified = false;
    bool downgrade_detected = false;
    bool redirected_with_authorization = false;
    std::string server_audience;
    std::string source_identifier;
};

struct JsPublishAuthorityContext {
    bool has_package_authority = false;
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    std::string package_id;
    std::string package_owner_builder_account_id;
    bool allow_admin_source_view = false;
    bool allow_rollback_any = false;
    bool allow_admin_revoke = false;
    bool allow_package_gc = false;
    bool staged_record_loaded = false;
    std::string package_version_id;
    std::string staged_digest;
    std::string manifest_checksum;
};

struct JsPublishRequest {
    JsPublishOperation operation = JsPublishOperation::StatusRead;
    std::string request_id;
    std::string actor_id;
    std::string builder_account_id;
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string base_live_checksum;
    std::string expected_live_checksum;
    std::string manifest_checksum;
    bool has_package = false;
    JsScriptPackage package;
    JsPublishTokenMetadata token;
    JsPublishTransportMetadata transport;
};

struct JsPublishAuthorizationOptions {
    long long now_epoch_seconds = 0;
    bool allow_mutating_operations = false;
    bool rate_limited = false;
    std::string expected_server_audience;
    std::string expected_workspace_id;
    std::string current_live_checksum;
    JsPublishAuthorityContext authority;
};

struct JsPublishDiagnostic {
    JsPublishDiagnosticCode code = JsPublishDiagnosticCode::InvalidRequest;
    std::string request_id;
    std::string package_id;
    std::string message;
};

struct JsPublishAuthorizationResult {
    bool ok = false;
    bool mutates_server_state = false;
    std::vector<JsPublishDiagnostic> diagnostics;
    JsScriptPackageValidationResult package_validation;
};

JsPublishAuthorizationResult js_publish_authorization_preflight(const JsPublishRequest& request,
    const JsPublishAuthorizationOptions& options = {});

unsigned js_publish_scope_for_operation(JsPublishOperation operation);
bool js_publish_operation_mutates_server_state(JsPublishOperation operation);

const char* js_publish_operation_name(JsPublishOperation operation);
const char* js_publish_diagnostic_code_name(JsPublishDiagnosticCode code);

#endif
