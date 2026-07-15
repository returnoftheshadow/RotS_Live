#include "../js_live_registry_admin.h"

#include "../js_publish_http_endpoint.h"
#include "../js_publish_activation.h"
#include "../json_utils.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <unistd.h>

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

JsPublishEndpointTransportContext make_publish_context(unsigned scopes) {
    JsPublishEndpointTransportContext context;
    context.request_id = "request:http";
    context.audit_id = "audit:http";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    context.zone = 33;
    context.builder_eligibility.ok = true;
    context.builder_eligibility.builder_account_id = "account:builder";
    context.builder_eligibility.eligible_character_name = "builderone";
    context.builder_eligibility.eligible_character_id = 1001;
    context.builder_eligibility.eligible_character_level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    context.target_zone_resolved = true;
    context.server_resolved_target_zone = 33;
    context.server_resolved_target_host = JsScriptPackageHost::Character;
    context.zone_exists = true;
    context.zone_owner_character_ids = { 1001 };
    context.token = make_token(JsPublishOperation::StatusRead);
    context.token.scopes = scopes;
    context.transport = make_transport();
    context.now_epoch_seconds = 100;
    context.allow_mutating_operations = true;
    context.allow_live_pointer_update = true;
    context.applied_at_epoch_seconds = 200;
    context.expected_server_audience = "server:main";
    context.expected_workspace_id = "workspace:main";
    context.current_live_checksum = "live:old";
    return context;
}

std::string quote(const std::string &value) {
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

std::string package_json(const JsScriptPackage &package) {
    return "{"
        "\"vnum\":" + std::to_string(package.vnum)
        + ",\"packageId\":" + quote(package.package_id)
        + ",\"host\":" + quote(js_script_package_host_name(package.host))
        + ",\"packageFormatVersion\":" + std::to_string(package.package_format_version)
        + ",\"manifestSchemaVersion\":" + std::to_string(package.manifest_schema_version)
        + ",\"triggerCatalogRevision\":"
        + std::to_string(package.trigger_catalog_revision)
        + ",\"manifestChecksum\":" + quote(package.manifest_checksum)
        + ",\"runtimeName\":" + quote(package.runtime_name)
        + ",\"runtimeVersion\":" + quote(package.runtime_version)
        + ",\"generatedTypingsVersion\":" + quote(package.generated_typings_version)
        + ",\"compiledJavaScriptChecksum\":" + quote(package.compiled_javascript_checksum)
        + ",\"compiledJavaScript\":" + quote(package.compiled_javascript)
        + ",\"triggerBindings\":[{\"kind\":"
        + quote(js_scripting_manifest_kind_name(JsScriptingManifestKind::LegacyScriptTrigger))
        + ",\"legacyValue\":" + std::to_string(ON_ENTER)
        + ",\"handlerName\":\"onEnter\"}]}";
}

JsPublishHttpEndpointRequest publish_request(const std::string &operation,
                                             const std::string &body) {
    JsPublishHttpEndpointRequest request;
    request.method = "POST";
    request.path = "/api/js-scripts/" + operation;
    request.content_type = "application/json";
    request.body = body;
    return request;
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

std::string temp_file_path(const std::string &name) {
    return "build/rots-js-live-registry-admin-" + std::to_string(static_cast<long>(getpid())) +
        "-" + name + ".json";
}

} // namespace

static_assert(!std::is_copy_constructible<JsLiveRegistryAdminService>::value,
              "JsLiveRegistryAdminService owns reference-bearing publish state");
static_assert(!std::is_move_constructible<JsLiveRegistryAdminService>::value,
              "JsLiveRegistryAdminService owns reference-bearing publish state");

TEST(JsLiveRegistryAdmin, PublishServicePersistsStateAcrossRouteDispatches) {
    JsLiveRegistryAdminService service;
    const JsScriptPackage package = make_package();

    JsPublishEndpointTransportResult stage = js_publish_http_endpoint_dispatch(
        service.publish_service(),
        publish_request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":" + package_json(package) + "}"),
        make_publish_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));
    ASSERT_TRUE(stage.ok);
    EXPECT_EQ("stage.accepted", stage.reason_code);

    JsPublishStagedPackageStatusResult staged =
        js_publish_latest_staged_package_status(
            service.publish_service().staged_repository(), "js:33:character:8701");
    ASSERT_TRUE(staged.ok);

    JsPublishEndpointTransportResult status = js_publish_http_endpoint_dispatch(
        service.publish_service(),
        publish_request("status", "{\"packageId\":\"js:33:character:8701\"}"),
        make_publish_context(JS_PUBLISH_SCOPE_STATUS_READ));
    EXPECT_TRUE(status.ok);
    EXPECT_EQ("status.current", status.reason_code);
    EXPECT_NE(std::string::npos, status.json.find(staged.status.staged_digest));

    JsPublishEndpointTransportResult activate = js_publish_http_endpoint_dispatch(
        service.publish_service(),
        publish_request("activate",
            "{\"packageId\":\"js:33:character:8701\",\"stagedDigest\":"
                + quote(staged.status.staged_digest)
                + ",\"baseLiveChecksum\":\"live:old\"}"),
        make_publish_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));
    ASSERT_TRUE(activate.ok);
    EXPECT_EQ("activate.accepted", activate.reason_code);

    JsLiveRegistryReloadResult reload = service.refresh();
    EXPECT_TRUE(reload.ok);
    EXPECT_EQ(1u, reload.package_count);
    JsLiveRegistryStatusResult live_status =
        service.status_for_package_id("js:33:character:8701");
    EXPECT_TRUE(live_status.ok);
    ASSERT_EQ(1u, live_status.packages.size());
    EXPECT_EQ(staged.status.staged_digest, live_status.packages[0].staged_digest);
}

