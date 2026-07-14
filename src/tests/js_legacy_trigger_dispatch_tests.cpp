#include "../js_legacy_trigger_dispatch.h"

#include "../db.h"
#include "../script.h"
#include "../structs.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

char_data make_character(const char *name) {
    char_data character {};
    character.nr = -1;
    character.in_room = 0;
    character.player.name = const_cast<char *>(name);
    character.player.short_descr = const_cast<char *>(name);
    character.player.race = 1;
    character.player.level = 10;
    character.tmpabilities.hit = 22;
    character.abilities.hit = 33;
    character.specials2.idnum = 1234;
    return character;
}

room_data make_room(const char *name, int number, int zone) {
    room_data room {};
    room.name = const_cast<char *>(name);
    room.number = number;
    room.zone = zone;
    return room;
}

JsGameAdapterOptions make_options(const char_data *const *characters,
                                  std::size_t character_count, room_data *world,
                                  int top_of_world) {
    static const char *races[] = {"God", "Human"};
    JsGameAdapterOptions options;
    options.live_characters = characters;
    options.live_character_count = character_count;
    options.world = world;
    options.world_count = top_of_world >= 0 ? static_cast<std::size_t>(top_of_world + 1) : 0;
    options.top_of_world = top_of_world;
    options.race_names = races;
    options.race_name_count = 2;
    return options;
}

JsTriggerDispatchRequest character_enter_request(const char_data *self) {
    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_ENTER;
    request.context_input.self = self;
    request.context_input.room = 0;
    request.context_input.text = "builder text should stay out of facade diagnostics";
    return request;
}

JsScriptPackage make_package(int vnum, const std::string &source) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "pkg-" + std::to_string(vnum);
    package.host = JsScriptPackageHost::Character;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = source;
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter"});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageStageOptions make_stage_options(const std::string &base_live = "live:old") {
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 30;
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

JsStagedPackageRecord activate_package(JsStagedPackageRepository &repository,
                                       JsLivePackageStore &live_store,
                                       const JsScriptPackage &package) {
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options());
    EXPECT_TRUE(staged.ok);
    JsLivePackagePointer pointer;
    pointer.zone = staged.record.identity.zone;
    pointer.vnum = staged.record.identity.vnum;
    pointer.host = staged.record.identity.host;
    pointer.package_id = staged.record.identity.package_id;
    pointer.package_version_id = staged.record.identity.package_version_id;
    pointer.staged_digest = staged.record.identity.canonical_digest;
    pointer.expected_previous_live_checksum = "live:old";
    pointer.current_live_checksum =
        js_live_package_current_checksum_for_identity(staged.record.identity);
    pointer.loaded_at_epoch_seconds = 200000;
    pointer.load_audit_id = "audit:legacy-dispatch";
    EXPECT_TRUE(live_store.activate_staged_record_pointer(staged.record, pointer).ok);
    return staged.record;
}

JsLiveRegistryReloadService make_refreshed_service(const JsScriptPackage &package) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, package);
    JsLiveRegistryReloadService service;
    EXPECT_TRUE(service.refresh_from_live_store(live_store));
    return service;
}

void refresh_service_with_package(JsLiveRegistryReloadService &service,
                                  const JsScriptPackage &package) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store, package);
    EXPECT_TRUE(service.refresh_from_live_store(live_store));
}

JsLegacyTriggerDispatchOptions enabled_options(
    const JsLiveRegistryReloadService &service, bool require_fresh_reload = true) {
    JsLegacyTriggerDispatchOptions options;
    options.enabled = true;
    options.require_fresh_reload = require_fresh_reload;
    options.expected_reload_generation = js_legacy_trigger_reload_generation(service);
    return options;
}

std::string read_first_available_file(const std::vector<std::string> &paths) {
    for (const std::string &path : paths) {
        std::ifstream file(path);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
    return "";
}

bool contains(const std::string &value, const std::string &needle) {
    return value.find(needle) != std::string::npos;
}

} // namespace

TEST(JsLegacyTriggerDispatch, DefaultsDisabledAndDoesNotExecuteJavaScript) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6101, "function onEnter(ctx) { syntax error if this runs }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options);

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Disabled);
    EXPECT_STREQ("disabled", js_legacy_trigger_dispatch_status_name(result.status));
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(result.dispatch_result.matched_package_count, 0u);
    EXPECT_TRUE(result.dispatch_result.package_id.empty());
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(contains(result.diagnostic, "syntax error"));
}

TEST(JsLegacyTriggerDispatch, EnabledFacadeRequiresLoadedRegistry) {
    JsLiveRegistryReloadService service;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);
    JsLegacyTriggerDispatchOptions options;
    options.enabled = true;
    options.require_fresh_reload = false;

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, options);

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::RegistryNotReady);
    EXPECT_STREQ("registry-not-ready", js_legacy_trigger_dispatch_status_name(result.status));
    EXPECT_TRUE(result.dispatch_result.package_id.empty());
    EXPECT_EQ(result.dispatch_result.matched_package_count, 0u);
}

TEST(JsLegacyTriggerDispatch, EnabledFacadeRequiresFreshReloadGenerationByDefault) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6102, "function onEnter(ctx) { return false; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);
    JsLegacyTriggerDispatchOptions options;
    options.enabled = true;

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, options);

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::StaleRegistry);
    EXPECT_STREQ("stale-registry", js_legacy_trigger_dispatch_status_name(result.status));
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_TRUE(result.dispatch_result.package_id.empty());
    EXPECT_FALSE(contains(result.diagnostic, "function onEnter"));
}

