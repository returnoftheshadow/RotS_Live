#ifndef JS_SCRIPT_REGISTRY_H
#define JS_SCRIPT_REGISTRY_H

#include "js_script_package.h"

#include <vector>

struct JsScriptRegistryReplaceOptions {
    JsScriptPackageValidationOptions validation_options;
    std::vector<int> legacy_script_vnums;
    bool allow_empty_replacement = true;
};

class JsScriptPackageRegistry {
public:
    bool replace_all(const std::vector<JsScriptPackage>& packages,
        const JsScriptRegistryReplaceOptions& options,
        JsScriptPackageValidationResult* result = nullptr);

    void clear();

    std::size_t package_count() const;
    bool empty() const;

    const JsScriptPackage* find_package_by_vnum(int vnum) const;
    const JsScriptPackage* find_package_by_id(const std::string& package_id) const;
    const JsScriptTriggerBinding* find_trigger_binding(int package_vnum,
        JsScriptPackageHost host, JsScriptingManifestKind kind, int legacy_value) const;
    std::vector<const JsScriptPackage*> find_packages_for_trigger(JsScriptPackageHost host,
        JsScriptingManifestKind kind, int legacy_value) const;

private:
    std::vector<JsScriptPackage> m_packages;
};

#endif
