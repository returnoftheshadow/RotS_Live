#include "js_publish_endpoint_transport.h"

#include "js_script_package_loader.h"
#include "js_publish_audit.h"
#include "json_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::size_t MaxTransportDiagnosticBytes = 180;

struct TransportEnvelope {
    std::string operation;
    std::string package_id;
    std::string base_live_checksum;
    std::string staged_digest;
    std::string target_live_checksum;
    std::string reason;
    JsScriptPackage package;
    bool has_package = false;
    bool saw_package_id = false;
    bool saw_base_live_checksum = false;
    bool saw_staged_digest = false;
    bool saw_target_live_checksum = false;
    bool saw_reason = false;
};

std::string bounded_single_line(std::string message)
{
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxTransportDiagnosticBytes)
        message.resize(MaxTransportDiagnosticBytes);
    return message;
}

bool mark_seen(std::vector<std::string> &seen_fields, const std::string &key,
    std::string *error_message)
{
    if (std::find(seen_fields.begin(), seen_fields.end(), key) != seen_fields.end()) {
        *error_message = "duplicate JSON field '" + key + "'";
        return false;
    }
    seen_fields.push_back(key);
    return true;
}

JsPublishEndpointTransportResult transport_response(JsPublishEndpointResponse response)
{
    JsPublishEndpointTransportResult result;
    result.ok = response.ok;
    result.http_status = response.http_status;
    result.reason_code = response.reason_code;
    result.json = js_publish_endpoint_response_body_json(response);
    return result;
}

JsPublishEndpointTransportResult transport_error(int http_status, const char *reason_code,
    const char *message, const char *diagnostic)
{
    JsPublishEndpointResponse response;
    response.operation = "publish";
    response.ok = false;
    response.http_status = http_status;
    response.reason_code = reason_code;
    response.message = message;
    response.diagnostics.push_back(bounded_single_line(diagnostic));
    return transport_response(response);
}

JsPublishEndpointTransportResult normalized_authorization_failure(const char *reason_code,
    const char *message)
{
    return transport_error(403, reason_code, message, "publish authorization failed");
}

bool parse_host_name(const std::string &value, JsScriptPackageHost *host)
{
    if (value == js_script_package_host_name(JsScriptPackageHost::Character)) {
        *host = JsScriptPackageHost::Character;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Object)) {
        *host = JsScriptPackageHost::Object;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Room)) {
        *host = JsScriptPackageHost::Room;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::MudlleMobile)) {
        *host = JsScriptPackageHost::MudlleMobile;
        return true;
    }
    return false;
}

bool is_digits_only(const std::string &value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch);
    });
}

bool is_bounded_token_char(unsigned char ch)
{
    return std::isalnum(ch) || ch == ':' || ch == '_' || ch == '-' || ch == '.';
}

bool is_canonical_live_checksum(const std::string &value)
{
    const std::string prefix = "live:";
    return value.size() > prefix.size() && value.size() <= MaxTransportDiagnosticBytes
        && value.compare(0, prefix.size(), prefix) == 0
        && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return is_bounded_token_char(ch);
        });
}

bool is_canonical_staged_digest(const std::string &value)
{
    const std::string prefix = "sha256:";
    return value.size() > prefix.size() && value.size() <= MaxTransportDiagnosticBytes
        && value.compare(0, prefix.size(), prefix) == 0
        && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return is_bounded_token_char(ch);
        });
}

std::string digest_body(const std::string &digest)
{
    const std::string::size_type colon = digest.find(':');
    return colon == std::string::npos ? digest : digest.substr(colon + 1);
}

bool is_bounded_reason_text(const std::string &value)
{
    return !value.empty() && value.size() <= 240
        && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= 0x20 && ch != 0x7f;
        });
}

std::string live_checksum_from_status(const JsPublishStagedPackageStatus &status)
{
    return std::string("live:") + status.digest_algorithm + ":" + digest_body(status.staged_digest);
}

bool token_has_scope(const JsPublishTokenMetadata &token, unsigned scope)
{
    return (token.scopes & scope) == scope;
}

