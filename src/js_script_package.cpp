#include "js_script_package.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>
#include <sstream>
#include <utility>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 240;

unsigned host_flag_for_package_host(JsScriptPackageHost host)
{
    switch (host) {
    case JsScriptPackageHost::Character:
        return JS_SCRIPTING_HOST_CHARACTER;
    case JsScriptPackageHost::Object:
        return JS_SCRIPTING_HOST_OBJECT;
    case JsScriptPackageHost::Room:
        return JS_SCRIPTING_HOST_ROOM;
    case JsScriptPackageHost::MudlleMobile:
        return JS_SCRIPTING_HOST_MUDLLE_MOBILE;
    }
    return 0;
}

std::string bounded_single_line(std::string message)
{
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

void add_diagnostic(JsScriptPackageValidationResult& result, JsScriptPackageDiagnosticCode code,
    const JsScriptPackage& package, const std::string& message)
{
    JsScriptPackageDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.vnum = package.vnum;
    diagnostic.package_id = package.package_id;
    diagnostic.message = bounded_single_line(message);
    result.diagnostics.push_back(diagnostic);
}

void add_diagnostic(JsScriptPackageValidationResult& result, JsScriptPackageDiagnosticCode code,
    const JsScriptPackage& package, const JsScriptTriggerBinding& binding,
    const std::string& message)
{
    std::ostringstream formatted;
    formatted << "vnum " << package.vnum << " package '" << package.package_id << "' trigger "
              << js_scripting_manifest_kind_name(binding.kind) << ":" << binding.legacy_value
              << " handler '" << binding.handler_name << "': " << message;
    add_diagnostic(result, code, package, formatted.str());
}

std::string stable_checksum(const std::string& value)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "fnv1a64:%016llx",
        static_cast<unsigned long long>(hash));
    return buffer;
}

void append_int(std::ostringstream& stream, const char* name, int value)
{
    stream << name << '=' << value << '\n';
}

void append_string(std::ostringstream& stream, const char* name, const std::string& value)
{
    stream << name << '=' << value.size() << ':' << value << '\n';
}

bool is_identifier_start(unsigned char ch) { return std::isalpha(ch) || ch == '_' || ch == '$'; }

bool is_identifier_continue(unsigned char ch)
{
    return std::isalnum(ch) || ch == '_' || ch == '$';
}

void reject_token(JsScriptPackageValidationResult& result, const JsScriptPackage& package,
    const std::string& token)
{
    add_diagnostic(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation, package,
        "compiled JavaScript uses forbidden token '" + token + "'");
}

void validate_source_policy(const JsScriptPackage& package,
    const JsScriptPackageValidationOptions& options,
    JsScriptPackageValidationResult& result)
{
    const std::string& source = package.compiled_javascript;
    if (source.empty()) {
        add_diagnostic(result, JsScriptPackageDiagnosticCode::InvalidMetadata, package,
            "compiled JavaScript must not be empty");
        return;
    }
    if (source.size() > options.maximum_source_bytes) {
        add_diagnostic(result, JsScriptPackageDiagnosticCode::InvalidMetadata, package,
            "compiled JavaScript exceeds maximum source size");
        return;
    }
    if (source.find("sourceMappingURL") != std::string::npos
        || source.find("sourceURL") != std::string::npos)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation, package,
            "compiled JavaScript must not include source map references");
    if (source.find("['eval']") != std::string::npos
        || source.find("[\"eval\"]") != std::string::npos
        || source.find("[`eval`]") != std::string::npos)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation, package,
            "compiled JavaScript uses forbidden eval property access");

    const std::set<std::string> forbidden_tokens = {
        "import",
        "eval",
        "Function",
        "AsyncFunction",
        "GeneratorFunction",
        "AsyncGeneratorFunction",
        "constructor",
        "async",
        "Promise",
        "setTimeout",
        "setInterval",
    };

    bool in_single_line_comment = false;
    bool in_multi_line_comment = false;
    bool in_string = false;
    bool in_template = false;
    char string_quote = 0;

    for (std::size_t index = 0; index < source.size();) {
        const char ch = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';

        if (in_single_line_comment) {
            if (ch == '\n' || ch == '\r')
                in_single_line_comment = false;
            ++index;
            continue;
        }
        if (in_multi_line_comment) {
            if (ch == '*' && next == '/') {
                in_multi_line_comment = false;
                index += 2;
            } else {
                ++index;
            }
            continue;
        }
        if (in_string || in_template) {
            if (ch == '\\') {
                index += index + 1 < source.size() ? 2 : 1;
                continue;
            }
            if (ch == string_quote) {
                in_string = false;
                in_template = false;
                string_quote = 0;
            }
            ++index;
            continue;
        }

        if (ch == '/' && next == '/') {
            in_single_line_comment = true;
            index += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_multi_line_comment = true;
            index += 2;
            continue;
        }
        if (ch == '\'' || ch == '"' || ch == '`') {
            in_string = ch != '`';
            in_template = ch == '`';
            string_quote = ch;
            ++index;
            continue;
        }
        if (is_identifier_start(static_cast<unsigned char>(ch))) {
            std::size_t end = index + 1;
            while (end < source.size()
                && is_identifier_continue(static_cast<unsigned char>(source[end])))
                ++end;
            const std::string token = source.substr(index, end - index);
            if (forbidden_tokens.find(token) != forbidden_tokens.end())
                reject_token(result, package, token);
            index = end;
            continue;
        }
        ++index;
    }
}

