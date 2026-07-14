#include "js_script_package_loader.h"

#include "json_utils.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t MaxLoaderDiagnosticBytes = 240;

std::string bounded_single_line(std::string message)
{
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxLoaderDiagnosticBytes)
        message.resize(MaxLoaderDiagnosticBytes);
    return message;
}

void add_loader_diagnostic(JsScriptPackageBundleLoadResult& result, int vnum,
    const std::string& package_id, const std::string& message)
{
    JsScriptPackageDiagnostic diagnostic;
    diagnostic.code = JsScriptPackageDiagnosticCode::InvalidMetadata;
    diagnostic.vnum = vnum;
    diagnostic.package_id = bounded_single_line(package_id);
    diagnostic.message = bounded_single_line(message);
    result.diagnostics.push_back(std::move(diagnostic));
}

bool mark_seen(std::vector<std::string>& seen_fields, const std::string& key,
    std::string* error_message)
{
    if (std::find(seen_fields.begin(), seen_fields.end(), key) != seen_fields.end()) {
        *error_message = "duplicate JSON field '" + key + "'";
        return false;
    }
    seen_fields.push_back(key);
    return true;
}

bool require_string_limit(const std::string& field_name, const std::string& value,
    const JsScriptPackageBundleLoadOptions& options, std::string* error_message)
{
    if (value.size() <= options.maximum_string_bytes)
        return true;
    *error_message = "string field '" + field_name + "' exceeds maximum size";
    return false;
}

bool require_seen(bool saw_field, const char* field_name, JsScriptPackageBundleLoadResult& result,
    const JsScriptPackage& package)
{
    if (saw_field)
        return true;
    add_loader_diagnostic(result, package.vnum, package.package_id,
        std::string("missing required package field '") + field_name + "'");
    return false;
}

bool parse_host(const std::string& value, JsScriptPackageHost* host)
{
    if (value == js_script_package_host_name(JsScriptPackageHost::Character)) {
        *host = JsScriptPackageHost::Character;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Object)) {
        *host = JsScriptPackageHost::Object;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::Room)) {
        *host = JsScriptPackageHost::Room;
        return true;
    }
    if (value == js_script_package_host_name(JsScriptPackageHost::MudlleMobile)) {
        *host = JsScriptPackageHost::MudlleMobile;
        return true;
    }
    return false;
}

bool parse_kind(const std::string& value, JsScriptingManifestKind* kind)
{
    if (value == js_scripting_manifest_kind_name(JsScriptingManifestKind::LegacyScriptTrigger)) {
        *kind = JsScriptingManifestKind::LegacyScriptTrigger;
        return true;
    }
    if (value == js_scripting_manifest_kind_name(JsScriptingManifestKind::MudlleCallFlag)) {
        *kind = JsScriptingManifestKind::MudlleCallFlag;
        return true;
    }
    return false;
}

bool parse_binding(json_utils::JsonReader* reader, JsScriptTriggerBinding* binding,
    const JsScriptPackageBundleLoadOptions& options, std::string* error_message)
{
    bool saw_kind = false;
    bool saw_legacy_value = false;
    bool saw_handler_name = false;
    std::vector<std::string> seen_fields;

    const bool parsed = reader->parse_object(
        [binding, &options, &saw_kind, &saw_legacy_value, &saw_handler_name,
            &seen_fields](const std::string& key, json_utils::JsonReader* nested_reader,
            std::string* nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "kind") {
                std::string value;
                if (!nested_reader->parse_string(&value, nested_error_message))
                    return false;
                if (!parse_kind(value, &binding->kind)) {
                    *nested_error_message = "unknown JavaScript trigger binding kind";
                    return false;
                }
                saw_kind = true;
                return true;
            }
            if (key == "legacyValue") {
                saw_legacy_value = true;
                return nested_reader->parse_integer(&binding->legacy_value, nested_error_message);
            }
            if (key == "handlerName") {
                saw_handler_name = true;
                return nested_reader->parse_string(&binding->handler_name, nested_error_message)
                    && require_string_limit(
                        "handlerName", binding->handler_name, options, nested_error_message);
            }
            *nested_error_message = "unknown trigger binding field '" + key + "'";
            return false;
        },
        error_message);
    if (!parsed)
        return false;
    if (!saw_kind) {
        *error_message = "missing required trigger binding field 'kind'";
        return false;
    }
    if (!saw_legacy_value) {
        *error_message = "missing required trigger binding field 'legacyValue'";
        return false;
    }
    if (!saw_handler_name) {
        *error_message = "missing required trigger binding field 'handlerName'";
        return false;
    }
    return true;
}