JsPublishZoneScriptingAuthorityResult stage_zone_authority_from_context(
    const TransportEnvelope &envelope, const JsPublishEndpointTransportContext &context)
{
    if (context.builder_eligibility.builder_account_id != context.builder_account_id) {
        JsPublishZoneScriptingAuthorityResult result;
        result.diagnostics.push_back({ JsPublishDiagnosticCode::PermissionMismatch, {}, {},
            "eligible builder account does not match publish context" });
        return result;
    }

    JsPublishZoneScriptingAuthorityInput input;
    input.builder = context.builder_eligibility;
    input.requested_zone = context.zone;
    input.requested_vnum = envelope.package.vnum;
    input.requested_host = envelope.package.host;
    input.target_zone_resolved = context.target_zone_resolved;
    input.server_resolved_target_zone = context.server_resolved_target_zone;
    input.server_resolved_target_host = context.server_resolved_target_host;
    input.zone_exists = context.zone_exists;
    input.zone_allows_all_builders = context.zone_allows_all_builders;
    input.zone_owner_character_ids = context.zone_owner_character_ids;
    return js_publish_evaluate_zone_scripting_authority(input);
}

JsPublishZoneScriptingAuthorityResult status_zone_authority_from_context(
    const JsPublishStagedPackageStatus &status,
    const JsPublishEndpointTransportContext &context)
{
    if (context.builder_eligibility.builder_account_id != context.builder_account_id) {
        JsPublishZoneScriptingAuthorityResult result;
        result.diagnostics.push_back({ JsPublishDiagnosticCode::PermissionMismatch, {}, {},
            "eligible builder account does not match publish context" });
        return result;
    }

    JsPublishZoneScriptingAuthorityInput input;
    input.builder = context.builder_eligibility;
    input.requested_zone = status.zone;
    input.requested_vnum = status.vnum;
    input.requested_host = status.host;
    input.target_zone_resolved = context.target_zone_resolved;
    input.server_resolved_target_zone = context.server_resolved_target_zone;
    input.server_resolved_target_host = context.server_resolved_target_host;
    input.zone_exists = context.zone_exists;
    input.zone_allows_all_builders = context.zone_allows_all_builders;
    input.zone_owner_character_ids = context.zone_owner_character_ids;
    return js_publish_evaluate_zone_scripting_authority(input);
}

JsPublishAuthorityContext stage_authority_from_context(
    const TransportEnvelope &envelope, const JsPublishEndpointTransportContext &context)
{
    const std::string canonical_package_id =
        js_staged_package_logical_package_id(context.zone, envelope.package.host,
            envelope.package.vnum);
    JsPublishAuthorityContext authority;
    authority.has_package_authority =
        stage_zone_authority_from_context(envelope, context).ok;
    authority.zone = context.zone;
    authority.vnum = envelope.package.vnum;
    authority.host = envelope.package.host;
    authority.package_id = canonical_package_id;
    authority.package_owner_builder_account_id = context.builder_account_id;
    return authority;
}

bool parse_logical_package_id(const std::string &package_id, int *zone,
    JsScriptPackageHost *host, int *vnum)
{
    const std::string prefix = "js:";
    if (package_id.compare(0, prefix.size(), prefix) != 0)
        return false;
    const std::string::size_type first = package_id.find(':', prefix.size());
    if (first == std::string::npos)
        return false;
    const std::string::size_type second = package_id.find(':', first + 1);
    if (second == std::string::npos)
        return false;
    if (package_id.find(':', second + 1) != std::string::npos)
        return false;

    const std::string zone_text = package_id.substr(prefix.size(), first - prefix.size());
    const std::string host_text = package_id.substr(first + 1, second - first - 1);
    const std::string vnum_text = package_id.substr(second + 1);
    if (!is_digits_only(zone_text) || !is_digits_only(vnum_text))
        return false;

    char *end = nullptr;
    const long parsed_zone = std::strtol(zone_text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed_zone <= 0
        || parsed_zone > std::numeric_limits<int>::max())
        return false;
    end = nullptr;
    const long parsed_vnum = std::strtol(vnum_text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed_vnum <= 0
        || parsed_vnum > std::numeric_limits<int>::max())
        return false;

    JsScriptPackageHost parsed_host;
    if (!parse_host_name(host_text, &parsed_host))
        return false;

    const int canonical_zone = static_cast<int>(parsed_zone);
    const int canonical_vnum = static_cast<int>(parsed_vnum);
    if (package_id != js_staged_package_logical_package_id(
            canonical_zone, parsed_host, canonical_vnum))
        return false;

    *zone = canonical_zone;
    *host = parsed_host;
    *vnum = canonical_vnum;
    return true;
}