bool compatible_manifest_status(const JsScriptingManifestEntry& entry,
    JsScriptPackageValidationMode mode)
{
    if (mode == JsScriptPackageValidationMode::Publish)
        return js_scripting_manifest_entry_publishable(entry);

    return entry.support_status == JsScriptingSupportStatus::Deferred
        && entry.builder_status == JsScriptingBuilderStatus::Deferred;
}

void validate_metadata(const JsScriptPackage& package, JsScriptPackageValidationResult& result)
{
    const JsScriptingManifestMetadata& metadata = js_scripting_manifest_metadata();

    if (package.vnum <= 0)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::InvalidMetadata, package,
            "package vnum must be positive");
    if (package.package_id.empty())
        add_diagnostic(result, JsScriptPackageDiagnosticCode::InvalidMetadata, package,
            "package id must not be empty");
    if (package.package_format_version != metadata.package_format_version)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::ManifestMismatch, package,
            "package format version does not match server manifest");
    if (package.manifest_schema_version != metadata.schema_version)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::ManifestMismatch, package,
            "manifest schema version does not match server manifest");
    if (package.trigger_catalog_revision != metadata.trigger_catalog_revision)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::ManifestMismatch, package,
            "trigger catalog revision does not match server manifest");
    if (package.manifest_checksum != metadata.manifest_checksum)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::ManifestMismatch, package,
            "manifest checksum does not match server manifest");
    if (package.runtime_name != metadata.selected_runtime_name
        || package.runtime_version != metadata.selected_runtime_version)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::RuntimeMismatch, package,
            "runtime identity does not match server runtime");
    if (package.generated_typings_version != metadata.generated_typings_version)
        add_diagnostic(result, JsScriptPackageDiagnosticCode::TypingsMismatch, package,
            "generated typings version does not match server manifest");
    if (package.compiled_javascript_checksum
        != js_script_package_compiled_javascript_checksum(package))
        add_diagnostic(result, JsScriptPackageDiagnosticCode::SourceChecksumMismatch, package,
            "compiled JavaScript checksum does not match server-computed checksum");
    if (package.trigger_bindings.empty())
        add_diagnostic(result, JsScriptPackageDiagnosticCode::InvalidMetadata, package,
            "package must declare at least one trigger binding");
}

void validate_bindings(const JsScriptPackage& package,
    const JsScriptPackageValidationOptions& options,
    JsScriptPackageValidationResult& result)
{
    std::set<std::pair<int, int>> seen_triggers;
    std::set<std::string> seen_handlers;
    const unsigned package_host_flag = host_flag_for_package_host(package.host);

    for (const JsScriptTriggerBinding& binding : package.trigger_bindings) {
        const int kind_key = static_cast<int>(binding.kind);
        if (!seen_triggers.insert(std::make_pair(kind_key, binding.legacy_value)).second)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::DuplicateTrigger, package,
                binding, "duplicate trigger binding");
        if (!binding.handler_name.empty()
            && !seen_handlers.insert(binding.handler_name).second)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::DuplicateTrigger, package,
                binding, "duplicate handler binding");
        if (binding.handler_name.empty())
            add_diagnostic(result, JsScriptPackageDiagnosticCode::MissingHandler, package, binding,
                "handler name must not be empty");

        const JsScriptingManifestEntry* entry = find_js_scripting_manifest_entry(binding.kind, binding.legacy_value);
        if (!entry) {
            add_diagnostic(result, JsScriptPackageDiagnosticCode::UnknownTrigger, package, binding,
                "trigger is not present in the server manifest");
            continue;
        }
        if (binding.handler_name != entry->javascript_handler_name)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::MissingHandler, package, binding,
                "handler name does not match the server manifest");
        if ((entry->host_flags & package_host_flag) == 0)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::WrongHost, package, binding,
                std::string("host '") + js_script_package_host_name(package.host)
                    + "' is not eligible for this trigger");
        if (package.host == JsScriptPackageHost::Room && !entry->room_owned_scripts_publishable)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::WrongHost, package, binding,
                "room-owned JavaScript publishing is not enabled");
        if (!compatible_manifest_status(*entry, options.mode))
            add_diagnostic(result, JsScriptPackageDiagnosticCode::UnsupportedTrigger, package,
                binding,
                std::string("trigger is not publishable in ")
                    + js_script_package_validation_mode_name(options.mode) + " mode");
    }
}

