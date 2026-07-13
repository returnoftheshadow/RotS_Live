#include "../js_publish_staging.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001, const std::string& body = "return true")
{
    const JsScriptingManifestMetadata& metadata = js_scripting_manifest_metadata();
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
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter" });
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageIdentityOptions make_identity_options(
    const std::string& builder = "account:builder", const std::string& base_live = "live:old")
{
    JsStagedPackageIdentityOptions options;
    options.zone = 30;
    options.builder_account_id = builder;
    options.base_live_checksum = base_live;
    options.server_instance_id = "server:main";
    return options;
}

JsStagedPackageRecord stage_package(JsStagedPackageRepository& repository,
    const JsScriptPackage& package, const JsStagedPackageIdentityOptions& options)
{
    JsStagedPackageStageResult staged = repository.stage_package(package, options);
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

JsPublishTokenMetadata make_token(JsPublishOperation operation,
    const std::string& builder = "account:builder")
{
    JsPublishTokenMetadata token;
    token.token_id = "token-1";
    token.claims_verified = true;
    token.actor_id = "actor:42";
    token.builder_account_id = builder;
    token.server_audience = "server:main";
    token.workspace_id = "workspace:main";
    token.scopes = js_publish_scope_for_operation(operation);
    token.issued_at_epoch_seconds = 90;
    token.expires_at_epoch_seconds = 200;
    return token;
}

JsPublishTransportMetadata make_transport()
{
    JsPublishTransportMetadata transport;
    transport.secure_channel = true;
    transport.server_identity_verified = true;
    transport.server_audience = "server:main";
    return transport;
}

JsPublishStagedRequestAssemblyInput make_input(const JsStagedPackageRecord& record,
    JsPublishOperation operation = JsPublishOperation::PackageActivate,
    const std::string& builder = "account:builder")
{
    JsPublishStagedRequestAssemblyInput input;
    input.operation = operation;
    input.request_id = "request-1";
    input.actor_id = "actor:42";
    input.builder_account_id = builder;
    input.package_id = record.identity.package_id;
    input.package_version_id = record.identity.package_version_id;
    input.token = make_token(operation, builder);
    input.transport = make_transport();
    return input;
}

JsPublishStagedRequestAssemblyOptions make_options()
{
    JsPublishStagedRequestAssemblyOptions options;
    options.now_epoch_seconds = 100;
    options.expected_server_audience = "server:main";
    options.expected_workspace_id = "workspace:main";
    options.current_live_checksum = "live:old";
    return options;
}

bool has_code(const JsPublishStagedPackageStatusResult& result,
    JsPublishStagingDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishStagingDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

bool has_code(const JsPublishStagedRequestAssemblyResult& result,
    JsPublishStagingDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishStagingDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

bool has_publish_code(const JsPublishAuthorizationResult& result, JsPublishDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

std::string staging_messages(const JsPublishStagedPackageStatusResult& result)
{
    std::string text;
    for (const JsPublishStagingDiagnostic& diagnostic : result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string staging_messages(const JsPublishStagedRequestAssemblyResult& result)
{
    std::string text;
    for (const JsPublishStagingDiagnostic& diagnostic : result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string read_first_available_file(std::initializer_list<const char*> paths)
{
    for (const char* path : paths) {
        std::ifstream file(path);
        if (file.good())
            return std::string((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
    }
    return {};
}

} // namespace

TEST(JsPublishStaging, ReportsExactStagedPackageStatusWithoutSourceBody)
{
    JsStagedPackageRepository repository;
    JsScriptPackage package = make_package(3001, "return ctx.self.name === 'secret-source'");
    JsStagedPackageRecord record =
        stage_package(repository, package, make_identity_options());

    JsPublishStagedPackageStatusResult result = js_publish_staged_package_status(
        repository, record.identity.package_id, record.identity.package_version_id);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(record.identity.package_id, result.status.package_id);
    EXPECT_EQ(record.identity.package_version_id, result.status.package_version_id);
    EXPECT_EQ(record.identity.zone, result.status.zone);
    EXPECT_EQ(record.identity.vnum, result.status.vnum);
    EXPECT_EQ(record.identity.host, result.status.host);
    EXPECT_EQ(record.identity.canonical_digest, result.status.staged_digest);
    EXPECT_EQ(record.identity.digest_algorithm, result.status.digest_algorithm);
    EXPECT_EQ(record.identity.canonical_format_version,
        result.status.canonical_format_version);
    EXPECT_EQ(record.identity.package_format_version, result.status.package_format_version);
    EXPECT_EQ(record.audit.staged_at_epoch_seconds, result.status.staged_at_epoch_seconds);
    EXPECT_EQ(record.identity.base_live_checksum, result.status.base_live_checksum);
    EXPECT_EQ(record.identity.manifest_checksum, result.status.manifest_checksum);
    EXPECT_EQ(record.identity.compiled_javascript_checksum,
        result.status.compiled_javascript_checksum);
    EXPECT_EQ(record.identity.runtime_name, result.status.runtime_name);
    EXPECT_EQ(record.identity.runtime_version, result.status.runtime_version);
    EXPECT_EQ(record.identity.generated_typings_version,
        result.status.generated_typings_version);
    EXPECT_TRUE(staging_messages(result).find("secret-source") == std::string::npos);
}

TEST(JsPublishStaging, ReportsLatestStatusForPackage)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord first =
        stage_package(repository, make_package(3001, "return true"), make_identity_options());
    JsStagedPackageRecord second = stage_package(
        repository, make_package(3001, "return false"), make_identity_options("account:builder",
                                                      "live:new"));

    JsPublishStagedPackageStatusResult result =
        js_publish_latest_staged_package_status(repository, first.identity.package_id);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(second.identity.package_version_id, result.status.package_version_id);
    EXPECT_EQ("live:new", result.status.base_live_checksum);
}

TEST(JsPublishStaging, MissingStatusReturnsBoundedSafeDiagnostic)
{
    JsStagedPackageRepository repository;

    JsPublishStagedPackageStatusResult result =
        js_publish_staged_package_status(repository, "js:30:character:3001", "missing");

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::StagedPackageNotFound));
    EXPECT_LT(staging_messages(result).size(), 260u);
}

TEST(JsPublishStaging, BlankStatusLookupIsInvalid)
{
    JsStagedPackageRepository repository;

    JsPublishStagedPackageStatusResult result =
        js_publish_staged_package_status(repository, "", "");
    JsPublishStagedPackageStatusResult whitespace =
        js_publish_staged_package_status(repository, " \t", "\n ");

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::InvalidRequest));
    EXPECT_FALSE(whitespace.ok);
    EXPECT_TRUE(has_code(whitespace, JsPublishStagingDiagnosticCode::InvalidRequest));
}

TEST(JsPublishStaging, AssemblesActivationPreflightFromStagedRecordMetadata)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());

    JsPublishStagedRequestAssemblyResult assembled = js_publish_assemble_staged_package_request(
        repository, make_input(record), make_options());

    ASSERT_TRUE(assembled.assembled);
    EXPECT_EQ(record.identity.zone, assembled.request.zone);
    EXPECT_EQ(record.identity.vnum, assembled.request.vnum);
    EXPECT_EQ(record.identity.host, assembled.request.host);
    EXPECT_EQ(record.identity.package_id, assembled.request.package_id);
    EXPECT_EQ(record.identity.package_version_id, assembled.request.package_version_id);
    EXPECT_EQ(record.identity.canonical_digest, assembled.request.staged_digest);
    EXPECT_EQ(record.identity.base_live_checksum, assembled.request.expected_live_checksum);
    EXPECT_EQ(record.identity.manifest_checksum, assembled.request.manifest_checksum);
    EXPECT_EQ(record.identity.package_id,
        assembled.authorization_options.authority.package_id);
    EXPECT_TRUE(assembled.authorization_options.authority.staged_record_loaded);
    EXPECT_TRUE(has_publish_code(
        assembled.authorization_result, JsPublishDiagnosticCode::PublishingDisabled));
}

TEST(JsPublishStaging, AssembledActivationStillPublishesDisabledByDefault)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());

    JsPublishStagedRequestAssemblyResult assembled = js_publish_assemble_staged_package_request(
        repository, make_input(record), make_options());

    ASSERT_TRUE(assembled.assembled);
    JsPublishAuthorizationResult preflight = assembled.authorization_result;

    EXPECT_FALSE(preflight.ok);
    EXPECT_TRUE(has_publish_code(preflight, JsPublishDiagnosticCode::PublishingDisabled));
}

TEST(JsPublishStaging, AssembledActivationCanPassOnlyWhenMutationGateIsExplicitlyEnabled)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;

    JsPublishStagedRequestAssemblyResult assembled = js_publish_assemble_staged_package_request(
        repository, make_input(record), options);

    ASSERT_TRUE(assembled.assembled);
    JsPublishAuthorizationResult preflight = assembled.authorization_result;

    EXPECT_TRUE(preflight.ok);
    EXPECT_TRUE(preflight.diagnostics.empty());
}

