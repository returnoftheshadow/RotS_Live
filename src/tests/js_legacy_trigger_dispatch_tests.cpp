#include "../js_legacy_trigger_dispatch.h"

#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../json_utils.h"
#include "../js_live_registry_admin.h"
#include "../js_publish_endpoint_transport.h"
#include "../mudlle.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int trigger_char_enter(char_data *ch, char_data *vict, room_data *room);
int trigger_before_char_enter(char_data *ch, char_data *vict, room_data *room);
int trigger_char_receive(char_data *ch1, char_data *ch2, obj_data *ob1);
int trigger_char_die(char_data *ch);
int trigger_char_damage(char_data *vict, char_data *ch);
int trigger_object_damage(obj_data *obj, char_data *vict, char_data *ch);
int trigger_object_event(int trigger_type, obj_data *obj, char_data *ch);
int trigger_room_event(int trigger_type, room_data *room, char_data *ch);
int trigger_char_hear(char_data *ch, char_data *speaking, char *text);
void perform_wear(char_data *character, obj_data *item, int item_slot, bool wearall = false);
void one_mobile_activity(char_data *ch);
extern room_data world;
extern int top_of_world;
extern int pulse;
extern char **mobile_program;
extern script_head *script_table;
extern int top_of_script_table;
extern char_data *character_list;
extern obj_data *object_list;

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

obj_data make_object(const char *name) {
    obj_data object {};
    object.item_number = -1;
    object.in_room = 0;
    object.name = const_cast<char *>(name);
    object.short_description = const_cast<char *>(name);
    return object;
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

JsTriggerDispatchRequest character_hear_request(int trigger_type, const char_data *listener,
                                                const char_data *speaker, const char *text) {
    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = trigger_type;
    request.context_input.self = listener;
    request.context_input.actor = speaker;
    request.context_input.room = listener != nullptr ? listener->in_room : -1;
    request.context_input.text = text;
    return request;
}

const char *handler_name_for_legacy_trigger(int legacy_value) {
    switch (legacy_value) {
    case ON_BEFORE_ENTER:
        return "onBeforeEnter";
    case ON_DIE:
        return "onDie";
    case ON_DAMAGE:
        return "onDamage";
    case ON_RECEIVE:
        return "onReceive";
    case ON_HEAR_SAY:
        return "onHearSay";
    case ON_HEAR_YELL:
        return "onHearYell";
    case ON_EXAMINE_OBJECT:
        return "onExamineObject";
    case ON_EAT:
        return "onEat";
    case ON_DRINK:
        return "onDrink";
    case ON_WEAR:
        return "onWear";
    case ON_PULL:
        return "onPull";
    default:
        return "onEnter";
    }
}

const char *handler_name_for_mudlle_call_flag(int call_flag) {
    switch (call_flag) {
    case SPECIAL_ENTER:
        return "onSpecialEnter";
    case SPECIAL_SELF:
        return "onSpecialSelf";
    case SPECIAL_TARGET:
        return "onSpecialTarget";
    case SPECIAL_DAMAGE:
        return "onSpecialDamage";
    case SPECIAL_DEATH:
        return "onSpecialDeath";
    default:
        return "onSpecialCommand";
    }
}

JsScriptPackage make_package(int vnum, const std::string &source,
                             int legacy_value = ON_ENTER,
                             JsScriptPackageHost host = JsScriptPackageHost::Character) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "pkg-" + std::to_string(vnum);
    package.host = host;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = source;
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, legacy_value,
         handler_name_for_legacy_trigger(legacy_value)});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsScriptPackage make_mudlle_package(int vnum, const std::string &source,
                                    int call_flag = SPECIAL_COMMAND) {
    JsScriptPackage package =
        make_package(vnum, source, ON_ENTER, JsScriptPackageHost::MudlleMobile);
    package.trigger_bindings.clear();
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::MudlleCallFlag, call_flag,
         handler_name_for_mudlle_call_flag(call_flag)});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

void attach_mudlle_program(char_data &host, int program_vnum, int call_mask, int *call_list) {
    SET_BIT(MOB_FLAGS(&host), MOB_ISNPC);
    host.specials.tactics = 0;
    host.specials.union1.prog_number = call_list;
    for (int index = 0; index < SPECIAL_CALLLIST; ++index)
        call_list[index] = 0;
    call_list[0] = program_vnum;
    CALL_MASK(&host) = call_mask;
}