bool parse_transport_envelope(const std::string &request_json, TransportEnvelope *envelope,
    std::string *error_message)
{
    bool saw_operation = false;
    std::vector<std::string> seen_fields;
    JsScriptPackageBundleLoadOptions package_options;
    json_utils::JsonReader reader(request_json);
    const bool parsed = reader.parse_root_object(
        [envelope, &saw_operation, &seen_fields, &package_options](
            const std::string &key, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "operation") {
                saw_operation = true;
                return nested_reader->parse_string(&envelope->operation, nested_error_message);
            }
            if (key == "packageId") {
                envelope->saw_package_id = true;
                return nested_reader->parse_string(&envelope->package_id, nested_error_message);
            }
            if (key == "baseLiveChecksum") {
                envelope->saw_base_live_checksum = true;
                return nested_reader->parse_string(
                    &envelope->base_live_checksum, nested_error_message);
            }
            if (key == "stagedDigest") {
                envelope->saw_staged_digest = true;
                return nested_reader->parse_string(
                    &envelope->staged_digest, nested_error_message);
            }
            if (key == "targetLiveChecksum") {
                envelope->saw_target_live_checksum = true;
                return nested_reader->parse_string(
                    &envelope->target_live_checksum, nested_error_message);
            }
            if (key == "reason") {
                envelope->saw_reason = true;
                return nested_reader->parse_string(&envelope->reason, nested_error_message);
            }
            if (key == "package") {
                envelope->has_package = true;
                return js_script_package_parse_json_object(nested_reader, package_options,
                    &envelope->package, nested_error_message);
            }
            *nested_error_message = "unknown publish request field '" + key + "'";
            return false;
        },
        error_message);
    if (!parsed)
        return false;
    if (!saw_operation) {
        *error_message = "missing required publish request field 'operation'";
        return false;
    }
    return true;
}

JsPublishEndpointStatusInput status_input_from_envelope(
    const TransportEnvelope &envelope, const JsPublishEndpointTransportContext &context)
{
    JsPublishEndpointStatusInput input;
    input.package_id = envelope.package_id;
    input.authorization_request.operation = JsPublishOperation::StatusRead;
    input.authorization_request.request_id = context.request_id;
    input.authorization_request.actor_id = context.actor_id;
    input.authorization_request.builder_account_id = context.builder_account_id;
    parse_logical_package_id(envelope.package_id, &input.authorization_request.zone,
        &input.authorization_request.host, &input.authorization_request.vnum);
    input.authorization_request.package_id = envelope.package_id;
    input.authorization_request.token = context.token;
    input.authorization_request.transport = context.transport;

    input.authorization_options.now_epoch_seconds = context.now_epoch_seconds;
    input.authorization_options.expected_server_audience = context.expected_server_audience;
    input.authorization_options.expected_workspace_id = context.expected_workspace_id;
    return input;
}

JsPublishEndpointStageInput stage_input_from_envelope(
    const TransportEnvelope &envelope, const JsPublishEndpointTransportContext &context)
{
    JsPublishEndpointStageInput input;
    JsScriptPackage package = envelope.package;
    package.package_id = js_staged_package_logical_package_id(context.zone, package.host,
        package.vnum);

    input.audit_id = context.audit_id;
    input.stage_options.identity_options.zone = context.zone;
    input.stage_options.identity_options.builder_account_id = context.builder_account_id;
    input.stage_options.identity_options.base_live_checksum = envelope.base_live_checksum;

    input.authorization_request.operation = JsPublishOperation::PackageStage;
    input.authorization_request.request_id = context.request_id;
    input.authorization_request.actor_id = context.actor_id;
    input.authorization_request.builder_account_id = context.builder_account_id;
    input.authorization_request.zone = context.zone;
    input.authorization_request.vnum = package.vnum;
    input.authorization_request.host = package.host;
    input.authorization_request.package_id = package.package_id;
    input.authorization_request.base_live_checksum = envelope.base_live_checksum;
    input.authorization_request.manifest_checksum = package.manifest_checksum;
    input.authorization_request.has_package = true;
    input.authorization_request.package = package;
    input.authorization_request.token = context.token;
    input.authorization_request.transport = context.transport;

    input.authorization_options.now_epoch_seconds = context.now_epoch_seconds;
    input.authorization_options.allow_mutating_operations = context.allow_mutating_operations;
    input.authorization_options.expected_server_audience = context.expected_server_audience;
    input.authorization_options.expected_workspace_id = context.expected_workspace_id;
    input.authorization_options.current_live_checksum = context.current_live_checksum;
    input.authorization_options.authority = stage_authority_from_context(envelope, context);
    return input;
}

