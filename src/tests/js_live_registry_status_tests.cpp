#include "../js_live_registry_status.h"

#include "../js_publish_activation.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 8001, const std::string &body = "return true") {
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

JsScriptPackage make_package_with_two_bindings(int vnum = 8011) {
    JsScriptPackage package = make_package(vnum);
    package.compiled_javascript =
        "function onEnter(ctx) { return true; } function onReceive(ctx) { return true; }";
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_RECEIVE, "onReceive"});
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

JsPublishStagedRequestAssemblyInput make_input(const JsStagedPackageRecord &record) {
    JsPublishStagedRequestAssemblyInput input;
    input.operation = JsPublishOperation::PackageActivate;
    input.request_id = "request-1";
    input.actor_id = "actor:42";
    input.builder_account_id = "account:builder";
    input.package_id = record.identity.package_id;
    input.package_version_id = record.identity.package_version_id;
    input.token = make_token(input.operation);
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
    options.durable_audit_precondition_ok = true;
    options.applied_at_epoch_seconds = 200000;
    options.live_pointer_audit_id = "audit:activate";
    return options;
}

JsStagedPackageRecord activate_package(JsStagedPackageRepository &repository,
                                       JsLivePackageStore &live_store,
                                       const JsScriptPackage &package,
                                       const std::string &base_live = "live:old") {
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options("account:builder", base_live));
    EXPECT_TRUE(staged.ok);
    JsPublishActivationResult activated = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(staged.record), make_activation_options(base_live));
    EXPECT_TRUE(activated.ok);
    return staged.record;
}

JsLiveRegistryReloadService refresh_service_with(JsLivePackageStore &live_store) {
    JsLiveRegistryReloadService service;
    EXPECT_TRUE(service.refresh_from_live_store(live_store, nullptr));
    return service;
}

bool has_code(const JsLiveRegistryStatusResult &result, JsLiveRegistryStatusDiagnosticCode code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const JsLiveRegistryStatusDiagnostic &diagnostic) {
                           return diagnostic.code == code;
                       });
}

std::string messages(const JsLiveRegistryStatusResult &result) {
    std::string text;
    for (const JsLiveRegistryStatusDiagnostic &diagnostic : result.diagnostics)
        text += diagnostic.message + "\n";
    return text;
}

std::string package_text(const JsLiveRegistryPackageInspection &package) {
    std::string text = package.host + "\n" + package.package_id + "\n" +
                       package.package_version_id + "\n" + package.staged_digest + "\n" +
                       package.current_live_checksum + "\n" + package.manifest_checksum + "\n" +
                       package.runtime_name + "\n" + package.runtime_version + "\n" +
                       package.generated_typings_version + "\n" +
                       package.compiled_javascript_checksum + "\n";
    for (const JsLiveRegistryTriggerBindingStatus &binding : package.trigger_bindings)
        text += binding.kind + "\n" + binding.handler_name + "\n";
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

TEST(JsLiveRegistryStatus, SnapshotReportsSummaryAndRedactedPackageMetadata) {
    const std::string source_sentinel = "SOURCE_SENTINEL_STATUS";
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = activate_package(
        repository, live_store, make_package(8001, "return '" + source_sentinel + "'"));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service);

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_FALSE(result.summary.empty);
    EXPECT_EQ(1u, result.summary.package_count);
    EXPECT_EQ(1u, result.summary.successful_reload_count);
    ASSERT_EQ(1u, result.packages.size());
    const JsLiveRegistryPackageInspection &package = result.packages.front();
    EXPECT_EQ(record.identity.zone, package.zone);
    EXPECT_EQ(record.identity.vnum, package.vnum);
    EXPECT_EQ("character", package.host);
    EXPECT_EQ(record.identity.package_id, package.package_id);
    EXPECT_EQ(record.identity.package_version_id, package.package_version_id);
    EXPECT_EQ(record.identity.canonical_digest, package.staged_digest);
    EXPECT_EQ(js_live_package_current_checksum_for_identity(record.identity),
              package.current_live_checksum);
    EXPECT_EQ(record.identity.manifest_checksum, package.manifest_checksum);
    EXPECT_EQ(record.identity.compiled_javascript_checksum, package.compiled_javascript_checksum);
    ASSERT_EQ(1u, package.trigger_bindings.size());
    EXPECT_EQ("legacy-script-trigger", package.trigger_bindings.front().kind);
    EXPECT_EQ(ON_ENTER, package.trigger_bindings.front().legacy_value);
    EXPECT_EQ("onEnter", package.trigger_bindings.front().handler_name);
    EXPECT_EQ(std::string::npos, package_text(package).find(source_sentinel));
    EXPECT_EQ(std::string::npos, messages(result).find(source_sentinel));
}

TEST(JsLiveRegistryStatus, SnapshotCanReturnSummaryOnly) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package(8002));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.include_package_details = false;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_EQ(1u, result.summary.package_count);
    EXPECT_TRUE(result.packages.empty());
}