JsScriptPackage make_package_with_triggers(int vnum, const std::string &source,
                                           const std::vector<int> &legacy_values,
                                           JsScriptPackageHost host) {
    EXPECT_FALSE(legacy_values.empty());
    JsScriptPackage package = make_package(vnum, source, legacy_values.front(), host);
    package.trigger_bindings.clear();
    for (int legacy_value : legacy_values) {
        package.trigger_bindings.push_back(
            {JsScriptingManifestKind::LegacyScriptTrigger, legacy_value,
             handler_name_for_legacy_trigger(legacy_value)});
    }
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

std::string quote_json(const std::string &value) {
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

std::string package_json(const JsScriptPackage &package) {
    return "{"
        "\"vnum\":" + std::to_string(package.vnum)
        + ",\"packageId\":" + quote_json(package.package_id)
        + ",\"host\":" + quote_json(js_script_package_host_name(package.host))
        + ",\"packageFormatVersion\":" + std::to_string(package.package_format_version)
        + ",\"manifestSchemaVersion\":" + std::to_string(package.manifest_schema_version)
        + ",\"triggerCatalogRevision\":"
        + std::to_string(package.trigger_catalog_revision)
        + ",\"manifestChecksum\":" + quote_json(package.manifest_checksum)
        + ",\"runtimeName\":" + quote_json(package.runtime_name)
        + ",\"runtimeVersion\":" + quote_json(package.runtime_version)
        + ",\"generatedTypingsVersion\":" + quote_json(package.generated_typings_version)
        + ",\"compiledJavaScriptChecksum\":" + quote_json(package.compiled_javascript_checksum)
        + ",\"compiledJavaScript\":" + quote_json(package.compiled_javascript)
        + ",\"triggerBindings\":[{\"kind\":"
        + quote_json(js_scripting_manifest_kind_name(JsScriptingManifestKind::LegacyScriptTrigger))
        + ",\"legacyValue\":" + std::to_string(package.trigger_bindings.front().legacy_value)
        + ",\"handlerName\":" + quote_json(package.trigger_bindings.front().handler_name)
        + "}]}";
}

std::string stage_json(const JsScriptPackage &package,
                       const std::string &base_live_checksum = "live:old") {
    return "{\"operation\":\"stage\",\"baseLiveChecksum\":"
        + quote_json(base_live_checksum) + ",\"package\":" + package_json(package) + "}";
}

std::string activate_json(const std::string &package_id, const std::string &staged_digest,
                          const std::string &base_live_checksum = "live:old") {
    return "{\"operation\":\"activate\",\"packageId\":" + quote_json(package_id)
        + ",\"stagedDigest\":" + quote_json(staged_digest)
        + ",\"baseLiveChecksum\":" + quote_json(base_live_checksum) + "}";
}

JsPublishTokenMetadata publish_token(unsigned scopes) {
    JsPublishTokenMetadata token;
    token.claims_verified = true;
    token.token_id = "token:runtime-execution";
    token.actor_id = "actor:42";
    token.builder_account_id = "account:builder";
    token.server_audience = "server:main";
    token.workspace_id = "workspace:main";
    token.scopes = scopes;
    token.issued_at_epoch_seconds = 90;
    token.expires_at_epoch_seconds = 200;
    return token;
}

JsPublishTransportMetadata publish_transport() {
    JsPublishTransportMetadata transport;
    transport.secure_channel = true;
    transport.server_identity_verified = true;
    transport.server_audience = "server:main";
    transport.source_identifier = "transport:runtime-execution-test";
    return transport;
}

JsPublishEndpointTransportContext publish_context(unsigned scopes,
                                                  const std::string &audit_path) {
    JsPublishEndpointTransportContext context;
    context.request_id = "request:runtime-execution";
    context.audit_id = "audit:runtime-execution";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    context.zone = 30;
    context.builder_eligibility.ok = true;
    context.builder_eligibility.builder_account_id = "account:builder";
    context.builder_eligibility.eligible_character_name = "Builder";
    context.builder_eligibility.eligible_character_id = 1001;
    context.builder_eligibility.eligible_character_level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    context.target_zone_resolved = true;
    context.server_resolved_target_zone = 30;
    context.server_resolved_target_host = JsScriptPackageHost::Character;
    context.zone_exists = true;
    context.zone_owner_character_ids = {1001};
    context.token = publish_token(scopes);
    context.transport = publish_transport();
    context.now_epoch_seconds = 100;
    context.allow_mutating_operations = true;
    context.allow_live_pointer_update = true;
    context.applied_at_epoch_seconds = 200;
    context.expected_server_audience = "server:main";
    context.expected_workspace_id = "workspace:main";
    context.expected_server_instance_id = "server:main";
    context.current_live_checksum = "live:old";
    context.publish_audit_log_path = audit_path;
    return context;
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
                                       const JsScriptPackage &package,
                                       const std::string &base_live = "live:old") {
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options(base_live));
    EXPECT_TRUE(staged.ok);
    JsLivePackagePointer pointer;
    pointer.zone = staged.record.identity.zone;
    pointer.vnum = staged.record.identity.vnum;
    pointer.host = staged.record.identity.host;
    pointer.package_id = staged.record.identity.package_id;
    pointer.package_version_id = staged.record.identity.package_version_id;
    pointer.staged_digest = staged.record.identity.canonical_digest;
    pointer.expected_previous_live_checksum = base_live;
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

bool appears_before(const std::string &value, const std::string &first, const std::string &second) {
    const std::size_t first_position = value.find(first);
    const std::size_t second_position = value.find(second);
    return first_position != std::string::npos && second_position != std::string::npos &&
        first_position < second_position;
}

bool appears_before_after(
    const std::string &value, const std::string &anchor, const std::string &first,
    const std::string &second) {
    const std::size_t anchor_position = value.find(anchor);
    if (anchor_position == std::string::npos)
        return false;
    const std::size_t first_position = value.find(first, anchor_position);
    const std::size_t second_position = value.find(second, anchor_position);
    return first_position != std::string::npos && second_position != std::string::npos &&
        first_position < second_position;
}

std::size_t count_occurrences(const std::string &value, const std::string &needle) {
    if (needle.empty())
        return 0;

    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

struct GlobalWorldFixtureGuard {
    room_data saved_world = world;
    int saved_top_of_world = top_of_world;
    char_data *saved_character_list = character_list;
    obj_data *saved_object_list = object_list;

    ~GlobalWorldFixtureGuard() {
        world = saved_world;
        top_of_world = saved_top_of_world;
        character_list = saved_character_list;
        object_list = saved_object_list;
        js_script_set_legacy_trigger_dispatch_enabled(false);
    }
};

struct GlobalLiveRegistryGuard {
    JsLiveRegistryAdminService &service = js_live_registry_admin_service();

    GlobalLiveRegistryGuard() {
        EXPECT_TRUE(service.live_store().hydrate_from_snapshot({}).ok);
        EXPECT_TRUE(service.refresh().ok);
        js_script_set_legacy_trigger_dispatch_enabled(false);
    }

    ~GlobalLiveRegistryGuard() {
        service.live_store().hydrate_from_snapshot({});
        service.refresh();
        js_script_set_legacy_trigger_dispatch_enabled(false);
    }
};

struct GlobalScriptTableGuard {
    script_head *saved_script_table = script_table;
    int saved_top_of_script_table = top_of_script_table;

    ~GlobalScriptTableGuard() {
        script_table = saved_script_table;
        top_of_script_table = saved_top_of_script_table;
    }
};

struct GlobalPulseGuard {
    int saved_pulse = pulse;

    ~GlobalPulseGuard() { pulse = saved_pulse; }
};

struct GlobalMobileProgramGuard {
    char **saved_mobile_program = mobile_program;

    ~GlobalMobileProgramGuard() { mobile_program = saved_mobile_program; }
};

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

TEST(JsLegacyTriggerDispatch, ScriptFacadeToggleDefaultsClosedAndCanBeChanged) {
    js_script_set_legacy_trigger_dispatch_enabled(false);
    EXPECT_FALSE(js_script_legacy_trigger_dispatch_enabled());

    js_script_set_legacy_trigger_dispatch_enabled(true);
    EXPECT_TRUE(js_script_legacy_trigger_dispatch_enabled());

    js_script_set_legacy_trigger_dispatch_enabled(false);
    EXPECT_FALSE(js_script_legacy_trigger_dispatch_enabled());
}

TEST(JsLegacyTriggerDispatch, DisabledScriptOnEnterPathAllowsLegacyFlowWithoutRegistry) {
    js_script_set_legacy_trigger_dispatch_enabled(false);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    room_data room = make_room("Room", 100, 0);

    EXPECT_EQ(trigger_char_enter(&self, &actor, &room), 1);
    EXPECT_FALSE(js_script_legacy_trigger_dispatch_enabled());
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnEnterPathExecutesLiveJavaScriptPackage) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;

    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6110, "function onEnter(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &world), 0);

    js_script_set_legacy_trigger_dispatch_enabled(false);
    ASSERT_TRUE(service.live_store().hydrate_from_snapshot({}).ok);
    ASSERT_TRUE(service.refresh().ok);
}

TEST(JsLegacyTriggerDispatch,
     TransportPublishedJavaScriptExecutesThroughCharacterEnterCallSiteAfterActivation) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    const std::string audit_path = "build/js-legacy-trigger-runtime-publish-audit.jsonl";
    std::remove(audit_path.c_str());
    JsScriptPackage package = make_package(6180,
        "function onEnter(ctx) { "
        "return !(ctx.self.name === 'Watcher' && ctx.actor.name === 'Actor' && "
        "ctx.room.vnum === 100); "
        "}");

    char_data watcher = make_character("Watcher");
    char_data actor = make_character("Actor");
    char_data other_actor = make_character("OtherActor");
    watcher.next = &actor;
    actor.next = &other_actor;
    other_actor.next = nullptr;
    character_list = &watcher;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    // This uses server-owned transport context directly. Full HTTP/session publish-to-trigger
    // coverage remains a later end-to-end slice.
    JsPublishEndpointTransportResult staged = js_publish_endpoint_dispatch_json(
        service.publish_service(), stage_json(package),
        publish_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE, audit_path));
    ASSERT_TRUE(staged.ok) << staged.json;
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(
            service.publish_service().staged_repository(), "js:30:character:6180");
    ASSERT_TRUE(status.ok);
    ASSERT_TRUE(service.refresh().ok);
    EXPECT_EQ(nullptr, service.reload_service().find_package_status_by_vnum(6180));
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);
    EXPECT_EQ(trigger_char_enter(&watcher, &actor, &world), 1);

    JsPublishEndpointTransportResult activated = js_publish_endpoint_dispatch_json(
        service.publish_service(),
        activate_json(status.status.package_id, status.status.staged_digest),
        publish_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE, audit_path));
    ASSERT_TRUE(activated.ok) << activated.json;
    EXPECT_EQ("activate.accepted", activated.reason_code);
    EXPECT_EQ(1u, service.live_store().package_record_count());
    EXPECT_EQ(1u, service.live_store().live_pointer_count());
    JsLivePackagePointerResult pointer =
        service.live_store().find_live_pointer(status.status.package_id);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(status.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_EQ(status.status.staged_digest, pointer.pointer.staged_digest);
    const std::string audit = read_first_available_file({audit_path});
    EXPECT_NE(std::string::npos, audit.find("\"operation\":\"activate\""));
    EXPECT_NE(std::string::npos, audit.find("\"packageId\":\"js:30:character:6180\""));

    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());

    EXPECT_EQ(trigger_char_enter(&watcher, &actor, &world), 0);
    EXPECT_EQ(trigger_char_enter(&watcher, &other_actor, &world), 1);
}

TEST(JsLegacyTriggerDispatch, LiveServerFacadeExecutesActivatedPackagesAcrossGameplayTriggers) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;

    activate_package(repository, service.live_store(),
                     make_package_with_triggers(6170,
                         "function onBeforeEnter(ctx) { "
                         "return !(ctx.self.name === 'Guard' && ctx.actor.name === 'Actor' && "
                         "ctx.room.vnum === 100); "
                         "}\n"
                         "function onDie(ctx) { return !(ctx.self.name === 'Victim' && "
                         "ctx.killer.name === 'Attacker'); }\n"
                         "function onDamage(ctx) { "
                         "return !(ctx.self.name === 'Victim' && ctx.actor.name === 'Attacker' && "
                         "ctx.attacker.name === 'Attacker' && ctx.victim.name === 'Victim' && "
                         "ctx.weapon.name === 'Blade'); "
                         "}\n"
                         "function onReceive(ctx) { "
                         "return !(ctx.self.name === 'Receiver' && ctx.actor.name === 'Giver' && "
                         "ctx.object.name === 'Token'); "
                         "}",
                         {ON_BEFORE_ENTER, ON_DIE, ON_DAMAGE, ON_RECEIVE},
                         JsScriptPackageHost::Character));
    activate_package(repository, service.live_store(),
                     make_package_with_triggers(6171,
                         "function objectNamed(ctx, name, actorName) { "
                         "return ctx.object && ctx.object.name === name && ctx.actor && "
                         "ctx.actor.name === actorName; "
                         "}\n"
                         "function onEnter(ctx) { return !objectNamed(ctx, 'Totem', 'Actor'); }\n"
                         "function onExamineObject(ctx) { return !objectNamed(ctx, 'Scroll', 'Actor'); }\n"
                         "function onEat(ctx) { return !objectNamed(ctx, 'Ration', 'Actor'); }\n"
                         "function onDrink(ctx) { return !objectNamed(ctx, 'Flask', 'Actor'); }\n"
                         "function onWear(ctx) { return !objectNamed(ctx, 'Coat', 'Actor'); }\n"
                         "function onPull(ctx) { return !objectNamed(ctx, 'Lever', 'Actor'); }\n"
                         "function onDamage(ctx) { "
                         "return !(objectNamed(ctx, 'Blade', 'Attacker') && "
                         "ctx.attacker.name === 'Attacker' && "
                         "ctx.victim.name === 'VictimForObject' && ctx.weapon.name === 'Blade'); "
                         "}",
                         {ON_ENTER, ON_EXAMINE_OBJECT, ON_EAT, ON_DRINK, ON_WEAR, ON_PULL,
                             ON_DAMAGE},
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data guard_character = make_character("Guard");
    char_data actor = make_character("Actor");
    char_data victim = make_character("Victim");
    char_data victim_for_object = make_character("VictimForObject");
    char_data attacker = make_character("Attacker");
    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    guard_character.next = &actor;
    actor.next = &victim;
    victim.next = &victim_for_object;
    victim_for_object.next = &attacker;
    attacker.next = &receiver;
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &guard_character;

    obj_data blade = make_object("Blade");
    obj_data token = make_object("Token");
    obj_data scroll = make_object("Scroll");
    obj_data ration = make_object("Ration");
    obj_data flask = make_object("Flask");
    obj_data coat = make_object("Coat");
    obj_data lever = make_object("Lever");
    obj_data totem = make_object("Totem");
    blade.next = &token;
    token.next = &scroll;
    scroll.next = &ration;
    ration.next = &flask;
    flask.next = &coat;
    coat.next = &lever;
    lever.next = &totem;
    totem.next = nullptr;
    object_list = &blade;
    token.carried_by = &receiver;
    attacker.equipment[WIELD] = &blade;

    world = make_room("Room", 100, -1);
    top_of_world = 0;

    world.people = &guard_character;
    guard_character.next_in_room = nullptr;
    EXPECT_EQ(call_trigger(ON_BEFORE_ENTER, &world, &actor, nullptr), 0);

    EXPECT_EQ(call_trigger(ON_DIE, &victim, &attacker, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_DAMAGE, &victim, &attacker, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_DAMAGE, &victim_for_object, &attacker, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_RECEIVE, &receiver, &giver, &token), 0);

    EXPECT_EQ(call_trigger(ON_EXAMINE_OBJECT, &scroll, &actor, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_EAT, &ration, &actor, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_DRINK, &flask, &actor, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_WEAR, &coat, &actor, nullptr), 0);
    EXPECT_EQ(call_trigger(ON_PULL, &lever, &actor, nullptr), 0);

    world.people = &actor;
    actor.next_in_room = nullptr;
    world.contents = &totem;
    totem.next_content = nullptr;
    EXPECT_EQ(call_trigger(ON_ENTER, &world, &actor, nullptr), 0);
}

TEST(JsLegacyTriggerDispatch, LegacyOnEnterBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6115, "function onEnter(ctx) { return true; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_enter {};
    script_data return_false {};
    on_enter.command_type = ON_ENTER;
    on_enter.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_enter;
    script_head script {};
    script.number = 9011;
    script.script = &on_enter;
    script_table = &script;
    top_of_script_table = 0;

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.specials.script_number = 9011;
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &world), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptBeforeEnterPathBlocksMovement) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6116, "function onBeforeEnter(ctx) { return false; }",
                         ON_BEFORE_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_before_char_enter(&self, &actor, &world), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptBeforeEnterRuntimeErrorBlocksMovement) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6118, "function onBeforeEnter(ctx) { throw 'block'; }",
                         ON_BEFORE_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_before_char_enter(&self, &actor, &world), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptBeforeEnterSkipsWhenObserverLeftTargetRoom) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6119, "function onBeforeEnter(ctx) { return false; }",
                         ON_BEFORE_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.in_room = -1;
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_before_char_enter(&self, &actor, &world), 1);
}

