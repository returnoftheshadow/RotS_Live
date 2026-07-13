#include "js_script_registry.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace {

void add_registry_diagnostic(JsScriptPackageValidationResult& result,
    JsScriptPackageDiagnosticCode code, const JsScriptPackage& package,
    const std::string& message)
{
    JsScriptPackageDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.vnum = package.vnum;
    diagnostic.package_id = package.package_id;
    diagnostic.message = message;
    if (diagnostic.message.size() > 240)
        diagnostic.message.resize(240);
    result.diagnostics.push_back(diagnostic);
}

bool has_legacy_conflict(int vnum, const std::vector<int>& legacy_script_vnums)
{
    return std::find(legacy_script_vnums.begin(), legacy_script_vnums.end(), vnum)
        != legacy_script_vnums.end();
}

} // namespace

bool JsScriptPackageRegistry::replace_all(const std::vector<JsScriptPackage>& packages,
    const JsScriptRegistryReplaceOptions& options,
    JsScriptPackageValidationResult* result)
{
    JsScriptPackageValidationResult candidate_result
        = js_script_package_registry_validate(packages, options.validation_options);

    if (packages.empty() && !options.allow_empty_replacement) {
        JsScriptPackage empty_package;
        add_registry_diagnostic(candidate_result, JsScriptPackageDiagnosticCode::InvalidMetadata,
            empty_package, "empty JavaScript package replacement is not allowed");
    }

    for (const JsScriptPackage& package : packages) {
        if (!has_legacy_conflict(package.vnum, options.legacy_script_vnums))
            continue;

        std::ostringstream message;
        message << "vnum " << package.vnum << " package '" << package.package_id
                << "' conflicts with an existing legacy .scr script vnum";
        add_registry_diagnostic(candidate_result, JsScriptPackageDiagnosticCode::LegacyVnumConflict,
            package, message.str());
    }

    candidate_result.ok = candidate_result.diagnostics.empty();
    if (!candidate_result.ok) {
        if (result)
            *result = candidate_result;
        return false;
    }

    std::vector<JsScriptPackage> candidate_packages = packages;
    m_packages.swap(candidate_packages);

    if (result)
        *result = candidate_result;
    return true;
}

void JsScriptPackageRegistry::clear()
{
    m_packages.clear();
}

std::size_t JsScriptPackageRegistry::package_count() const
{
    return m_packages.size();
}

bool JsScriptPackageRegistry::empty() const
{
    return m_packages.empty();
}

const JsScriptPackage* JsScriptPackageRegistry::find_package_by_vnum(int vnum) const
{
    for (const JsScriptPackage& package : m_packages) {
        if (package.vnum == vnum)
            return &package;
    }
    return nullptr;
}

const JsScriptPackage* JsScriptPackageRegistry::find_package_by_id(
    const std::string& package_id) const
{
    for (const JsScriptPackage& package : m_packages) {
        if (package.package_id == package_id)
            return &package;
    }
    return nullptr;
}

const JsScriptTriggerBinding* JsScriptPackageRegistry::find_trigger_binding(int package_vnum,
    JsScriptPackageHost host, JsScriptingManifestKind kind, int legacy_value) const
{
    const JsScriptPackage* package = find_package_by_vnum(package_vnum);
    if (!package || package->host != host)
        return nullptr;

    for (const JsScriptTriggerBinding& binding : package->trigger_bindings) {
        if (binding.kind == kind && binding.legacy_value == legacy_value)
            return &binding;
    }
    return nullptr;
}

std::vector<const JsScriptPackage*> JsScriptPackageRegistry::find_packages_for_trigger(
    JsScriptPackageHost host, JsScriptingManifestKind kind, int legacy_value) const
{
    std::vector<const JsScriptPackage*> matches;
    for (const JsScriptPackage& package : m_packages) {
        if (package.host != host)
            continue;
        for (const JsScriptTriggerBinding& binding : package.trigger_bindings) {
            if (binding.kind == kind && binding.legacy_value == legacy_value) {
                matches.push_back(&package);
                break;
            }
        }
    }
    return matches;
}
