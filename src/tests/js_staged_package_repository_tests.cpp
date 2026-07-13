#include "../js_staged_package_repository.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001, const std::string &body = "return true") {
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
    package.compiled_javascript = "function onEnter(ctx) { " + body + "; }";
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter"});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageIdentityOptions
make_identity_options(const std::string &base_live_checksum = "live:old") {
    JsStagedPackageIdentityOptions options;
    options.zone = 30;
    options.builder_account_id = "account:builder";
    options.base_live_checksum = base_live_checksum;
    options.server_instance_id = "server:main";
    return options;
}

JsStagedPackageStageOptions make_stage_options(const std::string &base_live_checksum = "live:old") {
    JsStagedPackageStageOptions options;
    options.identity_options = make_identity_options(base_live_checksum);
    options.audit.staged_at_epoch_seconds = 123456;
    options.audit.request_id = "request:stage-1";
    options.audit.actor_id = "actor:42";
    options.audit.permission_snapshot_id = "permission:snapshot-1";
    options.audit.audit_id = "audit:stage-1";
    options.audit.source_policy_decision = "source-policy:accepted";
    options.audit.validation_report_digest = "validation:sha256:abc";
    options.audit.transport_source_identifier = "transport:tls";
    return options;
}

bool has_code(const JsStagedPackageStageResult &result,
              JsStagedPackageRepositoryDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsStagedPackageRepositoryDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool has_code(const JsStagedPackageLookupResult &result,
              JsStagedPackageRepositoryDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsStagedPackageRepositoryDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool has_publish_code(const JsPublishAuthorizationResult &result, JsPublishDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic &diagnostic) { return diagnostic.code == code; });
}

JsPublishRequest make_activate_request(const JsStagedPackageRecord &record) {
    JsPublishRequest request;
    request.operation = JsPublishOperation::PackageActivate;
    request.request_id = "request-1";
    request.actor_id = "actor:42";
    request.builder_account_id = record.identity.builder_account_id;
    request.zone = record.identity.zone;
    request.vnum = record.identity.vnum;
    request.host = record.identity.host;
    request.package_id = record.identity.package_id;
    request.package_version_id = record.identity.package_version_id;
    request.staged_digest = record.identity.canonical_digest;
    request.expected_live_checksum = record.identity.base_live_checksum;
    request.manifest_checksum = record.identity.manifest_checksum;
    request.token.token_id = "token-1";
    request.token.claims_verified = true;
    request.token.actor_id = request.actor_id;
    request.token.builder_account_id = request.builder_account_id;
    request.token.server_audience = "server:main";
    request.token.workspace_id = "workspace:main";
    request.token.scopes = JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE;
    request.token.issued_at_epoch_seconds = 90;
    request.token.expires_at_epoch_seconds = 200;
    request.transport.secure_channel = true;
    request.transport.server_identity_verified = true;
    request.transport.server_audience = "server:main";
    return request;
}

JsPublishAuthorizationOptions make_publish_options(const JsPublishAuthorityContext &authority,
                                                   const std::string &current_live_checksum) {
    JsPublishAuthorizationOptions options;
    options.now_epoch_seconds = 100;
    options.allow_mutating_operations = true;
    options.expected_server_audience = "server:main";
    options.expected_workspace_id = "workspace:main";
    options.current_live_checksum = current_live_checksum;
    options.authority = authority;
    return options;
}

} // namespace

TEST(JsStagedPackageRepository, StartsEmptyAndReportsMissingLookups) {
    JsStagedPackageRepository repository;

    EXPECT_TRUE(repository.empty());
    EXPECT_EQ(0u, repository.size());

    JsStagedPackageLookupResult missing =
        repository.find_by_version("js:30:character:3001", "missing-version");

    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(has_code(missing, JsStagedPackageRepositoryDiagnosticCode::NotFound));
}

