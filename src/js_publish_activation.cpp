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

bool pointer_replacement_would_conflict(const JsLivePackageStore &live_store,
                                        const JsPublishStagedPackageStatus &status,
                                        const std::string &expected_previous_live_checksum) {
    JsLivePackagePointerResult existing =
        live_store.find_live_pointer(status.zone, status.host, status.vnum);
    return existing.ok && existing.pointer.current_live_checksum != expected_previous_live_checksum;
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
    if (options.applied_at_epoch_seconds <= 0 || is_blank(options.live_pointer_audit_id)) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::InvalidRequest,
                       "Live package activation requires an apply timestamp and audit id.");
        return result;
    }
    if (pointer_replacement_would_conflict(live_store, result.assembly.status,
                                           options.assembly_options.current_live_checksum)) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::LivePointerConflict,
                       "Current live pointer checksum changed before activation.");
        return result;
    }

    JsStagedPackageLookupResult lookup = repository.find_by_version(
        result.assembly.status.package_id, result.assembly.status.package_version_id);
    if (!lookup.ok) {
        add_diagnostic(result, JsPublishActivationDiagnosticCode::AssemblyFailed,
                       "Staged package record was not found after authorization.");
        return result;
    }

    result.live_pointer_result = live_store.activate_staged_record_pointer(
        lookup.record,
        live_pointer_from_assembly(result.assembly, lookup.record.identity, options));
    if (!result.live_pointer_result.ok) {
        add_store_diagnostics(result,
                              activation_code_for_live_store_failure(result.live_pointer_result),
                              result.live_pointer_result.diagnostics);
        return result;
    }

    result.applied = true;
    result.ok = true;
    return result;
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
    case JsPublishActivationDiagnosticCode::LivePointerConflict:
        return "live-pointer-conflict";
    case JsPublishActivationDiagnosticCode::StoreFailed:
        return "store-failed";
    case JsPublishActivationDiagnosticCode::PointerFailed:
        return "pointer-failed";
    }
    return "unknown";
}
