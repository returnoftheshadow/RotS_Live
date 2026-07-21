#include "../js_trigger_dispatch.h"

#include "../db.h"
#include "../interpre.h"
#include "../js_live_registry_reload_service.h"
#include "../js_scripting_runtime_policy.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

char_data make_character(const char* name, int race = 1, int level = 10, bool npc = false)
{
    char_data character {};
    character.nr = npc ? 0 : -1;
    character.in_room = 0;
    character.player.name = const_cast<char*>(name);
    character.player.short_descr = const_cast<char*>(name);
    character.player.race = race;
    character.player.level = level;
    character.tmpabilities.hit = 22;
    character.abilities.hit = 33;
    character.specials2.idnum = 1234;
    if (npc)
        character.specials2.act |= MOB_ISNPC;
    return character;
}

obj_data make_object(const char* name, int item_number = 0)
{
    obj_data object {};
    object.item_number = item_number;
    object.in_room = 0;
    object.name = const_cast<char*>(name);
    object.short_description = const_cast<char*>(name);
    return object;
}

room_data make_room(const char* name, int number, int zone)
{
    room_data room {};
    room.name = const_cast<char*>(name);
    room.number = number;
    room.zone = zone;
    return room;
}

zone_data make_zone(const char* name, int number)
{
    zone_data zone {};
    zone.name = const_cast<char*>(name);
    zone.number = number;
    return zone;
}

JsScriptRegistryReplaceOptions internal_options()
{
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

void refresh_checksum(JsScriptPackage& package)
{
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
}

JsScriptPackage make_package(int vnum, JsScriptPackageHost host, JsScriptingManifestKind kind,
    int trigger, const char* handler, const std::string& source)
{
    const JsScriptingManifestMetadata& metadata = js_scripting_manifest_metadata();
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
    package.trigger_bindings.push_back({ kind, trigger, handler });
    refresh_checksum(package);
    return package;
}

JsScriptPackage make_character_enter_package(int vnum, const std::string& source)
{
    return make_package(vnum, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter", source);
}

JsStagedPackageStageOptions make_stage_options(const std::string& base_live = "live:old")
{
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

JsStagedPackageRecord stage_package(
    JsStagedPackageRepository& repository, const JsScriptPackage& package,
    const std::string& base_live = "live:old")
{
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options(base_live));
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

JsStagedPackageRecord activate_live_package_for_dispatch(
    JsStagedPackageRepository& repository, JsLivePackageStore& live_store,
    const JsScriptPackage& package,
    const std::string& expected_previous_live_checksum = "live:old")
{
    JsStagedPackageRecord record =
        stage_package(repository, package, expected_previous_live_checksum);
    JsLivePackagePointer pointer;
    pointer.zone = record.identity.zone;
    pointer.vnum = record.identity.vnum;
    pointer.host = record.identity.host;
    pointer.package_id = record.identity.package_id;
    pointer.package_version_id = record.identity.package_version_id;
    pointer.staged_digest = record.identity.canonical_digest;
    pointer.expected_previous_live_checksum = expected_previous_live_checksum;
    pointer.current_live_checksum = js_live_package_current_checksum_for_identity(record.identity);
    pointer.loaded_at_epoch_seconds = 200000;
    pointer.load_audit_id = "audit:dispatch";
    JsLivePackagePointerResult activated =
        live_store.activate_staged_record_pointer(record, pointer);
    EXPECT_TRUE(activated.ok);
    return record;
}

JsGameAdapterOptions make_options(const char_data* const* characters, std::size_t character_count,
    const obj_data* const* objects, std::size_t object_count, room_data* world, int top_of_world,
    index_data* obj_index, std::size_t obj_index_count, zone_data* zones, std::size_t zone_count)
{
    static const char* races[] = { "God", "Human", "Dwarf" };
    JsGameAdapterOptions options;
    options.live_characters = characters;
    options.live_character_count = character_count;
    options.live_objects = objects;
    options.live_object_count = object_count;
    options.world = world;
    options.world_count = top_of_world >= 0 ? static_cast<std::size_t>(top_of_world + 1) : 0;
    options.top_of_world = top_of_world;
    options.object_index = obj_index;
    options.object_index_count = obj_index_count;
    options.zones = zones;
    options.zone_count = zone_count;
    options.race_names = races;
    options.race_name_count = 3;
    return options;
}

JsTriggerDispatchRequest character_request(const char_data* self)
{
    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_ENTER;
    request.context_input.self = self;
    request.context_input.room = 0;
    request.context_input.text = "builder supplied text must not leak";
    return request;
}

JsTriggerMutationAuthorityContext test_mutation_authority()
{
    JsTriggerMutationAuthorityContext authority;
    authority.allow_persistent_setter_mutations = true;
    authority.builder_account_id = "account:builder";
    authority.eligible_character_id = 1001;
    authority.target_zone = 30;
    authority.decision_evidence = "zone-authority:test";
    return authority;
}

bool contains(const std::string& value, const std::string& needle)
{
    return value.find(needle) != std::string::npos;
}

std::string read_first_available_file(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths) {
        std::ifstream file(path);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
    return "";
}

} // namespace

TEST(JsTriggerDispatch, StartsWithExplicitNoMatchStatusForEmptyRegistry)
{
    JsScriptPackageRegistry registry;
    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "no-match");
    EXPECT_EQ(result.matched_package_count, 0U);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsTriggerDispatch, PersistsFirstTextSettersToLiveGameRecords)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5825,
        "function onEnter(ctx) {\n"
        "  ctx.object.setName('lever keys');\n"
        "  ctx.object.setDescription('A brass lever has new glyphs.');\n"
        "  ctx.object.setShortDescription('a renamed lever');\n"
        "  ctx.object.setActionDescription(null);\n"
        "  ctx.room.setName('Changed Gate');\n"
        "  ctx.room.setDescription('The gate was changed by script.');\n"
        "  ctx.zone.setName('Changed Zone');\n"
        "  ctx.zone.setDescription(null);\n"
        "  return RotS.ScriptResult.block();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.description = str_dup("A lever is here.");
    object.short_description = str_dup("a lever");
    object.action_description = str_dup("Pulling the lever does nothing.");
    const char_data* live_characters[] = { &self };
    const obj_data* live_objects[] = { &object };
    room_data world[1] = { make_room("Gate", 100, 0) };
    world[0].name = str_dup("Gate");
    world[0].description = str_dup("The old gate.");
    zone_data zones[1] = { make_zone("Zone", 30) };
    zones[0].name = str_dup("Zone");
    zones[0].description = str_dup("The old zone.");
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;

    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_STREQ(object.name, "lever keys");
    EXPECT_STREQ(object.description, "A brass lever has new glyphs.");
    EXPECT_STREQ(object.short_description, "a renamed lever");
    EXPECT_EQ(object.action_description, nullptr);
    EXPECT_STREQ(world[0].name, "Changed Gate");
    EXPECT_STREQ(world[0].description, "The gate was changed by script.");
    EXPECT_STREQ(zones[0].name, "Changed Zone");
    EXPECT_EQ(zones[0].description, nullptr);

    free(object.name);
    free(object.description);
    free(object.short_description);
    free(object.action_description);
    free(world[0].name);
    free(world[0].description);
    free(zones[0].name);
    free(zones[0].description);
}

