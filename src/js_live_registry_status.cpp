#include "js_live_registry_status.h"

#include <algorithm>

namespace {

constexpr std::size_t MaxStatusDiagnosticBytes = 220;

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
    if (message.size() > MaxStatusDiagnosticBytes)
        message.resize(MaxStatusDiagnosticBytes);
    return message;
}

void add_diagnostic(JsLiveRegistryStatusResult &result, JsLiveRegistryStatusDiagnosticCode code,
                    const std::string &message) {
    result.diagnostics.push_back({code, bounded_single_line(message)});
}

JsLiveRegistryStatusSummary summary_from_service(const JsLiveRegistryReloadService &service) {
    JsLiveRegistryStatusSummary summary;
    summary.empty = service.empty();
    summary.package_count = service.package_count();
    summary.successful_reload_count = service.successful_reload_count();
    summary.last_successful_package_count = service.last_successful_package_count();
    return summary;
}

bool options_are_valid(const JsLiveRegistryStatusOptions &options) {
    if (options.include_package_details && options.maximum_packages == 0)
        return false;
    if (options.include_package_details && options.include_trigger_bindings &&
        options.maximum_trigger_bindings == 0)
        return false;
    return true;
}

bool snapshot_exceeds_package_limit(const JsLiveRegistryReloadService &service,
                                    const JsLiveRegistryStatusOptions &options) {
    return options.include_package_details &&
           service.package_statuses().size() > options.maximum_packages;
}

bool trigger_count_exceeds_limit(const std::vector<const JsLiveRegistryPackageStatus *> &packages,
                                 const JsLiveRegistryStatusOptions &options) {
    if (!options.include_package_details || !options.include_trigger_bindings)
        return false;
    std::size_t trigger_count = 0;
    for (const JsLiveRegistryPackageStatus *status : packages) {
        trigger_count += status->trigger_bindings.size();
        if (trigger_count > options.maximum_trigger_bindings)
            return true;
    }
    return false;
}

bool append_package(JsLiveRegistryStatusResult &result, const JsLiveRegistryPackageStatus &status,
                    const JsLiveRegistryStatusOptions &options) {
    if (!options.include_package_details)
        return true;

    JsLiveRegistryPackageInspection inspection;
    inspection.zone = status.zone;
    inspection.vnum = status.vnum;
    inspection.host = js_script_package_host_name(status.host);
    inspection.package_id = status.package_id;
    inspection.package_version_id = status.package_version_id;
    inspection.staged_digest = status.staged_digest;
    inspection.current_live_checksum = status.current_live_checksum;
    inspection.package_format_version = status.package_format_version;
    inspection.manifest_schema_version = status.manifest_schema_version;
    inspection.trigger_catalog_revision = status.trigger_catalog_revision;
    inspection.manifest_checksum = status.manifest_checksum;
    inspection.runtime_name = status.runtime_name;
    inspection.runtime_version = status.runtime_version;
    inspection.generated_typings_version = status.generated_typings_version;
    inspection.compiled_javascript_checksum = status.compiled_javascript_checksum;
    inspection.loaded_at_epoch_seconds = status.loaded_at_epoch_seconds;
    if (options.include_trigger_bindings) {
        inspection.trigger_bindings.reserve(status.trigger_bindings.size());
        for (const JsScriptTriggerBinding &binding : status.trigger_bindings) {
            inspection.trigger_bindings.push_back({js_scripting_manifest_kind_name(binding.kind),
                                                   binding.legacy_value, binding.handler_name});
        }
    }
    result.packages.push_back(inspection);
    return true;
}

JsLiveRegistryStatusResult invalid_options_result(const JsLiveRegistryReloadService &service) {
    JsLiveRegistryStatusResult result;
    result.summary = summary_from_service(service);
    add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::InvalidRequest,
                   "JavaScript live registry status requires positive package and trigger limits.");
    return result;
}

} // namespace

JsLiveRegistryStatusResult
js_live_registry_status_snapshot(const JsLiveRegistryReloadService &service,
                                 const JsLiveRegistryStatusOptions &options) {
    if (!options_are_valid(options))
        return invalid_options_result(service);

    JsLiveRegistryStatusResult result;
    result.summary = summary_from_service(service);
    if (snapshot_exceeds_package_limit(service, options)) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::PackageLimitExceeded,
                       "JavaScript live registry status package limit exceeded.");
        return result;
    }
    std::vector<const JsLiveRegistryPackageStatus *> packages;
    packages.reserve(service.package_statuses().size());
    for (const JsLiveRegistryPackageStatus &status : service.package_statuses())
        packages.push_back(&status);
    if (trigger_count_exceeds_limit(packages, options)) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded,
                       "JavaScript live registry status trigger binding limit exceeded.");
        return result;
    }
    for (const JsLiveRegistryPackageStatus *status : packages) {
        if (!append_package(result, *status, options))
            return result;
    }
    result.ok = true;
    return result;
}

JsLiveRegistryStatusResult
js_live_registry_status_for_package_id(const JsLiveRegistryReloadService &service,
                                       const std::string &package_id,
                                       const JsLiveRegistryStatusOptions &options) {
    if (!options_are_valid(options))
        return invalid_options_result(service);

    JsLiveRegistryStatusResult result;
    result.summary = summary_from_service(service);
    if (is_blank(package_id)) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::InvalidRequest,
                       "JavaScript live registry status package lookup requires a package id.");
        return result;
    }

    const JsLiveRegistryPackageStatus *status = service.find_package_status_by_id(package_id);
    if (!status) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::PackageNotFound,
                       "JavaScript live registry status package was not found.");
        return result;
    }
    if (trigger_count_exceeds_limit(std::vector<const JsLiveRegistryPackageStatus *>{status},
                                    options)) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded,
                       "JavaScript live registry status trigger binding limit exceeded.");
        return result;
    }
    if (!append_package(result, *status, options))
        return result;
    result.ok = true;
    return result;
}

JsLiveRegistryStatusResult
js_live_registry_status_for_vnum(const JsLiveRegistryReloadService &service, int vnum,
                                 const JsLiveRegistryStatusOptions &options) {
    if (!options_are_valid(options))
        return invalid_options_result(service);

    JsLiveRegistryStatusResult result;
    result.summary = summary_from_service(service);
    if (vnum <= 0) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::InvalidRequest,
                       "JavaScript live registry status package lookup requires a positive vnum.");
        return result;
    }

    const JsLiveRegistryPackageStatus *status = service.find_package_status_by_vnum(vnum);
    if (!status) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::PackageNotFound,
                       "JavaScript live registry status package was not found.");
        return result;
    }
    if (trigger_count_exceeds_limit(std::vector<const JsLiveRegistryPackageStatus *>{status},
                                    options)) {
        add_diagnostic(result, JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded,
                       "JavaScript live registry status trigger binding limit exceeded.");
        return result;
    }
    if (!append_package(result, *status, options))
        return result;
    result.ok = true;
    return result;
}

const char *js_live_registry_status_diagnostic_code_name(JsLiveRegistryStatusDiagnosticCode code) {
    switch (code) {
    case JsLiveRegistryStatusDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsLiveRegistryStatusDiagnosticCode::PackageNotFound:
        return "package-not-found";
    case JsLiveRegistryStatusDiagnosticCode::PackageLimitExceeded:
        return "package-limit-exceeded";
    case JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded:
        return "trigger-binding-limit-exceeded";
    }
    return "unknown";
}
