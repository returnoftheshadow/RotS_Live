#ifndef JS_LIVE_REGISTRY_STATUS_H
#define JS_LIVE_REGISTRY_STATUS_H

#include "js_live_registry_reload_service.h"

#include <cstddef>
#include <string>
#include <vector>

enum class JsLiveRegistryStatusDiagnosticCode {
    InvalidRequest,
    PackageNotFound,
    PackageLimitExceeded,
    TriggerBindingLimitExceeded,
};

struct JsLiveRegistryStatusOptions {
    bool include_package_details = true;
    bool include_trigger_bindings = true;
    std::size_t maximum_packages = 128;
    std::size_t maximum_trigger_bindings = 512;
};

struct JsLiveRegistryStatusSummary {
    bool empty = true;
    std::size_t package_count = 0;
    std::size_t successful_reload_count = 0;
    std::size_t last_successful_package_count = 0;
};

struct JsLiveRegistryTriggerBindingStatus {
    std::string kind;
    int legacy_value = 0;
    std::string handler_name;
};

struct JsLiveRegistryPackageInspection {
    int zone = 0;
    int vnum = 0;
    std::string host;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string current_live_checksum;
    int package_format_version = 0;
    int manifest_schema_version = 0;
    int trigger_catalog_revision = 0;
    std::string manifest_checksum;
    std::string runtime_name;
    std::string runtime_version;
    std::string generated_typings_version;
    std::string compiled_javascript_checksum;
    long long loaded_at_epoch_seconds = 0;
    std::vector<JsLiveRegistryTriggerBindingStatus> trigger_bindings;
};

struct JsLiveRegistryStatusDiagnostic {
    JsLiveRegistryStatusDiagnosticCode code = JsLiveRegistryStatusDiagnosticCode::InvalidRequest;
    std::string message;
};

struct JsLiveRegistryStatusResult {
    bool ok = false;
    JsLiveRegistryStatusSummary summary;
    std::vector<JsLiveRegistryPackageInspection> packages;
    std::vector<JsLiveRegistryStatusDiagnostic> diagnostics;
};

JsLiveRegistryStatusResult
js_live_registry_status_snapshot(const JsLiveRegistryReloadService &service,
                                 const JsLiveRegistryStatusOptions &options = {});

JsLiveRegistryStatusResult
js_live_registry_status_for_package_id(const JsLiveRegistryReloadService &service,
                                       const std::string &package_id,
                                       const JsLiveRegistryStatusOptions &options = {});

JsLiveRegistryStatusResult
js_live_registry_status_for_vnum(const JsLiveRegistryReloadService &service, int vnum,
                                 const JsLiveRegistryStatusOptions &options = {});

const char *js_live_registry_status_diagnostic_code_name(JsLiveRegistryStatusDiagnosticCode code);

#endif