TEST(JsTriggerDispatch, RejectsPersistentSettersWithoutExplicitAuthority)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5827,
        "function onEnter(ctx) {\n"
        "  ctx.object.setName('unauthorized changed name');\n"
        "  return true;\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data* live_characters[] = { &self };
    const obj_data* live_objects[] = { &object };
    room_data world[1] = { make_room("Gate", 100, 0) };
    zone_data zones[1] = { make_zone("Zone", 30) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_STREQ(object.name, "lever keys old");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsPersistentSettersWhenAuthorityEvidenceIsIncomplete)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5828,
        "function onEnter(ctx) {\n"
        "  ctx.object.setName('incomplete authority name');\n"
        "  return true;\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data* live_characters[] = { &self };
    const obj_data* live_objects[] = { &object };
    room_data world[1] = { make_room("Gate", 100, 0) };
    zone_data zones[1] = { make_zone("Zone", 30) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority.allow_persistent_setter_mutations = true;
    dispatch_options.mutation_authority.builder_account_id = "account:builder";
    dispatch_options.mutation_authority.target_zone = 30;
    dispatch_options.mutation_authority.decision_evidence = "zone-authority:test";

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_STREQ(object.name, "lever keys old");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, DoesNotPersistSetterSnapshotsWhenHandlerFails)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5826,
        "function onEnter(ctx) {\n"
        "  ctx.object.setName('unsafe changed name');\n"
        "  throw new TypeError('boom');\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data* live_characters[] = { &self };
    const obj_data* live_objects[] = { &object };
    room_data world[1] = { make_room("Gate", 100, 0) };
    zone_data zones[1] = { make_zone("Zone", 30) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_STREQ(object.name, "lever keys old");

    free(object.name);
    free(object.short_description);
}

TEST(JsScriptingRuntimePolicy, PinsLiveDispatchDefaults)
{
    const JsScriptingRuntimeSafetyPolicy& policy = js_scripting_runtime_safety_policy();

    EXPECT_EQ(policy.runtime_limits.memory_limit_bytes, 1024U * 1024U);
    EXPECT_EQ(policy.runtime_limits.stack_limit_bytes, 256U * 1024U);
    EXPECT_EQ(policy.runtime_limits.instruction_budget, 100000U);
    EXPECT_EQ(policy.budget_limits.max_invocations_per_pulse, 1024U);
    EXPECT_EQ(policy.budget_limits.max_invocations_per_package_per_pulse, 256U);
    EXPECT_EQ(policy.depth_limits.max_dispatch_depth, 8U);
    EXPECT_EQ(policy.max_dispatch_failure_logs_per_pulse, 16U);
    EXPECT_TRUE(contains(policy.failure_logging_policy, "Source text"));
    EXPECT_TRUE(contains(policy.failure_logging_policy, "actor speech"));
    EXPECT_TRUE(contains(policy.failure_logging_policy, "tokens"));
}