TEST(JsLiveRegistryStatus, SummaryOnlyIgnoresPackageLimit) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package(8009));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.include_package_details = false;
    options.maximum_packages = 0;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_EQ(1u, result.summary.package_count);
    EXPECT_TRUE(result.packages.empty());
}

TEST(JsLiveRegistryStatus, SnapshotCanOmitTriggerBindings) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package_with_two_bindings(8003));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.include_trigger_bindings = false;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    ASSERT_EQ(1u, result.packages.size());
    EXPECT_TRUE(result.packages.front().trigger_bindings.empty());
}

TEST(JsLiveRegistryStatus, OmittedTriggerBindingsIgnoreTriggerLimit) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package_with_two_bindings(8010));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.include_trigger_bindings = false;
    options.maximum_trigger_bindings = 0;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    ASSERT_EQ(1u, result.packages.size());
    EXPECT_TRUE(result.packages.front().trigger_bindings.empty());
}

TEST(JsLiveRegistryStatus, FindsPackageByIdAndVnum) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_package(repository, live_store, make_package(8004));
    JsStagedPackageRecord second = activate_package(repository, live_store, make_package(8005));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);

    JsLiveRegistryStatusResult by_id =
        js_live_registry_status_for_package_id(service, first.identity.package_id);
    JsLiveRegistryStatusResult by_vnum =
        js_live_registry_status_for_vnum(service, second.identity.vnum);

    ASSERT_TRUE(by_id.ok) << messages(by_id);
    ASSERT_TRUE(by_vnum.ok) << messages(by_vnum);
    ASSERT_EQ(1u, by_id.packages.size());
    ASSERT_EQ(1u, by_vnum.packages.size());
    EXPECT_EQ(first.identity.package_id, by_id.packages.front().package_id);
    EXPECT_EQ(second.identity.package_id, by_vnum.packages.front().package_id);
}

TEST(JsLiveRegistryStatus, TargetedLookupsRemainSourceRedacted) {
    const std::string source_sentinel = "SOURCE_SENTINEL_LOOKUP";
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = activate_package(
        repository, live_store, make_package(8012, "return '" + source_sentinel + "'"));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions no_triggers;
    no_triggers.include_trigger_bindings = false;

    JsLiveRegistryStatusResult by_id =
        js_live_registry_status_for_package_id(service, record.identity.package_id);
    JsLiveRegistryStatusResult by_vnum =
        js_live_registry_status_for_vnum(service, record.identity.vnum, no_triggers);

    ASSERT_TRUE(by_id.ok) << messages(by_id);
    ASSERT_TRUE(by_vnum.ok) << messages(by_vnum);
    ASSERT_EQ(1u, by_id.packages.size());
    ASSERT_EQ(1u, by_vnum.packages.size());
    EXPECT_EQ(std::string::npos, package_text(by_id.packages.front()).find(source_sentinel));
    EXPECT_EQ(std::string::npos, package_text(by_vnum.packages.front()).find(source_sentinel));
    EXPECT_TRUE(by_vnum.packages.front().trigger_bindings.empty());
}

