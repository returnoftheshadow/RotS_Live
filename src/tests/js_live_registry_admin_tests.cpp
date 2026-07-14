#include "../js_live_registry_admin.h"

#include "../js_publish_activation.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <fstream>
#include <initializer_list>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 8701, const std::string &body = "return true") {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "admin-pkg-" + std::to_string(vnum);
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

JsStagedPackageStageOptions make_stage_options(const std::string &base_live = "live:old") {
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 33;
    options.identity_options.builder_account_id = "account:builder";
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

JsPublishTokenMetadata make_token(JsPublishOperation operation) {
    JsPublishTokenMetadata token;
    token.token_id = "token-1";
    token.claims_verified = true;
    token.actor_id = "actor:42";
    token.builder_account_id = "account:builder";
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
    options.applied_at_epoch_seconds = 200000;
    options.live_pointer_audit_id = "audit:activate";
    return options;
}

JsStagedPackageRecord activate_package(JsLiveRegistryAdminService &service,
                                       JsStagedPackageRepository &repository,
                                       const JsScriptPackage &package,
                                       const std::string &base_live = "live:old") {
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options(base_live));
    EXPECT_TRUE(staged.ok);
    JsPublishActivationResult activated = js_publish_apply_staged_package_activation(
        repository, service.live_store(), make_input(staged.record),
        make_activation_options(base_live));
    EXPECT_TRUE(activated.ok);
    return staged.record;
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

TEST(JsLiveRegistryAdmin, RefreshCommandReloadsLiveStoreAndPrintsRedactedStatus) {
    const std::string source_sentinel = "ADMIN_SOURCE_SENTINEL";
    JsLiveRegistryAdminService service;
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(
        service, repository, make_package(8701, "return '" + source_sentinel + "'"));

    JsLiveRegistryAdminCommandResult result =
        js_live_registry_handle_reload_command(service, "js refresh");

    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(result.ok) << result.output;
    EXPECT_NE(std::string::npos, result.output.find("JavaScript live registry refresh: success"));
    EXPECT_NE(std::string::npos, result.output.find(record.identity.package_id));
    EXPECT_NE(std::string::npos, result.output.find("trigger legacy-script-trigger:"));
    EXPECT_EQ(std::string::npos, result.output.find(source_sentinel));
    EXPECT_EQ(1u, service.reload_service().successful_reload_count());
}

TEST(JsLiveRegistryAdmin, CustomReloadOptionsAreHonoredByRefresh) {
    JsLiveRegistryReloadOptions options = js_live_registry_server_reload_options();
    options.replace_options.allow_empty_replacement = false;
    JsLiveRegistryAdminService service(options);

    JsLiveRegistryAdminCommandResult result =
        js_live_registry_handle_reload_command(service, "js refresh");

    ASSERT_TRUE(result.handled);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.output.find("validation-failed"));
    EXPECT_EQ(0u, service.reload_service().successful_reload_count());
}

TEST(JsLiveRegistryAdmin, StatusCommandDoesNotRefreshLiveRegistry) {
    JsLiveRegistryAdminService service;
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(service, repository, make_package(8702));

    JsLiveRegistryAdminCommandResult before_refresh =
        js_live_registry_handle_reload_command(service, "js status");
    JsLiveRegistryAdminCommandResult refresh =
        js_live_registry_handle_reload_command(service, "javascript");
    JsLiveRegistryAdminCommandResult after_refresh =
        js_live_registry_handle_reload_command(service, "javascripts status");

    ASSERT_TRUE(before_refresh.handled);
    EXPECT_TRUE(before_refresh.ok);
    EXPECT_NE(std::string::npos, before_refresh.output.find("packages=0"));
    ASSERT_TRUE(refresh.ok) << refresh.output;
    ASSERT_TRUE(after_refresh.ok) << after_refresh.output;
    EXPECT_NE(std::string::npos, after_refresh.output.find(record.identity.package_id));
    EXPECT_EQ(1u, service.reload_service().successful_reload_count());
}

TEST(JsLiveRegistryAdmin, StatusCommandCanInspectByVnumOrPackageId) {
    const std::string source_sentinel = "TARGETED_SOURCE_SENTINEL";
    JsLiveRegistryAdminService service;
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(
        service, repository, make_package(8703, "return '" + source_sentinel + "'"));
    ASSERT_TRUE(js_live_registry_handle_reload_command(service, "js refresh").ok);

    JsLiveRegistryAdminCommandResult by_vnum =
        js_live_registry_handle_reload_command(service, "js status 8703");
    JsLiveRegistryAdminCommandResult by_package =
        js_live_registry_handle_reload_command(service, "js status " + record.identity.package_id);

    ASSERT_TRUE(by_vnum.ok) << by_vnum.output;
    ASSERT_TRUE(by_package.ok) << by_package.output;
    EXPECT_NE(std::string::npos, by_vnum.output.find(record.identity.package_id));
    EXPECT_NE(std::string::npos, by_package.output.find("vnum=8703"));
    EXPECT_EQ(std::string::npos, by_vnum.output.find(source_sentinel));
    EXPECT_EQ(std::string::npos, by_package.output.find(source_sentinel));
    EXPECT_EQ(std::string::npos, by_vnum.output.find("function onEnter"));
    EXPECT_EQ(std::string::npos, by_package.output.find("compiled_javascript"));
    EXPECT_EQ(std::string::npos, by_package.output.find("audit:activate"));
}

