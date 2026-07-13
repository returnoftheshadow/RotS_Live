#include "../js_live_registry_reload_service.h"

#include "../js_publish_activation.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 7001, const std::string &body = "return true") {
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

JsPublishActivationOptions make_activation_options(const std::string &current_live = "live:old") {
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

JsScriptRegistryReplaceOptions internal_registry_options() {
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

JsLiveRegistryReloadOptions live_reload_options() {
    JsLiveRegistryReloadOptions options;
    options.replace_options = internal_registry_options();
    return options;
}

JsStagedPackageRecord activate_package(JsStagedPackageRepository &repository,
                                       JsLivePackageStore &live_store, int vnum,
                                       const std::string &body = "return true",
                                       const std::string &base_live = "live:old") {
    JsStagedPackageRecord record = stage_package(repository, make_package(vnum, body),
                                                 make_stage_options("account:builder", base_live));
    JsPublishActivationResult activated = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record), make_activation_options(base_live));
    EXPECT_TRUE(activated.ok);
    return record;
}

bool has_code(const JsLiveRegistryReloadResult &result, JsLiveRegistryReloadDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsLiveRegistryReloadDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

std::string messages(const JsLiveRegistryReloadResult &result) {
    std::string text;
    for (const JsLiveRegistryReloadDiagnostic &diagnostic : result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string validation_messages(const JsLiveRegistryReloadResult &result) {
    std::string text;
    for (const JsScriptPackageDiagnostic &diagnostic : result.validation_result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string status_text(const JsLiveRegistryPackageStatus &status) {
    std::string text = status.package_id + "\n" + status.package_version_id + "\n" +
                       status.staged_digest + "\n" + status.current_live_checksum + "\n" +
                       status.manifest_checksum + "\n" + status.runtime_name + "\n" +
                       status.runtime_version + "\n" + status.generated_typings_version + "\n" +
                       status.compiled_javascript_checksum + "\n";
    for (const JsScriptTriggerBinding &binding : status.trigger_bindings)
        text += binding.handler_name + "\n";
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

TEST(JsLiveRegistryReloadService, StartsEmptyAndRefreshesEmptyLiveStore) {
    JsLivePackageStore live_store;
    JsLiveRegistryReloadService service;
    JsLiveRegistryReloadResult result;

    ASSERT_TRUE(service.refresh_from_live_store(live_store, &result));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(JsLiveRegistryReloadStatus::Success, result.status);
    EXPECT_EQ(0u, result.package_count);
    EXPECT_TRUE(service.empty());
    EXPECT_EQ(1u, service.successful_reload_count());
    EXPECT_EQ(0u, service.last_successful_package_count());
}

TEST(JsLiveRegistryReloadService, RefreshesRegistryFromCurrentLivePointers) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_package(repository, live_store, 7001);
    JsStagedPackageRecord second = activate_package(repository, live_store, 7002);
    JsLiveRegistryReloadService service(live_reload_options());
    JsLiveRegistryReloadResult result;

    ASSERT_TRUE(service.refresh_from_live_store(live_store, &result)) << messages(result);

    EXPECT_EQ(2u, result.package_count);
    EXPECT_EQ(2u, service.package_count());
    EXPECT_EQ(2u, service.package_statuses().size());
    EXPECT_EQ(1u, service.successful_reload_count());
    EXPECT_EQ(2u, service.last_successful_package_count());
    const JsLiveRegistryPackageStatus *first_status =
        service.find_package_status_by_id(first.identity.package_id);
    ASSERT_NE(nullptr, first_status);
    EXPECT_EQ(first.identity.zone, first_status->zone);
    EXPECT_EQ(first.identity.vnum, first_status->vnum);
    EXPECT_EQ(first.identity.package_version_id, first_status->package_version_id);
    EXPECT_EQ(first.identity.canonical_digest, first_status->staged_digest);
    EXPECT_EQ(js_live_package_current_checksum_for_identity(first.identity),
              first_status->current_live_checksum);
    EXPECT_EQ(first.identity.compiled_javascript_checksum,
              first_status->compiled_javascript_checksum);
    ASSERT_NE(nullptr, service.find_package_status_by_vnum(second.identity.vnum));
    const JsScriptTriggerBinding *binding =
        service.find_trigger_binding(first.identity.vnum, JsScriptPackageHost::Character,
                                     JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER);
    ASSERT_NE(nullptr, binding);
    EXPECT_EQ("onEnter", binding->handler_name);
}

TEST(JsLiveRegistryReloadService, DefaultConstructorRefreshesNonEmptyLiveStore) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord live = activate_package(repository, live_store, 7005);
    JsLiveRegistryReloadService service;

    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));

    ASSERT_NE(nullptr, service.find_package_status_by_id(live.identity.package_id));
    EXPECT_NE(nullptr,
              service.find_trigger_binding(live.identity.vnum, JsScriptPackageHost::Character,
                                           JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER));
}

TEST(JsLiveRegistryReloadService, RefreshUsesOnlyLivePointersNotStoredRecords) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord live = activate_package(repository, live_store, 7011);
    JsStagedPackageRecord staged_only =
        stage_package(repository, make_package(7012), make_stage_options());
    ASSERT_TRUE(live_store.store_staged_record(staged_only).ok);
    JsLiveRegistryReloadService service(live_reload_options());

    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));

    EXPECT_EQ(1u, service.package_count());
    EXPECT_NE(nullptr, service.find_package_status_by_id(live.identity.package_id));
    EXPECT_EQ(nullptr, service.find_package_status_by_id(staged_only.identity.package_id));
}