TEST(JsStagedPackageRepository, StagesValidatedPackageIdentity) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();

    JsStagedPackageStageResult result = repository.stage_package(package, make_identity_options());

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.inserted);
    EXPECT_FALSE(result.idempotent);
    EXPECT_EQ(1u, repository.size());
    EXPECT_EQ("js:30:character:3001", result.record.identity.package_id);
    EXPECT_EQ("account:builder", result.record.identity.builder_account_id);
    EXPECT_EQ(package.compiled_javascript, result.record.package.compiled_javascript);
}

TEST(JsStagedPackageRepository, StagesAuditMetadataWithValidatedPackage) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.inserted);
    EXPECT_EQ(123456, result.record.audit.staged_at_epoch_seconds);
    EXPECT_EQ("request:stage-1", result.record.audit.request_id);
    EXPECT_EQ("actor:42", result.record.audit.actor_id);
    EXPECT_EQ("permission:snapshot-1", result.record.audit.permission_snapshot_id);
    EXPECT_EQ("audit:stage-1", result.record.audit.audit_id);
    EXPECT_EQ("source-policy:accepted", result.record.audit.source_policy_decision);
    EXPECT_EQ("validation:sha256:abc", result.record.audit.validation_report_digest);
    EXPECT_EQ("transport:tls", result.record.audit.transport_source_identifier);
}

TEST(JsStagedPackageRepository, RepeatedStageOfSameVersionIsIdempotent) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();

    JsStagedPackageStageResult first = repository.stage_package(package, make_identity_options());
    JsStagedPackageStageResult second = repository.stage_package(package, make_identity_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(first.inserted);
    EXPECT_FALSE(second.inserted);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ(1u, repository.size());
    EXPECT_EQ(first.record.identity.package_version_id, second.record.identity.package_version_id);
}

TEST(JsStagedPackageRepository, RepeatedStageWithSameAuditMetadataIsIdempotent) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();
    JsStagedPackageStageOptions options = make_stage_options();

    JsStagedPackageStageResult first = repository.stage_package(package, options);
    JsStagedPackageStageResult second = repository.stage_package(package, options);

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ("request:stage-1", second.record.audit.request_id);
    EXPECT_EQ("audit:stage-1", second.record.audit.audit_id);
    EXPECT_EQ(1u, repository.size());
}

TEST(JsStagedPackageRepository, RejectsRepeatedStageWithChangedAuditMetadata) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();
    JsStagedPackageStageOptions first_options = make_stage_options();
    JsStagedPackageStageOptions second_options = make_stage_options();
    second_options.audit.request_id = "request:stage-2";
    second_options.audit.audit_id = "audit:stage-2";

    JsStagedPackageStageResult first = repository.stage_package(package, first_options);
    JsStagedPackageStageResult second = repository.stage_package(package, second_options);

    ASSERT_TRUE(first.ok);
    EXPECT_FALSE(second.ok);
    EXPECT_TRUE(
        has_code(second, JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict));
    EXPECT_EQ(1u, repository.size());
}

TEST(JsStagedPackageRepository, StoresImmutableCopyOfPackageAndIdentity) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();
    JsStagedPackageStageResult staged = repository.stage_package(package, make_identity_options());
    ASSERT_TRUE(staged.ok);

    package.compiled_javascript = "function onEnter(ctx) { return false; }";
    staged.record.package.compiled_javascript = "mutated copy";
    staged.record.audit.audit_id = "mutated audit";

    JsStagedPackageLookupResult lookup = repository.find_by_version(
        staged.record.identity.package_id, staged.record.identity.package_version_id);

    ASSERT_TRUE(lookup.ok);
    EXPECT_NE(package.compiled_javascript, lookup.record.package.compiled_javascript);
    EXPECT_NE(staged.record.package.compiled_javascript, lookup.record.package.compiled_javascript);
    EXPECT_EQ("function onEnter(ctx) { return true; }", lookup.record.package.compiled_javascript);
    EXPECT_NE("mutated audit", lookup.record.audit.audit_id);
}

