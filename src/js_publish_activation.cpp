#include "js_publish_activation.h"

#include <algorithm>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 220;

bool is_blank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    });
}

std::string bounded_single_line(std::string message) {
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

void add_diagnostic(JsPublishActivationResult &result, JsPublishActivationDiagnosticCode code,
                    const std::string &message) {
    result.diagnostics.push_back({code, bounded_single_line(message)});
}

void add_diagnostic(JsPublishStagedRequestAssemblyResult &result,
                    JsPublishStagingDiagnosticCode code, const std::string &message) {
    result.diagnostics.push_back({code, bounded_single_line(message)});
}

void add_store_diagnostics(JsPublishActivationResult &result,
                           JsPublishActivationDiagnosticCode code,
                           const std::vector<JsLivePackageStoreDiagnostic> &diagnostics) {
    for (const JsLivePackageStoreDiagnostic &diagnostic : diagnostics)
        add_diagnostic(result, code, diagnostic.message);
    if (diagnostics.empty())
        add_diagnostic(result, code, "Live package store operation failed.");
}

JsPublishActivationDiagnosticCode
activation_code_for_live_store_failure(const JsLivePackagePointerResult &pointer_result) {
    for (const JsLivePackageStoreDiagnostic &diagnostic : pointer_result.diagnostics) {
        if (diagnostic.code == JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict ||
            diagnostic.code == JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded)
            return JsPublishActivationDiagnosticCode::StoreFailed;
    }
    return JsPublishActivationDiagnosticCode::PointerFailed;
}

JsLivePackagePointerResult conflicting_live_pointer(const JsLivePackageStore &live_store,
                                                    const JsPublishStagedPackageStatus &status,
                                                    const std::string &expected_previous_live_checksum) {
    JsLivePackagePointerResult existing =
        live_store.find_live_pointer(status.zone, status.host, status.vnum);
    if (existing.ok && existing.pointer.current_live_checksum != expected_previous_live_checksum)
        return existing;
    return {};
}

JsLivePackagePointer
live_pointer_from_assembly(const JsPublishStagedRequestAssemblyResult &assembly,
                           const JsStagedPackageIdentity &identity,
                           const JsPublishActivationOptions &options) {
    JsLivePackagePointer pointer;
    pointer.zone = assembly.status.zone;
    pointer.vnum = assembly.status.vnum;
    pointer.host = assembly.status.host;
    pointer.package_id = assembly.status.package_id;
    pointer.package_version_id = assembly.status.package_version_id;
    pointer.staged_digest = assembly.status.staged_digest;
    pointer.expected_previous_live_checksum = options.assembly_options.current_live_checksum;
    pointer.current_live_checksum = js_live_package_current_checksum_for_identity(identity);
    pointer.loaded_at_epoch_seconds = options.applied_at_epoch_seconds;
    pointer.load_audit_id = options.live_pointer_audit_id;
    return pointer;
}

JsStagedPackageRecord staged_record_from_live_record(const JsLivePackageRecord &record) {
    JsStagedPackageRecord staged;
    staged.identity = record.identity;
    staged.audit = record.staged_audit;
    staged.package = record.package;
    return staged;
}

JsPublishStagedPackageStatus status_from_live_record(const JsLivePackageRecord &record) {
    JsPublishStagedPackageStatus status;
    status.zone = record.identity.zone;
    status.vnum = record.identity.vnum;
    status.host = record.identity.host;
    status.package_id = record.identity.package_id;
    status.package_version_id = record.identity.package_version_id;
    status.staged_digest = record.identity.canonical_digest;
    status.digest_algorithm = record.identity.digest_algorithm;
    status.canonical_format_version = record.identity.canonical_format_version;
    status.package_format_version = record.identity.package_format_version;
    status.staged_at_epoch_seconds = record.staged_audit.staged_at_epoch_seconds;
    status.audit_id = record.staged_audit.audit_id;
    status.base_live_checksum = record.identity.base_live_checksum;
    status.manifest_checksum = record.identity.manifest_checksum;
    status.compiled_javascript_checksum = record.identity.compiled_javascript_checksum;
    status.runtime_name = record.identity.runtime_name;
    status.runtime_version = record.identity.runtime_version;
    status.generated_typings_version = record.identity.generated_typings_version;
    return status;
}

JsPublishStagedRequestAssemblyResult
assemble_live_record_activation_request(const JsLivePackageRecord &record,
                                        const JsPublishStagedRequestAssemblyInput &input,
                                        const JsPublishActivationOptions &options) {
    JsPublishStagedRequestAssemblyResult assembly;
    assembly.request.operation = input.operation;
    assembly.request.request_id = input.request_id;
    assembly.request.actor_id = input.actor_id;
    assembly.request.builder_account_id = input.builder_account_id;
    assembly.request.token = input.token;
    assembly.request.transport = input.transport;

    if (input.package_id != record.identity.package_id ||
        input.package_version_id != record.identity.package_version_id) {
        add_diagnostic(assembly, JsPublishStagingDiagnosticCode::InvalidRequest,
                       "Live package activation request does not match retained package record.");
        return assembly;
    }
    if (is_blank(options.assembly_options.current_live_checksum)) {
        add_diagnostic(assembly, JsPublishStagingDiagnosticCode::InvalidRequest,
                       "Live package activation requires current live checksum.");
        return assembly;
    }

    assembly.status = status_from_live_record(record);
    assembly.request.zone = record.identity.zone;
    assembly.request.vnum = record.identity.vnum;
    assembly.request.host = record.identity.host;
    assembly.request.package_id = record.identity.package_id;
    assembly.request.package_version_id = record.identity.package_version_id;
    assembly.request.staged_digest = record.identity.canonical_digest;
    assembly.request.expected_live_checksum = input.expected_live_checksum;
    assembly.request.manifest_checksum = record.identity.manifest_checksum;

    assembly.authorization_options.now_epoch_seconds = options.assembly_options.now_epoch_seconds;
    assembly.authorization_options.allow_mutating_operations =
        options.assembly_options.allow_mutating_operations;
    assembly.authorization_options.rate_limited = options.assembly_options.rate_limited;
    assembly.authorization_options.expected_server_audience =
        options.assembly_options.expected_server_audience;
    assembly.authorization_options.expected_workspace_id =
        options.assembly_options.expected_workspace_id;
    assembly.authorization_options.current_live_checksum =
        options.assembly_options.current_live_checksum;
    assembly.authorization_options.authority =
        js_publish_authority_context_from_staged_identity(record.identity);
    assembly.authorization_options.authority.allow_rollback_any =
        options.assembly_options.allow_rollback_any;

    assembly.assembled = true;
    assembly.authorization_result =
        js_publish_authorization_preflight(assembly.request, assembly.authorization_options);
    return assembly;
}

JsPublishActivationResult
apply_assembled_live_package_activation(const JsStagedPackageRecord &record,
                                        JsPublishActivationResult result,
                                        JsLivePackageStore &live_store,
                                        const JsPublishActivationOptions &options) {
    result.assembled = result.assembly.assembled;
    result.authorized = result.assembly.authorization_result.ok;

    if (!result.assembly.assembled) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AssemblyFailed,
                       "Live package activation request assembly failed.");
        return result;
    }
    if (!result.assembly.authorization_result.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AuthorizationFailed,
                       "Live package activation preflight was not authorized.");
        return result;
    }
    if (!options.allow_live_pointer_update) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::LiveUpdateDisabled,
                       "Live package pointer updates are disabled for this activation request.");
        return result;
    }
    if (!options.durable_audit_precondition_ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AuditPreconditionFailed,
                       "Durable publish audit precondition failed before live mutation.");
        return result;
    }
    if (options.applied_at_epoch_seconds <= 0 || is_blank(options.live_pointer_audit_id)) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::InvalidRequest,
                       "Live package activation requires an apply timestamp and audit id.");
        return result;
    }
    JsLivePackagePointerResult existing_conflict = conflicting_live_pointer(
        live_store, result.assembly.status, options.assembly_options.current_live_checksum);
    if (existing_conflict.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::LivePointerConflict,
                       "Current live pointer checksum changed before activation.");
        result.live_pointer_result = existing_conflict;
        return result;
    }

    const JsLivePackageStoreSnapshot previous_snapshot = live_store.export_snapshot();
    result.live_pointer_result = live_store.activate_staged_record_pointer(
        record, live_pointer_from_assembly(result.assembly, record.identity, options));
    if (!result.live_pointer_result.ok) {
        add_store_diagnostics(result,
                              activation_code_for_live_store_failure(result.live_pointer_result),
                              result.live_pointer_result.diagnostics);
        return result;
    }

    if (!options.persist_live_store_path.empty()) {
        result.persistence_result = js_live_package_store_snapshot_save_file(
            options.persist_live_store_path, live_store.export_snapshot());
        if (!result.persistence_result.ok) {
            if (!result.persistence_result.target_replaced)
                result.rollback_hydration = live_store.hydrate_from_snapshot(previous_snapshot);
            else
                result.applied = true;
            add_diagnostic(result, JsPublishActivationDiagnosticCode::PersistenceFailed,
                           "Live package store persistence failed after activation.");
            for (const JsLivePackageStorePersistenceDiagnostic &diagnostic :
                 result.persistence_result.diagnostics)
                add_diagnostic(result, JsPublishActivationDiagnosticCode::PersistenceFailed,
                               diagnostic.message);
            return result;
        }
    }

    result.applied = true;
    result.ok = true;
    return result;
}

} // namespace

