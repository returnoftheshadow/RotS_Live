#include "js_live_registry_reload_service.h"

#include <algorithm>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 220;

std::string bounded_single_line(std::string message) {
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

void add_diagnostic(JsLiveRegistryReloadResult &result, JsLiveRegistryReloadDiagnosticCode code,
                    const std::string &message) {
    result.diagnostics.push_back({code, bounded_single_line(message)});
}

void add_live_store_diagnostics(JsLiveRegistryReloadResult &result,
                                const std::vector<JsLivePackageStoreDiagnostic> &diagnostics) {
    for (const JsLivePackageStoreDiagnostic &diagnostic : diagnostics)
        add_diagnostic(result, JsLiveRegistryReloadDiagnosticCode::LiveStoreFailed,
                       diagnostic.message);
    if (diagnostics.empty())
        add_diagnostic(result, JsLiveRegistryReloadDiagnosticCode::LiveStoreFailed,
                       "Live package registry snapshot failed.");
}

void add_validation_diagnostics(JsLiveRegistryReloadResult &result,
                                const JsScriptPackageValidationResult &validation) {
    for (const JsScriptPackageDiagnostic &diagnostic : validation.diagnostics)
        add_diagnostic(result, JsLiveRegistryReloadDiagnosticCode::ValidationFailed,
                       diagnostic.message);
    if (validation.diagnostics.empty())
        add_diagnostic(result, JsLiveRegistryReloadDiagnosticCode::ValidationFailed,
                       "Live package registry snapshot validation failed.");
}

JsLiveRegistryPackageStatus package_status_from_package(const JsScriptPackage &package,
                                                        const JsLivePackageStore &live_store) {
    JsLiveRegistryPackageStatus status;
    JsLivePackagePointerResult pointer = live_store.find_live_pointer(package.package_id);
    if (pointer.ok) {
        status.zone = pointer.pointer.zone;
        status.package_version_id = pointer.pointer.package_version_id;
        status.staged_digest = pointer.pointer.staged_digest;
        status.current_live_checksum = pointer.pointer.current_live_checksum;
        status.loaded_at_epoch_seconds = pointer.pointer.loaded_at_epoch_seconds;
    }
    status.vnum = package.vnum;
    status.package_id = package.package_id;
    status.host = package.host;
    status.package_format_version = package.package_format_version;
    status.manifest_schema_version = package.manifest_schema_version;
    status.trigger_catalog_revision = package.trigger_catalog_revision;
    status.manifest_checksum = package.manifest_checksum;
    status.runtime_name = package.runtime_name;
    status.runtime_version = package.runtime_version;
    status.generated_typings_version = package.generated_typings_version;
    status.compiled_javascript_checksum = package.compiled_javascript_checksum;
    status.trigger_bindings = package.trigger_bindings;
    return status;
}

std::vector<JsLiveRegistryPackageStatus>
package_statuses_from_packages(const std::vector<JsScriptPackage> &packages,
                               const JsLivePackageStore &live_store) {
    std::vector<JsLiveRegistryPackageStatus> statuses;
    statuses.reserve(packages.size());
    for (const JsScriptPackage &package : packages)
        statuses.push_back(package_status_from_package(package, live_store));
    return statuses;
}

JsLiveRegistryReloadOptions default_reload_options() {
    JsLiveRegistryReloadOptions options;
    options.replace_options.validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

} // namespace

JsLiveRegistryReloadService::JsLiveRegistryReloadService() : m_options(default_reload_options()) {}

JsLiveRegistryReloadService::JsLiveRegistryReloadService(const JsLiveRegistryReloadOptions &options)
    : m_options(options) {}

bool JsLiveRegistryReloadService::refresh_from_live_store(const JsLivePackageStore &live_store,
                                                          JsLiveRegistryReloadResult *result) {
    JsLiveRegistryReloadResult candidate_result;
    JsLivePackageRegistrySnapshotResult snapshot =
        live_store.build_live_registry_snapshot(m_options.replace_options);

    candidate_result.validation_result = snapshot.package_validation;
    if (!snapshot.ok) {
        if (!snapshot.diagnostics.empty()) {
            candidate_result.status = JsLiveRegistryReloadStatus::LiveStoreFailed;
            add_live_store_diagnostics(candidate_result, snapshot.diagnostics);
        } else {
            candidate_result.status = JsLiveRegistryReloadStatus::ValidationFailed;
            add_validation_diagnostics(candidate_result, snapshot.package_validation);
        }
        if (result)
            *result = candidate_result;
        return false;
    }

    m_registry = snapshot.registry;
    m_package_statuses = package_statuses_from_packages(snapshot.packages, live_store);
    ++m_successful_reload_count;
    m_last_successful_package_count = m_registry.package_count();

    candidate_result.ok = true;
    candidate_result.status = JsLiveRegistryReloadStatus::Success;
    candidate_result.package_count = m_last_successful_package_count;
    if (result)
        *result = candidate_result;
    return true;
}

std::size_t JsLiveRegistryReloadService::package_count() const {
    return m_registry.package_count();
}

bool JsLiveRegistryReloadService::empty() const { return m_registry.empty(); }

std::size_t JsLiveRegistryReloadService::successful_reload_count() const {
    return m_successful_reload_count;
}

std::size_t JsLiveRegistryReloadService::last_successful_package_count() const {
    return m_last_successful_package_count;
}

const std::vector<JsLiveRegistryPackageStatus> &
JsLiveRegistryReloadService::package_statuses() const {
    return m_package_statuses;
}

const JsLiveRegistryPackageStatus *
JsLiveRegistryReloadService::find_package_status_by_vnum(int vnum) const {
    const auto found = std::find_if(
        m_package_statuses.begin(), m_package_statuses.end(),
        [vnum](const JsLiveRegistryPackageStatus &status) { return status.vnum == vnum; });
    return found == m_package_statuses.end() ? nullptr : &(*found);
}

const JsLiveRegistryPackageStatus *
JsLiveRegistryReloadService::find_package_status_by_id(const std::string &package_id) const {
    const auto found = std::find_if(
        m_package_statuses.begin(), m_package_statuses.end(),
        [&](const JsLiveRegistryPackageStatus &status) { return status.package_id == package_id; });
    return found == m_package_statuses.end() ? nullptr : &(*found);
}

const JsScriptTriggerBinding *
JsLiveRegistryReloadService::find_trigger_binding(int package_vnum, JsScriptPackageHost host,
                                                  JsScriptingManifestKind kind,
                                                  int legacy_value) const {
    return m_registry.find_trigger_binding(package_vnum, host, kind, legacy_value);
}

const char *js_live_registry_reload_status_name(JsLiveRegistryReloadStatus status) {
    switch (status) {
    case JsLiveRegistryReloadStatus::Success:
        return "success";
    case JsLiveRegistryReloadStatus::LiveStoreFailed:
        return "live-store-failed";
    case JsLiveRegistryReloadStatus::ValidationFailed:
        return "validation-failed";
    }
    return "unknown";
}

const char *js_live_registry_reload_diagnostic_code_name(JsLiveRegistryReloadDiagnosticCode code) {
    switch (code) {
    case JsLiveRegistryReloadDiagnosticCode::LiveStoreFailed:
        return "live-store-failed";
    case JsLiveRegistryReloadDiagnosticCode::ValidationFailed:
        return "validation-failed";
    }
    return "unknown";
}
