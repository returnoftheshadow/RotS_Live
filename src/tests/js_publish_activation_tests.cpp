#include "../js_publish_activation.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
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

JsStagedPackageStageOptions make_stage_options(const std::string &builder = "account:builder",
                                               const std::string &base_live = "live:old") {
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 30;
    options.identity_options.builder_account_id = builder;
    options.identity_options.base_live_checksum = base_live;
    options.identity_options.server_instance_id = "server:main";
    options.audit.staged_at_epoch_seconds = 123456;
    options.audit.request_id = "request:stage";
    options.audit.actor_id = "actor:42";
    options.audit.permission_snapshot_id = "permission:snapshot";
    options.audit.audit_id = "audit:stage";
    options.audit.source_policy_decision = "source-policy:accepted";
    options.audit.validation_report_digest = "validation:sha256:abc";
    options.audit.transport_source_identifier = "transport:tls";
    return options;
}

JsStagedPackageRecord stage_package(JsStagedPackageRepository &repository,
                                    const JsScriptPackage &package,
                                    const JsStagedPackageStageOptions &options) {
    JsStagedPackageStageResult staged = repository.stage_package(package, options);
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

JsPublishTokenMetadata make_token(JsPublishOperation operation,
                                  const std::string &builder = "account:builder") {
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

JsPublishTransportMetadata make_transport() {
    JsPublishTransportMetadata transport;
    transport.secure_channel = true;
    transport.server_identity_verified = true;
    transport.server_audience = "server:main";
    return transport;
}

JsPublishStagedRequestAssemblyInput
make_input(const JsStagedPackageRecord &record,
           JsPublishOperation operation = JsPublishOperation::PackageActivate,
           const std::string &builder = "account:builder") {
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

JsPublishActivationOptions make_options(const std::string &current_live = "live:old") {
    JsPublishActivationOptions options;
    options.assembly_options.now_epoch_seconds = 100;
    options.assembly_options.allow_mutating_operations = true;
    options.assembly_options.expected_server_audience = "server:main";
    options.assembly_options.expected_workspace_id = "workspace:main";
    options.assembly_options.current_live_checksum = current_live;
    options.allow_live_pointer_update = true;
    options.applied_at_epoch_seconds = 200000;
    options.live_pointer_audit_id = "audit:activate";
    return options;
}

bool has_code(const JsPublishActivationResult &result, JsPublishActivationDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsPublishActivationDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool has_publish_code(const JsPublishAuthorizationResult &result, JsPublishDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic &diagnostic) { return diagnostic.code == code; });
}

std::string messages(const JsPublishActivationResult &result) {
    std::string text;
    for (const JsPublishActivationDiagnostic &diagnostic : result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string read_first_available_file(std::initializer_list<const char *> paths) {
    for (const char *path : paths) {
        std::ifstream file(path);
        if (file.good())
            return std::string((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    }
    return {};
}

} // namespace

TEST(JsPublishActivation, PublishingDisabledPreflightDoesNotMutateLiveStore) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_options();
    options.assembly_options.allow_mutating_operations = false;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.assembled);
    EXPECT_FALSE(result.authorized);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::AuthorizationFailed));
    EXPECT_TRUE(has_publish_code(result.assembly.authorization_result,
                                 JsPublishDiagnosticCode::PublishingDisabled));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, LivePointerGateDefaultsClosedAfterSuccessfulPreflight) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_options();
    options.allow_live_pointer_update = false;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.authorized);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::LiveUpdateDisabled));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, AppliesAuthorizedActivationIntoLivePointerStore) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), make_options());

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(1u, live_store.package_record_count());
    EXPECT_EQ(1u, live_store.live_pointer_count());
    ASSERT_TRUE(result.live_pointer_result.ok);
    EXPECT_EQ(record.identity.package_id, result.live_pointer_result.pointer.package_id);
    EXPECT_EQ(record.identity.package_version_id,
              result.live_pointer_result.pointer.package_version_id);
    EXPECT_EQ(record.identity.canonical_digest, result.live_pointer_result.pointer.staged_digest);
    EXPECT_EQ(js_live_package_current_checksum_for_identity(record.identity),
              result.live_pointer_result.pointer.current_live_checksum);
    EXPECT_EQ("audit:activate", result.live_pointer_result.pointer.load_audit_id);
}

TEST(JsPublishActivation, PopulatesRegistrySnapshotWithoutDispatchingGameplay) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(
        repository, make_package(3001, "return ctx.self.name === 'ok'"), make_stage_options());
    ASSERT_TRUE(js_publish_apply_staged_package_activation(repository, live_store,
                                                           make_input(record), make_options())
                    .ok);

    JsScriptRegistryReplaceOptions registry_options;
    registry_options.validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    JsLivePackageRegistrySnapshotResult snapshot =
        live_store.build_live_registry_snapshot(registry_options);

    ASSERT_TRUE(snapshot.ok);
    ASSERT_NE(nullptr, snapshot.registry.find_package_by_id(record.identity.package_id));
    EXPECT_NE(nullptr, snapshot.registry.find_trigger_binding(
                           record.identity.vnum, record.identity.host,
                           JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER));
}