TEST(JsTriggerDispatch, NearMissHostKindAndValueDoNotExecutePackageSource)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5001,
        "function onEnter(ctx) { syntax error if this runs }\n"
        "function onDamage(ctx) { return false; }");
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage" });
    refresh_checksum(package);
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest wrong_host = character_request(&self);
    wrong_host.host = JsScriptPackageHost::Object;
    wrong_host.context_input.self = nullptr;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, wrong_host, options).status,
        JsTriggerDispatchStatus::NoMatch);

    JsTriggerDispatchRequest wrong_kind = character_request(&self);
    wrong_kind.kind = JsScriptingManifestKind::MudlleCallFlag;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, wrong_kind, options).status,
        JsTriggerDispatchStatus::NoMatch);

    JsTriggerDispatchRequest wrong_value = character_request(&self);
    wrong_value.legacy_value = ON_RECEIVE;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, wrong_value, options).status,
        JsTriggerDispatchStatus::NoMatch);
}

TEST(JsTriggerDispatch, InvokesOnlyBoundHandlerFromFullCompiledPackage)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5101,
        "function onDamage(ctx) { return false; }\n"
        "function onEnter(ctx) {\n"
        "  return ctx.self.name === 'Self' && ctx.trigger.legacyName === 'ON_ENTER'\n"
        "    && ctx.trigger.hostType === 'character' && ctx.trigger.blocksGameplay === true;\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "allow");
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5101);
    EXPECT_EQ(result.package_id, "pkg-5101");
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_EQ(result.matched_package_count, 1U);
}

TEST(JsTriggerDispatch, DispatchesBuilderClientCommonJsExportsAndScriptResultHelpers)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5151,
        "\"use strict\";\n"
        "Object.defineProperty(exports, \"__esModule\", { value: true });\n"
        "exports.onEnter = onEnter;\n"
        "function onEnter(ctx) {\n"
        "  return ctx.self.name === 'Self' ? RotS.ScriptResult.block() : RotS.ScriptResult.allow();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5151);
    EXPECT_EQ(result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, PrefersCompiledExportOverGlobalHandlerFallback)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5152,
        "exports.onEnter = function(ctx) { return RotS.ScriptResult.allow(); };\n"
        "function onEnter(ctx) { return RotS.ScriptResult.block(); }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5152);
    EXPECT_EQ(result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, MapsFalseReturnToBlockForBlockingManifestTriggers)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5201, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER, "onBeforeEnter",
        "function onBeforeEnter(ctx) {\n"
        "  return ctx.trigger.blocksGameplay ? false : true;\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Guard");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request = character_request(&self);
    request.legacy_value = ON_BEFORE_ENTER;
    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "block");
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
}

TEST(JsTriggerDispatch, UsesFirstMatchingPackageInRegistryOrder)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5301,
        "function onEnter(ctx) { return false; }");
    JsScriptPackage second = make_character_enter_package(5302,
        "function onEnter(ctx) { syntax error if this runs }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(result.package_vnum, 5301);
    EXPECT_EQ(result.matched_package_count, 2U);
}

TEST(JsTriggerDispatch, PackageVnumFilterDispatchesOnlyAttachedPackage)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5351,
        "function onEnter(ctx) { return false; }");
    JsScriptPackage second = make_character_enter_package(5352,
        "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request = character_request(&self);
    request.package_vnum = 5352;
    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.package_vnum, 5352);
    EXPECT_EQ(result.matched_package_count, 1U);

    request.package_vnum = 9999;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, request, options).status,
        JsTriggerDispatchStatus::NoMatch);
}

TEST(JsTriggerDispatch, PackageVnumFilterDoesNotFallBackWhenAttachedPackageIsWrongTrigger)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage wrong_attached = make_package(5371, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) { return true; }");
    JsScriptPackage global_match = make_character_enter_package(5372,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ wrong_attached, global_match }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request = character_request(&self);
    request.package_vnum = 5371;
    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(result.matched_package_count, 0U);
}

TEST(JsTriggerDispatch, DispatchesFromRefreshedLiveRegistryService)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsScriptPackage package = make_character_enter_package(5381,
        "function onEnter(ctx) { return ctx.self.name === 'Self'; }");
    JsStagedPackageRecord record =
        activate_live_package_for_dispatch(repository, live_store, package);
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_live_first_match(service, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, record.identity.vnum);
    EXPECT_EQ(result.package_id, record.identity.package_id);
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(result.matched_package_count, 1U);
}