TEST(JsPublishStaging, CrossBuilderActivationUsesServerOwnerAndIsRejected)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options("account:owner"));
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageActivate, "account:other");

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, input, options);

    ASSERT_TRUE(assembled.assembled);
    JsPublishAuthorizationResult preflight = assembled.authorization_result;

    EXPECT_FALSE(preflight.ok);
    EXPECT_TRUE(has_publish_code(preflight, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishStaging, ExpectedLiveChecksumMismatchFailsPreflight)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    JsPublishStagedRequestAssemblyInput input = make_input(record);
    input.expected_live_checksum = "live:stale";

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, input, options);

    ASSERT_TRUE(assembled.assembled);
    JsPublishAuthorizationResult preflight = assembled.authorization_result;

    EXPECT_FALSE(preflight.ok);
    EXPECT_TRUE(
        has_publish_code(preflight, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishStaging, RefusesAssemblyWithoutCurrentLiveChecksum)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    options.current_live_checksum.clear();

    JsPublishStagedRequestAssemblyResult result =
        js_publish_assemble_staged_package_request(repository, make_input(record), options);

    EXPECT_FALSE(result.assembled);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::InvalidRequest));
}

TEST(JsPublishStaging, AssemblesRollbackOwnFromStagedRecord)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageRollbackOwn);

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, input, options);

    ASSERT_TRUE(assembled.assembled);
    JsPublishAuthorizationResult preflight = assembled.authorization_result;

    EXPECT_TRUE(preflight.ok);
}