TEST(JsLiveRegistryAdmin, StatusCommandReportsLookupFailuresWithoutRefreshing) {
    JsLiveRegistryAdminService service;
    ASSERT_TRUE(js_live_registry_handle_reload_command(service, "js refresh").ok);

    JsLiveRegistryAdminCommandResult result =
        js_live_registry_handle_reload_command(service, "js status missing-package");

    ASSERT_TRUE(result.handled);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.output.find("diagnostic package-not-found"));
    EXPECT_EQ(1u, service.reload_service().successful_reload_count());
}

TEST(JsLiveRegistryAdmin, StatusCommandRejectsMalformedNumericLookupsWithoutRefreshing) {
    JsLiveRegistryAdminService service;
    ASSERT_TRUE(js_live_registry_handle_reload_command(service, "js refresh").ok);

    const char *bad_vnums[] = {"-1", "+", "0", "2147483648", "999999999999999999999999"};
    for (const char *bad_vnum : bad_vnums) {
        JsLiveRegistryAdminCommandResult result =
            js_live_registry_handle_reload_command(service, std::string("js status ") + bad_vnum);

        ASSERT_TRUE(result.handled) << bad_vnum;
        EXPECT_FALSE(result.ok) << bad_vnum << "\n" << result.output;
        EXPECT_NE(std::string::npos, result.output.find("diagnostic invalid-request"))
            << bad_vnum << "\n" << result.output;
        EXPECT_EQ(1u, service.reload_service().successful_reload_count()) << bad_vnum;
    }
}

TEST(JsLiveRegistryAdmin, ReloadCommandRejectsUnknownSubcommandsAndIgnoresOtherReloads) {
    JsLiveRegistryAdminService service;

    JsLiveRegistryAdminCommandResult unknown =
        js_live_registry_handle_reload_command(service, "js publish now");
    JsLiveRegistryAdminCommandResult unrelated =
        js_live_registry_handle_reload_command(service, "wizlist");

    ASSERT_TRUE(unknown.handled);
    EXPECT_FALSE(unknown.ok);
    EXPECT_NE(std::string::npos, unknown.output.find("Usage: reload js"));
    EXPECT_FALSE(unrelated.handled);
}

TEST(JsLiveRegistryAdmin, SourceDoesNotWireGameplayDispatchIntoAdminPlumbing) {
    const std::string source = read_first_available_file(
        {"../js_live_registry_admin.cpp", "src/js_live_registry_admin.cpp"});

    ASSERT_FALSE(source.empty());
    EXPECT_EQ(std::string::npos, source.find("js_trigger_dispatch"));
    EXPECT_EQ(std::string::npos, source.find("dispatch_trigger"));
    EXPECT_EQ(std::string::npos, source.find("compiled_javascript;"));
}

TEST(JsLiveRegistryAdmin, DbIntegrationKeepsStartupAndReloadHooksInPlace) {
    const std::string source = read_first_available_file({"src/db.cpp", "../db.cpp"});

    ASSERT_FALSE(source.empty());
    EXPECT_NE(std::string::npos, source.find("js_live_registry_handle_reload_command"));
    EXPECT_NE(std::string::npos, source.find("js_live_registry_admin_service()"));
    EXPECT_NE(std::string::npos, source.find("js_live_registry_startup_refresh()"));
    EXPECT_EQ(std::string::npos, source.find("js_trigger_dispatch"));
}

TEST(JsLiveRegistryAdmin, BuildFilesIncludeAdminModuleAndTests) {
    const std::string cmake = read_first_available_file({"../CMakeLists.txt", "src/CMakeLists.txt"});
    const std::string raw_make = read_first_available_file({"../Makefile", "src/Makefile"});
    const std::string tests_make = read_first_available_file({"src/tests/Makefile", "Makefile"});

    EXPECT_NE(std::string::npos, cmake.find("js_live_registry_admin.cpp"));
    EXPECT_NE(std::string::npos, cmake.find("tests/js_live_registry_admin_tests.cpp"));
    EXPECT_NE(std::string::npos, raw_make.find("js_live_registry_admin.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_live_registry_admin.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_live_registry_admin_tests.cpp"));
}
