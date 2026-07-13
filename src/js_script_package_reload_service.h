#ifndef JS_SCRIPT_PACKAGE_RELOAD_SERVICE_H
#define JS_SCRIPT_PACKAGE_RELOAD_SERVICE_H

#include "js_script_package_loader.h"
#include "js_script_registry.h"

#include <cstddef>
#include <string>
#include <vector>

enum class JsScriptPackageReloadStatus {
    Success,
    InvalidRoot,
    InvalidRequestPath,
    MissingFile,
    Directory,
    Symlink,
    OutsideRoot,
    LoadFailed,
    ValidationFailed,
};

struct JsScriptPackageReloadOptions {
    std::string package_root;
    JsScriptPackageBundleLoadOptions load_options;
    JsScriptRegistryReplaceOptions replace_options;
    bool create_package_root = false;
};

struct JsScriptPackageReloadResult {
    bool ok = false;
    JsScriptPackageReloadStatus status = JsScriptPackageReloadStatus::InvalidRequestPath;
    std::string request_path;
    std::size_t package_count = 0;
    std::vector<JsScriptPackageDiagnostic> diagnostics;
    JsScriptPackageBundleLoadResult load_result;
    JsScriptPackageValidationResult validation_result;
};

class JsScriptPackageReloadService {
public:
    explicit JsScriptPackageReloadService(const JsScriptPackageReloadOptions& options);

    bool reload_bundle(const std::string& request_path, JsScriptPackageReloadResult* result);

    const JsScriptPackageRegistry& registry() const;
    std::size_t package_count() const;
    bool empty() const;
    const std::string& last_successful_request_path() const;
    std::size_t last_successful_package_count() const;

    const JsScriptPackage* find_package_by_vnum(int vnum) const;
    const JsScriptPackage* find_package_by_id(const std::string& package_id) const;
    const JsScriptTriggerBinding* find_trigger_binding(int package_vnum,
        JsScriptPackageHost host, JsScriptingManifestKind kind, int legacy_value) const;

private:
    JsScriptPackageReloadOptions m_options;
    JsScriptPackageRegistry m_registry;
    std::string m_canonical_root;
    std::string m_last_successful_request_path;
    std::size_t m_last_successful_package_count = 0;
};

const char* js_script_package_reload_status_name(JsScriptPackageReloadStatus status);

#endif
