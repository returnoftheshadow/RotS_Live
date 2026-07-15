#include "js_publish_endpoint_service.h"

namespace {

bool stage_input_matches_authorized_request(const JsPublishEndpointStageInput &input)
{
    const JsPublishRequest &request = input.authorization_request;
    return request.operation == JsPublishOperation::PackageStage && request.has_package
        && request.vnum == request.package.vnum && request.host == request.package.host
        && request.builder_account_id == input.stage_options.identity_options.builder_account_id
        && request.package_id == request.package.package_id
        && request.manifest_checksum == request.package.manifest_checksum;
}

bool status_input_has_package_authority(const JsPublishEndpointStatusInput &input)
{
    return input.authorization_request.package_id == input.package_id
        && input.authorization_request.operation == JsPublishOperation::StatusRead;
}

JsStagedPackageStageOptions stage_options_from_authorized_request(
    const JsPublishEndpointStageInput &input,
    const JsPublishEndpointServiceOptions &service_options)
{
    JsStagedPackageStageOptions options;
    options.identity_options.zone = input.authorization_request.zone;
    options.identity_options.builder_account_id = input.authorization_request.builder_account_id;
    options.identity_options.base_live_checksum = input.authorization_request.base_live_checksum;
    options.identity_options.server_instance_id = service_options.server_instance_id;
    options.identity_options.package_validation_options = service_options.package_validation_options;
    options.audit.staged_at_epoch_seconds = input.authorization_options.now_epoch_seconds;
    options.audit.request_id = input.authorization_request.request_id;
    options.audit.actor_id = input.authorization_request.actor_id;
    options.audit.permission_snapshot_id = input.authorization_request.token.token_id;
    options.audit.audit_id = input.audit_id;
    options.audit.source_policy_decision = "publish-preflight:accepted";
    options.audit.validation_report_digest =
        input.authorization_request.package.compiled_javascript_checksum;
    options.audit.transport_source_identifier =
        input.authorization_request.transport.source_identifier.empty()
        ? "transport:verified"
        : input.authorization_request.transport.source_identifier;
    return options;
}

JsPublishAuthorizationResult invalid_stage_authorization_result()
{
    JsPublishAuthorizationResult result;
    result.diagnostics.push_back(
        { JsPublishDiagnosticCode::InvalidRequest, {}, {}, "stage request did not match" });
    return result;
}

JsPublishAuthorizationResult status_permission_authorization_result()
{
    JsPublishAuthorizationResult result;
    result.diagnostics.push_back(
        { JsPublishDiagnosticCode::PermissionMismatch, {}, {}, "status request was not authorized" });
    return result;
}

JsPublishActivationResult invalid_activation_request_result()
{
    JsPublishActivationResult result;
    result.diagnostics.push_back(
        { JsPublishActivationDiagnosticCode::InvalidRequest, "endpoint operation mismatch" });
    return result;
}

JsPublishEndpointResponse unauthorized_status_response(
    const JsPublishAuthorizationResult &authorization)
{
    JsPublishEndpointResponse response;
    response.operation = "status";
    response.ok = false;
    response.http_status = 403;
    response.reason_code = "status.authorization-failed";
    response.message = "Package status was not authorized.";
    for (const JsPublishDiagnostic &diagnostic : authorization.diagnostics) {
        if (diagnostic.code == JsPublishDiagnosticCode::InvalidRequest) {
            response.http_status = 400;
            response.reason_code = "status.invalid-request";
            response.diagnostics.push_back("status request is invalid");
        } else if (diagnostic.code == JsPublishDiagnosticCode::MissingScope) {
            response.diagnostics.push_back("required publish scope is missing");
        } else {
            response.diagnostics.push_back("package status authorization failed");
        }
    }
    if (response.diagnostics.empty())
        response.diagnostics.push_back("package status authorization failed");
    return response;
}

} // namespace

JsPublishEndpointService::JsPublishEndpointService(JsLivePackageStore &live_store)
    : m_live_store(live_store)
{
}

JsPublishEndpointService::JsPublishEndpointService(
    JsLivePackageStore &live_store, const JsPublishEndpointServiceOptions &options)
    : m_live_store(live_store)
    , m_options(options)
{
}

JsPublishEndpointService::JsPublishEndpointService(
    JsLivePackageStore &live_store, const JsStagedPackageRepositoryOptions &staged_options)
    : m_live_store(live_store)
    , m_staged_repository(staged_options)
{
}

JsPublishEndpointService::JsPublishEndpointService(
    JsLivePackageStore &live_store, const JsStagedPackageRepositoryOptions &staged_options,
    const JsPublishEndpointServiceOptions &options)
    : m_live_store(live_store)
    , m_staged_repository(staged_options)
    , m_options(options)
{
}

JsPublishEndpointServiceResult
JsPublishEndpointService::with_json(const JsPublishEndpointResponse &response) const
{
    JsPublishEndpointServiceResult result;
    result.response = response;
    result.json = js_publish_endpoint_response_body_json(response);
    return result;
}