TEST(JsLiveRegistryAdmin, PublishServicePersistsStateWithServerReloadOptionsConstructor) {
    JsLiveRegistryAdminService service(js_live_registry_server_reload_options());
    const JsScriptPackage package = make_package();

    JsPublishEndpointTransportResult stage = js_publish_http_endpoint_dispatch(
        service.publish_service(),
        publish_request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":" + package_json(package) + "}"),
        make_publish_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));
    ASSERT_TRUE(stage.ok);

    JsPublishEndpointTransportResult status = js_publish_http_endpoint_dispatch(
        service.publish_service(),
        publish_request("status", "{\"packageId\":\"js:33:character:8701\"}"),
        make_publish_context(JS_PUBLISH_SCOPE_STATUS_READ));

    EXPECT_TRUE(status.ok);
    EXPECT_EQ("status.current", status.reason_code);
    EXPECT_NE(std::string::npos, status.json.find("js:33:character:8701"));
}

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

TEST(JsLiveRegistryAdmin, StartupHydratesLiveStoreFromPersistedFileAndRefreshesRegistry) {
    const std::string source_sentinel = "STARTUP_LOAD_SOURCE_SENTINEL";
    const std::string path = temp_file_path("startup-load");
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
    JsLiveRegistryAdminService writer;
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(
        writer, repository, make_package(8710, "return '" + source_sentinel + "'"));
    ASSERT_TRUE(js_live_package_store_snapshot_save_file(path, writer.live_store().export_snapshot())
                    .ok);

    JsLiveRegistryAdminService reader;
    JsLiveRegistryStartupLoadResult loaded = reader.hydrate_from_file(path);
    JsLiveRegistryAdminCommandResult status =
        js_live_registry_handle_reload_command(reader, "js status 8710");

    ASSERT_TRUE(loaded.ok);
    ASSERT_TRUE(loaded.file_load.ok);
    EXPECT_TRUE(loaded.file_load.snapshot.records.empty());
    EXPECT_TRUE(loaded.file_load.snapshot.live_pointers.empty());
    ASSERT_TRUE(loaded.store_hydration.ok);
    ASSERT_TRUE(loaded.reload.ok);
    ASSERT_TRUE(status.ok) << status.output;
    EXPECT_NE(std::string::npos, status.output.find(record.identity.package_id));
    EXPECT_EQ(std::string::npos, status.output.find(source_sentinel));
    EXPECT_EQ(1u, reader.live_store().package_record_count());
    EXPECT_EQ(1u, reader.reload_service().successful_reload_count());
    std::remove(path.c_str());
}