JsPublishActivationOptions activation_options_from_context(
    const JsPublishEndpointTransportContext &context)
{
    JsPublishActivationOptions options;
    options.assembly_options.now_epoch_seconds = context.now_epoch_seconds;
    options.assembly_options.allow_mutating_operations = context.allow_mutating_operations;
    options.assembly_options.allow_rollback_any = context.allow_rollback_any;
    options.assembly_options.expected_server_audience = context.expected_server_audience;
    options.assembly_options.expected_workspace_id = context.expected_workspace_id;
    options.assembly_options.current_live_checksum = context.current_live_checksum;
    options.allow_live_pointer_update = context.allow_live_pointer_update;
    options.durable_audit_precondition_ok = false;
    options.applied_at_epoch_seconds = context.applied_at_epoch_seconds;
    options.live_pointer_audit_id = context.audit_id;
    options.persist_live_store_path = context.live_store_persistence_path;
    return options;
}

JsPublishAuditAppendResult append_publish_audit_event(
    const JsPublishStagedPackageStatus &status,
    const JsPublishEndpointTransportContext &context,
    const std::string &operation,
    const std::string &expected_previous_live_checksum,
    const std::string &current_live_checksum)
{
    JsPublishAuditEvent event;
    event.operation = operation;
    event.audit_id = context.audit_id;
    event.request_id = context.request_id;
    event.actor_id = context.actor_id;
    event.builder_account_id = context.builder_account_id;
    event.package_id = status.package_id;
    event.package_version_id = status.package_version_id;
    event.staged_digest = status.staged_digest;
    event.expected_previous_live_checksum = expected_previous_live_checksum;
    event.current_live_checksum = current_live_checksum;
    event.occurred_at_epoch_seconds = context.applied_at_epoch_seconds;

    JsPublishAuditAppendOptions options;
    options.path = context.publish_audit_log_path;
    return js_publish_audit_append_event(event, options);
}

JsPublishStagedPackageStatus status_from_live_pointer(const JsLivePackagePointer &pointer)
{
    JsPublishStagedPackageStatus status;
    status.zone = pointer.zone;
    status.vnum = pointer.vnum;
    status.host = pointer.host;
    status.package_id = pointer.package_id;
    status.package_version_id = pointer.package_version_id;
    status.staged_digest = pointer.staged_digest;
    status.current_live_checksum = pointer.current_live_checksum;
    return status;
}

JsPublishStagedRequestAssemblyInput staged_request_from_status(
    const JsPublishStagedPackageStatus &status, JsPublishOperation operation,
    const std::string &expected_live_checksum,
    const JsPublishEndpointTransportContext &context)
{
    JsPublishStagedRequestAssemblyInput input;
    input.operation = operation;
    input.request_id = context.request_id;
    input.actor_id = context.actor_id;
    input.builder_account_id = context.builder_account_id;
    input.package_id = status.package_id;
    input.package_version_id = status.package_version_id;
    input.expected_live_checksum = expected_live_checksum;
    input.token = context.token;
    input.transport = context.transport;
    return input;
}

} // namespace