TEST(JsStagedPackageRepository, RejectsMissingRequiredAuditMetadata) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.request_id.clear();

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, RejectsWhitespaceOnlyRequiredAuditMetadata) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.permission_snapshot_id = " \t\n";

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, RejectsUnsafeAuditMetadataText) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.audit_id = "audit/path";

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, AllowsAuditMetadataAtMaximumFieldLength) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.permission_snapshot_id.assign(160, 'p');

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(160u, result.record.audit.permission_snapshot_id.size());
}

TEST(JsStagedPackageRepository, RejectsOverlongAuditMetadataText) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.permission_snapshot_id.assign(161, 'p');

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, RejectsMissingAuditTimestamp) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.staged_at_epoch_seconds = 0;

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, RejectsNegativeAuditTimestamp) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.staged_at_epoch_seconds = -1;

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, StagesMultipleImmutableVersionsForSamePackage) {
    JsStagedPackageRepository repository;
    JsScriptPackage first_package = make_package();
    JsScriptPackage second_package = make_package(3001, "return false");

    JsStagedPackageStageResult first =
        repository.stage_package(first_package, make_identity_options("live:old"));
    JsStagedPackageStageResult second =
        repository.stage_package(second_package, make_identity_options("live:new"));

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(first.record.identity.package_id, second.record.identity.package_id);
    EXPECT_NE(first.record.identity.package_version_id, second.record.identity.package_version_id);
    EXPECT_EQ(2u, repository.size());

    JsStagedPackageLookupResult latest =
        repository.find_latest_for_package(first.record.identity.package_id);
    ASSERT_TRUE(latest.ok);
    EXPECT_EQ(second.record.identity.package_version_id, latest.record.identity.package_version_id);
}