TEST(JsTriggerDispatch, LiveRegistryBridgeNoMatchDoesNotExposePackageMetadata)
{
    JsLiveRegistryReloadService empty_service;
    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult empty_result =
        js_trigger_dispatch_live_first_match(empty_service, character_request(&self), options);

    EXPECT_EQ(empty_result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(empty_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(empty_result.matched_package_count, 0U);
    EXPECT_EQ(empty_result.package_vnum, 0);
    EXPECT_TRUE(empty_result.package_id.empty());
    EXPECT_TRUE(empty_result.handler_name.empty());
    EXPECT_TRUE(empty_result.diagnostic.empty());

    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    activate_live_package_for_dispatch(
        repository, live_store,
        make_character_enter_package(5383, "function onEnter(ctx) { return true; }"));
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));
    JsTriggerDispatchRequest wrong_vnum = character_request(&self);
    wrong_vnum.package_vnum = 9999;

    JsTriggerDispatchResult wrong_vnum_result =
        js_trigger_dispatch_live_first_match(service, wrong_vnum, options);

    EXPECT_EQ(wrong_vnum_result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(wrong_vnum_result.matched_package_count, 0U);
    EXPECT_TRUE(wrong_vnum_result.package_id.empty());
    EXPECT_TRUE(wrong_vnum_result.handler_name.empty());
    EXPECT_TRUE(wrong_vnum_result.diagnostic.empty());
}

TEST(JsTriggerDispatch, LiveRegistryDispatchUsesRefreshSnapshotUntilReloaded)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_live_package_for_dispatch(
        repository, live_store,
        make_character_enter_package(5382, "function onEnter(ctx) { return false; }"));
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchRequest request = character_request(&self);
    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);
    activate_live_package_for_dispatch(
        repository, live_store,
        make_character_enter_package(5382, "function onEnter(ctx) { return true; }"),
        first_live_checksum);

    JsTriggerDispatchResult stale_result =
        js_trigger_dispatch_live_first_match(service, request, options);
    ASSERT_TRUE(service.refresh_from_live_store(live_store));
    JsTriggerDispatchResult refreshed_result =
        js_trigger_dispatch_live_first_match(service, request, options);

    EXPECT_EQ(stale_result.status, JsTriggerDispatchStatus::Block) << stale_result.diagnostic;
    EXPECT_EQ(stale_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_TRUE(stale_result.diagnostic.empty());
    EXPECT_EQ(refreshed_result.status, JsTriggerDispatchStatus::Allow)
        << refreshed_result.diagnostic;
    EXPECT_EQ(refreshed_result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_TRUE(refreshed_result.diagnostic.empty());
    EXPECT_EQ(stale_result.package_vnum, refreshed_result.package_vnum);
    EXPECT_EQ(stale_result.package_id, refreshed_result.package_id);
}

TEST(JsTriggerDispatch, FailedLiveRegistryRefreshKeepsPreviousDispatchSnapshot)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore first_store;
    activate_live_package_for_dispatch(
        repository, first_store,
        make_character_enter_package(5384, "function onEnter(ctx) { return false; }"));
    JsLiveRegistryReloadOptions options_with_conflict;
    options_with_conflict.replace_options.validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    options_with_conflict.expected_server_instance_id = "server:main";
    options_with_conflict.replace_options.legacy_script_vnums.push_back(5385);
    JsLiveRegistryReloadService service(options_with_conflict);
    ASSERT_TRUE(service.refresh_from_live_store(first_store));

    JsLivePackageStore conflict_store;
    activate_live_package_for_dispatch(
        repository, conflict_store,
        make_character_enter_package(5385, "function onEnter(ctx) { return true; }"));
    JsLiveRegistryReloadResult reload_result;
    ASSERT_FALSE(service.refresh_from_live_store(conflict_store, &reload_result));
    EXPECT_EQ(reload_result.status, JsLiveRegistryReloadStatus::ValidationFailed);

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchResult result =
        js_trigger_dispatch_live_first_match(service, character_request(&self), adapter_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5384);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.successful_reload_count(), 1U);
}

TEST(JsTriggerDispatch, RejectsMissingRequiredCharacterHostBeforeRuntimeExecution)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5401,
        "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_other = make_character("Live");
    const char_data* live_characters[] = { &live_other };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&stale_self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "error");
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "missing live character"));
    EXPECT_FALSE(contains(result.diagnostic, "0x"));
    EXPECT_FALSE(contains(result.diagnostic, "Stale"));
}

TEST(JsTriggerDispatch, RejectsMissingRequiredObjectHostBeforeRuntimeExecution)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5501, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    obj_data stale_object = make_object("stale object", 0);
    obj_data live_object = make_object("live object", 0);
    const obj_data* live_objects[] = { &live_object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(nullptr, 0, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &stale_object;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "missing live object"));
    EXPECT_FALSE(contains(result.diagnostic, "stale object"));
}