JsPublishEndpointSessionContextResult js_publish_endpoint_context_from_builder_session(
    const JsPublishEndpointTransportContext &base_context,
    const JsBuilderSessionStore &session_store, const std::string &bearer_token,
    const JsBuilderSessionStoreOptions &session_options)
{
    JsPublishEndpointSessionContextResult result;
    result.context = base_context;
    JsBuilderSessionStoreResult session =
        session_store.lookup(bearer_token, session_options);
    result.ok = session.ok;
    result.session_reason = session.reason;
    result.reason_code = session.reason_code;
    if (!session.ok) {
        result.context.token = JsPublishTokenMetadata();
        result.context.builder_eligibility = JsPublishBuilderEligibilityResult();
        result.context.actor_id.clear();
        result.context.builder_account_id.clear();
        result.context.now_epoch_seconds = 0;
        result.context.expected_server_audience.clear();
        result.context.expected_workspace_id.clear();
        return result;
    }

    result.context.token = session.token_metadata;
    result.context.builder_eligibility = session.builder_eligibility;
    result.context.actor_id = session.token_metadata.actor_id;
    result.context.builder_account_id = session.token_metadata.builder_account_id;
    result.context.now_epoch_seconds = session_options.now_epoch_seconds;
    result.context.expected_server_audience = session_options.server_audience;
    result.context.expected_workspace_id = session_options.workspace_id;
    return result;
}