JsPublishActivationResult js_publish_apply_staged_package_activation(
    const JsStagedPackageRepository &repository, JsLivePackageStore &live_store,
    const JsPublishStagedRequestAssemblyInput &input, const JsPublishActivationOptions &options) {
    JsPublishActivationResult result;
    result.assembly =
        js_publish_assemble_staged_package_request(repository, input, options.assembly_options);
    result.assembled = result.assembly.assembled;
    result.authorized = result.assembly.authorization_result.ok;

    if (!result.assembly.assembled) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AssemblyFailed,
                       "Staged package activation request assembly failed.");
        return result;
    }
    if (!result.assembly.authorization_result.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AuthorizationFailed,
                       "Staged package activation preflight was not authorized.");
        return result;
    }
    if (!options.allow_live_pointer_update) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::LiveUpdateDisabled,
                       "Live package pointer updates are disabled for this activation request.");
        return result;
    }
    if (!options.durable_audit_precondition_ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AuditPreconditionFailed,
                       "Durable publish audit precondition failed before live mutation.");
        return result;
    }
    if (options.applied_at_epoch_seconds <= 0 || is_blank(options.live_pointer_audit_id)) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::InvalidRequest,
                       "Live package activation requires an apply timestamp and audit id.");
        return result;
    }
    JsLivePackagePointerResult existing_conflict = conflicting_live_pointer(
        live_store, result.assembly.status, options.assembly_options.current_live_checksum);
    if (existing_conflict.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::LivePointerConflict,
                       "Current live pointer checksum changed before activation.");
        result.live_pointer_result = existing_conflict;
        return result;
    }

    JsStagedPackageLookupResult lookup = repository.find_by_version(
        result.assembly.status.package_id, result.assembly.status.package_version_id);
    if (!lookup.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AssemblyFailed,
                       "Staged package record was not found after authorization.");
        return result;
    }

    const JsLivePackageStoreSnapshot previous_snapshot = live_store.export_snapshot();
    result.live_pointer_result = live_store.activate_staged_record_pointer(
        lookup.record,
        live_pointer_from_assembly(result.assembly, lookup.record.identity, options));
    if (!result.live_pointer_result.ok) {
        add_store_diagnostics(result,
                              activation_code_for_live_store_failure(result.live_pointer_result),
                              result.live_pointer_result.diagnostics);
        return result;
    }

    if (!options.persist_live_store_path.empty()) {
        result.persistence_result = js_live_package_store_snapshot_save_file(
            options.persist_live_store_path, live_store.export_snapshot());
        if (!result.persistence_result.ok) {
            if (!result.persistence_result.target_replaced)
                result.rollback_hydration = live_store.hydrate_from_snapshot(previous_snapshot);
            else
                result.applied = true;
            add_diagnostic(result, JsPublishActivationDiagnosticCode::PersistenceFailed,
                           "Live package store persistence failed after activation.");
            for (const JsLivePackageStorePersistenceDiagnostic &diagnostic :
                 result.persistence_result.diagnostics)
                add_diagnostic(result, JsPublishActivationDiagnosticCode::PersistenceFailed,
                               diagnostic.message);
            return result;
        }
    }

    result.applied = true;
    result.ok = true;
    return result;
}