TEST(JsTriggerDispatch, ObjectHostProvidesObjectContextAndNoCharacterSelfAlias)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5601, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.self !== null) throw new TypeError('self-alias');\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.id !== 'object') throw new TypeError(ctx.object.id);\n"
        "  if (ctx.object.name !== 'Blade') throw new TypeError(ctx.object.name);\n"
        "  if (ctx.object.vnum !== 300) throw new TypeError(String(ctx.object.vnum));\n"
        "  if (ctx.object.room.vnum !== 100) throw new TypeError(String(ctx.object.room && ctx.object.room.vnum));\n"
        "  if (ctx.object.room.zone.vnum !== 10) throw new TypeError(String(ctx.object.room.zone && ctx.object.room.zone.vnum));\n"
        "  if (ctx.room.vnum !== 100) throw new TypeError(String(ctx.room.vnum));\n"
        "  return ctx.trigger.hostType === 'object';\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    obj_data object = make_object("Blade", 0);
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    zone_data zones[1] = { make_zone("Test Zone", 10) };
    JsGameAdapterOptions options =
        make_options(nullptr, 0, live_objects, 1, world, 0, object_index, 1, zones, 1);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &object;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, ObjectHostProvidesCarriedBySnapshot)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5602, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.room !== null) throw new TypeError('unexpected-room');\n"
        "  if (ctx.object.wornBy !== null) throw new TypeError('unexpected-worn');\n"
        "  if (ctx.object.carriedBy === null) throw new TypeError('missing-carrier');\n"
        "  if (ctx.object.carriedBy.name !== 'Carrier') throw new TypeError(ctx.object.carriedBy.name);\n"
        "  if (ctx.object.carriedBy.room.vnum !== 100) throw new TypeError('carrier-room');\n"
        "  return ctx.object.carriedBy.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data carrier = make_character("Carrier");
    obj_data object = make_object("Blade", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    carrier.carrying = &object;
    const char_data* live_characters[] = { &carrier };
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &object;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, ObjectHostProvidesWornBySnapshot)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5603, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.room !== null) throw new TypeError('unexpected-room');\n"
        "  if (ctx.object.carriedBy !== null) throw new TypeError('unexpected-carried');\n"
        "  if (ctx.object.wornBy === null) throw new TypeError('missing-wearer');\n"
        "  if (ctx.object.wornBy.name !== 'Wearer') throw new TypeError(ctx.object.wornBy.name);\n"
        "  if (ctx.object.wornBy.room.vnum !== 100) throw new TypeError('wearer-room');\n"
        "  return ctx.object.wornBy.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data wearer = make_character("Wearer");
    obj_data object = make_object("Blade", 0);
    object.in_room = -1;
    object.carried_by = &wearer;
    wearer.equipment[WIELD] = &object;
    const char_data* live_characters[] = { &wearer };
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &object;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, CharacterDieProvidesKillerRoleSnapshot)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5608, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "onDie",
        "function onDie(ctx) {\n"
        "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
        "  if (ctx.self.name !== 'Victim') throw new TypeError('self');\n"
        "  if (ctx.killer.name !== 'Killer') throw new TypeError('killer');\n"
        "  return ctx.killer.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data victim = make_character("Victim");
    char_data killer = make_character("Killer");
    const char_data* live_characters[] = { &victim, &killer };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DIE;
    request.context_input.self = &victim;
    request.context_input.killer = &killer;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, CharacterDieModelsMissingKillerAsNull)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5609, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "onDie",
        "function onDie(ctx) { return ctx.self.name === 'Victim' && ctx.killer === null; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data victim = make_character("Victim");
    const char_data* live_characters[] = { &victim };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DIE;
    request.context_input.self = &victim;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, CharacterDamageProvidesAttackerAndVictimRoleSnapshots)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5610, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
        "  if (ctx.self.name !== 'Victim') throw new TypeError('self');\n"
        "  if (ctx.actor.name !== 'Attacker') throw new TypeError('actor');\n"
        "  if (ctx.attacker.name !== 'Attacker') throw new TypeError('attacker');\n"
        "  if (ctx.victim.name !== 'Victim') throw new TypeError('victim');\n"
        "  if (ctx.weapon.name !== 'Blade') throw new TypeError('weapon');\n"
        "  return ctx.attacker.isValid() && ctx.victim.isValid() && ctx.weapon.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    obj_data weapon = make_object("Blade", 0);
    const char_data* live_characters[] = { &victim, &attacker };
    const obj_data* live_objects[] = { &weapon };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.self = &victim;
    request.context_input.actor = &attacker;
    request.context_input.attacker = &attacker;
    request.context_input.victim = &victim;
    request.context_input.weapon = &weapon;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, ObjectDamageProvidesAttackerAndVictimRoleSnapshots)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5612, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.hostType !== 'object') throw new TypeError('host');\n"
        "  if (ctx.self !== null) throw new TypeError('self');\n"
        "  if (ctx.object.name !== 'Blade') throw new TypeError('object');\n"
        "  if (ctx.actor.name !== 'Attacker') throw new TypeError('actor');\n"
        "  if (ctx.attacker.name !== 'Attacker') throw new TypeError('attacker');\n"
        "  if (ctx.victim.name !== 'Victim') throw new TypeError('victim');\n"
        "  if (ctx.weapon.name !== 'Blade') throw new TypeError('weapon');\n"
        "  return ctx.attacker.isValid() && ctx.victim.isValid() && ctx.weapon.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    obj_data object = make_object("Blade", 0);
    const char_data* live_characters[] = { &victim, &attacker };
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &object;
    request.context_input.actor = &attacker;
    request.context_input.attacker = &attacker;
    request.context_input.victim = &victim;
    request.context_input.weapon = &object;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, CharacterDamageModelsMissingWeaponAsNull)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5613, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) { return ctx.weapon === null && ctx.attacker.name === 'Attacker'; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    const char_data* live_characters[] = { &victim, &attacker };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.self = &victim;
    request.context_input.actor = &attacker;
    request.context_input.attacker = &attacker;
    request.context_input.victim = &victim;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, ObjectWearProvidesWearSlot)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5614, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR, "onWear",
        "function onWear(ctx) {\n"
        "  if (ctx.hostType !== 'object') throw new TypeError('host');\n"
        "  if (ctx.object.name !== 'Helm') throw new TypeError('object');\n"
        "  if (ctx.actor.name !== 'Actor') throw new TypeError('actor');\n"
        "  return ctx.wearSlot === 'head';\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data actor = make_character("Actor");
    obj_data object = make_object("Helm", 0);
    const char_data* live_characters[] = { &actor };
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_WEAR;
    request.context_input.object = &object;
    request.context_input.actor = &actor;
    request.context_input.wear_slot = WEAR_HEAD;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, CharacterHearProvidesSpeakerRoleSnapshot)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5611, JsScriptPackageHost::Character,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY, "onHearSay",
        "function onHearSay(ctx) {\n"
        "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
        "  if (ctx.self.name !== 'Listener') throw new TypeError('self');\n"
        "  if (ctx.actor.name !== 'Speaker') throw new TypeError('actor');\n"
        "  if (ctx.speaker.name !== 'Speaker') throw new TypeError('speaker');\n"
        "  return ctx.text === 'hello there' && ctx.speaker.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    const char_data* live_characters[] = { &listener, &speaker };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_HEAR_SAY;
    request.context_input.self = &listener;
    request.context_input.actor = &speaker;
    request.context_input.speaker = &speaker;
    request.context_input.room = 0;
    request.context_input.text = "hello there";

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, RejectsMudlleMobileDispatchWhenSelfIsNotNpc)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5651, JsScriptPackageHost::MudlleMobile,
        JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND, "onSpecialCommand",
        "function onSpecialCommand(ctx) { return ctx.self.isNpc === true; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data player = make_character("Player");
    char_data mobile = make_character("Mobile", 1, 10, true);
    const char_data* live_characters[] = { &player, &mobile };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 2, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::MudlleMobile;
    request.kind = JsScriptingManifestKind::MudlleCallFlag;
    request.legacy_value = SPECIAL_COMMAND;
    request.context_input.self = &player;
    request.context_input.room = 0;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);
    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "missing live mudlle-mobile"));

    request.context_input.self = &mobile;
    result = js_trigger_dispatch_first_match(registry, request, options);
    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
}

