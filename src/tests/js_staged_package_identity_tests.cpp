#include "../js_staged_package_identity.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "client-pkg-" + std::to_string(vnum);
    package.host = JsScriptPackageHost::Character;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = "function onEnter(ctx) { return true; }";
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter"});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageIdentityOptions make_options() {
    JsStagedPackageIdentityOptions options;
    options.zone = 30;
    options.builder_account_id = "account:builder";
    options.base_live_checksum = "live:old";
    options.server_instance_id = "server:main";
    return options;
}

bool has_code(const JsStagedPackageIdentityResult &result,
              JsStagedPackageIdentityDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsStagedPackageIdentityDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool has_publish_code(const JsPublishAuthorizationResult &result, JsPublishDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic &diagnostic) { return diagnostic.code == code; });
}

JsPublishRequest make_mutating_request(const JsStagedPackageIdentity &identity,
                                       JsPublishOperation operation) {
    JsPublishRequest request;
    request.operation = operation;
    request.request_id = "request-1";
    request.actor_id = "actor:42";
    request.builder_account_id = "account:builder";
    request.zone = identity.zone;
    request.vnum = identity.vnum;
    request.host = identity.host;
    request.package_id = identity.package_id;
    request.package_version_id = identity.package_version_id;
    request.staged_digest = identity.canonical_digest;
    request.expected_live_checksum = identity.base_live_checksum;
    request.manifest_checksum = identity.manifest_checksum;
    request.token.token_id = "token-1";
    request.token.claims_verified = true;
    request.token.actor_id = request.actor_id;
    request.token.builder_account_id = request.builder_account_id;
    request.token.server_audience = "server:main";
    request.token.workspace_id = "workspace:main";
    request.token.scopes = js_publish_scope_for_operation(operation);
    request.token.issued_at_epoch_seconds = 90;
    request.token.expires_at_epoch_seconds = 200;
    request.transport.secure_channel = true;
    request.transport.server_identity_verified = true;
    request.transport.server_audience = "server:main";
    return request;
}

JsPublishAuthorizationOptions make_publish_options(const JsStagedPackageIdentity &identity) {
    JsPublishAuthorizationOptions options;
    options.now_epoch_seconds = 100;
    options.allow_mutating_operations = true;
    options.expected_server_audience = "server:main";
    options.expected_workspace_id = "workspace:main";
    options.current_live_checksum = identity.base_live_checksum;
    options.authority = js_publish_authority_context_from_staged_identity(identity);
    return options;
}

bool digest_is_sha256(const std::string &digest) {
    if (digest.size() != 71 || digest.rfind("sha256:", 0) != 0)
        return false;
    return std::all_of(digest.begin() + 7, digest.end(),
                       [](unsigned char c) { return std::isxdigit(c) != 0; });
}

} // namespace

TEST(JsStagedPackageIdentity, BuildsDeterministicServerOwnedIdentity) {
    JsScriptPackage package = make_package();
    JsStagedPackageIdentityOptions options = make_options();

    JsStagedPackageIdentityResult first = js_staged_package_identity_build(package, options);
    JsStagedPackageIdentityResult second = js_staged_package_identity_build(package, options);

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ("js:30:character:3001", first.identity.package_id);
    EXPECT_NE(package.package_id, first.identity.package_id)
        << "Builder supplied package ids must not become server authority ids.";
    EXPECT_EQ(first.identity.canonical_digest, second.identity.canonical_digest);
    EXPECT_EQ(first.identity.package_version_id, second.identity.package_version_id);
    EXPECT_EQ("sha256:v1", first.identity.digest_algorithm);
    EXPECT_TRUE(digest_is_sha256(first.identity.canonical_digest));
    EXPECT_NE(std::string::npos,
              first.identity.package_version_id.find(first.identity.canonical_digest.substr(7)))
        << "Version ids should carry the full SHA-256 digest body.";
    EXPECT_EQ(package.compiled_javascript_checksum, first.identity.compiled_javascript_checksum);
}