TEST(JsLegacyTriggerDispatch, LegacyBeforeEnterBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6117, "function onBeforeEnter(ctx) { return true; }",
                         ON_BEFORE_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data before_enter {};
    script_data return_false {};
    before_enter.command_type = ON_BEFORE_ENTER;
    before_enter.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &before_enter;
    script_head script {};
    script.number = 9012;
    script.script = &before_enter;
    script_table = &script;
    top_of_script_table = 0;

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.specials.script_number = 9012;
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_before_char_enter(&self, &actor, &world), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDiePathBlocksDeath) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6120, "function onDie(ctx) { return false; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    self.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_die(&self), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDieRuntimeErrorBlocksDeath) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6121, "function onDie(ctx) { throw 'block'; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    self.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_die(&self), 0);
}

TEST(JsLegacyTriggerDispatch, CallTriggerOnDieProvidesKillerToJavaScript) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6183,
                         "function onDie(ctx) { "
                         "return !(ctx.self.name === 'Victim' && ctx.killer.name === 'Killer'); "
                         "}",
                         ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data killer = make_character("Killer");
    victim.next = &killer;
    killer.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &victim, &killer, nullptr), 0);
}

TEST(JsLegacyTriggerDispatch, DirectOnDieModelsKillerAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6184,
                         "function onDie(ctx) { return ctx.self.name === 'Victim' && "
                         "ctx.killer === null; }",
                         ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    victim.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_die(&victim), 1);
}

TEST(JsLegacyTriggerDispatch, CallTriggerOnDieModelsNullKillerAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6186,
                         "function onDie(ctx) { return ctx.self.name === 'Victim' && "
                         "ctx.killer === null; }",
                         ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    victim.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &victim, nullptr, nullptr), 1);
}

TEST(JsLegacyTriggerDispatch, CallTriggerOnDieModelsStaleKillerAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6185,
                         "function onDie(ctx) { return ctx.self.name === 'Victim' && "
                         "ctx.killer === null; }",
                         ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data stale_killer = make_character("StaleKiller");
    victim.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &victim, &stale_killer, nullptr), 1);
}

TEST(JsLegacyTriggerDispatch, CallTriggerOnDieKeepsSelfKillRoleAliases) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6187,
                         "function onDie(ctx) { return ctx.self.name === ctx.killer.name && "
                         "ctx.self.id === 'self' && ctx.killer.id === 'killer'; }",
                         ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    victim.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &victim, &victim, nullptr), 1);
}

TEST(JsLegacyTriggerDispatch, CallTriggerOnDieSkipsJavaScriptForStaleVictimWithLiveKiller) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6188, "function onDie(ctx) { return false; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data stale_victim = make_character("StaleVictim");
    char_data killer = make_character("Killer");
    killer.next = nullptr;
    character_list = &killer;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &stale_victim, &killer, nullptr), 1);
}

TEST(JsLegacyTriggerDispatch, LegacyOnDieBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6122, "function onDie(ctx) { return true; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_die {};
    script_data return_false {};
    on_die.command_type = ON_DIE;
    on_die.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_die;
    script_head script {};
    script.number = 9014;
    script.script = &on_die;
    script_table = &script;
    top_of_script_table = 0;

    char_data self = make_character("Self");
    self.specials.script_number = 9014;
    self.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_die(&self), 0);
}

TEST(JsLegacyTriggerDispatch, LegacyOnDieBlockWithKillerPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6189, "function onDie(ctx) { return true; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_die {};
    script_data return_false {};
    on_die.command_type = ON_DIE;
    on_die.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_die;
    script_head script {};
    script.number = 9015;
    script.script = &on_die;
    script_table = &script;
    top_of_script_table = 0;

    char_data victim = make_character("Victim");
    char_data killer = make_character("Killer");
    victim.specials.script_number = 9015;
    victim.next = &killer;
    killer.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_DIE, &victim, &killer, nullptr), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDiePathSkipsWhenCharacterIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6123, "function onDie(ctx) { return false; }", ON_DIE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    character_list = nullptr;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_die(&self), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDamagePathBlocksDamage) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6124,
                         "function onDamage(ctx) { "
                         "return !(ctx.self.name === 'Victim' && ctx.actor.name === 'Attacker' && "
                         "ctx.attacker.name === 'Attacker' && ctx.victim.name === 'Victim' && "
                         "ctx.weapon.name === 'Blade'); "
                         "}",
                         ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    obj_data weapon = make_object("Blade");
    weapon.next = nullptr;
    object_list = &weapon;
    attacker.equipment[WIELD] = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, CharacterOnDamageModelsUnarmedWeaponAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6177,
                         "function onDamage(ctx) { "
                         "return ctx.weapon === null && ctx.attacker.name === 'Attacker'; "
                         "}",
                         ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, CharacterOnDamageModelsStaleEquippedWeaponAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6178,
                         "function onDamage(ctx) { "
                         "return ctx.weapon === null && ctx.attacker.name === 'Attacker'; "
                         "}",
                         ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    obj_data stale_weapon = make_object("Stale Blade");
    attacker.equipment[WIELD] = &stale_weapon;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDamageRuntimeErrorBlocksDamage) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6125, "function onDamage(ctx) { throw 'block'; }", ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, LegacyOnDamageBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6126, "function onDamage(ctx) { return true; }", ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_damage {};
    script_data return_false {};
    on_damage.command_type = ON_DAMAGE;
    on_damage.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_damage;
    script_head script {};
    script.number = 9018;
    script.script = &on_damage;
    script_table = &script;
    top_of_script_table = 0;

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.specials.script_number = 9018;
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDamagePathSkipsWhenVictimIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6127, "function onDamage(ctx) { return false; }", ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    attacker.next = nullptr;
    character_list = &attacker;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnDamagePathSkipsWhenAttackerIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6128, "function onDamage(ctx) { return false; }", ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_damage(&victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, CharacterOnDamageBlockShortCircuitsWeaponObjectDamageFromCallTrigger) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6129, "function onDamage(ctx) { return false; }", ON_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    obj_data weapon {};
    weapon.obj_flags.script_number = 9019;
    attacker.equipment[WIELD] = &weapon;

    script_data on_damage {};
    script_data return_false {};
    on_damage.command_type = ON_DAMAGE;
    on_damage.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_damage;
    script_head script {};
    script.number = 9019;
    script.script = &on_damage;
    script_table = &script;
    top_of_script_table = 0;

    EXPECT_EQ(call_trigger(ON_DAMAGE, &victim, &attacker, nullptr), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnDamagePathBlocksDamage) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6130,
                         "function onDamage(ctx) { "
                         "return !(ctx.object.name === 'Blade' && ctx.weapon.name === ctx.object.name && "
                         "ctx.object.id === 'object' && ctx.weapon.id === 'weapon' && "
                         "ctx.actor.name === 'Attacker' && ctx.self === null); "
                         "}",
                         ON_DAMAGE, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;

    obj_data weapon = make_object("Blade");
    weapon.next = nullptr;
    object_list = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnDamageRuntimeErrorBlocksDamage) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6131, "function onDamage(ctx) { throw 'block'; }", ON_DAMAGE,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;

    obj_data weapon = make_object("Blade");
    weapon.next = nullptr;
    object_list = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, LegacyObjectOnDamageBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6132, "function onDamage(ctx) { return true; }", ON_DAMAGE,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_damage {};
    script_data return_false {};
    on_damage.command_type = ON_DAMAGE;
    on_damage.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_damage;
    script_head script {};
    script.number = 9020;
    script.script = &on_damage;
    script_table = &script;
    top_of_script_table = 0;

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;

    obj_data weapon = make_object("Blade");
    weapon.obj_flags.script_number = 9020;
    weapon.next = nullptr;
    object_list = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnDamagePathSkipsWhenObjectIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6133, "function onDamage(ctx) { return false; }", ON_DAMAGE,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = &attacker;
    attacker.next = nullptr;
    character_list = &victim;

    obj_data weapon = make_object("Blade");
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnDamagePathSkipsWhenVictimIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6134, "function onDamage(ctx) { return false; }", ON_DAMAGE,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    attacker.next = nullptr;
    character_list = &attacker;

    obj_data weapon = make_object("Blade");
    weapon.next = nullptr;
    object_list = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnDamagePathSkipsWhenAttackerIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6135, "function onDamage(ctx) { return false; }", ON_DAMAGE,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    victim.next = nullptr;
    character_list = &victim;

    obj_data weapon = make_object("Blade");
    weapon.next = nullptr;
    object_list = &weapon;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_damage(&weapon, &victim, &attacker), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectEventPathsBlockForEachTrigger) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;

    struct EventCase {
        int trigger_type;
        int vnum;
        const char *handler_name;
    };
    const EventCase cases[] = {
        {ON_ENTER, 6140, "onEnter"},
        {ON_EXAMINE_OBJECT, 6141, "onExamineObject"},
        {ON_EAT, 6142, "onEat"},
        {ON_DRINK, 6143, "onDrink"},
        {ON_WEAR, 6144, "onWear"},
        {ON_PULL, 6145, "onPull"},
    };

    for (const EventCase &event_case : cases) {
        activate_package(repository, service.live_store(),
                         make_package(event_case.vnum,
                             std::string("function ") + event_case.handler_name +
                                 "(ctx) { return !(ctx.object.name === 'Relic' && "
                                 "ctx.actor.name === 'Actor' && ctx.self === null); }",
                             event_case.trigger_type, JsScriptPackageHost::Object));
    }
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    for (const EventCase &event_case : cases)
        EXPECT_EQ(trigger_object_event(event_case.trigger_type, &object, &actor), 0)
            << "trigger_type=" << event_case.trigger_type;
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectOnEnterRunsThroughRoomEventPath) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6150,
                         "function onEnter(ctx) { "
                         "return !(ctx.object.name === 'Relic' && ctx.actor.name === 'Actor' && "
                         "ctx.room.name === 'Room' && ctx.self === null); "
                         "}",
                         ON_ENTER, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.in_room = 0;
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object.next_content = nullptr;
    object_list = &object;

    world = make_room("Room", 100, -1);
    world.contents = &object;
    world.people = nullptr;
    top_of_world = 0;

    EXPECT_EQ(trigger_room_event(ON_ENTER, &world, &actor), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectEventRuntimeErrorBlocksAction) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6146, "function onWear(ctx) { throw 'block'; }", ON_WEAR,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_event(ON_WEAR, &object, &actor), 0);
}