TEST(JsPublishStaging, RollbackAnyRequiresExplicitServerAuthority)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options("account:owner"));
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageRollbackAny, "account:admin");

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, input, options);

    ASSERT_TRUE(assembled.assembled);
    EXPECT_FALSE(assembled.authorization_result.ok);
    EXPECT_TRUE(
        has_publish_code(assembled.authorization_result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishStaging, RollbackAnyAllowsCrossBuilderWhenServerAuthorityIsExplicit)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options("account:owner"));
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    options.allow_rollback_any = true;
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageRollbackAny, "account:admin");

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, input, options);

    ASSERT_TRUE(assembled.assembled);
    EXPECT_TRUE(assembled.authorization_result.ok);
}

TEST(JsPublishStaging, PropagatesRateLimitAndExpectedServerOptionsToPreflight)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    options.rate_limited = true;

    JsPublishStagedRequestAssemblyResult rate_limited =
        js_publish_assemble_staged_package_request(repository, make_input(record), options);

    ASSERT_TRUE(rate_limited.assembled);
    EXPECT_TRUE(
        has_publish_code(rate_limited.authorization_result, JsPublishDiagnosticCode::RateLimited));

    options.rate_limited = false;
    options.expected_server_audience = "server:other";
    JsPublishStagedRequestAssemblyResult wrong_audience =
        js_publish_assemble_staged_package_request(repository, make_input(record), options);

    ASSERT_TRUE(wrong_audience.assembled);
    EXPECT_TRUE(has_publish_code(wrong_audience.authorization_result,
                    JsPublishDiagnosticCode::TokenAudienceMismatch)
        || has_publish_code(wrong_audience.authorization_result,
            JsPublishDiagnosticCode::TokenServerMismatch));
}