TEST(JsPublishActivation, CrossBuilderActivationIsRejectedBeforeMutation) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options("account:owner"));

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store,
        make_input(record, JsPublishOperation::PackageActivate, "account:intruder"),
        make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.authorized);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::AuthorizationFailed));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, StaleLivePointerReplacementIsRejectedBeforeNewRecordStorage) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first =
        stage_package(repository, make_package(3001, "return true"), make_stage_options());
    ASSERT_TRUE(js_publish_apply_staged_package_activation(repository, live_store,
                                                           make_input(first), make_options())
                    .ok);
    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);

    JsStagedPackageRecord second =
        stage_package(repository, make_package(3001, "return false"),
                      make_stage_options("account:builder", first_live_checksum));
    JsPublishActivationOptions stale_options = make_options(first_live_checksum);
    ASSERT_TRUE(js_publish_apply_staged_package_activation(repository, live_store,
                                                           make_input(second), stale_options)
                    .ok);

    JsStagedPackageRecord third =
        stage_package(repository, make_package(3001, "return ctx.self.level > 10"),
                      make_stage_options("account:builder", first_live_checksum));

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(third), make_options(first_live_checksum));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::LivePointerConflict));
    EXPECT_EQ(2u, live_store.package_record_count());
    ASSERT_TRUE(
        live_store.find_live_pointer(third.identity.zone, third.identity.host, third.identity.vnum)
            .ok);
    EXPECT_EQ(
        second.identity.package_version_id,
        live_store.find_live_pointer(third.identity.zone, third.identity.host, third.identity.vnum)
            .pointer.package_version_id);
}

TEST(JsPublishActivation, RollbackOwnUsesSameLivePointerGate) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record, JsPublishOperation::PackageRollbackOwn),
        make_options());

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(1u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, RollbackAnyRequiresExplicitAuthority) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options("account:owner"));
    JsPublishActivationOptions options = make_options();
    options.assembly_options.allow_rollback_any = true;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store,
        make_input(record, JsPublishOperation::PackageRollbackAny, "account:staff"), options);

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_TRUE(result.applied);
}

TEST(JsPublishActivation, MissingApplyAuditMetadataIsRejectedBeforeMutation) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_options();
    options.applied_at_epoch_seconds = 0;
    options.live_pointer_audit_id = "";

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::InvalidRequest));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, UnsafeLivePointerAuditIdDoesNotStorePackageRecord) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_options();
    options.live_pointer_audit_id = "audit id with spaces";

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::PointerFailed));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, LivePointerLimitFailureDoesNotStorePackageRecord) {
    JsStagedPackageRepository repository;
    JsLivePackageStoreOptions store_options;
    store_options.maximum_live_pointers = 0;
    JsLivePackageStore live_store(store_options);
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::PointerFailed));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, PackageRecordLimitFailureDoesNotLoadLivePointer) {
    JsStagedPackageRepository repository;
    JsLivePackageStoreOptions store_options;
    store_options.maximum_package_records = 0;
    JsLivePackageStore live_store(store_options);
    JsStagedPackageRecord record = stage_package(repository, make_package(), make_stage_options());

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::StoreFailed));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishActivation, PackageRecordLimitFailureDuringReplacementPreservesPointer) {
    JsStagedPackageRepository repository;
    JsLivePackageStoreOptions store_options;
    store_options.maximum_package_records = 1;
    JsLivePackageStore live_store(store_options);
    JsStagedPackageRecord first =
        stage_package(repository, make_package(3001, "return true"), make_stage_options());
    ASSERT_TRUE(js_publish_apply_staged_package_activation(repository, live_store,
                                                           make_input(first), make_options())
                    .ok);
    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);
    JsStagedPackageRecord second =
        stage_package(repository, make_package(3001, "return false"),
                      make_stage_options("account:builder", first_live_checksum));

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(second), make_options(first_live_checksum));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishActivationDiagnosticCode::StoreFailed));
    EXPECT_EQ(1u, live_store.package_record_count());
    EXPECT_EQ(1u, live_store.live_pointer_count());
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer(first.identity.zone, first.identity.host, first.identity.vnum);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(first.identity.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishActivation, DiagnosticsAreBoundedAndHaveStableNames) {
    EXPECT_STREQ("invalid-request", js_publish_activation_diagnostic_code_name(
                                        JsPublishActivationDiagnosticCode::InvalidRequest));
    EXPECT_STREQ("assembly-failed", js_publish_activation_diagnostic_code_name(
                                        JsPublishActivationDiagnosticCode::AssemblyFailed));
    EXPECT_STREQ("authorization-failed",
                 js_publish_activation_diagnostic_code_name(
                     JsPublishActivationDiagnosticCode::AuthorizationFailed));
    EXPECT_STREQ("live-update-disabled",
                 js_publish_activation_diagnostic_code_name(
                     JsPublishActivationDiagnosticCode::LiveUpdateDisabled));
    EXPECT_STREQ("live-pointer-conflict",
                 js_publish_activation_diagnostic_code_name(
                     JsPublishActivationDiagnosticCode::LivePointerConflict));
    EXPECT_STREQ("store-failed", js_publish_activation_diagnostic_code_name(
                                     JsPublishActivationDiagnosticCode::StoreFailed));
    EXPECT_STREQ("pointer-failed", js_publish_activation_diagnostic_code_name(
                                       JsPublishActivationDiagnosticCode::PointerFailed));

    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, JsPublishStagedRequestAssemblyInput(), make_options());
    EXPECT_LT(messages(result).size(), 260u);
}

TEST(JsPublishActivation, BuildFilesReferenceActivationModule) {
    const std::string cmake =
        read_first_available_file({"../CMakeLists.txt", "src/CMakeLists.txt"});
    const std::string raw_make = read_first_available_file({"../Makefile", "src/Makefile"});
    const std::string tests_make = read_first_available_file({"src/tests/Makefile", "Makefile"});

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(raw_make.empty());
    ASSERT_FALSE(tests_make.empty());
    EXPECT_NE(std::string::npos, cmake.find("js_publish_activation.cpp"));
    EXPECT_NE(std::string::npos, cmake.find("tests/js_publish_activation_tests.cpp"));
    EXPECT_NE(std::string::npos, raw_make.find("js_publish_activation.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_publish_activation_tests.cpp"));
}