TEST(JsLegacyTriggerDispatch, EnabledFacadeRejectsPreviouslyCapturedReloadGeneration) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6106, "function onEnter(ctx) { return false; }"));
    JsLegacyTriggerReloadGeneration stale_generation =
        js_legacy_trigger_reload_generation(service);
    refresh_service_with_package(
        service, make_package(6107, "function onEnter(ctx) { syntax error if this runs }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);
    JsLegacyTriggerDispatchOptions options;
    options.enabled = true;
    options.expected_reload_generation = stale_generation;

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, options);

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::StaleRegistry);
    EXPECT_TRUE(result.dispatch_result.package_id.empty());
    EXPECT_FALSE(contains(result.diagnostic, "syntax error"));
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeDispatchesAndMapsAllow) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6108, "function onEnter(ctx) { return true; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Allow);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Allow);
    EXPECT_EQ(result.dispatch_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6108);
    EXPECT_EQ(result.dispatch_result.handler_name, "onEnter");
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeDispatchesAndMapsBlock) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6103, "function onEnter(ctx) { return false; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Block);
    EXPECT_STREQ("block", js_legacy_trigger_dispatch_status_name(result.status));
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6103);
    EXPECT_EQ(result.dispatch_result.handler_name, "onEnter");
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshnessOptOutAllowsLoadedRegistryDispatchWithNoToken) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6109, "function onEnter(ctx) { return true; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);
    JsLegacyTriggerDispatchOptions options;
    options.enabled = true;
    options.require_fresh_reload = false;

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, options);

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Allow);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Allow);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6109);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadePropagatesNoMatchWithoutMetadata) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6104, "function onEnter(ctx) { return true; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);
    JsTriggerDispatchRequest request = character_enter_request(&self);
    request.legacy_value = ON_DAMAGE;

    JsLegacyTriggerDispatchResult result =
        js_legacy_trigger_dispatch(service, request, adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(result.dispatch_result.matched_package_count, 0u);
    EXPECT_TRUE(result.dispatch_result.package_id.empty());
    EXPECT_TRUE(result.dispatch_result.handler_name.empty());
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, RuntimeErrorsFailClosedAndKeepDiagnosticsRedacted) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6105, "function onEnter(ctx) { throw ctx.text; }"));
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Error);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6105);
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(contains(result.diagnostic, "builder text"));
    EXPECT_FALSE(contains(result.diagnostic, "function onEnter"));
    EXPECT_FALSE(contains(result.diagnostic, "\n"));
    EXPECT_FALSE(contains(result.dispatch_result.diagnostic, "builder text"));
    EXPECT_FALSE(contains(result.dispatch_result.diagnostic, "function onEnter"));
    EXPECT_FALSE(contains(result.dispatch_result.diagnostic, "\n"));
    EXPECT_LE(result.diagnostic.size(), 220u);
}

TEST(JsLegacyTriggerDispatch, StatusNamesAreStable) {
    EXPECT_STREQ("disabled",
                 js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus::Disabled));
    EXPECT_STREQ("registry-not-ready",
                 js_legacy_trigger_dispatch_status_name(
                     JsLegacyTriggerDispatchStatus::RegistryNotReady));
    EXPECT_STREQ("stale-registry",
                 js_legacy_trigger_dispatch_status_name(
                     JsLegacyTriggerDispatchStatus::StaleRegistry));
    EXPECT_STREQ("no-match",
                 js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus::NoMatch));
    EXPECT_STREQ("allow",
                 js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus::Allow));
    EXPECT_STREQ("block",
                 js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus::Block));
    EXPECT_STREQ("error",
                 js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus::Error));
}

TEST(JsLegacyTriggerDispatch, BuildFilesReferenceFacadeSourcesAndTests) {
    const std::string cmake =
        read_first_available_file({"src/CMakeLists.txt", "../src/CMakeLists.txt"});
    const std::string server_makefile =
        read_first_available_file({"src/Makefile", "../src/Makefile"});
    const std::string test_makefile =
        read_first_available_file({"src/tests/Makefile", "../src/tests/Makefile"});

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(server_makefile.empty());
    ASSERT_FALSE(test_makefile.empty());
    EXPECT_TRUE(contains(cmake, "js_legacy_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(cmake, "tests/js_legacy_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.o"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.h"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.h"));
    EXPECT_TRUE(contains(server_makefile, "js_live_registry_reload_service.h"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch.o"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch.h"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch.h"));
    EXPECT_TRUE(contains(test_makefile, "js_live_registry_reload_service.h"));
}

TEST(JsLegacyTriggerDispatch, LegacyGameplayCallSitesRemainUnwired) {
    const std::string script = read_first_available_file({"src/script.cpp", "../script.cpp"});
    const std::string act_move = read_first_available_file({"src/act_move.cpp", "../act_move.cpp"});
    const std::string act_obj1 = read_first_available_file({"src/act_obj1.cpp", "../act_obj1.cpp"});
    const std::string act_obj2 = read_first_available_file({"src/act_obj2.cpp", "../act_obj2.cpp"});
    const std::string act_info = read_first_available_file({"src/act_info.cpp", "../act_info.cpp"});

    ASSERT_FALSE(script.empty());
    EXPECT_FALSE(contains(script, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_move, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_obj1, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_obj2, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_info, "js_legacy_trigger_dispatch("));
}