bool parse_bindings(json_utils::JsonReader* reader, std::vector<JsScriptTriggerBinding>* bindings,
    const JsScriptPackageBundleLoadOptions& options, std::string* error_message)
{
    bindings->clear();
    return reader->parse_array(
        [bindings, &options](json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (bindings->size() >= options.maximum_trigger_binding_count) {
                *nested_error_message = "too many JavaScript trigger bindings in package";
                return false;
            }
            JsScriptTriggerBinding binding;
            if (!parse_binding(nested_reader, &binding, options, nested_error_message))
                return false;
            bindings->push_back(std::move(binding));
            return true;
        },
        error_message);
}

bool parse_package(json_utils::JsonReader* reader, JsScriptPackageBundleLoadResult& result,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackage* package,
    std::string* error_message)
{
    bool saw_vnum = false;
    bool saw_package_id = false;
    bool saw_host = false;
    bool saw_package_format_version = false;
    bool saw_manifest_schema_version = false;
    bool saw_trigger_catalog_revision = false;
    bool saw_manifest_checksum = false;
    bool saw_runtime_name = false;
    bool saw_runtime_version = false;
    bool saw_generated_typings_version = false;
    bool saw_compiled_javascript_checksum = false;
    bool saw_compiled_javascript = false;
    bool saw_trigger_bindings = false;
    std::vector<std::string> seen_fields;

    const bool parsed = reader->parse_object(
        [package, &options, &saw_vnum, &saw_package_id, &saw_host, &saw_package_format_version,
            &saw_manifest_schema_version, &saw_trigger_catalog_revision, &saw_manifest_checksum,
            &saw_runtime_name, &saw_runtime_version, &saw_generated_typings_version,
            &saw_compiled_javascript_checksum, &saw_compiled_javascript, &saw_trigger_bindings,
            &seen_fields](const std::string& key, json_utils::JsonReader* nested_reader,
            std::string* nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "vnum") {
                saw_vnum = true;
                return nested_reader->parse_integer(&package->vnum, nested_error_message);
            }
            if (key == "packageId") {
                saw_package_id = true;
                return nested_reader->parse_string(&package->package_id, nested_error_message)
                    && require_string_limit(
                        "packageId", package->package_id, options, nested_error_message);
            }
            if (key == "host") {
                std::string value;
                if (!nested_reader->parse_string(&value, nested_error_message))
                    return false;
                if (!parse_host(value, &package->host)) {
                    *nested_error_message = "unknown JavaScript package host";
                    return false;
                }
                saw_host = true;
                return true;
            }
            if (key == "packageFormatVersion") {
                saw_package_format_version = true;
                return nested_reader->parse_integer(
                    &package->package_format_version, nested_error_message);
            }
            if (key == "manifestSchemaVersion") {
                saw_manifest_schema_version = true;
                return nested_reader->parse_integer(
                    &package->manifest_schema_version, nested_error_message);
            }
            if (key == "triggerCatalogRevision") {
                saw_trigger_catalog_revision = true;
                return nested_reader->parse_integer(
                    &package->trigger_catalog_revision, nested_error_message);
            }
            if (key == "manifestChecksum") {
                saw_manifest_checksum = true;
                return nested_reader->parse_string(&package->manifest_checksum, nested_error_message)
                    && require_string_limit(
                        "manifestChecksum", package->manifest_checksum, options, nested_error_message);
            }
            if (key == "runtimeName") {
                saw_runtime_name = true;
                return nested_reader->parse_string(&package->runtime_name, nested_error_message)
                    && require_string_limit(
                        "runtimeName", package->runtime_name, options, nested_error_message);
            }
            if (key == "runtimeVersion") {
                saw_runtime_version = true;
                return nested_reader->parse_string(&package->runtime_version, nested_error_message)
                    && require_string_limit(
                        "runtimeVersion", package->runtime_version, options, nested_error_message);
            }
            if (key == "generatedTypingsVersion") {
                saw_generated_typings_version = true;
                return nested_reader->parse_string(
                           &package->generated_typings_version, nested_error_message)
                    && require_string_limit("generatedTypingsVersion",
                        package->generated_typings_version, options, nested_error_message);
            }
            if (key == "compiledJavaScriptChecksum") {
                saw_compiled_javascript_checksum = true;
                return nested_reader->parse_string(
                           &package->compiled_javascript_checksum, nested_error_message)
                    && require_string_limit("compiledJavaScriptChecksum",
                        package->compiled_javascript_checksum, options, nested_error_message);
            }
            if (key == "compiledJavaScript") {
                saw_compiled_javascript = true;
                return nested_reader->parse_string(
                           &package->compiled_javascript, nested_error_message)
                    && require_string_limit("compiledJavaScript", package->compiled_javascript,
                        options, nested_error_message);
            }
            if (key == "triggerBindings") {
                saw_trigger_bindings = true;
                return parse_bindings(
                    nested_reader, &package->trigger_bindings, options, nested_error_message);
            }
            *nested_error_message = "unknown package field '" + key + "'";
            return false;
        },
        error_message);
    if (!parsed)
        return false;

    bool ok = true;
    ok = require_seen(saw_vnum, "vnum", result, *package) && ok;
    ok = require_seen(saw_package_id, "packageId", result, *package) && ok;
    ok = require_seen(saw_host, "host", result, *package) && ok;
    ok = require_seen(saw_package_format_version, "packageFormatVersion", result, *package) && ok;
    ok = require_seen(saw_manifest_schema_version, "manifestSchemaVersion", result, *package) && ok;
    ok = require_seen(saw_trigger_catalog_revision, "triggerCatalogRevision", result, *package) && ok;
    ok = require_seen(saw_manifest_checksum, "manifestChecksum", result, *package) && ok;
    ok = require_seen(saw_runtime_name, "runtimeName", result, *package) && ok;
    ok = require_seen(saw_runtime_version, "runtimeVersion", result, *package) && ok;
    ok = require_seen(saw_generated_typings_version, "generatedTypingsVersion", result, *package) && ok;
    ok = require_seen(saw_compiled_javascript_checksum, "compiledJavaScriptChecksum", result, *package) && ok;
    ok = require_seen(saw_compiled_javascript, "compiledJavaScript", result, *package) && ok;
    ok = require_seen(saw_trigger_bindings, "triggerBindings", result, *package) && ok;
    return ok;
}