TEST(JsLegacyTriggerDispatch, PerformWearProvidesRequestedWearSlotToJavaScript) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6179,
                         "function onWear(ctx) { "
                         "return !(ctx.object.name === 'Helm' && ctx.actor.name === 'Actor' && "
                         "ctx.wearSlot === 'head'); "
                         "}",
                         ON_WEAR, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Helm");
    object.obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_HEAD;
    object.carried_by = &actor;
    object.next_content = nullptr;
    actor.carrying = &object;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    perform_wear(&actor, &object, WEAR_HEAD);

    EXPECT_EQ(actor.equipment[WEAR_HEAD], nullptr);
    EXPECT_EQ(object.carried_by, &actor);
}

TEST(JsLegacyTriggerDispatch, PerformWearReportsRequestedSlotBeforeAlternateFallback) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6180,
                         "function onWear(ctx) { "
                         "return !(ctx.object.name === 'Ring' && ctx.wearSlot === 'fingerRight'); "
                         "}",
                         ON_WEAR, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data worn_ring = make_object("Existing Ring");
    actor.equipment[WEAR_FINGER_R] = &worn_ring;

    obj_data object = make_object("Ring");
    object.obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_FINGER;
    object.carried_by = &actor;
    object.next_content = nullptr;
    actor.carrying = &object;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    perform_wear(&actor, &object, WEAR_FINGER_R);

    EXPECT_EQ(actor.equipment[WEAR_FINGER_L], nullptr);
    EXPECT_EQ(object.carried_by, &actor);
}

TEST(JsLegacyTriggerDispatch, DirectObjectWearEventModelsWearSlotAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6181,
                         "function onWear(ctx) { return ctx.object.name === 'Relic' && "
                         "ctx.wearSlot === null; }",
                         ON_WEAR, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_event(ON_WEAR, &object, &actor), 1);
}

TEST(JsLegacyTriggerDispatch, CallTriggerWearIgnoresInvalidSubject3AndModelsWearSlotAsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6182,
                         "function onWear(ctx) { return ctx.object.name === 'Relic' && "
                         "ctx.wearSlot === null; }",
                         ON_WEAR, JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    int invalid_slot = MAX_WEAR;

    EXPECT_EQ(call_trigger(ON_WEAR, &object, &actor, &invalid_slot), 1);
}

TEST(JsLegacyTriggerDispatch, LegacyObjectEventBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6147, "function onPull(ctx) { return true; }", ON_PULL,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_pull {};
    script_data return_false {};
    on_pull.command_type = ON_PULL;
    on_pull.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_pull;
    script_head script {};
    script.number = 9021;
    script.script = &on_pull;
    script_table = &script;
    top_of_script_table = 0;

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object.obj_flags.script_number = 9021;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_event(ON_PULL, &object, &actor), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectEventPathSkipsWhenObjectIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6148, "function onEat(ctx) { return false; }", ON_EAT,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;

    obj_data object = make_object("Relic");
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_event(ON_EAT, &object, &actor), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptObjectEventPathSkipsWhenActorIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6149, "function onDrink(ctx) { return false; }", ON_DRINK,
                         JsScriptPackageHost::Object));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    character_list = nullptr;

    obj_data object = make_object("Relic");
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_object_event(ON_DRINK, &object, &actor), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceivePathReturnsBlockResult) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6151,
                         "function onReceive(ctx) { "
                         "var ok = ctx.self && ctx.actor && ctx.object && "
                         "ctx.self.name === 'Receiver' && ctx.actor.name === 'Giver' && "
                         "ctx.object.name === 'Token'; "
                         "return !ok; "
                         "}",
                         ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 0);
}

TEST(JsLegacyTriggerDispatch, CallTriggerReceivePathReturnsBlockResult) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6157, "function onReceive(ctx) { return false; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(call_trigger(ON_RECEIVE, &receiver, &giver, &object), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceiveRuntimeErrorReturnsBlockResult) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6152, "function onReceive(ctx) { throw 'block'; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 0);
}

TEST(JsLegacyTriggerDispatch, LegacyReceiveBlockPreventsJavaScriptDispatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalScriptTableGuard script_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6153, "function onReceive(ctx) { return true; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    script_data on_receive {};
    script_data return_false {};
    on_receive.command_type = ON_RECEIVE;
    on_receive.next = &return_false;
    return_false.command_type = SCRIPT_RETURN_FALSE;
    return_false.prev = &on_receive;
    script_head script {};
    script.number = 9022;
    script.script = &on_receive;
    script_table = &script;
    top_of_script_table = 0;

    char_data receiver = make_character("Receiver");
    receiver.specials.script_number = 9022;
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 0);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceivePathSkipsWhenReceiverIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6154, "function onReceive(ctx) { return false; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    giver.next = nullptr;
    character_list = &giver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceivePathSkipsWhenGiverIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6155, "function onReceive(ctx) { return false; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = &receiver;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceivePathSkipsWhenObjectIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6156, "function onReceive(ctx) { return false; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptReceivePathSkipsWhenObjectMovedAwayFromReceiver) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6158, "function onReceive(ctx) { return false; }", ON_RECEIVE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data receiver = make_character("Receiver");
    char_data giver = make_character("Giver");
    receiver.next = &giver;
    giver.next = nullptr;
    character_list = &receiver;

    obj_data object = make_object("Token");
    object.carried_by = nullptr;
    object.next = nullptr;
    object_list = &object;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_receive(&receiver, &giver, &object), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearSayPathAllowsWhenHandlerReturnsFalse) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6160,
                         "function onHearSay(ctx) { "
                         "return !(ctx.self.name === 'Listener' && ctx.actor.name === 'Speaker' && "
                         "ctx.speaker.name === 'Speaker' && ctx.text === 'hello'); "
                         "}",
                         ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    listener.next = &speaker;
    speaker.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "hello";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearYellPathAllowsWhenRuntimeErrors) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6161, "function onHearYell(ctx) { throw ctx.text; }",
                         ON_HEAR_YELL));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    listener.next = &speaker;
    speaker.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "LOUD";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearPathSkipsWhenListenerIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6162, "function onHearSay(ctx) { return false; }", ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    speaker.next = nullptr;
    character_list = &speaker;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "hello";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearPathSkipsWhenSpeakerIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6163, "function onHearSay(ctx) { return false; }", ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    listener.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "hello";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearPathSkipsWhenTextIsNull) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6164, "function onHearSay(ctx) { return false; }", ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    listener.next = &speaker;
    speaker.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, nullptr), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearPathSkipsWhenListenerRoomIsInvalid) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6165, "function onHearSay(ctx) { return false; }", ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    listener.in_room = -1;
    listener.next = &speaker;
    speaker.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "hello";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptHearPathSkipsWhenSpeakerRoomIsInvalid) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6166, "function onHearSay(ctx) { return false; }", ON_HEAR_SAY));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    speaker.in_room = -1;
    listener.next = &speaker;
    speaker.next = nullptr;
    character_list = &listener;
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char text[] = "hello";

    EXPECT_EQ(trigger_char_hear(&listener, &speaker, text), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnEnterPathAllowsWhenRegistryGenerationIsStale) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;

    JsStagedPackageRecord first = activate_package(
        repository, service.live_store(), make_package(6111, "function onEnter(ctx) { return true; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());

    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);
    activate_package(repository, service.live_store(),
                     make_package(6111, "function onEnter(ctx) { return false; }"),
                     first_live_checksum);
    ASSERT_TRUE(service.refresh().ok);
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &world), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnEnterPathAllowsWhenRegistryIsEmpty) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    js_script_set_legacy_trigger_dispatch_enabled(true);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &world), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnEnterPathSkipsWhenCharacterIsNoLongerLive) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6113, "function onEnter(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    actor.next = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &world), 1);
}