TEST(JsStagedPackageIdentity, IgnoresBuilderSuppliedPackageIdForAuthorityIdentity) {
    JsScriptPackage first_package = make_package();
    JsScriptPackage second_package = make_package();
    second_package.package_id = "client-controlled-alias";

    JsStagedPackageIdentityResult first =
        js_staged_package_identity_build(first_package, make_options());
    JsStagedPackageIdentityResult second =
        js_staged_package_identity_build(second_package, make_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(first.identity.package_id, second.identity.package_id);
    EXPECT_EQ(first.identity.canonical_digest, second.identity.canonical_digest);
    EXPECT_EQ(first.identity.package_version_id, second.identity.package_version_id);
}

TEST(JsStagedPackageIdentity, CanonicalizesTriggerBindingOrder) {
    JsScriptPackage first_package = make_package();
    first_package.compiled_javascript =
        "function onEnter(ctx) { return true; }\nfunction onDie(ctx) { return false; }";
    first_package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "onDie"});
    first_package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(first_package);

    JsScriptPackage second_package = first_package;
    std::reverse(second_package.trigger_bindings.begin(), second_package.trigger_bindings.end());
    second_package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(second_package);

    JsStagedPackageIdentityResult first =
        js_staged_package_identity_build(first_package, make_options());
    JsStagedPackageIdentityResult second =
        js_staged_package_identity_build(second_package, make_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_NE(first_package.compiled_javascript_checksum,
              second_package.compiled_javascript_checksum)
        << "This test proves the staged digest is not inheriting package checksum ordering.";
    EXPECT_EQ(first.identity.canonical_digest, second.identity.canonical_digest);
    EXPECT_EQ(first.identity.package_version_id, second.identity.package_version_id);
}

TEST(JsStagedPackageIdentity, ChangesDigestAndVersionWhenCompiledJavaScriptChanges) {
    JsScriptPackage first_package = make_package();
    JsScriptPackage second_package = make_package();
    second_package.compiled_javascript = "function onEnter(ctx) { return false; }";
    second_package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(second_package);

    JsStagedPackageIdentityResult first =
        js_staged_package_identity_build(first_package, make_options());
    JsStagedPackageIdentityResult second =
        js_staged_package_identity_build(second_package, make_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_NE(first.identity.canonical_digest, second.identity.canonical_digest);
    EXPECT_NE(first.identity.package_version_id, second.identity.package_version_id);
}

TEST(JsStagedPackageIdentity, ChangesDigestWhenBaseLiveChecksumChanges) {
    JsStagedPackageIdentityOptions old_options = make_options();
    JsStagedPackageIdentityOptions new_options = make_options();
    new_options.base_live_checksum = "live:newer";

    JsStagedPackageIdentityResult old_identity =
        js_staged_package_identity_build(make_package(), old_options);
    JsStagedPackageIdentityResult new_identity =
        js_staged_package_identity_build(make_package(), new_options);

    ASSERT_TRUE(old_identity.ok);
    ASSERT_TRUE(new_identity.ok);
    EXPECT_NE(old_identity.identity.canonical_digest, new_identity.identity.canonical_digest);
    EXPECT_NE(old_identity.identity.package_version_id, new_identity.identity.package_version_id);
}

TEST(JsStagedPackageIdentity, RejectsIncompleteStagingMetadata) {
    JsStagedPackageIdentityOptions options = make_options();
    options.zone = 0;
    options.builder_account_id.clear();
    options.base_live_checksum.clear();
    options.server_instance_id.clear();

    JsStagedPackageIdentityResult result =
        js_staged_package_identity_build(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata));
    EXPECT_TRUE(result.package_validation.ok)
        << "Invalid staging metadata should be reported separately from package validity.";
    EXPECT_TRUE(result.identity.canonical_digest.empty());
    EXPECT_TRUE(result.identity.package_version_id.empty());
}