TEST(JsPublishStaging, PropagatesExpectedWorkspaceToPreflight)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyOptions options = make_options();
    options.allow_mutating_operations = true;
    options.expected_workspace_id = "workspace:other";

    JsPublishStagedRequestAssemblyResult assembled =
        js_publish_assemble_staged_package_request(repository, make_input(record), options);

    ASSERT_TRUE(assembled.assembled);
    EXPECT_FALSE(assembled.authorization_result.ok);
    EXPECT_TRUE(
        has_publish_code(assembled.authorization_result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishStaging, RejectsUnsupportedAssemblyOperation)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::StatusRead);

    JsPublishStagedRequestAssemblyResult result =
        js_publish_assemble_staged_package_request(repository, input, make_options());

    EXPECT_FALSE(result.assembled);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::InvalidRequest));
}

TEST(JsPublishStaging, RejectsWhitespaceOnlyAssemblyIds)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_identity_options());
    JsPublishStagedRequestAssemblyInput input = make_input(record);
    input.package_id = " \t";
    input.package_version_id = "\n ";

    JsPublishStagedRequestAssemblyResult result =
        js_publish_assemble_staged_package_request(repository, input, make_options());

    EXPECT_FALSE(result.assembled);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::InvalidRequest));
}

TEST(JsPublishStaging, MissingAssemblyLookupDoesNotExposeSourceText)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = stage_package(
        repository, make_package(3001, "return 'private package source'"),
        make_identity_options());
    JsPublishStagedRequestAssemblyInput input = make_input(record);
    input.package_version_id = "missing";

    JsPublishStagedRequestAssemblyResult result =
        js_publish_assemble_staged_package_request(repository, input, make_options());

    EXPECT_FALSE(result.assembled);
    EXPECT_TRUE(has_code(result, JsPublishStagingDiagnosticCode::StagedPackageNotFound));
    EXPECT_TRUE(staging_messages(result).find("private package source") == std::string::npos);
}

TEST(JsPublishStaging, ExposesStableDiagnosticNames)
{
    EXPECT_STREQ("invalid-request",
        js_publish_staging_diagnostic_code_name(JsPublishStagingDiagnosticCode::InvalidRequest));
    EXPECT_STREQ("staged-package-not-found",
        js_publish_staging_diagnostic_code_name(
            JsPublishStagingDiagnosticCode::StagedPackageNotFound));
}

TEST(JsPublishStaging, BuildFilesIncludePublishStagingSourcesAndTests)
{
    const std::string cmake_text =
        read_first_available_file({ "src/CMakeLists.txt", "../src/CMakeLists.txt" });
    const std::string src_make_text =
        read_first_available_file({ "src/Makefile", "../src/Makefile" });
    const std::string test_make_text =
        read_first_available_file({ "src/tests/Makefile", "../src/tests/Makefile" });
    ASSERT_FALSE(cmake_text.empty());
    ASSERT_FALSE(src_make_text.empty());
    ASSERT_FALSE(test_make_text.empty());

    EXPECT_NE(std::string::npos, cmake_text.find("js_publish_staging.cpp"));
    EXPECT_NE(std::string::npos, cmake_text.find("tests/js_publish_staging_tests.cpp"));
    EXPECT_NE(std::string::npos, src_make_text.find("js_publish_staging.o"));
    EXPECT_NE(std::string::npos, test_make_text.find("js_publish_staging.o"));
    EXPECT_NE(std::string::npos, test_make_text.find("js_publish_staging_tests.cpp"));
}
