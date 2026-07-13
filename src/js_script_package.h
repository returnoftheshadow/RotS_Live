#ifndef JS_SCRIPT_PACKAGE_H
#define JS_SCRIPT_PACKAGE_H

#include "js_scripting_manifest.h"

#include <string>
#include <vector>

enum class JsScriptPackageHost {
    Character,
    Object,
    Room,
    MudlleMobile,
};

enum class JsScriptPackageValidationMode {
    Publish,
    InternalValidationOnly,
};

enum class JsScriptPackageDiagnosticCode {
    InvalidMetadata,
    ManifestMismatch,
    RuntimeMismatch,
    TypingsMismatch,
    SourceChecksumMismatch,
    SourcePolicyViolation,
    UnknownTrigger,
    UnsupportedTrigger,
    WrongHost,
    DuplicateVnum,
    DuplicatePackageId,
    LegacyVnumConflict,
    DuplicateTrigger,
    MissingHandler,
};

struct JsScriptTriggerBinding {
    JsScriptingManifestKind kind = JsScriptingManifestKind::LegacyScriptTrigger;
    int legacy_value = 0;
    std::string handler_name;
};

struct JsScriptPackage {
    int vnum = 0;
    std::string package_id;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    int package_format_version = 1;
    int manifest_schema_version = 0;
    int trigger_catalog_revision = 0;
    std::string manifest_checksum;
    std::string runtime_name;
    std::string runtime_version;
    std::string generated_typings_version;
    std::string compiled_javascript_checksum;
    std::string compiled_javascript;
    std::vector<JsScriptTriggerBinding> trigger_bindings;
};

struct JsScriptPackageValidationOptions {
    JsScriptPackageValidationMode mode = JsScriptPackageValidationMode::Publish;
    std::size_t maximum_source_bytes = 256 * 1024;
};

struct JsScriptPackageDiagnostic {
    JsScriptPackageDiagnosticCode code = JsScriptPackageDiagnosticCode::InvalidMetadata;
    int vnum = 0;
    std::string package_id;
    std::string message;
};

struct JsScriptPackageValidationResult {
    bool ok = false;
    std::vector<JsScriptPackageDiagnostic> diagnostics;
};

std::string js_script_package_compiled_javascript_checksum(const JsScriptPackage& package);

JsScriptPackageValidationResult js_script_package_validate(
    const JsScriptPackage& package, const JsScriptPackageValidationOptions& options = {});

JsScriptPackageValidationResult js_script_package_registry_validate(
    const std::vector<JsScriptPackage>& packages,
    const JsScriptPackageValidationOptions& options = {});

const char* js_script_package_host_name(JsScriptPackageHost host);
const char* js_script_package_validation_mode_name(JsScriptPackageValidationMode mode);
const char* js_script_package_diagnostic_code_name(JsScriptPackageDiagnosticCode code);

#endif