TEST(JsTriggerDispatch, RuntimeErrorsKeepSafeMetadataAndRedactContextText)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5701,
        "function onEnter(ctx) { throw ctx.text; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.package_vnum, 5701);
    EXPECT_EQ(result.package_id, "pkg-5701");
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(contains(result.diagnostic, "builder supplied text"));
    EXPECT_FALSE(contains(result.diagnostic, "function onEnter"));
    EXPECT_FALSE(contains(result.diagnostic, "0x"));
    EXPECT_FALSE(contains(result.diagnostic, "\n"));
    EXPECT_FALSE(contains(result.diagnostic, "\r"));
    EXPECT_LE(result.diagnostic.size(), 120U);
}

TEST(JsTriggerDispatch, TopLevelPackageReturnCannotPreemptBoundHandler)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5751,
        "return false;\n"
        "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_NE(result.status, JsTriggerDispatchStatus::Block);
}

TEST(JsTriggerDispatch, RuntimeLimitsPropagateThroughFacade)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5771,
        "function onEnter(ctx) { while (true) {} }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsRuntimeLimits limits;
    limits.instruction_budget = 1;

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options, limits);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Interrupted);
    EXPECT_EQ(result.package_vnum, 5771);
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_FALSE(contains(result.diagnostic, "while"));
}

TEST(JsTriggerDispatch, SamePulsePerPackageBudgetSkipsRuntimeExecution)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5772,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 90;

    JsTriggerDispatchResult first =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);
    JsTriggerDispatchResult second =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(first.status, JsTriggerDispatchStatus::Block) << first.diagnostic;
    EXPECT_EQ(first.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(second.status, JsTriggerDispatchStatus::BudgetExceeded);
    EXPECT_STREQ(js_trigger_dispatch_status_name(second.status), "budget-exceeded");
    EXPECT_EQ(second.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(second.package_vnum, 5772);
    EXPECT_EQ(second.package_id, "pkg-5772");
    EXPECT_EQ(second.handler_name, "onEnter");
    EXPECT_EQ(second.matched_package_count, 1U);
    EXPECT_TRUE(contains(second.diagnostic, "budget exceeded"));
    EXPECT_FALSE(contains(second.diagnostic, "function onEnter"));
}

TEST(JsTriggerDispatch, BudgetResetsWhenPulseChanges)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5773,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 91;

    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::Block);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::BudgetExceeded);

    dispatch_options.current_pulse = 92;
    JsTriggerDispatchResult next_pulse =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(next_pulse.status, JsTriggerDispatchStatus::Block) << next_pulse.diagnostic;
    EXPECT_EQ(next_pulse.package_vnum, 5773);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::BudgetExceeded);

    dispatch_options.current_pulse = 0;
    JsTriggerDispatchResult wrapped_pulse =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(wrapped_pulse.status, JsTriggerDispatchStatus::Block) << wrapped_pulse.diagnostic;
    EXPECT_EQ(wrapped_pulse.package_vnum, 5773);
}

