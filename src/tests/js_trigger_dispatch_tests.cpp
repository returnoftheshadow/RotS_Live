#include "../js_trigger_dispatch.h"

#include "../db.h"
#include "../interpre.h"
#include "../script.h"
#include "../structs.h"
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
        "    && ctx.trigger.hostType === 'character' && ctx.trigger.blocksGameplay === false;\n"
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
        "  if (ctx.room.vnum !== 100) throw new TypeError(String(ctx.room.vnum));\n"
        "  return ctx.trigger.hostType === 'object';\n"
        "}");
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    obj_data object = make_object("Blade", 0);
    const obj_data* live_objects[] = { &object };
    index_data object_index[1] {};
    object_index[0].virt = 300;
    room_data world[1] = { make_room("Room", 100, 0) };
    JsGameAdapterOptions options =
        make_options(nullptr, 0, live_objects, 1, world, 0, object_index, 1, nullptr, 0);

    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Object;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_DAMAGE;
    request.context_input.object = &object;
    request.context_input.room = 0;

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
    EXPECT_TRUE(contains(cmake, "tests/js_trigger_dispatch_tests.cpp"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.o"));
    EXPECT_TRUE(contains(server_makefile, "js_trigger_dispatch.cpp"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch.o"));
    EXPECT_TRUE(contains(test_makefile, "js_trigger_dispatch_tests.cpp"));
}