TEST(JsLiveRegistryAdmin, StartupLoadFailureDoesNotMutateExistingLiveStoreOrRegistry) {
    JsLiveRegistryAdminService service;
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(service, repository, make_package(8711));
    ASSERT_TRUE(service.refresh().ok);
    const std::size_t previous_success_count = service.reload_service().successful_reload_count();

    JsLiveRegistryStartupLoadResult loaded =
        service.hydrate_from_file(temp_file_path("missing-startup"));
    JsLiveRegistryAdminCommandResult status =
        js_live_registry_handle_reload_command(service, "js status 8711");

    EXPECT_FALSE(loaded.ok);
    EXPECT_FALSE(loaded.file_load.ok);
    ASSERT_TRUE(status.ok) << status.output;
    EXPECT_NE(std::string::npos, status.output.find(record.identity.package_id));
    EXPECT_EQ(1u, service.live_store().package_record_count());
    EXPECT_EQ(previous_success_count, service.reload_service().successful_reload_count());
}

TEST(JsLiveRegistryAdmin, StartupRefreshFailureRollsBackHydratedLiveStore) {
    const std::string path = temp_file_path("startup-rollback");
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
    JsLiveRegistryAdminService writer;
    JsStagedPackageRepository writer_repository;
    JsStagedPackageRecord persisted_record =
        activate_package(writer, writer_repository, make_package(8713));
    ASSERT_TRUE(js_live_package_store_snapshot_save_file(path, writer.live_store().export_snapshot())
                    .ok);

    JsLiveRegistryReloadOptions options = js_live_registry_server_reload_options();
    options.replace_options.legacy_script_vnums.push_back(8713);
    JsLiveRegistryAdminService service(options);
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record = activate_package(service, repository, make_package(8712));
    ASSERT_TRUE(service.refresh().ok);

    JsLiveRegistryStartupLoadResult loaded = service.hydrate_from_file(path);
    JsLiveRegistryAdminCommandResult status =
        js_live_registry_handle_reload_command(service, "js status 8712");

    EXPECT_FALSE(loaded.ok);
    EXPECT_TRUE(loaded.file_load.ok);
    EXPECT_TRUE(loaded.file_load.snapshot.records.empty());
    EXPECT_TRUE(loaded.file_load.snapshot.live_pointers.empty());
    EXPECT_TRUE(loaded.store_hydration.ok);
    EXPECT_FALSE(loaded.reload.ok);
    EXPECT_TRUE(loaded.rollback_hydration.ok);
    ASSERT_TRUE(status.ok) << status.output;
    EXPECT_NE(std::string::npos, status.output.find(record.identity.package_id));
    EXPECT_EQ(std::string::npos, status.output.find(persisted_record.identity.package_id));
    EXPECT_EQ(1u, service.live_store().package_record_count());
    EXPECT_TRUE(service.live_store()
                    .find_record(record.identity.package_id, record.identity.package_version_id)
                    .ok);
    EXPECT_FALSE(service.live_store()
                     .find_record(persisted_record.identity.package_id,
                                  persisted_record.identity.package_version_id)
                     .ok);
    EXPECT_TRUE(service.live_store()
                    .find_live_pointer(record.identity.zone, record.identity.host,
                                       record.identity.vnum)
                    .ok);
    EXPECT_FALSE(service.live_store()
                     .find_live_pointer(persisted_record.identity.zone, persisted_record.identity.host,
                                        persisted_record.identity.vnum)
                     .ok);
    EXPECT_EQ(1u, service.reload_service().successful_reload_count());
    std::remove(path.c_str());
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
    EXPECT_NE(std::string::npos, source.find("js_live_registry_startup_load_file"));
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