TEST(JsTriggerDispatch, TotalPulseBudgetAppliesAcrossPackages)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5774,
        "function onEnter(ctx) { return true; }");
    JsScriptPackage second = make_character_enter_package(5775,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_pulse = 1;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 10;
    dispatch_options.current_pulse = 93;

    JsTriggerDispatchRequest first_request = character_request(&self);
    first_request.package_vnum = 5774;
    JsTriggerDispatchRequest second_request = character_request(&self);
    second_request.package_vnum = 5775;

    JsTriggerDispatchResult first_result =
        js_trigger_dispatch_first_match(registry, first_request, adapter_options, dispatch_options);
    JsTriggerDispatchResult second_result =
        js_trigger_dispatch_first_match(registry, second_request, adapter_options, dispatch_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Allow) << first_result.diagnostic;
    EXPECT_EQ(first_result.package_vnum, 5774);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::BudgetExceeded);
    EXPECT_EQ(second_result.package_vnum, 5775);
    EXPECT_EQ(second_result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, BudgetExceededAttemptDoesNotConsumeTotalBudget)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5777,
        "function onEnter(ctx) { return true; }");
    JsScriptPackage second = make_character_enter_package(5778,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_pulse = 2;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 95;

    JsTriggerDispatchRequest first_request = character_request(&self);
    first_request.package_vnum = 5777;
    JsTriggerDispatchRequest second_request = character_request(&self);
    second_request.package_vnum = 5778;

    EXPECT_EQ(js_trigger_dispatch_first_match(registry, first_request, adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::Allow);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, first_request, adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::BudgetExceeded);
    JsTriggerDispatchResult second_package =
        js_trigger_dispatch_first_match(registry, second_request, adapter_options,
            dispatch_options);

    EXPECT_EQ(second_package.status, JsTriggerDispatchStatus::Block) << second_package.diagnostic;
    EXPECT_EQ(second_package.package_vnum, 5778);
}

TEST(JsTriggerDispatch, ZeroBudgetLimitsAreUnlimitedWhenBudgetIsProvided)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5779,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.current_pulse = 96;

    for (int invocation = 0; invocation < 4; ++invocation) {
        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                dispatch_options);
        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << invocation << ": "
                                                                 << result.diagnostic;
    }
}

TEST(JsTriggerDispatch, DepthExceededSkipsRuntimeExecution)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5780,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchDepthLimits depth_limits;
    depth_limits.max_dispatch_depth = 1;
    ASSERT_TRUE(depth_guard.try_enter(depth_limits));
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits = depth_limits;

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::DepthExceeded);
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "depth-exceeded");
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5780);
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_TRUE(contains(result.diagnostic, "depth exceeded"));
    EXPECT_FALSE(contains(result.diagnostic, "function onEnter"));
    EXPECT_EQ(depth_guard.current_depth(), 1U);
    depth_guard.leave();
}

TEST(JsTriggerDispatch, SuccessfulDispatchLeavesDepthForNextInvocation)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5781,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;

    JsTriggerDispatchResult first =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);
    JsTriggerDispatchResult second =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(first.status, JsTriggerDispatchStatus::Block) << first.diagnostic;
    EXPECT_EQ(second.status, JsTriggerDispatchStatus::Block) << second.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, ZeroDepthLimitIsUnlimitedWhenGuardIsProvided)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5782,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchDepthLimits unlimited_limits;
    ASSERT_TRUE(depth_guard.try_enter(unlimited_limits));
    ASSERT_TRUE(depth_guard.try_enter(unlimited_limits));
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits = unlimited_limits;

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 2U);
    depth_guard.leave();
    depth_guard.leave();
}

TEST(JsTriggerDispatch, RuntimeErrorReleasesTriggerDepthForNextDispatch)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5783,
        "function onEnter(ctx) { throw new Error('boom'); }");
    JsScriptPackage second = make_character_enter_package(5784,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;
    JsTriggerDispatchRequest first_request = character_request(&self);
    first_request.package_vnum = 5783;
    JsTriggerDispatchRequest second_request = character_request(&self);
    second_request.package_vnum = 5784;

    JsTriggerDispatchResult first_result =
        js_trigger_dispatch_first_match(registry, first_request, adapter_options, dispatch_options);
    JsTriggerDispatchResult second_result =
        js_trigger_dispatch_first_match(registry, second_request, adapter_options, dispatch_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::Block) << second_result.diagnostic;
}

TEST(JsTriggerDispatch, InterruptedRuntimeReleasesTriggerDepthForNextDispatch)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_character_enter_package(5785,
        "function onEnter(ctx) { while (true) {} }");
    JsScriptPackage second = make_character_enter_package(5786,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ first, second }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions interrupt_options;
    interrupt_options.depth_guard = &depth_guard;
    interrupt_options.depth_limits.max_dispatch_depth = 1;
    interrupt_options.runtime_limits.instruction_budget = 1;
    JsTriggerDispatchOptions normal_options;
    normal_options.depth_guard = &depth_guard;
    normal_options.depth_limits.max_dispatch_depth = 1;
    JsTriggerDispatchRequest first_request = character_request(&self);
    first_request.package_vnum = 5785;
    JsTriggerDispatchRequest second_request = character_request(&self);
    second_request.package_vnum = 5786;

    JsTriggerDispatchResult first_result =
        js_trigger_dispatch_first_match(registry, first_request, adapter_options, interrupt_options);
    JsTriggerDispatchResult second_result =
        js_trigger_dispatch_first_match(registry, second_request, adapter_options, normal_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(first_result.runtime_status, JsRuntimeStatus::Interrupted);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::Block) << second_result.diagnostic;
}