JsPublishActivationResult
js_publish_apply_live_package_activation(const JsLivePackageRecord &record,
                                         JsLivePackageStore &live_store,
                                         const JsPublishStagedRequestAssemblyInput &input,
                                         const JsPublishActivationOptions &options) {
    JsPublishActivationResult result;
    result.assembly = assemble_live_record_activation_request(record, input, options);
    return apply_assembled_live_package_activation(staged_record_from_live_record(record),
                                                   result, live_store, options);
}

const char *js_publish_activation_diagnostic_code_name(JsPublishActivationDiagnosticCode code) {
    switch (code) {
    case JsPublishActivationDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsPublishActivationDiagnosticCode::AssemblyFailed:
        return "assembly-failed";
    case JsPublishActivationDiagnosticCode::AuthorizationFailed:
        return "authorization-failed";
    case JsPublishActivationDiagnosticCode::LiveUpdateDisabled:
        return "live-update-disabled";
    case JsPublishActivationDiagnosticCode::AuditPreconditionFailed:
        return "audit-precondition-failed";
    case JsPublishActivationDiagnosticCode::LivePointerConflict:
        return "live-pointer-conflict";
    case JsPublishActivationDiagnosticCode::StoreFailed:
        return "store-failed";
    case JsPublishActivationDiagnosticCode::PointerFailed:
        return "pointer-failed";
    case JsPublishActivationDiagnosticCode::PersistenceFailed:
        return "persistence-failed";
    }
    return "unknown";
}