bool parse_packages(json_utils::JsonReader* reader, JsScriptPackageBundleLoadResult& result,
    const JsScriptPackageBundleLoadOptions& options, std::string* error_message)
{
    result.packages.clear();
    return reader->parse_array(
        [&result, &options](json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (result.packages.size() >= options.maximum_package_count) {
                *nested_error_message = "too many JavaScript packages in bundle";
                return false;
            }
            JsScriptPackage package;
            if (!parse_package(nested_reader, result, options, &package, nested_error_message))
                return false;
            result.packages.push_back(std::move(package));
            return true;
        },
        error_message);
}

bool read_file_limited(const std::string& path, const JsScriptPackageBundleLoadOptions& options,
    std::string* contents, std::string* error_message)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        *error_message = "failed to open JavaScript package bundle";
        return false;
    }

    const std::ifstream::pos_type size = file.tellg();
    if (size < 0) {
        *error_message = "failed to determine JavaScript package bundle size";
        return false;
    }
    if (static_cast<std::size_t>(size) > options.maximum_file_bytes) {
        *error_message = "JavaScript package bundle exceeds maximum size";
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        *error_message = "failed to read JavaScript package bundle";
        return false;
    }

    *contents = buffer.str();
    if (contents->size() > options.maximum_file_bytes) {
        *error_message = "JavaScript package bundle exceeds maximum size";
        contents->clear();
        return false;
    }
    return true;
}

} // namespace