TEST(JsLiveRegistryStatus, MissingAndInvalidLookupsReturnBoundedDiagnostics) {
    JsLiveRegistryReloadService service;

    JsLiveRegistryStatusResult blank = js_live_registry_status_for_package_id(service, "");
    JsLiveRegistryStatusResult whitespace =
        js_live_registry_status_for_package_id(service, " \t\n");
    JsLiveRegistryStatusResult missing =
        js_live_registry_status_for_package_id(service, "js:missing");
    JsLiveRegistryStatusResult bad_vnum = js_live_registry_status_for_vnum(service, 0);
    JsLiveRegistryStatusOptions invalid_options;
    invalid_options.maximum_packages = 0;
    JsLiveRegistryStatusResult invalid = js_live_registry_status_snapshot(service, invalid_options);

    EXPECT_FALSE(blank.ok);
    EXPECT_TRUE(has_code(blank, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_FALSE(whitespace.ok);
    EXPECT_TRUE(has_code(whitespace, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(has_code(missing, JsLiveRegistryStatusDiagnosticCode::PackageNotFound));
    EXPECT_FALSE(bad_vnum.ok);
    EXPECT_TRUE(has_code(bad_vnum, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_FALSE(invalid.ok);
    EXPECT_TRUE(has_code(invalid, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_LT(messages(blank).size(), 260u);
    EXPECT_LT(messages(missing).size(), 260u);
}

TEST(JsLiveRegistryStatus, ZeroTriggerLimitIsInvalidWhenTriggerOutputIsIncluded) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record = activate_package(repository, live_store, make_package(8018));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_trigger_bindings = 0;

    JsLiveRegistryStatusResult snapshot = js_live_registry_status_snapshot(service, options);
    JsLiveRegistryStatusResult by_id =
        js_live_registry_status_for_package_id(service, record.identity.package_id, options);
    JsLiveRegistryStatusResult by_vnum =
        js_live_registry_status_for_vnum(service, record.identity.vnum, options);

    EXPECT_FALSE(snapshot.ok);
    EXPECT_FALSE(by_id.ok);
    EXPECT_FALSE(by_vnum.ok);
    EXPECT_TRUE(has_code(snapshot, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(has_code(by_id, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(has_code(by_vnum, JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(snapshot.packages.empty());
    EXPECT_TRUE(by_id.packages.empty());
    EXPECT_TRUE(by_vnum.packages.empty());
}

TEST(JsLiveRegistryStatus, PackageLimitFailsClosedWithoutMutatingService) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package(8006));
    activate_package(repository, live_store, make_package(8007));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_packages = 1;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLiveRegistryStatusDiagnosticCode::PackageLimitExceeded));
    EXPECT_TRUE(result.packages.empty());
    EXPECT_EQ(2u, service.package_count());
    EXPECT_EQ(2u, service.package_statuses().size());
}

TEST(JsLiveRegistryStatus, PackageLimitAllowsExactBoundary) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package(8013));
    activate_package(repository, live_store, make_package(8014));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_packages = 2;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    EXPECT_EQ(2u, result.packages.size());
}

TEST(JsLiveRegistryStatus, TriggerBindingLimitFailsWithoutReturningPackage) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package_with_two_bindings(8008));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_trigger_bindings = 1;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded));
    EXPECT_TRUE(result.packages.empty());
    EXPECT_EQ(1u, service.package_count());
}

TEST(JsLiveRegistryStatus, TriggerBindingLimitFailsClosedAcrossPackages) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package(8015));
    activate_package(repository, live_store, make_package_with_two_bindings(8016));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_trigger_bindings = 2;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded));
    EXPECT_TRUE(result.packages.empty());
    EXPECT_EQ(2u, service.package_count());
}

TEST(JsLiveRegistryStatus, TriggerBindingLimitAllowsExactBoundary) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, make_package_with_two_bindings(8017));
    JsLiveRegistryReloadService service = refresh_service_with(live_store);
    JsLiveRegistryStatusOptions options;
    options.maximum_trigger_bindings = 2;

    JsLiveRegistryStatusResult result = js_live_registry_status_snapshot(service, options);

    ASSERT_TRUE(result.ok) << messages(result);
    ASSERT_EQ(1u, result.packages.size());
    EXPECT_EQ(2u, result.packages.front().trigger_bindings.size());
}

TEST(JsLiveRegistryStatus, PublicDiagnosticNamesAreStable) {
    EXPECT_STREQ("invalid-request", js_live_registry_status_diagnostic_code_name(
                                        JsLiveRegistryStatusDiagnosticCode::InvalidRequest));
    EXPECT_STREQ("package-not-found", js_live_registry_status_diagnostic_code_name(
                                          JsLiveRegistryStatusDiagnosticCode::PackageNotFound));
    EXPECT_STREQ("package-limit-exceeded",
                 js_live_registry_status_diagnostic_code_name(
                     JsLiveRegistryStatusDiagnosticCode::PackageLimitExceeded));
    EXPECT_STREQ("trigger-binding-limit-exceeded",
                 js_live_registry_status_diagnostic_code_name(
                     JsLiveRegistryStatusDiagnosticCode::TriggerBindingLimitExceeded));
}

TEST(JsLiveRegistryStatus, StatusLayerDoesNotReferenceDispatchOrSourceBearingPackages) {
    const std::string header = read_first_available_file(
        {"../js_live_registry_status.h", "src/js_live_registry_status.h"});
    const std::string source = read_first_available_file(
        {"../js_live_registry_status.cpp", "src/js_live_registry_status.cpp"});

    ASSERT_FALSE(header.empty());
    ASSERT_FALSE(source.empty());
    EXPECT_EQ(std::string::npos, header.find("JsScriptPackage"));
    EXPECT_EQ(std::string::npos, source.find("js_trigger_dispatch"));
    EXPECT_EQ(std::string::npos, source.find("compiled_javascript;"));
}

TEST(JsLiveRegistryStatus, BuildFilesReferenceLiveRegistryStatus) {
    const std::string cmake =
        read_first_available_file({"../CMakeLists.txt", "src/CMakeLists.txt"});
    const std::string raw_make = read_first_available_file({"../Makefile", "src/Makefile"});
    const std::string tests_make = read_first_available_file({"src/tests/Makefile", "Makefile"});

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(raw_make.empty());
    ASSERT_FALSE(tests_make.empty());
    EXPECT_NE(std::string::npos, cmake.find("js_live_registry_status.cpp"));
    EXPECT_NE(std::string::npos, cmake.find("tests/js_live_registry_status_tests.cpp"));
    EXPECT_NE(std::string::npos, raw_make.find("js_live_registry_status.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_live_registry_status_tests.cpp"));
}