TEST(JsLiveRegistryReloadService, RecoversAfterFailedRefreshWithLaterValidSnapshot) {
    JsStagedPackageRepository repository;
    JsLivePackageStore initial_store;
    JsStagedPackageRecord first = activate_package(repository, initial_store, 7022);
    JsLiveRegistryReloadOptions options = live_reload_options();
    options.replace_options.allow_empty_replacement = false;
    JsLiveRegistryReloadService service(options);
    ASSERT_TRUE(service.refresh_from_live_store(initial_store, nullptr));

    JsLivePackageStore empty_store;
    JsLiveRegistryReloadResult failed;
    ASSERT_FALSE(service.refresh_from_live_store(empty_store, &failed));

    JsLivePackageStore replacement_store;
    JsStagedPackageRecord second = activate_package(repository, replacement_store, 7023);
    JsLiveRegistryReloadResult recovered;
    ASSERT_TRUE(service.refresh_from_live_store(replacement_store, &recovered))
        << messages(recovered);

    EXPECT_EQ(1u, recovered.package_count);
    EXPECT_EQ(2u, service.successful_reload_count());
    EXPECT_EQ(1u, service.last_successful_package_count());
    EXPECT_EQ(nullptr, service.find_package_status_by_id(first.identity.package_id));
    EXPECT_NE(nullptr, service.find_package_status_by_id(second.identity.package_id));
}

TEST(JsLiveRegistryReloadService, FailedRefreshPreservesLastSuccessfulRegistry) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_package(repository, live_store, 7021);
    JsLiveRegistryReloadService service(live_reload_options());
    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));

    JsLiveRegistryReloadOptions no_empty_options = live_reload_options();
    no_empty_options.replace_options.allow_empty_replacement = false;
    JsLiveRegistryReloadService failing_service(no_empty_options);
    ASSERT_TRUE(failing_service.refresh_from_live_store(live_store, nullptr));

    JsLivePackageStore empty_store;
    JsLiveRegistryReloadResult result;
    EXPECT_FALSE(failing_service.refresh_from_live_store(empty_store, &result));

    EXPECT_EQ(JsLiveRegistryReloadStatus::ValidationFailed, result.status);
    EXPECT_TRUE(has_code(result, JsLiveRegistryReloadDiagnosticCode::ValidationFailed));
    EXPECT_EQ(1u, failing_service.package_count());
    EXPECT_EQ(1u, failing_service.package_statuses().size());
    EXPECT_EQ(1u, failing_service.successful_reload_count());
    EXPECT_NE(nullptr, failing_service.find_package_status_by_id(first.identity.package_id));
}

TEST(JsLiveRegistryReloadService, RefreshesSameSlotReplacementFromLivePointerStore) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_package(repository, live_store, 7026, "return true");
    JsLiveRegistryReloadService service(live_reload_options());
    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));
    const JsLiveRegistryPackageStatus *first_status =
        service.find_package_status_by_vnum(first.identity.vnum);
    ASSERT_NE(nullptr, first_status);
    const std::string first_checksum = first_status->compiled_javascript_checksum;

    const std::string current_live = js_live_package_current_checksum_for_identity(first.identity);
    JsStagedPackageRecord second =
        activate_package(repository, live_store, 7026, "return false", current_live);
    JsLiveRegistryReloadResult result;
    ASSERT_TRUE(service.refresh_from_live_store(live_store, &result)) << messages(result);

    ASSERT_EQ(1u, service.package_statuses().size());
    const JsLiveRegistryPackageStatus *second_status =
        service.find_package_status_by_vnum(second.identity.vnum);
    ASSERT_NE(nullptr, second_status);
    EXPECT_EQ(second.identity.package_version_id, second_status->package_version_id);
    EXPECT_EQ(second.identity.canonical_digest, second_status->staged_digest);
    EXPECT_NE(first_checksum, second_status->compiled_javascript_checksum);
    EXPECT_EQ(second.identity.compiled_javascript_checksum,
              second_status->compiled_javascript_checksum);
    EXPECT_EQ(2u, service.successful_reload_count());
    EXPECT_NE(nullptr,
              service.find_trigger_binding(second.identity.vnum, JsScriptPackageHost::Character,
                                           JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER));
}