JsPublishEndpointServiceResult
JsPublishEndpointService::stage(const JsPublishEndpointStageInput &input)
{
    JsPublishAuthorizationOptions authorization_options = input.authorization_options;
    authorization_options.package_validation_options = m_options.package_validation_options;
    JsPublishAuthorizationResult authorization = stage_input_matches_authorized_request(input)
        ? js_publish_authorization_preflight(input.authorization_request, authorization_options)
        : invalid_stage_authorization_result();
    if (!authorization.ok) {
        JsPublishStagePreflightEndpointInput preflight;
        preflight.authorization_result = authorization;
        preflight.package_id = input.authorization_request.package_id;
        preflight.current_live_checksum = input.authorization_options.current_live_checksum;
        preflight.audit_id = input.audit_id;
        return with_json(js_publish_endpoint_stage_preflight_response(preflight));
    }

    return with_json(js_publish_endpoint_stage_response(
        m_staged_repository.stage_package(input.authorization_request.package,
            stage_options_from_authorized_request(input, m_options))));
}

JsPublishEndpointServiceResult
JsPublishEndpointService::status(const JsPublishEndpointStatusInput &input) const
{
    if (!status_input_has_package_authority(input)) {
        JsPublishAuthorizationResult invalid = invalid_stage_authorization_result();
        return with_json(unauthorized_status_response(invalid));
    }

    JsStagedPackageLookupResult latest =
        m_staged_repository.find_latest_for_package(input.package_id);
    if (!latest.ok
        || latest.record.identity.builder_account_id
            != input.authorization_request.builder_account_id)
        return with_json(unauthorized_status_response(status_permission_authorization_result()));

    JsPublishAuthorizationOptions authorization_options = input.authorization_options;
    authorization_options.authority = {};
    authorization_options.authority.has_package_authority = true;
    authorization_options.authority.zone = latest.record.identity.zone;
    authorization_options.authority.vnum = latest.record.identity.vnum;
    authorization_options.authority.host = latest.record.identity.host;
    authorization_options.authority.package_id = latest.record.identity.package_id;
    authorization_options.authority.package_owner_builder_account_id =
        latest.record.identity.builder_account_id;

    JsPublishAuthorizationResult authorization =
        js_publish_authorization_preflight(input.authorization_request, authorization_options);
    if (!authorization.ok)
        return with_json(unauthorized_status_response(authorization));

    JsPublishStagedPackageStatusResult status;
    status.ok = true;
    status.status.zone = latest.record.identity.zone;
    status.status.vnum = latest.record.identity.vnum;
    status.status.host = latest.record.identity.host;
    status.status.package_id = latest.record.identity.package_id;
    status.status.package_version_id = latest.record.identity.package_version_id;
    status.status.staged_digest = latest.record.identity.canonical_digest;
    status.status.base_live_checksum = latest.record.identity.base_live_checksum;
    status.status.audit_id = latest.record.audit.audit_id;
    const JsLivePackagePointerResult live_pointer =
        m_live_store.find_live_pointer(latest.record.identity.zone, latest.record.identity.host,
            latest.record.identity.vnum);
    if (live_pointer.ok)
        status.status.current_live_checksum = live_pointer.pointer.current_live_checksum;
    return with_json(js_publish_endpoint_status_response(status));
}

JsPublishEndpointServiceResult
JsPublishEndpointService::activate(const JsPublishStagedRequestAssemblyInput &input,
                                   const JsPublishActivationOptions &options)
{
    if (input.operation != JsPublishOperation::PackageActivate)
        return with_json(js_publish_endpoint_activation_response(invalid_activation_request_result()));

    return with_json(js_publish_endpoint_activation_response(
        js_publish_apply_staged_package_activation(m_staged_repository, m_live_store, input,
            options)));
}

JsPublishEndpointServiceResult
JsPublishEndpointService::rollback(const JsPublishStagedRequestAssemblyInput &input,
                                   const JsPublishActivationOptions &options)
{
    if (input.operation != JsPublishOperation::PackageRollbackOwn
        && input.operation != JsPublishOperation::PackageRollbackAny)
        return with_json(js_publish_endpoint_rollback_response(invalid_activation_request_result()));

    JsLivePackageStoreRecordResult record =
        m_live_store.find_record(input.package_id, input.package_version_id);
    if (!record.ok)
        return with_json(js_publish_endpoint_rollback_response(invalid_activation_request_result()));

    return with_json(js_publish_endpoint_rollback_response(
        js_publish_apply_live_package_activation(record.record, m_live_store, input, options)));
}

const JsStagedPackageRepository &JsPublishEndpointService::staged_repository() const
{
    return m_staged_repository;
}

const JsLivePackageStore &JsPublishEndpointService::live_store() const
{
    return m_live_store;
}