void merge_result(JsScriptPackageValidationResult& target,
    const JsScriptPackageValidationResult& source)
{
    target.diagnostics.insert(target.diagnostics.end(), source.diagnostics.begin(),
        source.diagnostics.end());
}

} // namespace

std::string js_script_package_compiled_javascript_checksum(const JsScriptPackage& package)
{
    std::ostringstream canonical;
    append_int(canonical, "format", package.package_format_version);
    append_int(canonical, "vnum", package.vnum);
    append_int(canonical, "host", static_cast<int>(package.host));
    append_int(canonical, "manifest_schema", package.manifest_schema_version);
    append_int(canonical, "trigger_catalog", package.trigger_catalog_revision);
    append_string(canonical, "manifest_checksum", package.manifest_checksum);
    append_string(canonical, "runtime_name", package.runtime_name);
    append_string(canonical, "runtime_version", package.runtime_version);
    append_string(canonical, "typings", package.generated_typings_version);
    append_string(canonical, "compiled_js", package.compiled_javascript);
    for (const JsScriptTriggerBinding& binding : package.trigger_bindings) {
        append_int(canonical, "binding_kind", static_cast<int>(binding.kind));
        append_int(canonical, "binding_value", binding.legacy_value);
        append_string(canonical, "binding_handler", binding.handler_name);
    }
    return stable_checksum(canonical.str());
}

JsScriptPackageValidationResult js_script_package_validate(
    const JsScriptPackage& package, const JsScriptPackageValidationOptions& options)
{
    JsScriptPackageValidationResult result;

    validate_metadata(package, result);
    validate_bindings(package, options, result);
    validate_source_policy(package, options, result);

    result.ok = result.diagnostics.empty();
    return result;
}

JsScriptPackageValidationResult js_script_package_registry_validate(
    const std::vector<JsScriptPackage>& packages, const JsScriptPackageValidationOptions& options)
{
    JsScriptPackageValidationResult result;
    std::set<int> seen_vnums;
    std::set<std::string> seen_package_ids;

    for (const JsScriptPackage& package : packages) {
        if (!seen_vnums.insert(package.vnum).second)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::DuplicateVnum, package,
                "duplicate JavaScript package vnum");
        if (!package.package_id.empty() && !seen_package_ids.insert(package.package_id).second)
            add_diagnostic(result, JsScriptPackageDiagnosticCode::DuplicatePackageId, package,
                "duplicate JavaScript package id");
        merge_result(result, js_script_package_validate(package, options));
    }

    result.ok = result.diagnostics.empty();
    return result;
}

const char* js_script_package_host_name(JsScriptPackageHost host)
{
    switch (host) {
    case JsScriptPackageHost::Character:
        return "character";
    case JsScriptPackageHost::Object:
        return "object";
    case JsScriptPackageHost::Room:
        return "room";
    case JsScriptPackageHost::MudlleMobile:
        return "mudlle-mobile";
    }
    return "unknown";
}

const char* js_script_package_validation_mode_name(JsScriptPackageValidationMode mode)
{
    switch (mode) {
    case JsScriptPackageValidationMode::Publish:
        return "publish";
    case JsScriptPackageValidationMode::InternalValidationOnly:
        return "internal-validation-only";
    }
    return "unknown";
}

const char* js_script_package_diagnostic_code_name(JsScriptPackageDiagnosticCode code)
{
    switch (code) {
    case JsScriptPackageDiagnosticCode::InvalidMetadata:
        return "invalid-metadata";
    case JsScriptPackageDiagnosticCode::ManifestMismatch:
        return "manifest-mismatch";
    case JsScriptPackageDiagnosticCode::RuntimeMismatch:
        return "runtime-mismatch";
    case JsScriptPackageDiagnosticCode::TypingsMismatch:
        return "typings-mismatch";
    case JsScriptPackageDiagnosticCode::SourceChecksumMismatch:
        return "source-checksum-mismatch";
    case JsScriptPackageDiagnosticCode::SourcePolicyViolation:
        return "source-policy-violation";
    case JsScriptPackageDiagnosticCode::UnknownTrigger:
        return "unknown-trigger";
    case JsScriptPackageDiagnosticCode::UnsupportedTrigger:
        return "unsupported-trigger";
    case JsScriptPackageDiagnosticCode::WrongHost:
        return "wrong-host";
    case JsScriptPackageDiagnosticCode::DuplicateVnum:
        return "duplicate-vnum";
    case JsScriptPackageDiagnosticCode::DuplicatePackageId:
        return "duplicate-package-id";
    case JsScriptPackageDiagnosticCode::LegacyVnumConflict:
        return "legacy-vnum-conflict";
    case JsScriptPackageDiagnosticCode::DuplicateTrigger:
        return "duplicate-trigger";
    case JsScriptPackageDiagnosticCode::MissingHandler:
        return "missing-handler";
    }
    return "unknown";
}