TEST(JsLiveRegistryReloadService, FailedValidationDoesNotReplaceExistingSnapshot) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_package(repository, live_store, 7031);
    JsLiveRegistryReloadService service(live_reload_options());
    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));

    JsLiveRegistryReloadOptions conflict_options = live_reload_options();
    conflict_options.replace_options.legacy_script_vnums.push_back(7032);
    JsLiveRegistryReloadService conflict_service(conflict_options);
    ASSERT_TRUE(conflict_service.refresh_from_live_store(live_store, nullptr));

    JsLivePackageStore conflict_store;
    activate_package(repository, conflict_store, 7032);
    JsLiveRegistryReloadResult result;
    EXPECT_FALSE(conflict_service.refresh_from_live_store(conflict_store, &result));

    EXPECT_EQ(JsLiveRegistryReloadStatus::ValidationFailed, result.status);
    EXPECT_TRUE(has_code(result, JsLiveRegistryReloadDiagnosticCode::ValidationFailed));
    EXPECT_EQ(1u, conflict_service.package_count());
    EXPECT_NE(nullptr, conflict_service.find_package_status_by_id(first.identity.package_id));
    EXPECT_EQ(nullptr, conflict_service.find_package_status_by_vnum(7032));
    EXPECT_EQ(std::string::npos, messages(result).find("function onEnter"));
    EXPECT_EQ(std::string::npos, validation_messages(result).find("function onEnter"));
}

TEST(JsLiveRegistryReloadService, StatusSnapshotIsMetadataOnly) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    const std::string source_sentinel = "SOURCE_SENTINEL_7041";
    JsStagedPackageRecord live =
        activate_package(repository, live_store, 7041, "return '" + source_sentinel + "'");
    JsLiveRegistryReloadService service(live_reload_options());

    ASSERT_TRUE(service.refresh_from_live_store(live_store, nullptr));

    ASSERT_EQ(1u, service.package_statuses().size());
    const JsLiveRegistryPackageStatus &status = service.package_statuses().front();
    EXPECT_EQ(live.identity.vnum, status.vnum);
    EXPECT_EQ(live.identity.package_id, status.package_id);
    EXPECT_EQ(live.identity.compiled_javascript_checksum, status.compiled_javascript_checksum);
    ASSERT_EQ(1u, status.trigger_bindings.size());
    EXPECT_EQ("onEnter", status.trigger_bindings.front().handler_name);
    EXPECT_EQ(std::string::npos, status_text(status).find(source_sentinel));

    const std::string header = read_first_available_file(
        {"../js_live_registry_reload_service.h", "src/js_live_registry_reload_service.h"});
    ASSERT_FALSE(header.empty());
    EXPECT_EQ(std::string::npos, header.find("const JsScriptPackage *find_package"));
    EXPECT_EQ(std::string::npos, header.find("const JsScriptPackageRegistry &registry"));
}

TEST(JsLiveRegistryReloadService, DiagnosticsAreBoundedAndStable) {
    EXPECT_STREQ("success",
                 js_live_registry_reload_status_name(JsLiveRegistryReloadStatus::Success));
    EXPECT_STREQ("live-store-failed",
                 js_live_registry_reload_status_name(JsLiveRegistryReloadStatus::LiveStoreFailed));
    EXPECT_STREQ("validation-failed",
                 js_live_registry_reload_status_name(JsLiveRegistryReloadStatus::ValidationFailed));
    EXPECT_STREQ("live-store-failed", js_live_registry_reload_diagnostic_code_name(
                                          JsLiveRegistryReloadDiagnosticCode::LiveStoreFailed));
    EXPECT_STREQ("validation-failed", js_live_registry_reload_diagnostic_code_name(
                                          JsLiveRegistryReloadDiagnosticCode::ValidationFailed));

    JsLiveRegistryReloadOptions options = live_reload_options();
    options.replace_options.allow_empty_replacement = false;
    JsLiveRegistryReloadService service(options);
    JsLivePackageStore empty_store;
    JsLiveRegistryReloadResult result;

    EXPECT_FALSE(service.refresh_from_live_store(empty_store, &result));
    EXPECT_LT(messages(result).size(), 260u);
    EXPECT_EQ(std::string::npos, messages(result).find("function onEnter"));
}

TEST(JsLiveRegistryReloadService, BuildFilesReferenceLiveRegistryReloadService) {
    const std::string cmake =
        read_first_available_file({"../CMakeLists.txt", "src/CMakeLists.txt"});
    const std::string raw_make = read_first_available_file({"../Makefile", "src/Makefile"});
    const std::string tests_make = read_first_available_file({"src/tests/Makefile", "Makefile"});

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(raw_make.empty());
    ASSERT_FALSE(tests_make.empty());
    EXPECT_NE(std::string::npos, cmake.find("js_live_registry_reload_service.cpp"));
    EXPECT_NE(std::string::npos, cmake.find("tests/js_live_registry_reload_service_tests.cpp"));
    EXPECT_NE(std::string::npos, raw_make.find("js_live_registry_reload_service.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_live_registry_reload_service_tests.cpp"));
}