TEST(JsLegacyTriggerDispatch, EnabledScriptOnEnterPathSkipsRoomsOutsideWorldTable) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_package(6114, "function onEnter(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    room_data detached_room = make_room("Detached", 200, -1);
    self.next = &actor;
    actor.next = nullptr;
    character_list = &self;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    EXPECT_EQ(trigger_char_enter(&self, &actor, &detached_room), 1);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandDispatchesMudlleMobileJavaScriptWithCommandPayload) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6201,
                         "function onSpecialCommand(ctx) { "
                         "return !(ctx.self.name === 'Guard' && ctx.actor.name === 'Builder' && "
                         "ctx.command === 'say' && ctx.args === 'open sesame' && "
                         "ctx.room.vnum === 100); "
                         "}"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6201, SPECIAL_COMMAND, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "   open sesame";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 1);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandModelsTargetFieldsAsDefaultsUntilTargetBacking) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6224,
                         "function onSpecialCommand(ctx) { "
                         "return !(ctx.command === 'say' && ctx.args === 'open sesame' && "
                         "ctx.target === null && ctx.targ1 === null && ctx.targ2 === null && "
                         "Array.isArray(ctx.targetTypes) && ctx.targetTypes.length === 0); "
                         "}"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6224, SPECIAL_COMMAND, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "   open sesame";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 1);
}

TEST(JsLegacyTriggerDispatch, SpecialEnterDispatchesMudlleMobileJavaScriptWithDirections) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6202,
                         "function onSpecialEnter(ctx) { "
                         "return !(ctx.self.name === 'Guard' && ctx.actor.name === 'Builder' && "
                         "ctx.direction === 'south' && ctx.reverseDirection === 'north'); "
                         "}",
                         SPECIAL_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6202, SPECIAL_ENTER, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;

    EXPECT_EQ(special(&actor, 3, const_cast<char *>(""), SPECIAL_ENTER, nullptr, 0), 1);
}

TEST(JsLegacyTriggerDispatch, SpecialSelfDispatchesMudlleMobileJavaScriptWithPulseTick) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalPulseGuard pulse_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6220,
                         "function onSpecialSelf(ctx) { "
                         "return !(ctx.self.name === 'Guard' && ctx.actor.name === 'Guard' && "
                         "ctx.tick === 77 && ctx.room.vnum === 100); "
                         "}",
                         SPECIAL_SELF));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6220, SPECIAL_SELF, call_list);
    host.next = nullptr;
    host.next_in_room = nullptr;
    character_list = &host;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &host;
    top_of_world = 0;
    pulse = 77;

    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &host, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, NOWHERE),
        1);
}

TEST(JsLegacyTriggerDispatch, SpecialSelfRunsFromOneMobileActivityAfterLegacyDeclines) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalPulseGuard pulse_guard;
    GlobalMobileProgramGuard mobile_program_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6222,
                         "function onSpecialSelf(ctx) { "
                         "return !(ctx.self.name === 'Guard' && ctx.actor.name === 'Guard' && "
                         "ctx.tick === 99 && ctx.room.vnum === 100); "
                         "}",
                         SPECIAL_SELF));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    std::vector<char *> programs(6223, nullptr);
    programs[6222] = const_cast<char *>("");
    mobile_program = programs.data();
    char_data host = make_character("Guard");
    host.nr = 0;
    host.specials.position = POSITION_STANDING;
    host.specials.default_pos = POSITION_STANDING;
    host.interrupt_count = 5;
    int call_list[SPECIAL_CALLLIST];
    int prog_points[SPECIAL_CALLLIST] {};
    special_list list {};
    attach_mudlle_program(host, 6222, SPECIAL_SELF, call_list);
    host.specials.union2.prog_point = prog_points;
    host.specials.poofOut = reinterpret_cast<char *>(&list);
    host.next = nullptr;
    host.next_in_room = nullptr;
    character_list = &host;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &host;
    top_of_world = 0;
    pulse = 99;

    one_mobile_activity(&host);

    EXPECT_EQ(host.interrupt_count, 5);
}

TEST(JsLegacyTriggerDispatch, SpecialSelfRuntimeErrorFailsOpen) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    GlobalPulseGuard pulse_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6221,
                         "function onSpecialSelf(ctx) { throw new Error('boom'); }",
                         SPECIAL_SELF));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6221, SPECIAL_SELF, call_list);
    host.next = nullptr;
    host.next_in_room = nullptr;
    character_list = &host;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &host;
    top_of_world = 0;
    pulse = 88;

    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &host, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, NOWHERE),
        0);
}

TEST(JsLegacyTriggerDispatch, SpecialSelfSkipsStaleInvalidAndMaskedInputs) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6223,
                         "function onSpecialSelf(ctx) { return false; }",
                         SPECIAL_SELF));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6223, SPECIAL_SELF, call_list);
    actor.next = &host;
    host.next = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    top_of_world = 0;

    actor.next = nullptr;
    character_list = &actor;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &host, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, NOWHERE),
        0);

    actor.next = &host;
    character_list = &host;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, NOWHERE),
        0);

    character_list = &host;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &host, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, 1),
        0);

    attach_mudlle_program(host, 6223, SPECIAL_COMMAND, call_list);
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &host, 0, const_cast<char *>(""), SPECIAL_SELF, nullptr, NOWHERE),
        0);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandUsesMudlleProgramVnumWithoutFallback) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6203,
                         "function onSpecialCommand(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 9999, SPECIAL_COMMAND, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "ignored";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 0);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandSkipsJavaScriptWhenCallMaskDoesNotMatch) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6204,
                         "function onSpecialCommand(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6204, SPECIAL_ENTER, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "ignored";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 0);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandUsesCurrentMudlleTacticsProgramVnum) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6205,
                         "function onSpecialCommand(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 9999, SPECIAL_COMMAND, call_list);
    call_list[1] = 6205;
    host.specials.tactics = 1;
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "ignored";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 1);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandSkipsInvalidMudlleTactics) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6211,
                         "function onSpecialCommand(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6211, SPECIAL_COMMAND, call_list);
    host.specials.tactics = static_cast<ubyte>(-1);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "ignored";

    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0),
        0);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandRuntimeErrorFailsOpen) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6206,
                         "function onSpecialCommand(ctx) { throw new Error('boom'); }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6206, SPECIAL_COMMAND, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    char args[] = "ignored";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0), 0);
}

TEST(JsLegacyTriggerDispatch, SpecialEnterRuntimeErrorFailsOpen) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6207,
                         "function onSpecialEnter(ctx) { throw new Error('boom'); }",
                         SPECIAL_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6207, SPECIAL_ENTER, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;

    EXPECT_EQ(special(&actor, 3, const_cast<char *>(""), SPECIAL_ENTER, nullptr, 0), 0);
}

TEST(JsLegacyTriggerDispatch, SpecialCommandDispatchesInvalidCommandIndexesWithNullCommand) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6208,
                         "function onSpecialCommand(ctx) { "
                         "return !(ctx.command === null && ctx.args === 'invalid'); "
                         "}"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6208, SPECIAL_COMMAND, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;

    const int invalid_commands[] = {0, -1, MAX_CMD_LIST + 1};
    for (int invalid_command : invalid_commands) {
        char args[] = " invalid";
        EXPECT_EQ(special(&actor, invalid_command, args, SPECIAL_COMMAND, nullptr, 0), 1)
            << "invalid command " << invalid_command;
    }
}

TEST(JsLegacyTriggerDispatch, SpecialEnterDispatchesInvalidDirectionsWithNullDirections) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6209,
                         "function onSpecialEnter(ctx) { "
                         "return !(ctx.direction === null && ctx.reverseDirection === null); "
                         "}",
                         SPECIAL_ENTER));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6209, SPECIAL_ENTER, call_list);
    actor.next = &host;
    host.next = nullptr;
    actor.next_in_room = &host;
    host.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;

    const int invalid_directions[] = {0, NUM_OF_DIRS + 1};
    for (int invalid_direction : invalid_directions) {
        EXPECT_EQ(special(&actor, invalid_direction, const_cast<char *>(""), SPECIAL_ENTER,
                      nullptr, 0),
            1)
            << "invalid direction " << invalid_direction;
    }
}