TEST(JsStagedPackageIdentity, RejectsWhitespaceOnlyStagingMetadata) {
    JsStagedPackageIdentityOptions options = make_options();
    options.builder_account_id = " \t ";
    options.base_live_checksum = "\r\n";
    options.server_instance_id = "   ";

    JsStagedPackageIdentityResult result =
        js_staged_package_identity_build(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageIdentityDiagnosticCode::InvalidMetadata));
    EXPECT_TRUE(result.identity.canonical_digest.empty());
    EXPECT_TRUE(result.identity.package_version_id.empty());
}

TEST(JsStagedPackageIdentity, RejectsPackagesThatFailValidation) {
    JsScriptPackage package = make_package();
    package.manifest_checksum = "stale";

    JsStagedPackageIdentityResult result =
        js_staged_package_identity_build(package, make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageIdentityDiagnosticCode::PackageValidationFailed));
    EXPECT_FALSE(result.package_validation.ok);
}

TEST(JsStagedPackageIdentity, PublishModeStillRejectsDeferredManifestEntries) {
    JsStagedPackageIdentityOptions options = make_options();
    options.package_validation_options.mode = JsScriptPackageValidationMode::Publish;

    JsStagedPackageIdentityResult result =
        js_staged_package_identity_build(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageIdentityDiagnosticCode::PackageValidationFailed));
    EXPECT_FALSE(result.package_validation.ok)
        << "Identity helpers must not quietly make deferred triggers publishable.";
}

TEST(JsStagedPackageIdentity, AuthorityContextFeedsActivatePreflight) {
    JsStagedPackageIdentityResult staged =
        js_staged_package_identity_build(make_package(), make_options());
    ASSERT_TRUE(staged.ok);

    JsPublishRequest request =
        make_mutating_request(staged.identity, JsPublishOperation::PackageActivate);
    JsPublishAuthorizationResult result =
        js_publish_authorization_preflight(request, make_publish_options(staged.identity));

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.mutates_server_state);
    EXPECT_TRUE(result.diagnostics.empty());
}

TEST(JsStagedPackageIdentity, ActivatePreflightRejectsDigestMismatch) {
    JsStagedPackageIdentityResult staged =
        js_staged_package_identity_build(make_package(), make_options());
    ASSERT_TRUE(staged.ok);

    JsPublishRequest request =
        make_mutating_request(staged.identity, JsPublishOperation::PackageActivate);
    request.staged_digest = "sha256:wrong";
    JsPublishAuthorizationResult result =
        js_publish_authorization_preflight(request, make_publish_options(staged.identity));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_publish_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsStagedPackageIdentity, ActivatePreflightRejectsVersionMismatch) {
    JsStagedPackageIdentityResult staged =
        js_staged_package_identity_build(make_package(), make_options());
    ASSERT_TRUE(staged.ok);

    JsPublishRequest request =
        make_mutating_request(staged.identity, JsPublishOperation::PackageActivate);
    request.package_version_id = "jsv:wrong";
    JsPublishAuthorizationResult result =
        js_publish_authorization_preflight(request, make_publish_options(staged.identity));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_publish_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsStagedPackageIdentity, RollbackOwnPreflightUsesStagedOwnerFromIdentity) {
    JsStagedPackageIdentityResult staged =
        js_staged_package_identity_build(make_package(), make_options());
    ASSERT_TRUE(staged.ok);

    JsPublishRequest request =
        make_mutating_request(staged.identity, JsPublishOperation::PackageRollbackOwn);
    JsPublishAuthorizationResult result =
        js_publish_authorization_preflight(request, make_publish_options(staged.identity));

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.mutates_server_state);

    request.builder_account_id = "account:other-builder";
    request.token.builder_account_id = request.builder_account_id;
    JsPublishAuthorizationResult rejected =
        js_publish_authorization_preflight(request, make_publish_options(staged.identity));

    EXPECT_FALSE(rejected.ok);
    EXPECT_TRUE(has_publish_code(rejected, JsPublishDiagnosticCode::PermissionMismatch));
}
