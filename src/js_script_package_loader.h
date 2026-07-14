#ifndef JS_SCRIPT_PACKAGE_LOADER_H
#define JS_SCRIPT_PACKAGE_LOADER_H

#include "js_script_package.h"
#include "js_script_registry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace json_utils {
class JsonReader;
}

struct JsScriptPackageBundleLoadOptions {
    std::size_t maximum_file_bytes = 1024 * 1024;
    std::size_t maximum_package_count = 1024;
    std::size_t maximum_trigger_binding_count = 128;
    std::size_t maximum_string_bytes = 256 * 1024;
    bool allow_empty_bundle = false;
};

struct JsScriptPackageBundleLoadResult {
    bool ok = false;
    std::vector<JsScriptPackage> packages;
    std::vector<JsScriptPackageDiagnostic> diagnostics;
};

bool js_script_package_bundle_parse_json(const std::string& json,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackageBundleLoadResult* result);

bool js_script_package_parse_json_object(json_utils::JsonReader* reader,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackage* package,
    std::string* error_message);

bool js_script_package_bundle_parse_json(const std::string& json,
    JsScriptPackageBundleLoadResult* result);

bool js_script_package_bundle_load_file(const std::string& path,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackageBundleLoadResult* result);

bool js_script_package_registry_load_file(const std::string& path,
    const JsScriptPackageBundleLoadOptions& load_options,
    const JsScriptRegistryReplaceOptions& replace_options, JsScriptPackageRegistry* registry,
    JsScriptPackageBundleLoadResult* load_result = nullptr,
    JsScriptPackageValidationResult* validation_result = nullptr);

#endif