TEST(JsLegacyTriggerDispatch, SpecialCommandSkipsStaleAndInvalidLiveInputs) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6210,
                         "function onSpecialCommand(ctx) { return false; }"));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data host = make_character("Guard");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(host, 6210, SPECIAL_COMMAND, call_list);
    world = make_room("Room", 100, -1);
    top_of_world = 0;
    char args[] = "ignored";

    character_list = &actor;
    actor.next = nullptr;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0),
        0);

    character_list = &host;
    host.next = nullptr;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0),
        0);

    actor.next = &host;
    host.next = nullptr;
    character_list = &actor;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 1),
        0);

    host.in_room = NOWHERE;
    EXPECT_EQ(js_script_dispatch_mudlle_mobile_special(
                  &host, &actor, CMD_SAY, args, SPECIAL_COMMAND, nullptr, 0),
        0);
}

TEST(JsLegacyTriggerDispatch, SpecialTargetDispatchesMudlleMobileJavaScriptWithTypedTargets) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6212,
                         "function onSpecialTarget(ctx) { "
                         "return !(ctx.self.name === 'Target Guard' && "
                         "ctx.actor.name === 'Builder' && ctx.command === 'say' && "
                         "ctx.args === 'targeted' && ctx.targ1.name === 'Target Guard' && "
                         "ctx.target.name === 'Target Guard' && "
                         "ctx.targetTypes[0] === 'character'); "
                         "}",
                         SPECIAL_TARGET));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data target = make_character("Target Guard");
    target.abs_number = 201;
    set_char_exists(target.abs_number);
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(target, 6212, SPECIAL_TARGET, call_list);
    actor.next = &target;
    target.next = nullptr;
    actor.next_in_room = &target;
    target.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    waiting_type wait {};
    wait.targ1.type = TARGET_CHAR;
    wait.targ1.ptr.ch = &target;
    wait.targ1.ch_num = target.abs_number;
    char args[] = " targeted";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_TARGET, &wait, 0), 1);
    remove_char_exists(target.abs_number);
}

TEST(JsLegacyTriggerDispatch, SpecialTargetUsesActivatedTarg2HostAsPrimaryTarget) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6219,
                         "function onSpecialTarget(ctx) { "
                         "return !(ctx.self.name === 'TargetTwo' && "
                         "ctx.target.name === 'TargetTwo' && "
                         "ctx.targ1.name === 'OtherTarget' && ctx.targ2.name === 'TargetTwo'); "
                         "}",
                         SPECIAL_TARGET));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Builder");
    char_data other_target = make_character("OtherTarget");
    char_data target = make_character("TargetTwo");
    other_target.abs_number = 206;
    target.abs_number = 207;
    set_char_exists(other_target.abs_number);
    set_char_exists(target.abs_number);
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(target, 6219, SPECIAL_TARGET, call_list);
    actor.next = &other_target;
    other_target.next = &target;
    target.next = nullptr;
    actor.next_in_room = &other_target;
    other_target.next_in_room = &target;
    target.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    waiting_type wait {};
    wait.targ1.type = TARGET_CHAR;
    wait.targ1.ptr.ch = &other_target;
    wait.targ1.ch_num = other_target.abs_number;
    wait.targ2.type = TARGET_CHAR;
    wait.targ2.ptr.ch = &target;
    wait.targ2.ch_num = target.abs_number;
    char args[] = "target";

    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_TARGET, &wait, 0), 1);
    remove_char_exists(other_target.abs_number);
    remove_char_exists(target.abs_number);
}

TEST(JsLegacyTriggerDispatch, SpecialDamageDispatchesMudlleMobileJavaScriptWithVictimTarget) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6213,
                         "function onSpecialDamage(ctx) { "
                         "return !(ctx.self.name === 'Victim' && ctx.actor.name === 'Attacker' && "
                         "ctx.attacker.name === 'Attacker' && ctx.victim.name === 'Victim' && "
                         "ctx.target.name === 'Victim' && ctx.targ1.name === 'Victim'); "
                         "}",
                         SPECIAL_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data attacker = make_character("Attacker");
    char_data victim = make_character("Victim");
    victim.abs_number = 202;
    set_char_exists(victim.abs_number);
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(victim, 6213, SPECIAL_DAMAGE, call_list);
    attacker.next = &victim;
    victim.next = nullptr;
    attacker.next_in_room = &victim;
    victim.next_in_room = nullptr;
    character_list = &attacker;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &attacker;
    top_of_world = 0;
    waiting_type wait {};
    wait.targ1.type = TARGET_CHAR;
    wait.targ1.ptr.ch = &victim;
    wait.targ1.ch_num = victim.abs_number;

    EXPECT_EQ(special(&attacker, 0, const_cast<char *>(""), SPECIAL_DAMAGE, &wait, 0), 1);
    remove_char_exists(victim.abs_number);
}

TEST(JsLegacyTriggerDispatch, SpecialDamageUsesActivatedTarg2HostAsVictimTarget) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6218,
                         "function onSpecialDamage(ctx) { "
                         "return !(ctx.self.name === 'VictimTwo' && "
                         "ctx.victim.name === 'VictimTwo' && ctx.target.name === 'VictimTwo' && "
                         "ctx.targ1.name === 'OtherTarget' && ctx.targ2.name === 'VictimTwo' && "
                         "ctx.targetTypes[0] === 'character' && "
                         "ctx.targetTypes[1] === 'character'); "
                         "}",
                         SPECIAL_DAMAGE));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data attacker = make_character("Attacker");
    char_data other_target = make_character("OtherTarget");
    char_data victim = make_character("VictimTwo");
    other_target.abs_number = 204;
    victim.abs_number = 205;
    set_char_exists(other_target.abs_number);
    set_char_exists(victim.abs_number);
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(victim, 6218, SPECIAL_DAMAGE, call_list);
    attacker.next = &other_target;
    other_target.next = &victim;
    victim.next = nullptr;
    attacker.next_in_room = &other_target;
    other_target.next_in_room = &victim;
    victim.next_in_room = nullptr;
    character_list = &attacker;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &attacker;
    top_of_world = 0;
    waiting_type wait {};
    wait.targ1.type = TARGET_CHAR;
    wait.targ1.ptr.ch = &other_target;
    wait.targ1.ch_num = other_target.abs_number;
    wait.targ2.type = TARGET_CHAR;
    wait.targ2.ptr.ch = &victim;
    wait.targ2.ch_num = victim.abs_number;

    EXPECT_EQ(special(&attacker, 0, const_cast<char *>(""), SPECIAL_DAMAGE, &wait, 0), 1);
    remove_char_exists(other_target.abs_number);
    remove_char_exists(victim.abs_number);
}

TEST(JsLegacyTriggerDispatch, SpecialDeathDispatchesMudlleMobileJavaScriptWithDyingTarget) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6214,
                         "function onSpecialDeath(ctx) { "
                         "return !(ctx.self.name === 'Dying' && ctx.actor.name === 'Dying' && "
                         "ctx.dying.name === 'Dying' && ctx.target.name === 'Dying' && "
                         "ctx.killer === null); "
                         "}",
                         SPECIAL_DEATH));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data dying = make_character("Dying");
    int call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(dying, 6214, SPECIAL_DEATH, call_list);
    dying.next = nullptr;
    dying.next_in_room = nullptr;
    character_list = &dying;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &dying;
    top_of_world = 0;
    waiting_type wait {};

    EXPECT_EQ(special(&dying, 0, const_cast<char *>(""), SPECIAL_DEATH, &wait, 0), 1);
}

