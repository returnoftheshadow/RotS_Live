#ifndef JS_LIVE_REGISTRY_RELOAD_SERVICE_H
#define JS_LIVE_REGISTRY_RELOAD_SERVICE_H

#include "js_live_package_store.h"
#include "js_script_registry.h"

#include <cstddef>
#include <string>
#include <vector>

struct JsGameAdapterOptions;
struct JsRuntimeLimits;
struct JsTriggerDispatchOptions;
struct JsTriggerDispatchRequest;
struct JsTriggerDispatchResult;

enum class JsLiveRegistryReloadStatus {
    Success,
    LiveStoreFailed,
    ValidationFailed,
};

enum class JsLiveRegistryReloadDiagnosticCode {
    LiveStoreFailed,
    ValidationFailed,
};

struct JsLiveRegistryReloadOptions {
    JsScriptRegistryReplaceOptions replace_options;
    std::string expected_server_instance_id;
};

struct JsLiveRegistryReloadDiagnostic {
    JsLiveRegistryReloadDiagnosticCode code = JsLiveRegistryReloadDiagnosticCode::LiveStoreFailed;
    std::string message;
};

struct JsLiveRegistryReloadResult {
    bool ok = false;
    JsLiveRegistryReloadStatus status = JsLiveRegistryReloadStatus::LiveStoreFailed;
    std::size_t package_count = 0;
    std::vector<JsLiveRegistryReloadDiagnostic> diagnostics;
    JsScriptPackageValidationResult validation_result;
};

struct JsLiveRegistryPackageStatus {
    int zone = 0;
    int vnum = 0;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string current_live_checksum;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    int package_format_version = 0;
    int manifest_schema_version = 0;
    int trigger_catalog_revision = 0;
    std::string manifest_checksum;
    std::string runtime_name;
    std::string runtime_version;
    std::string generated_typings_version;
    std::string compiled_javascript_checksum;
    long long loaded_at_epoch_seconds = 0;
    std::vector<JsScriptTriggerBinding> trigger_bindings;
};

// Internal server-side cache for reload/admin plumbing. Public status helpers return
// metadata only; source-bearing packages stay behind the future dispatch boundary.
class JsLiveRegistryReloadService {
  public:
    JsLiveRegistryReloadService();
    explicit JsLiveRegistryReloadService(const JsLiveRegistryReloadOptions &options);

    bool refresh_from_live_store(const JsLivePackageStore &live_store,
                                 JsLiveRegistryReloadResult *result = nullptr);

    std::size_t package_count() const;
    bool empty() const;
    std::size_t successful_reload_count() const;
    std::size_t last_successful_package_count() const;
    const std::vector<JsLiveRegistryPackageStatus> &package_statuses() const;
    const JsLiveRegistryPackageStatus *find_package_status_by_vnum(int vnum) const;
    const JsLiveRegistryPackageStatus *
    find_package_status_by_id(const std::string &package_id) const;
    const std::string &expected_server_instance_id() const;

    const JsScriptTriggerBinding *find_trigger_binding(int package_vnum, JsScriptPackageHost host,
                                                       JsScriptingManifestKind kind,
                                                       int legacy_value) const;

  private:
    friend JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
        const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
        const JsGameAdapterOptions &adapter_options, const JsRuntimeLimits &limits);
    friend JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
        const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
        const JsGameAdapterOptions &adapter_options, const JsTriggerDispatchOptions &options);

    JsLiveRegistryReloadOptions m_options;
    JsScriptPackageRegistry m_registry;
    std::vector<JsLiveRegistryPackageStatus> m_package_statuses;
    std::size_t m_successful_reload_count = 0;
    std::size_t m_last_successful_package_count = 0;
};

const char *js_live_registry_reload_status_name(JsLiveRegistryReloadStatus status);
const char *js_live_registry_reload_diagnostic_code_name(JsLiveRegistryReloadDiagnosticCode code);
bool js_live_registry_snapshot_matches_server_instance(
    const JsLivePackageStoreSnapshot &snapshot, const std::string &expected_server_instance_id,
    JsLiveRegistryReloadResult *result = nullptr);

#endif