bool js_script_package_parse_json_object(json_utils::JsonReader* reader,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackage* package,
    std::string* error_message)
{
    if (reader == nullptr || package == nullptr) {
        if (error_message)
            *error_message = "package parser input is invalid";
        return false;
    }

    JsScriptPackageBundleLoadResult result;
    std::string parse_error;
    if (!parse_package(reader, result, options, package, &parse_error)) {
        if (error_message) {
            if (!parse_error.empty())
                *error_message = parse_error;
            else if (!result.diagnostics.empty())
                *error_message = result.diagnostics.front().message;
            else
                *error_message = "failed to parse JavaScript package";
        }
        return false;
    }
    return true;
}

bool js_script_package_bundle_parse_json(const std::string& json,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackageBundleLoadResult* result)
{
    if (result == nullptr)
        return false;

    *result = {};
    bool saw_packages = false;
    std::vector<std::string> seen_fields;
    std::string error_message;
    json_utils::JsonReader reader(json);
    const bool parsed = reader.parse_root_object(
        [&result, &options, &saw_packages, &seen_fields](const std::string& key,
            json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (!mark_seen(seen_fields, key, nested_error_message))
                return false;
            if (key == "packages") {
                saw_packages = true;
                return parse_packages(nested_reader, *result, options, nested_error_message);
            }
            *nested_error_message = "unknown package bundle field '" + key + "'";
            return false;
        },
        &error_message);
    if (!parsed) {
        add_loader_diagnostic(*result, 0, "", "failed to parse JavaScript package bundle: " + error_message);
        result->packages.clear();
        result->ok = false;
        return false;
    }
    if (!saw_packages) {
        add_loader_diagnostic(*result, 0, "", "missing required bundle field 'packages'");
        result->ok = false;
        return false;
    }
    if (result->packages.empty() && !options.allow_empty_bundle) {
        add_loader_diagnostic(*result, 0, "", "JavaScript package bundle is empty");
        result->ok = false;
        return false;
    }

    result->ok = result->diagnostics.empty();
    if (!result->ok)
        result->packages.clear();
    return result->ok;
}

bool js_script_package_bundle_parse_json(
    const std::string& json, JsScriptPackageBundleLoadResult* result)
{
    JsScriptPackageBundleLoadOptions options;
    return js_script_package_bundle_parse_json(json, options, result);
}

bool js_script_package_bundle_load_file(const std::string& path,
    const JsScriptPackageBundleLoadOptions& options, JsScriptPackageBundleLoadResult* result)
{
    if (result == nullptr)
        return false;

    *result = {};
    std::string contents;
    std::string error_message;
    if (!read_file_limited(path, options, &contents, &error_message)) {
        add_loader_diagnostic(*result, 0, "", error_message);
        result->ok = false;
        return false;
    }

    return js_script_package_bundle_parse_json(contents, options, result);
}

bool js_script_package_registry_load_file(const std::string& path,
    const JsScriptPackageBundleLoadOptions& load_options,
    const JsScriptRegistryReplaceOptions& replace_options, JsScriptPackageRegistry* registry,
    JsScriptPackageBundleLoadResult* load_result,
    JsScriptPackageValidationResult* validation_result)
{
    if (validation_result)
        *validation_result = {};
    if (registry == nullptr)
        return false;

    JsScriptPackageBundleLoadResult local_load_result;
    JsScriptPackageBundleLoadResult& parsed = load_result ? *load_result : local_load_result;
    if (!js_script_package_bundle_load_file(path, load_options, &parsed))
        return false;

    return registry->replace_all(parsed.packages, replace_options, validation_result);
}