TEST(JsLegacyTriggerDispatch, RemainingMudlleSpecialErrorsUseManifestPolicy) {
    GlobalWorldFixtureGuard guard;
    GlobalLiveRegistryGuard registry_guard;
    JsLiveRegistryAdminService &service = registry_guard.service;
    JsStagedPackageRepository repository;
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6215,
                         "function onSpecialTarget(ctx) { throw new Error('boom'); }",
                         SPECIAL_TARGET));
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6216,
                         "function onSpecialDamage(ctx) { throw new Error('boom'); }",
                         SPECIAL_DAMAGE));
    activate_package(repository, service.live_store(),
                     make_mudlle_package(6217,
                         "function onSpecialDeath(ctx) { throw new Error('boom'); }",
                         SPECIAL_DEATH));
    ASSERT_TRUE(service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    char_data actor = make_character("Actor");
    char_data target = make_character("Target");
    target.abs_number = 203;
    set_char_exists(target.abs_number);
    int target_call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(target, 6215, SPECIAL_TARGET, target_call_list);
    actor.next = &target;
    target.next = nullptr;
    actor.next_in_room = &target;
    target.next_in_room = nullptr;
    character_list = &actor;
    object_list = nullptr;
    world = make_room("Room", 100, -1);
    world.people = &actor;
    top_of_world = 0;
    waiting_type wait {};
    wait.targ1.type = TARGET_CHAR;
    wait.targ1.ptr.ch = &target;
    wait.targ1.ch_num = target.abs_number;
    char args[] = "target";
    EXPECT_EQ(special(&actor, CMD_SAY, args, SPECIAL_TARGET, &wait, 0), 0);

    int damage_call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(target, 6216, SPECIAL_DAMAGE, damage_call_list);
    EXPECT_EQ(special(&actor, 0, const_cast<char *>(""), SPECIAL_DAMAGE, &wait, 0), 1);

    int death_call_list[SPECIAL_CALLLIST];
    attach_mudlle_program(target, 6217, SPECIAL_DEATH, death_call_list);
    EXPECT_EQ(special(&target, 0, const_cast<char *>(""), SPECIAL_DEATH, &wait, 0), 1);
    remove_char_exists(target.abs_number);
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

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeUsesFirstLivePackageWhenMultiplePackagesMatch) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store,
                     make_package(6172, "function onEnter(ctx) { return false; }"));
    activate_package(repository, live_store,
                     make_package(6173, "function onEnter(ctx) { syntax error if this runs }"));
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_enter_request(&self), adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6172);
    EXPECT_EQ(result.dispatch_result.handler_name, "onEnter");
    EXPECT_EQ(result.dispatch_result.matched_package_count, 2u);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_TRUE(result.dispatch_result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadePackageVnumTargetsLaterMatchWithoutFallback) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_package(repository, live_store,
                     make_package(6174, "function onEnter(ctx) { return false; }"));
    activate_package(repository, live_store,
                     make_package(6175, "function onEnter(ctx) { return true; }"));
    activate_package(repository, live_store,
                     make_package(6176, "function onDamage(ctx) { return false; }", ON_DAMAGE));
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 1, world, 0);

    JsTriggerDispatchRequest request = character_enter_request(&self);
    request.package_vnum = 6175;
    JsLegacyTriggerDispatchResult targeted_result = js_legacy_trigger_dispatch(
        service, request, adapter_options, enabled_options(service));

    EXPECT_EQ(targeted_result.status, JsLegacyTriggerDispatchStatus::Allow);
    EXPECT_EQ(targeted_result.dispatch_result.status, JsTriggerDispatchStatus::Allow);
    EXPECT_EQ(targeted_result.dispatch_result.package_vnum, 6175);
    EXPECT_EQ(targeted_result.dispatch_result.handler_name, "onEnter");
    EXPECT_EQ(targeted_result.dispatch_result.matched_package_count, 1u);
    EXPECT_TRUE(targeted_result.diagnostic.empty());

    request.package_vnum = 6176;
    JsLegacyTriggerDispatchResult wrong_trigger_result = js_legacy_trigger_dispatch(
        service, request, adapter_options, enabled_options(service));

    EXPECT_EQ(wrong_trigger_result.status, JsLegacyTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(wrong_trigger_result.dispatch_result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(wrong_trigger_result.dispatch_result.matched_package_count, 0u);
    EXPECT_TRUE(wrong_trigger_result.dispatch_result.package_id.empty());
    EXPECT_TRUE(wrong_trigger_result.dispatch_result.handler_name.empty());
    EXPECT_TRUE(wrong_trigger_result.diagnostic.empty());
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

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeDispatchesHearSayContextAndMapsBlock) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6167,
            "function onHearSay(ctx) { "
            "return !(ctx.self.name === 'Listener' && ctx.actor.name === 'Speaker' && "
            "ctx.text === 'hello'); "
            "}",
            ON_HEAR_SAY));
    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    const char_data *live_characters[] = {&listener, &speaker};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 2, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_hear_request(ON_HEAR_SAY, &listener, &speaker, "hello"),
        adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6167);
    EXPECT_EQ(result.dispatch_result.handler_name, "onHearSay");
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeDispatchesHearYellAcrossRooms) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6168,
            "function onHearYell(ctx) { "
            "return !(ctx.self.name === 'Listener' && ctx.actor.name === 'Speaker' && "
            "ctx.room.vnum === 100 && "
            "ctx.text === 'LOUD'); "
            "}",
            ON_HEAR_YELL));
    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    speaker.in_room = 1;
    const char_data *live_characters[] = {&listener, &speaker};
    room_data world[2] = {make_room("Room", 100, 0), make_room("Elsewhere", 101, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 2, world, 1);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service, character_hear_request(ON_HEAR_YELL, &listener, &speaker, "LOUD"),
        adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Block);
    EXPECT_EQ(result.dispatch_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6168);
    EXPECT_EQ(result.dispatch_result.handler_name, "onHearYell");
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsLegacyTriggerDispatch, FreshEnabledFacadeRedactsHearRuntimeErrors) {
    JsLiveRegistryReloadService service = make_refreshed_service(
        make_package(6169, "function onHearYell(ctx) { throw ctx.text; }", ON_HEAR_YELL));
    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    const char_data *live_characters[] = {&listener, &speaker};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options = make_options(live_characters, 2, world, 0);

    JsLegacyTriggerDispatchResult result = js_legacy_trigger_dispatch(
        service,
        character_hear_request(ON_HEAR_YELL, &listener, &speaker, "SECRET_HEARD_TEXT"),
        adapter_options, enabled_options(service));

    EXPECT_EQ(result.status, JsLegacyTriggerDispatchStatus::Error);
    EXPECT_EQ(result.dispatch_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.dispatch_result.package_vnum, 6169);
    EXPECT_EQ(result.dispatch_result.handler_name, "onHearYell");
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(contains(result.diagnostic, "SECRET_HEARD_TEXT"));
    EXPECT_FALSE(contains(result.diagnostic, "function onHearYell"));
    EXPECT_FALSE(contains(result.diagnostic, "\n"));
    EXPECT_FALSE(contains(result.dispatch_result.diagnostic, "SECRET_HEARD_TEXT"));
    EXPECT_FALSE(contains(result.dispatch_result.diagnostic, "function onHearYell"));
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
    const std::string mobact = read_first_available_file({"src/mobact.cpp", "../src/mobact.cpp"});

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(server_makefile.empty());
    ASSERT_FALSE(test_makefile.empty());
    ASSERT_FALSE(mobact.empty());
    EXPECT_TRUE(contains(cmake, "js_legacy_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(cmake, "tests/js_legacy_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.o"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_legacy_trigger_dispatch.h"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.h"));
    EXPECT_TRUE(contains(server_makefile, "js_live_registry_reload_service.h"));
    EXPECT_TRUE(contains(server_makefile, "mobact.o : mobact.cpp"));
    EXPECT_TRUE(contains(server_makefile, "script.h"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch.o"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(test_makefile, "js_legacy_trigger_dispatch.h"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch.h"));
    EXPECT_TRUE(contains(test_makefile, "js_live_registry_reload_service.h"));
}

TEST(JsLegacyTriggerDispatch, ActiveLegacyTriggerInventoryHasServerFacadeCoverage) {
    const std::string script = read_first_available_file({"src/script.cpp", "../script.cpp"});

    ASSERT_FALSE(script.empty());

    const JsScriptingManifestEntry *before_die =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_DIE);
    ASSERT_NE(before_die, nullptr);
    EXPECT_EQ(before_die->support_status, JsScriptingSupportStatus::Reserved);
    EXPECT_FALSE(contains(script, "case ON_BEFORE_DIE:"));

    struct ExpectedTrigger {
        int legacy_value;
        const char *handler_name;
        const char *dispatch_snippet;
    };

    const ExpectedTrigger expected_triggers[] = {
        {ON_ENTER, "onEnter",
            "dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_ENTER)"},
        {ON_BEFORE_ENTER, "onBeforeEnter",
            "dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_BEFORE_ENTER)"},
        {ON_DIE, "onDie", "dispatch_javascript_character_death_trigger(ch, killer)"},
        {ON_DAMAGE, "onDamage", "dispatch_javascript_character_damage_trigger(vict, ch)"},
        {ON_RECEIVE, "onReceive", "dispatch_javascript_character_receive_trigger(ch1, ch2, ob1)"},
        {ON_HEAR_SAY, "onHearSay",
            "dispatch_javascript_character_hear_trigger(ON_HEAR_SAY, ch, speaking, text)"},
        {ON_HEAR_YELL, "onHearYell",
            "dispatch_javascript_character_hear_trigger(ON_HEAR_YELL, ch, speaking, text)"},
        {ON_EXAMINE_OBJECT, "onExamineObject", "case ON_EXAMINE_OBJECT:"},
        {ON_EAT, "onEat", "case ON_EAT:"},
        {ON_DRINK, "onDrink", "case ON_DRINK:"},
        {ON_WEAR, "onWear", "case ON_WEAR:"},
        {ON_PULL, "onPull", "case ON_PULL:"},
    };

    for (const ExpectedTrigger &expected : expected_triggers) {
        const JsScriptingManifestEntry *entry = find_js_scripting_manifest_entry(
            JsScriptingManifestKind::LegacyScriptTrigger, expected.legacy_value);
        ASSERT_NE(entry, nullptr) << expected.legacy_value;
        EXPECT_EQ(entry->support_status, JsScriptingSupportStatus::Deferred)
            << entry->legacy_name;
        EXPECT_STREQ(expected.handler_name, entry->javascript_handler_name);
        EXPECT_TRUE(contains(script, expected.dispatch_snippet))
            << expected.legacy_value << " missing " << expected.dispatch_snippet;
    }

    EXPECT_TRUE(contains(script, "dispatch_javascript_object_damage_trigger(obj, vict, ch)"));
    EXPECT_TRUE(
        contains(script, "dispatch_javascript_object_event_trigger(trigger_type, obj, ch, wear_slot)"));
    EXPECT_TRUE(contains(script,
        "trigger_type == ON_ENTER || trigger_type == ON_EXAMINE_OBJECT || trigger_type == ON_EAT"));
    EXPECT_TRUE(contains(script, "trigger_type == ON_DRINK || trigger_type == ON_WEAR || trigger_type == ON_PULL"));
    EXPECT_TRUE(contains(script, "case ON_BEFORE_ENTER:"));
    EXPECT_TRUE(contains(script, "case ON_ENTER:"));
    EXPECT_TRUE(contains(script, "trigger_before_char_enter(tmpch, ch, room)"));
    EXPECT_TRUE(contains(script, "trigger_char_enter(tmpch, ch, room)"));
    EXPECT_TRUE(contains(script, "trigger_object_event(ON_ENTER, tmpobj, ch)"));
}

TEST(JsLegacyTriggerDispatch, CharacterGameplayPathsUseFacade) {
    const std::string script = read_first_available_file({"src/script.cpp", "../script.cpp"});
    const std::string act_move = read_first_available_file({"src/act_move.cpp", "../act_move.cpp"});
    const std::string act_obj1 = read_first_available_file({"src/act_obj1.cpp", "../act_obj1.cpp"});
    const std::string act_obj2 = read_first_available_file({"src/act_obj2.cpp", "../act_obj2.cpp"});
    const std::string act_info = read_first_available_file({"src/act_info.cpp", "../act_info.cpp"});
    const std::string interpre = read_first_available_file({"src/interpre.cpp", "../interpre.cpp"});
    const std::string mobact = read_first_available_file({"src/mobact.cpp", "../mobact.cpp"});

    ASSERT_FALSE(script.empty());
    ASSERT_FALSE(interpre.empty());
    ASSERT_FALSE(mobact.empty());
    EXPECT_EQ(count_occurrences(script, "js_legacy_trigger_dispatch("), 8u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_character_movement_entry_trigger(ch, vict, room,"),
        2u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_character_death_trigger(ch,"), 1u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_character_damage_trigger(vict, ch)"),
        1u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_character_receive_trigger(ch1, ch2, ob1)"),
        1u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_character_hear_trigger(ON_HEAR_"),
        2u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_object_damage_trigger(obj, vict, ch)"),
        1u);
    EXPECT_EQ(
        count_occurrences(script, "dispatch_javascript_object_event_trigger(trigger_type, obj, ch,"),
        1u);
    EXPECT_TRUE(contains(script,
        "dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_BEFORE_ENTER)"));
    EXPECT_TRUE(contains(
        script, "dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_ENTER)"));
    EXPECT_TRUE(contains(script, "legacy_value != ON_ENTER && legacy_value != ON_BEFORE_ENTER"));
    EXPECT_TRUE(contains(script, "request.legacy_value = legacy_value;"));
    EXPECT_TRUE(contains(script, "request.context_input.room = room_index;"));
    EXPECT_FALSE(contains(script, "dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_ENTER)"
                                  "\n    request.context_input.room = ch->in_room;"));
    EXPECT_TRUE(contains(script, "if (ch->in_room != room_index)"));
    EXPECT_TRUE(contains(script, "if (legacy_value == ON_ENTER && vict->in_room != room_index)"));
    EXPECT_TRUE(contains(script, "request.legacy_value = ON_DIE;"));
    EXPECT_TRUE(contains(script, "request.context_input.killer = killer;"));
    EXPECT_TRUE(contains(script, "request.legacy_value = ON_DAMAGE;"));
    EXPECT_TRUE(contains(script, "request.context_input.attacker = ch;"));
    EXPECT_TRUE(contains(script, "request.context_input.victim = vict;"));
    EXPECT_TRUE(contains(script, "request.context_input.weapon = ch->equipment[WIELD];"));
    EXPECT_TRUE(contains(script, "request.context_input.weapon = obj;"));
    EXPECT_TRUE(contains(script, "request.legacy_value = ON_RECEIVE;"));
    EXPECT_TRUE(contains(script, "request.legacy_value = trigger_type;"));
    EXPECT_TRUE(contains(script, "request.context_input.speaker = speaker;"));
    EXPECT_TRUE(contains(script, "request.context_input.text = text;"));
    EXPECT_TRUE(contains(script, "!js_game_adapter_room_is_valid(speaker->in_room, adapter_options)"));
    EXPECT_TRUE(contains(script, "trigger_type == ON_HEAR_SAY && listener->in_room != speaker->in_room"));
    EXPECT_TRUE(contains(script,
        "trigger_type == ON_ENTER || trigger_type == ON_EXAMINE_OBJECT || trigger_type == ON_EAT"));
    EXPECT_TRUE(contains(script, "trigger_type == ON_DRINK || trigger_type == ON_WEAR || trigger_type == ON_PULL"));
    EXPECT_TRUE(contains(script, "request.host = JsScriptPackageHost::Object;"));
    EXPECT_TRUE(contains(script, "request.context_input.object = obj;"));
    EXPECT_TRUE(contains(script, "request.context_input.wear_slot = wear_slot;"));
    EXPECT_TRUE(contains(script, "request.host = JsScriptPackageHost::MudlleMobile;"));
    EXPECT_TRUE(contains(script, "request.kind = JsScriptingManifestKind::MudlleCallFlag;"));
    EXPECT_TRUE(contains(script, "request.package_vnum = package_vnum;"));
    EXPECT_TRUE(contains(script, "request.context_input.command = command_name_for_index(cmd);"));
    EXPECT_TRUE(contains(script, "request.context_input.args = normalized_mudlle_command_args(arg);"));
    EXPECT_TRUE(contains(script, "callflag != SPECIAL_SELF"));
    EXPECT_TRUE(contains(script, "request.context_input.has_tick = true;"));
    EXPECT_TRUE(contains(script, "request.context_input.tick = pulse;"));
    EXPECT_TRUE(contains(script, "request.context_input.direction = direction_name_for_special_enter(cmd);"));
    EXPECT_TRUE(contains(
        script, "request.context_input.reverse_direction = reverse_direction_name_for_special_enter(cmd);"));
    EXPECT_TRUE(contains(script,
        "return result.status == JsLegacyTriggerDispatchStatus::Block ? 1 : 0;"));
    EXPECT_TRUE(appears_before_after(interpre, "int activate_char_special",
        "intelligent(character, victim, cmd, argument, callflag, wait_data)",
        "js_script_dispatch_mudlle_mobile_special("));
    EXPECT_TRUE(contains(mobact, "#include \"script.h\""));
    EXPECT_TRUE(contains(mobact, "static int dispatch_javascript_mobile_self(char_data* ch)"));
    EXPECT_EQ(count_occurrences(mobact, "dispatch_javascript_mobile_self(ch)"), 1u);
    EXPECT_TRUE(contains(mobact,
        "if (intelligent(ch, ch, 0, \"\", SPECIAL_SELF, 0)) {\n"
        "                    return;\n"
        "                }\n"
        "                if (dispatch_javascript_mobile_self(ch))"));
    EXPECT_TRUE(contains(act_obj2, "trigger_object_wear_event(item, character, item_slot)"));
    EXPECT_FALSE(contains(script, "static_cast<int*>(subject3)"));
    EXPECT_TRUE(contains(script, "if (js_game_adapter_room_is_valid(ch->in_room, adapter_options))"));
    EXPECT_TRUE(contains(script, "if (js_game_adapter_room_is_valid(vict->in_room, adapter_options))"));
    EXPECT_TRUE(contains(script, "request.host = JsScriptPackageHost::Character;"));
    EXPECT_TRUE(contains(script, "bool javascript_legacy_trigger_dispatch_enabled = false;"));
    EXPECT_TRUE(contains(script, "if (return_value)"));
    EXPECT_TRUE(
        contains(script, "trigger_char_die_with_killer((char_data*)subject, (char_data*)subject2)"));
    EXPECT_TRUE(contains(script,
        "int trigger_char_die(char_data* ch)\n{\n    return trigger_char_die_with_killer(ch, nullptr);\n}"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_char_die_with_killer",
        "return_value = run_script(ch->specials.script_info",
        "return_value = dispatch_javascript_character_death_trigger(ch, killer);"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_char_damage",
        "return_value = run_script(vict->specials.script_info",
        "return_value = dispatch_javascript_character_damage_trigger(vict, ch);"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_char_receive",
        "return_value = run_script(ch1->specials.script_info",
        "return_value = dispatch_javascript_character_receive_trigger(ch1, ch2, ob1);"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_char_hear",
        "return_value = run_script(ch->specials.script_info",
        "return_value = dispatch_javascript_character_hear_trigger(ON_HEAR_SAY, ch, speaking, text);"));
    EXPECT_TRUE(appears_before_after(script, "ON_HEAR_YELL",
        "return_value = run_script(ch->specials.script_info",
        "return_value = dispatch_javascript_character_hear_trigger(ON_HEAR_YELL, ch, speaking, text);"));
    EXPECT_TRUE(appears_before(script,
        "return_value = dispatch_javascript_character_hear_trigger(ON_HEAR_SAY, ch, speaking, text);",
        "return_value = dispatch_javascript_character_hear_trigger(ON_HEAR_YELL, ch, speaking, text);"));
    EXPECT_TRUE(contains(act_obj1, "call_trigger(ON_RECEIVE, vict, ch, obj);"));
    EXPECT_FALSE(contains(act_obj1, "if (call_trigger(ON_RECEIVE"));
    EXPECT_FALSE(contains(act_obj1, "if (!call_trigger(ON_RECEIVE"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_object_damage",
        "return_value = run_script(obj->obj_flags.script_info",
        "return_value = dispatch_javascript_object_damage_trigger(obj, vict, ch);"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_object_event_with_wear_slot",
        "return_value = run_script(obj->obj_flags.script_info",
        "return_value = dispatch_javascript_object_event_trigger(trigger_type, obj, ch, wear_slot);"));
    EXPECT_TRUE(appears_before_after(script, "case ON_DAMAGE:",
        "return_value = trigger_char_damage((char_data*)subject, (char_data*)subject2);",
        "if (return_value)"));
    EXPECT_TRUE(contains(script, "((char_data*)subject2)->equipment[WIELD]"));
    EXPECT_TRUE(appears_before_after(script, "int trigger_before_char_enter",
        "return_value = run_script(ch->specials.script_info",
        "return_value =\n            dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_BEFORE_ENTER);"));
    EXPECT_TRUE(appears_before(script, "return_value = run_script(ch->specials.script_info",
        "return_value =\n            dispatch_javascript_character_movement_entry_trigger(ch, vict, room, ON_ENTER);"));
    EXPECT_FALSE(contains(act_move, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_obj1, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_obj2, "js_legacy_trigger_dispatch("));
    EXPECT_FALSE(contains(act_info, "js_legacy_trigger_dispatch("));
}