TEST(JsTriggerDispatch, DepthExceededAttemptDoesNotConsumeDepth)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5787,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;
    ASSERT_TRUE(depth_guard.try_enter(dispatch_options.depth_limits));

    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::DepthExceeded);
    EXPECT_EQ(depth_guard.current_depth(), 1U);
    depth_guard.leave();

    JsTriggerDispatchResult after_release =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(after_release.status, JsTriggerDispatchStatus::Block) << after_release.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, DepthExceededDoesNotConsumeBudget)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5788,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_pulse = 1;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;
    dispatch_options.current_pulse = 97;
    ASSERT_TRUE(depth_guard.try_enter(dispatch_options.depth_limits));

    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::DepthExceeded);
    depth_guard.leave();
    JsTriggerDispatchResult after_release =
        js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
            dispatch_options);

    EXPECT_EQ(after_release.status, JsTriggerDispatchStatus::Block) << after_release.diagnostic;
}

TEST(JsTriggerDispatch, NoMatchAndMissingLiveContextDoNotAcquireTriggerDepth)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5789,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_self = make_character("Live");
    const char_data* live_characters[] = { &live_self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;

    JsTriggerDispatchRequest no_match = character_request(&live_self);
    no_match.legacy_value = ON_DAMAGE;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, no_match, adapter_options,
                  dispatch_options)
                  .status,
        JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&stale_self),
                  adapter_options, dispatch_options)
                  .status,
        JsTriggerDispatchStatus::Error);
    EXPECT_EQ(depth_guard.current_depth(), 0U);

    JsTriggerDispatchResult live_result =
        js_trigger_dispatch_first_match(registry, character_request(&live_self), adapter_options,
            dispatch_options);

    EXPECT_EQ(live_result.status, JsTriggerDispatchStatus::Block) << live_result.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, MissingLiveContextDoesNotConsumeBudget)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5776,
        "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_self = make_character("Live");
    const char_data* live_characters[] = { &live_self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 94;

    JsTriggerDispatchResult stale_result =
        js_trigger_dispatch_first_match(registry, character_request(&stale_self), adapter_options,
            dispatch_options);
    JsTriggerDispatchResult live_result =
        js_trigger_dispatch_first_match(registry, character_request(&live_self), adapter_options,
            dispatch_options);

    EXPECT_EQ(stale_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_TRUE(contains(stale_result.diagnostic, "missing live character"));
    EXPECT_EQ(live_result.status, JsTriggerDispatchStatus::Block) << live_result.diagnostic;
    EXPECT_EQ(live_result.package_vnum, 5776);
}

TEST(JsTriggerDispatch, MissingHandlerFailsClosedInsteadOfAllowing)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5801,
        "function onDamage(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    char_data self = make_character("Self");
    const char_data* live_characters[] = { &self };
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_EQ(result.package_vnum, 5801);
    EXPECT_EQ(result.package_id, "pkg-5801");
    EXPECT_EQ(result.handler_name, "onEnter");
    EXPECT_FALSE(result.diagnostic.empty());
    EXPECT_FALSE(contains(result.diagnostic, "function onDamage"));
    EXPECT_NE(result.status, JsTriggerDispatchStatus::Allow);
}

TEST(JsTriggerDispatch, BuildFilesReferenceDispatchSourcesAndTests)
{
    const std::string cmake =
        read_first_available_file({ "src/CMakeLists.txt", "../src/CMakeLists.txt" });
    const std::string server_makefile =
        read_first_available_file({ "src/Makefile", "../src/Makefile" });
    const std::string test_makefile =
        read_first_available_file({ "src/tests/Makefile", "../src/tests/Makefile" });

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(server_makefile.empty());
    ASSERT_FALSE(test_makefile.empty());

    EXPECT_TRUE(contains(cmake, "js_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(cmake, "js_scripting_runtime_policy.cpp"));
    EXPECT_TRUE(contains(cmake, "tests/js_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.o"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_scripting_runtime_policy.o"));
    EXPECT_TRUE(contains(server_makefile, "js_scripting_runtime_policy.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_live_registry_reload_service.h"));
    EXPECT_TRUE(contains(server_makefile, "js_live_package_store.h"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch.o"));
    EXPECT_TRUE(contains(test_makefile, "js_scripting_runtime_policy.o"));
    EXPECT_TRUE(contains(test_makefile, "js_scripting_runtime_policy.cpp"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(test_makefile, "js_live_registry_reload_service.h"));
    EXPECT_TRUE(contains(test_makefile, "js_live_package_store.h"));
}
