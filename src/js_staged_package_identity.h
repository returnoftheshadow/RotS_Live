#ifndef JS_STAGED_PACKAGE_IDENTITY_H
#define JS_STAGED_PACKAGE_IDENTITY_H

#include "js_publish_authorization.h"
#include "js_script_package.h"

#include <string>
#include <vector>

enum class JsStagedPackageIdentityDiagnosticCode {
    InvalidMetadata,
    PackageValidationFailed,
};

struct JsStagedPackageIdentityOptions {
    int zone = 0;
    std::string builder_account_id;
    std::string base_live_checksum;
    std::string server_instance_id;
    int canonical_format_version = 1;
    JsScriptPackageValidationOptions package_validation_options;

    JsStagedPackageIdentityOptions();
};

struct JsStagedPackageIdentity {
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    std::string package_id;
    std::string package_version_id;
    std::string canonical_digest;
    std::string digest_algorithm;
    int canonical_format_version = 1;
    int package_format_version = 1;
    std::string builder_account_id;
    std::string server_instance_id;
    std::string base_live_checksum;
    std::string manifest_checksum;
    std::string compiled_javascript_checksum;
    std::string runtime_name;
    std::string runtime_version;
    std::string generated_typings_version;
};

struct JsStagedPackageIdentityDiagnostic {
    JsStagedPackageIdentityDiagnosticCode code =
        JsStagedPackageIdentityDiagnosticCode::InvalidMetadata;
    std::string message;
};

struct JsStagedPackageIdentityResult {
    bool ok = false;
    JsStagedPackageIdentity identity;
    std::vector<JsStagedPackageIdentityDiagnostic> diagnostics;
    JsScriptPackageValidationResult package_validation;
};

std::string js_staged_package_logical_package_id(int zone, JsScriptPackageHost host, int vnum);
std::string js_staged_package_version_id(const JsStagedPackageIdentity &identity);

JsStagedPackageIdentityResult
js_staged_package_identity_build(const JsScriptPackage &package,
                                 const JsStagedPackageIdentityOptions &options);

JsPublishAuthorityContext
js_publish_authority_context_from_staged_identity(const JsStagedPackageIdentity &identity);

#endif