TEST(JsStagedPackageRepository, RestagesDifferentClientPackageIdIdempotently) {
    JsStagedPackageRepository repository;
    JsScriptPackage first_package = make_package();
    JsScriptPackage second_package = first_package;
    second_package.package_id = "client-controlled-alias";
    second_package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(second_package);

    JsStagedPackageStageResult first =
        repository.stage_package(first_package, make_identity_options());
    JsStagedPackageStageResult second =
        repository.stage_package(second_package, make_identity_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ(1u, repository.size());
    EXPECT_EQ(first.record.identity.package_version_id, second.record.identity.package_version_id);
}

TEST(JsStagedPackageRepository, RestagesReorderedBindingsIdempotently) {
    JsStagedPackageRepository repository;
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

    JsStagedPackageStageResult first =
        repository.stage_package(first_package, make_identity_options());
    JsStagedPackageStageResult second =
        repository.stage_package(second_package, make_identity_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ(1u, repository.size());
    EXPECT_EQ(first.record.identity.package_version_id, second.record.identity.package_version_id);
}

TEST(JsStagedPackageRepository, RejectsInvalidIdentityWithoutChangingRepository) {
    JsStagedPackageRepository repository;
    JsStagedPackageIdentityOptions options = make_identity_options();
    options.builder_account_id.clear();

    JsStagedPackageStageResult result = repository.stage_package(make_package(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed));
    EXPECT_FALSE(result.identity_result.ok);
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, RejectsPackagesThatFailValidation) {
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package();
    package.manifest_checksum = "stale";

    JsStagedPackageStageResult result = repository.stage_package(package, make_identity_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed));
    EXPECT_FALSE(result.identity_result.package_validation.ok);
    EXPECT_TRUE(repository.empty());
}

TEST(JsStagedPackageRepository, EnforcesRecordLimitWithoutDroppingExistingRecords) {
    JsStagedPackageRepositoryOptions options;
    options.maximum_records = 1;
    JsStagedPackageRepository repository(options);

    JsStagedPackageStageResult first =
        repository.stage_package(make_package(3001), make_identity_options());
    JsStagedPackageStageResult second =
        repository.stage_package(make_package(3002), make_identity_options());

    ASSERT_TRUE(first.ok);
    EXPECT_FALSE(second.ok);
    EXPECT_TRUE(has_code(second, JsStagedPackageRepositoryDiagnosticCode::RecordLimitExceeded));
    EXPECT_EQ(1u, repository.size());
    EXPECT_TRUE(repository
                    .find_by_version(first.record.identity.package_id,
                                     first.record.identity.package_version_id)
                    .ok);
}

TEST(JsStagedPackageRepository, AllowsIdempotentRetryWhenAtCapacity) {
    JsStagedPackageRepositoryOptions options;
    options.maximum_records = 1;
    JsStagedPackageRepository repository(options);
    JsScriptPackage package = make_package(3001);

    JsStagedPackageStageResult first = repository.stage_package(package, make_identity_options());
    JsStagedPackageStageResult second = repository.stage_package(package, make_identity_options());

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ(1u, repository.size());
}

TEST(JsStagedPackageRepository, RejectsBlankLookupInputs) {
    JsStagedPackageRepository repository;

    JsStagedPackageLookupResult by_version = repository.find_by_version(" ", "\t");
    JsStagedPackageLookupResult latest = repository.find_latest_for_package("\n");

    EXPECT_FALSE(by_version.ok);
    EXPECT_TRUE(has_code(by_version, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_FALSE(latest.ok);
    EXPECT_TRUE(has_code(latest, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
}

TEST(JsStagedPackageRepository, AuthorityContextFeedsActivatePreflight) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(), make_identity_options());
    ASSERT_TRUE(staged.ok);

    JsPublishAuthorityContext authority = repository.authority_context_for_version(
        staged.record.identity.package_id, staged.record.identity.package_version_id);
    JsPublishRequest request = make_activate_request(staged.record);
    JsPublishAuthorizationResult result = js_publish_authorization_preflight(
        request, make_publish_options(authority, staged.record.identity.base_live_checksum));

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.mutates_server_state);
}

TEST(JsStagedPackageRepository, AuthorityContextRejectsCrossBuilderActivatePreflight) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(), make_identity_options());
    ASSERT_TRUE(staged.ok);

    JsPublishAuthorityContext authority = repository.authority_context_for_version(
        staged.record.identity.package_id, staged.record.identity.package_version_id);
    JsPublishRequest request = make_activate_request(staged.record);
    request.builder_account_id = "account:other-builder";
    request.token.builder_account_id = request.builder_account_id;
    JsPublishAuthorizationResult result = js_publish_authorization_preflight(
        request, make_publish_options(authority, staged.record.identity.base_live_checksum));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_publish_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsStagedPackageRepository, MissingAuthorityContextFailsActivatePreflight) {
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(), make_identity_options());
    ASSERT_TRUE(staged.ok);

    JsPublishAuthorityContext missing =
        repository.authority_context_for_version(staged.record.identity.package_id, "missing");
    JsPublishRequest request = make_activate_request(staged.record);
    JsPublishAuthorizationResult result = js_publish_authorization_preflight(
        request, make_publish_options(missing, staged.record.identity.base_live_checksum));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_publish_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsStagedPackageRepository, PublicDiagnosticNamesAreStable) {
    EXPECT_STREQ("invalid-request", js_staged_package_repository_diagnostic_code_name(
                                        JsStagedPackageRepositoryDiagnosticCode::InvalidRequest));
    EXPECT_STREQ("identity-build-failed",
                 js_staged_package_repository_diagnostic_code_name(
                     JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed));
    EXPECT_STREQ("duplicate-version-conflict",
                 js_staged_package_repository_diagnostic_code_name(
                     JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict));
    EXPECT_STREQ("record-limit-exceeded",
                 js_staged_package_repository_diagnostic_code_name(
                     JsStagedPackageRepositoryDiagnosticCode::RecordLimitExceeded));
    EXPECT_STREQ("not-found", js_staged_package_repository_diagnostic_code_name(
                                  JsStagedPackageRepositoryDiagnosticCode::NotFound));
}