JsPublishEndpointTransportResult js_publish_endpoint_dispatch_json(
    JsPublishEndpointService &service, const std::string &request_json,
    const JsPublishEndpointTransportContext &context,
    const JsPublishEndpointTransportOptions &options)
{
    if (request_json.empty())
        return transport_error(400, "publish.invalid-request", "Publish request rejected.",
            "publish request body is required");
    if (request_json.size() > options.maximum_request_bytes)
        return transport_error(413, "publish.request-too-large", "Publish request rejected.",
            "publish request body exceeds the configured size limit");

    TransportEnvelope envelope;
    std::string error_message;
    if (!parse_transport_envelope(request_json, &envelope, &error_message))
        return transport_error(400, "publish.invalid-json", "Publish request rejected.",
            error_message.empty() ? "publish request JSON could not be parsed"
                                  : error_message.c_str());

    if (envelope.operation == "status") {
        if (!envelope.saw_package_id || envelope.has_package
            || envelope.saw_base_live_checksum || envelope.saw_staged_digest
            || envelope.saw_target_live_checksum || envelope.saw_reason)
            return transport_error(400, "status.invalid-request", "Package status rejected.",
                "status request body is invalid");
        int zone = 0;
        int vnum = 0;
        JsScriptPackageHost host = JsScriptPackageHost::Character;
        if (!parse_logical_package_id(envelope.package_id, &zone, &host, &vnum))
            return transport_error(400, "status.invalid-request", "Package status rejected.",
                "status request package id is invalid");

        return transport_response(
            service.status(status_input_from_envelope(envelope, context)).response);
    }

    if (envelope.operation == "stage") {
        if (envelope.saw_package_id || !envelope.has_package
            || !envelope.saw_base_live_checksum || envelope.base_live_checksum.empty()
            || envelope.saw_staged_digest || envelope.saw_target_live_checksum
            || envelope.saw_reason)
            return transport_error(400, "stage.invalid-request", "Package stage rejected.",
                "stage request body is invalid");
        if (!is_canonical_live_checksum(envelope.base_live_checksum))
            return transport_error(400, "stage.invalid-request", "Package stage rejected.",
                "stage base live checksum is invalid");

        return transport_response(
            service.stage(stage_input_from_envelope(envelope, context)).response);
    }

    if (envelope.operation == "activate") {
        if (!envelope.saw_package_id || envelope.has_package
            || !envelope.saw_staged_digest || !envelope.saw_base_live_checksum
            || envelope.saw_target_live_checksum || envelope.saw_reason
            || !is_canonical_live_checksum(envelope.base_live_checksum)
            || !is_canonical_staged_digest(envelope.staged_digest))
            return transport_error(400, "activate.invalid-request",
                "Package activate rejected.", "activate request body is invalid");
        int zone = 0;
        int vnum = 0;
        JsScriptPackageHost host = JsScriptPackageHost::Character;
        if (!parse_logical_package_id(envelope.package_id, &zone, &host, &vnum))
            return transport_error(400, "activate.invalid-request",
                "Package activate rejected.", "activate package id is invalid");
        if (!token_has_scope(context.token, JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE))
            return transport_error(403, "activate.missing-scope",
                "Package activate rejected.", "required publish scope is missing");
        JsPublishStagedPackageStatusResult latest =
            js_publish_latest_staged_package_status(
                service.staged_repository(), envelope.package_id);
        if (!latest.ok || latest.status.staged_digest != envelope.staged_digest)
            return normalized_authorization_failure("activate.authorization-failed",
                "Package activate rejected.");
        JsPublishEndpointTransportContext activation_context = context;
        if (activation_context.current_live_checksum.empty()
            || activation_context.current_live_checksum == envelope.base_live_checksum)
            activation_context.current_live_checksum = latest.status.base_live_checksum;
        if (!status_zone_authority_from_context(latest.status, activation_context).ok)
            return normalized_authorization_failure("activate.authorization-failed",
                "Package activate rejected.");

        JsPublishActivationOptions activation_options =
            activation_options_from_context(activation_context);
        activation_options.durable_audit_append =
            [activation_context, expected_live_checksum = envelope.base_live_checksum](
                const JsPublishStagedPackageStatus &status) {
                return append_publish_audit_event(status, activation_context, "activate",
                    expected_live_checksum, live_checksum_from_status(status)).ok;
            };
        JsPublishEndpointTransportResult result = transport_response(service.activate(
            staged_request_from_status(latest.status, JsPublishOperation::PackageActivate,
                envelope.base_live_checksum, activation_context),
            activation_options).response);
        if (!result.ok && result.http_status == 403)
            return normalized_authorization_failure("activate.authorization-failed",
                "Package activate rejected.");
        return result;
    }

    if (envelope.operation == "rollback") {
        if (!envelope.saw_package_id || envelope.has_package
            || envelope.saw_staged_digest || envelope.saw_base_live_checksum
            || !envelope.saw_target_live_checksum
            || (envelope.saw_reason && !is_bounded_reason_text(envelope.reason))
            || !is_canonical_live_checksum(envelope.target_live_checksum))
            return transport_error(400, "rollback.invalid-request",
                "Package rollback rejected.", "rollback request body is invalid");
        int zone = 0;
        int vnum = 0;
        JsScriptPackageHost host = JsScriptPackageHost::Character;
        if (!parse_logical_package_id(envelope.package_id, &zone, &host, &vnum))
            return transport_error(400, "rollback.invalid-request",
                "Package rollback rejected.", "rollback package id is invalid");
        const bool rollback_any = context.allow_rollback_any;
        const unsigned rollback_scope = rollback_any ? JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_ANY
                                                     : JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN;
        if (!token_has_scope(context.token, rollback_scope))
            return transport_error(403, "rollback.missing-scope",
                "Package rollback rejected.", "required publish scope is missing");
        JsLivePackagePointerResult current =
            service.live_store().find_live_pointer(zone, host, vnum);
        if (!current.ok)
            return normalized_authorization_failure("rollback.authorization-failed",
                "Package rollback rejected.");
        JsLivePackagePointerResult prior =
            service.live_store().find_latest_prior_live_pointer(zone, host, vnum);
        if (!prior.ok)
            return normalized_authorization_failure("rollback.authorization-failed",
                "Package rollback rejected.");
        JsPublishStagedPackageStatus prior_status = status_from_live_pointer(prior.pointer);
        if (!status_zone_authority_from_context(prior_status, context).ok)
            return normalized_authorization_failure("rollback.authorization-failed",
                "Package rollback rejected.");
        JsPublishEndpointTransportContext rollback_context = context;
        rollback_context.current_live_checksum = current.pointer.current_live_checksum;

        JsPublishActivationOptions activation_options =
            activation_options_from_context(rollback_context);
        activation_options.durable_audit_append =
            [rollback_context, expected_live_checksum = envelope.target_live_checksum](
                const JsPublishStagedPackageStatus &status) {
                return append_publish_audit_event(status, rollback_context, "rollback",
                    expected_live_checksum, live_checksum_from_status(status)).ok;
            };
        JsPublishEndpointTransportResult result = transport_response(service.rollback(
            staged_request_from_status(prior_status,
                rollback_any ? JsPublishOperation::PackageRollbackAny
                             : JsPublishOperation::PackageRollbackOwn,
                envelope.target_live_checksum, rollback_context),
            activation_options).response);
        if (!result.ok && result.http_status == 403)
            return normalized_authorization_failure("rollback.authorization-failed",
                "Package rollback rejected.");
        return result;
    }

    return transport_error(400, "publish.unsupported-operation", "Publish request rejected.",
        "publish operation is not supported by this transport adapter");
}
