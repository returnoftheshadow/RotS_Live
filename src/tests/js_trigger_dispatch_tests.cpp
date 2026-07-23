#include "../js_trigger_dispatch.h"

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../js_api_struct_mapping.h"
#include "../js_live_registry_reload_service.h"
#include "../js_scripting_runtime_policy.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern char world_map[];
extern char *sector_types[];
extern char num_of_sector_types;
extern descriptor_data *descriptor_list;
extern index_data *obj_index;
extern obj_data *obj_proto;
extern obj_data *object_list;
extern int top_of_objt;
extern char_data *waiting_list;
void draw_map();

namespace {

char_data make_character(const char *name, int race = 1, int level = 10, bool npc = false) {
    char_data character{};
    character.nr = npc ? 0 : -1;
    character.in_room = 0;
    character.player.name = const_cast<char *>(name);
    character.player.short_descr = const_cast<char *>(name);
    character.player.race = race;
    character.player.level = level;
    character.tmpabilities.hit = 22;
    character.abilities.hit = 33;
    character.specials2.idnum = 1234;
    if (npc)
        character.specials2.act |= MOB_ISNPC;
    return character;
}

obj_data make_object(const char *name, int item_number = 0) {
    obj_data object{};
    object.item_number = item_number;
    object.in_room = 0;
    object.name = const_cast<char *>(name);
    object.short_description = const_cast<char *>(name);
    return object;
}

room_data make_room(const char *name, int number, int zone) {
    room_data room{};
    room.name = const_cast<char *>(name);
    room.number = number;
    room.zone = zone;
    return room;
}

zone_data make_zone(const char *name, int number) {
    zone_data zone{};
    zone.name = const_cast<char *>(name);
    zone.number = number;
    return zone;
}

void attach_descriptor(descriptor_data &descriptor, char_data &character) {
    descriptor = {};
    descriptor.connected = CON_PLYNG;
    descriptor.character = &character;
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    character.desc = &descriptor;
}

struct DescriptorListGuard {
    descriptor_data *saved_descriptor_list = descriptor_list;

    ~DescriptorListGuard() { descriptor_list = saved_descriptor_list; }
};

struct ObjectPrototypeGuard {
    index_data *saved_obj_index = obj_index;
    obj_data *saved_obj_proto = obj_proto;
    obj_data *saved_object_list = object_list;
    int saved_top_of_objt = top_of_objt;
    index_data fixture_index[1] = {};
    obj_data fixture_proto[1] = {};

    explicit ObjectPrototypeGuard(int vnum) {
        fixture_index[0].virt = vnum;
        fixture_proto[0] = make_object("script reward", 0);
        fixture_proto[0].description = const_cast<char *>("A script reward rests here.");
        fixture_proto[0].action_description = nullptr;
        fixture_proto[0].in_room = NOWHERE;
        fixture_proto[0].obj_flags.weight = 1;
        obj_index = fixture_index;
        obj_proto = fixture_proto;
        top_of_objt = 0;
    }

    ~ObjectPrototypeGuard() {
        while (object_list != nullptr && object_list != saved_object_list)
            extract_obj(object_list);
        object_list = saved_object_list;
        obj_index = saved_obj_index;
        obj_proto = saved_obj_proto;
        top_of_objt = saved_top_of_objt;
    }
};

struct WaitingListGuard {
    char_data *saved_waiting_list = waiting_list;

    ~WaitingListGuard() { waiting_list = saved_waiting_list; }
};

JsScriptRegistryReplaceOptions internal_options() {
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

void refresh_checksum(JsScriptPackage &package) {
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
}

JsScriptPackage make_package(int vnum, JsScriptPackageHost host, JsScriptingManifestKind kind,
                             int trigger, const char *handler, const std::string &source) {
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
    package.trigger_bindings.push_back({kind, trigger, handler});
    refresh_checksum(package);
    return package;
}

JsScriptPackage make_character_enter_package(int vnum, const std::string &source) {
    return make_package(vnum, JsScriptPackageHost::Character,
                        JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter", source);
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

JsStagedPackageRecord stage_package(JsStagedPackageRepository &repository,
                                    const JsScriptPackage &package,
                                    const std::string &base_live = "live:old") {
    JsStagedPackageStageResult staged =
        repository.stage_package(package, make_stage_options(base_live));
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

JsStagedPackageRecord activate_live_package_for_dispatch(
    JsStagedPackageRepository &repository, JsLivePackageStore &live_store,
    const JsScriptPackage &package,
    const std::string &expected_previous_live_checksum = "live:old") {
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

JsGameAdapterOptions make_options(const char_data *const *characters, std::size_t character_count,
                                  const obj_data *const *objects, std::size_t object_count,
                                  room_data *world, int top_of_world, index_data *obj_index,
                                  std::size_t obj_index_count, zone_data *zones,
                                  std::size_t zone_count) {
    static const char *races[] = {"God", "Human", "Dwarf"};
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

JsTriggerDispatchRequest character_request(const char_data *self) {
    JsTriggerDispatchRequest request;
    request.host = JsScriptPackageHost::Character;
    request.kind = JsScriptingManifestKind::LegacyScriptTrigger;
    request.legacy_value = ON_ENTER;
    request.context_input.self = self;
    request.context_input.room = 0;
    request.context_input.text = "builder supplied text must not leak";
    return request;
}

JsTriggerMutationAuthorityContext test_mutation_authority(int target_zone = 30) {
    JsTriggerMutationAuthorityContext authority;
    authority.allow_persistent_setter_mutations = true;
    authority.builder_account_id = "account:builder";
    authority.eligible_character_id = 1001;
    authority.target_zone = target_zone;
    authority.target_token_secret = "test-room-target-secret";
    authority.decision_evidence = "zone-authority:test";
    return authority;
}

void add_accepting_command_audit(JsTriggerDispatchOptions &options, int *audit_calls = nullptr) {
    options.helper_mutation_options.command_audit_user_data = audit_calls;
    options.helper_mutation_options.command_audit_callback =
        [](const JsTriggerCommandMutationAuditRequest &, std::string *, void *user_data) {
            if (user_data != nullptr)
                ++*static_cast<int *>(user_data);
            return true;
        };
}

void add_accepting_command_audit(JsTriggerHelperMutationTransactionOptions &options,
                                 int *audit_calls = nullptr) {
    options.command_audit_user_data = audit_calls;
    options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &, std::string *,
                                        void *user_data) {
        if (user_data != nullptr)
            ++*static_cast<int *>(user_data);
        return true;
    };
}

JsRuntimeMutation make_zone_name_setter(const char *value) {
    JsRuntimeMutation mutation;
    mutation.kind = "setter";
    mutation.target_type = "zone";
    mutation.target_id = "zone";
    mutation.property = "name";
    mutation.value_kind = "string";
    mutation.has_value = true;
    mutation.value = value;
    return mutation;
}

JsRuntimeMutation make_script_command_mutation(const char *operation,
                                               const std::string &arguments_json) {
    JsRuntimeMutation mutation;
    mutation.kind = "command";
    mutation.operation = operation;
    mutation.arguments_json = arguments_json;
    return mutation;
}

JsRuntimeMutation make_helper_mutation(const char *operation) {
    JsRuntimeMutation mutation;
    mutation.kind = "helper";
    mutation.operation = operation;
    mutation.target_token = "room-token:v1:30:100:test-room-target-secret";
    mutation.arguments_json = "{\"flag\":\"peaceRoom\"}";
    return mutation;
}

bool contains(const std::string &value, const std::string &needle) {
    return value.find(needle) != std::string::npos;
}

std::vector<std::string> split_pipe_list(const char *value) {
    std::vector<std::string> entries;
    if (value == nullptr)
        return entries;

    std::stringstream stream(value);
    std::string entry;
    while (std::getline(stream, entry, '|')) {
        if (!entry.empty())
            entries.push_back(entry);
    }
    return entries;
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

std::vector<std::string> parse_dispatch_room_flag_helper_names() {
    const std::string source =
        read_first_available_file({"src/js_trigger_dispatch.cpp", "../js_trigger_dispatch.cpp"});
    EXPECT_FALSE(source.empty());
    const std::string start_marker = "constexpr RoomFlagHelperFlag RoomFlagHelperAllowedFlags[]";
    const std::size_t start = source.find(start_marker);
    EXPECT_NE(start, std::string::npos);
    const std::size_t open_brace = source.find('{', start);
    EXPECT_NE(open_brace, std::string::npos);
    const std::size_t end = source.find("};", open_brace);
    EXPECT_NE(end, std::string::npos);

    std::vector<std::string> names;
    std::stringstream table(source.substr(open_brace, end - open_brace));
    std::string line;
    while (std::getline(table, line)) {
        if (line.find('{') == std::string::npos)
            continue;
        const std::size_t quote = line.find('"');
        if (quote == std::string::npos)
            continue;
        const std::size_t close = line.find('"', quote + 1);
        EXPECT_NE(close, std::string::npos);
        if (close == std::string::npos)
            continue;
        names.push_back(line.substr(quote + 1, close - quote - 1));
    }
    return names;
}

std::size_t world_map_symbol_offset(int x, int y) {
    return static_cast<std::size_t>((y + 1) * (WORLD_SIZE_X + 4) + x * 2 + 1);
}

} // namespace

TEST(JsTriggerDispatch, RuntimeMutationDiscriminatorAcceptsOnlyScalarSetterEnvelopes) {
    JsRuntimeMutation missing_kind_setter;
    missing_kind_setter.target_type = "room";
    missing_kind_setter.target_id = "room";
    missing_kind_setter.property = "level";
    missing_kind_setter.value_kind = "number";
    missing_kind_setter.has_value = true;
    missing_kind_setter.value = "10";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(missing_kind_setter));

    JsRuntimeMutation tagged_setter = missing_kind_setter;
    tagged_setter.kind = "setter";
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(tagged_setter));

    JsRuntimeMutation helper = tagged_setter;
    helper.kind = "helper";
    helper.operation = "room.flags.add";
    helper.target_token = "test-token";
    helper.arguments_json = "{\"flag\":\"peace\"}";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(helper));

    JsRuntimeMutation unknown = tagged_setter;
    unknown.kind = "world-helper";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(unknown));
}

TEST(JsTriggerDispatch, RuntimeMutationDiscriminatorDoesNotTreatHelperNamesAsSetterKinds) {
    for (const char *property : {"moveTo", "setExit", "addAffect", "inventory", "contents", "flags",
                                 "cmd", "resetCommands"}) {
        JsRuntimeMutation mutation;
        mutation.kind = property;
        mutation.target_type = "room";
        mutation.target_id = "room";
        mutation.property = property;
        mutation.value_kind = "string";
        mutation.has_value = true;
        mutation.value = "forged";
        EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(mutation)) << property;
    }
}

TEST(JsTriggerDispatch, RuntimeMutationDiscriminatorRejectsHelperFieldsOnSetterEnvelopes) {
    JsRuntimeMutation mutation;
    mutation.kind = "setter";
    mutation.target_type = "zone";
    mutation.target_id = "zone";
    mutation.property = "name";
    mutation.value_kind = "string";
    mutation.has_value = true;
    mutation.value = "Builder Zone";
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(mutation));

    mutation.operation = "zone.description.add";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(mutation));
    mutation.operation.clear();

    mutation.target_token = "opaque-token";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(mutation));
    mutation.target_token.clear();

    mutation.arguments_json = "{}";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(mutation));
}

TEST(JsTriggerDispatch, RuntimeMutationDiscriminatorAcceptsOnlySupportedCommandHelpers) {
    const JsRuntimeMutation say = make_script_command_mutation(
        "script.do_say", "{\"speakerId\":\"char:1001\",\"text\":\"The gate opens.\"}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(say));

    const JsRuntimeMutation send = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"player:7\",\"text\":\"You hear a click.\"}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(send));

    const JsRuntimeMutation room = make_script_command_mutation(
        "script.send_to_room", "{\"roomId\":\"room:100\",\"text\":\"Stone grinds nearby.\"}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(room));

    const JsRuntimeMutation load =
        make_script_command_mutation("script.load_obj", "{\"vnum\":4201}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(load));

    const JsRuntimeMutation targeted_load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":4201,\"loadTargetId\":\"player:7\"}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(targeted_load));

    const JsRuntimeMutation give = make_script_command_mutation(
        "script.do_give",
        "{\"giverId\":\"char:1001\",\"recipientId\":\"player:7\",\"objectId\":\"object:301\"}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(give));

    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":4}");
    EXPECT_TRUE(js_trigger_dispatch_supports_runtime_mutation(wait));

    const JsRuntimeMutation unknown =
        make_script_command_mutation("script.raw_command", "{\"text\":\"force north\"}");
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(unknown));

    const JsRuntimeMutation multiline = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"player:7\",\"text\":\"bad\\ntext\"}");
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(multiline));

    JsRuntimeMutation polluted = wait;
    polluted.target_token = "room-token:v1:1:100:secret";
    EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(polluted));
}

TEST(JsTriggerDispatch, CommandHelpersRejectMalformedTargetIdsBeforeAudit) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6206);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    struct Case {
        const char *name;
        const char *operation;
        const char *arguments_json;
    };
    const Case cases[] = {
        {"speaker overflow", "script.do_say",
         "{\"speakerId\":\"char:2147483648\",\"text\":\"No.\"}"},
        {"speaker sign", "script.do_say", "{\"speakerId\":\"char:-1\",\"text\":\"No.\"}"},
        {"target decimal", "script.send_to_char", "{\"targetId\":\"player:7.5\",\"text\":\"No.\"}"},
        {"target whitespace", "script.send_to_char",
         "{\"targetId\":\" player:7\",\"text\":\"No.\"}"},
        {"room empty suffix", "script.send_to_room", "{\"roomId\":\"room:\",\"text\":\"No.\"}"},
        {"room huge digits", "script.send_to_room",
         "{\"roomId\":\"room:999999999999999999999999999999\",\"text\":\"No.\"}"},
        {"load target sign", "script.load_obj", "{\"vnum\":6206,\"loadTargetId\":\"room:-1\"}"},
        {"load target overflow", "script.load_obj",
         "{\"vnum\":6206,\"loadTargetId\":\"mob:2147483648\"}"},
        {"giver whitespace", "script.do_give",
         "{\"giverId\":\"char:1001 \",\"recipientId\":\"actor\",\"objectId\":\"object\"}"},
        {"recipient empty suffix", "script.do_give",
         "{\"giverId\":\"self\",\"recipientId\":\"player:\",\"objectId\":\"object\"}"},
        {"object overflow", "script.do_give",
         "{\"giverId\":\"self\",\"recipientId\":\"actor\",\"objectId\":\"object:2147483648\"}"},
        {"duplicate target key", "script.send_to_char",
         "{\"targetId\":\"actor\",\"targetId\":\"player:7\",\"text\":\"No.\"}"},
    };

    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    for (const Case &entry : cases) {
        const JsRuntimeMutation mutation =
            make_script_command_mutation(entry.operation, entry.arguments_json);
        EXPECT_FALSE(js_trigger_dispatch_supports_runtime_mutation(mutation)) << entry.name;

        const JsTriggerRuntimeMutationTransactionApplyResult result =
            js_trigger_dispatch_apply_runtime_mutation_transaction(
                {mutation}, request, adapter_options, test_mutation_authority(), helper_options);

        EXPECT_FALSE(result.ok) << entry.name;
        EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated)
            << entry.name;
        EXPECT_EQ(result.diagnostic, "JavaScript trigger command helper mutation rejected")
            << entry.name;
        EXPECT_EQ(command_audit_calls, 0) << entry.name;
        EXPECT_STREQ(zones[0].name, "Zone") << entry.name;
        EXPECT_EQ(world[0].room_flags, 0) << entry.name;
        EXPECT_EQ(actor.carrying, nullptr) << entry.name;
        EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING)) << entry.name;
        EXPECT_STREQ(actor_descriptor.output, "") << entry.name;
        EXPECT_EQ(object_list, nullptr) << entry.name;
        EXPECT_EQ(obj_index[0].number, 0) << entry.name;
    }
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MutatingCommandHelpersRequireCommandAuditBeforeSideEffects) {
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6206);
    waiting_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6206,\"loadTargetId\":\"actor\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction({load}, request, adapter_options,
                                                               test_mutation_authority());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger command helper audit required");
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
}

TEST(JsTriggerDispatch, OutputCommandHelpersApplyToLiveDescriptorsWithoutPersistentAuthority) {
    DescriptorListGuard descriptor_guard;
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5851, "function onEnter(ctx) {\n"
              "  const tell = RotS.Script.send_to_char(ctx.actor, 'Private notice.');\n"
              "  const room = RotS.Script.send_to_room(ctx.room, 'Room notice.');\n"
              "  const say = RotS.Script.do_say(ctx.self, 'Gate opens.');\n"
              "  return tell.ok && room.ok && say.ok;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    char_data observer = make_character("Observer");
    self.next_in_room = &actor;
    actor.next_in_room = &observer;
    observer.next_in_room = nullptr;
    descriptor_data self_descriptor{};
    descriptor_data actor_descriptor{};
    descriptor_data observer_descriptor{};
    attach_descriptor(self_descriptor, self);
    attach_descriptor(actor_descriptor, actor);
    attach_descriptor(observer_descriptor, observer);
    self_descriptor.next = &actor_descriptor;
    actor_descriptor.next = &observer_descriptor;
    observer_descriptor.next = nullptr;
    descriptor_list = &self_descriptor;

    const char_data *live_characters[] = {&self, &actor, &observer};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].people = &self;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 3, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int command_audit_calls = 0;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.helper_mutation_options.command_audit_user_data = &command_audit_calls;
    dispatch_options.helper_mutation_options.command_audit_callback =
        [](const JsTriggerCommandMutationAuditRequest &request, std::string *, void *user_data) {
            ++*static_cast<int *>(user_data);
            EXPECT_EQ(request.mutation_count, 3U);
            EXPECT_EQ(request.operations_summary,
                      "script.do_say,script.send_to_char,script.send_to_room");
            EXPECT_EQ(request.package_vnum, 5851);
            EXPECT_EQ(request.package_id, "pkg-5851");
            EXPECT_EQ(request.handler_name, "onEnter");
            EXPECT_EQ(request.host, JsScriptPackageHost::Character);
            EXPECT_EQ(request.kind, JsScriptingManifestKind::LegacyScriptTrigger);
            EXPECT_EQ(request.legacy_value, ON_ENTER);
            EXPECT_EQ(request.authority_target_zone, -1);
            return true;
        };
    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(command_audit_calls, 1);
    EXPECT_TRUE(contains(self_descriptor.output, "Room notice.\n\r"));
    EXPECT_TRUE(contains(self_descriptor.output, "Self says 'Gate opens.'\n\r"));
    EXPECT_FALSE(contains(self_descriptor.output, "Private notice."));
    EXPECT_TRUE(contains(actor_descriptor.output, "Private notice.\n\r"));
    EXPECT_TRUE(contains(actor_descriptor.output, "Room notice.\n\r"));
    EXPECT_TRUE(contains(actor_descriptor.output, "Self says 'Gate opens.'\n\r"));
    EXPECT_TRUE(contains(observer_descriptor.output, "Room notice.\n\r"));
    EXPECT_TRUE(contains(observer_descriptor.output, "Self says 'Gate opens.'\n\r"));
}

TEST(JsTriggerDispatch, OutputCommandHelpersRejectForgedTargetsWithoutWriting) {
    DescriptorListGuard descriptor_guard;
    char_data self = make_character("Self");
    descriptor_data descriptor{};
    attach_descriptor(descriptor, self);
    descriptor_list = &descriptor;

    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation forged = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"mob:999\",\"text\":\"Forged notice.\"}");

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction({forged}, request, adapter_options,
                                                               {});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger output command target rejected");
    EXPECT_STREQ(descriptor.output, "");
}

TEST(JsTriggerDispatch, OutputCommandHelpersRejectOverflowRoomIdsWithoutWriting) {
    DescriptorListGuard descriptor_guard;
    char_data self = make_character("Self");
    descriptor_data descriptor{};
    attach_descriptor(descriptor, self);
    descriptor_list = &descriptor;

    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation overflow_room = make_script_command_mutation(
        "script.send_to_room", "{\"roomId\":\"room:999999999999999999999999\",\"text\":\"Nope.\"}");

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction({overflow_room}, request,
                                                               adapter_options, {});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger command helper mutation rejected");
    EXPECT_STREQ(descriptor.output, "");
}

TEST(JsTriggerDispatch, LoadObjCommandHelperPlacesObjectInLiveCharacterInventory) {
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5854, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.load_obj(6201, ctx.actor);\n"
              "  return reward.ok;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    ASSERT_NE(actor.carrying, nullptr);
    EXPECT_EQ(actor.carrying->item_number, 0);
    EXPECT_EQ(actor.carrying->carried_by, &actor);
    EXPECT_EQ(obj_index[0].number, 1);
}

TEST(JsTriggerDispatch, LoadObjInlineNotFoundResultAllowsBuilderFallbackMessage) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5883, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(9999, ctx.actor);\n"
              "  if (!reward.ok && reward.code === 'not-found') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'That reward prototype is missing.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    EXPECT_TRUE(contains(self_descriptor.output, "That reward prototype is missing.\n\r"));
}

TEST(JsTriggerDispatch, LoadObjInlineUnauthorizedDoesNotRevealPrototypeExistence) {
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5886, "function onEnter(ctx) {\n"
              "  const existing = RotS.Script.loadObj(6201, ctx.actor);\n"
              "  const missing = RotS.Script.loadObj(9999, ctx.actor);\n"
              "  return !existing.ok && !missing.ok && existing.code === 'not-authorized' && "
              "missing.code === 'not-authorized';\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
}

TEST(JsTriggerDispatch, LoadObjInlineInventoryFullResultDoesNotQueueObjectCreate) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5884, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(6201, ctx.actor);\n"
              "  if (!reward.ok && reward.code === 'inventory-full') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'You cannot carry the reward.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    actor.specials.carry_items = CAN_CARRY_N(&actor);
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    EXPECT_TRUE(contains(self_descriptor.output, "You cannot carry the reward.\n\r"));
}

TEST(JsTriggerDispatch, LoadObjInlineTooHeavyResultDoesNotQueueObjectCreate) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(6201);
    object_guard.fixture_proto[0].obj_flags.weight = 2;
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5887, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(6201, ctx.actor);\n"
              "  if (!reward.ok && reward.code === 'too-heavy') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'The reward is too heavy.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    actor.specials.carry_weight = CAN_CARRY_W(&actor) - 1;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    EXPECT_TRUE(contains(self_descriptor.output, "The reward is too heavy.\n\r"));
}

TEST(JsTriggerDispatch, LoadObjInlineAuditRejectedResultDoesNotQueueObjectCreate) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5888, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(6201, ctx.actor);\n"
              "  if (!reward.ok && reward.code === 'audit-rejected') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'The reward is not approved.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    std::vector<std::string> audit_operations;
    dispatch_options.helper_mutation_options.command_audit_user_data = &audit_calls;
    dispatch_options.helper_mutation_options.command_audit_callback =
        [](const JsTriggerCommandMutationAuditRequest &request, std::string *diagnostic,
           void *user_data) {
            ++*static_cast<int *>(user_data);
            if (request.operations_summary == "script.load_obj") {
                if (diagnostic != nullptr)
                    *diagnostic = "private audit detail";
                return false;
            }
            return true;
        };

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 2);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    EXPECT_TRUE(contains(self_descriptor.output, "The reward is not approved.\n\r"));
    EXPECT_FALSE(contains(self_descriptor.output, "private audit detail"));
}

TEST(JsTriggerDispatch, LoadObjInlineSuccessAuditsOnceAndCreatesAtCommit) {
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5885, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(6201, ctx.actor);\n"
              "  return reward.ok && reward.code === 'ok' && ctx.actor.inventory.length === 0;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    ASSERT_NE(actor.carrying, nullptr);
    EXPECT_EQ(actor.carrying->item_number, 0);
    EXPECT_EQ(actor.carrying->carried_by, &actor);
    EXPECT_EQ(actor.specials.carry_items, 1);
    EXPECT_EQ(actor.specials.carry_weight, 1);
    EXPECT_EQ(obj_index[0].number, 1);
}

TEST(JsTriggerDispatch, LoadObjInlineNoTargetAuditsOnceWithoutObjectCreation) {
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5889, "function onEnter() {\n"
              "  const reward = RotS.Script.loadObj(6201);\n"
              "  return reward.ok && reward.code === 'ok' && reward.field === 'script.load_obj';\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
}

TEST(JsTriggerDispatch, LoadObjInlineRoomTargetAuditsOnceAndCreatesAtCommit) {
    ObjectPrototypeGuard object_guard(6201);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5890, "function onEnter(ctx) {\n"
              "  const reward = RotS.Script.loadObj(6201, ctx.room);\n"
              "  return reward.ok && reward.code === 'ok' && ctx.room.contents.length === 0;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    ASSERT_NE(world[0].contents, nullptr);
    EXPECT_EQ(world[0].contents->item_number, 0);
    EXPECT_EQ(world[0].contents->in_room, 0);
    EXPECT_EQ(obj_index[0].number, 1);
}

TEST(JsTriggerDispatch, DoGiveCommandHelperTransfersCurrentLiveCarriedObject) {
    ObjectPrototypeGuard object_guard(5104);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5855, "function onEnter(ctx) {\n"
              "  const give = RotS.Script.do_give(ctx.actor, ctx.self, ctx.object);\n"
              "  return give.ok;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(self.carrying, &token);
    EXPECT_EQ(token.carried_by, &self);
    EXPECT_EQ(actor.specials.carry_items, 0);
    EXPECT_EQ(self.specials.carry_items, 1);
}

TEST(JsTriggerDispatch, DoGiveInlineInventoryFullResultAllowsBuilderFallbackMessage) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(5104);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5880, "function onEnter(ctx) {\n"
              "  const give = RotS.Script.doGive(ctx.actor, ctx.self, ctx.object);\n"
              "  if (!give.ok && give.code === 'inventory-full') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'It appears your inventory is full.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    token.obj_flags.weight = 1;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    self.specials.carry_items = CAN_CARRY_N(&self);
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, &token);
    EXPECT_EQ(self.carrying, nullptr);
    EXPECT_EQ(token.carried_by, &actor);
    EXPECT_TRUE(contains(self_descriptor.output, "It appears your inventory is full.\n\r"));
}

TEST(JsTriggerDispatch, DoGiveInlineAuditRejectedResultDoesNotQueueTransfer) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    ObjectPrototypeGuard object_guard(5104);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5881, "function onEnter(ctx) {\n"
              "  const give = RotS.Script.doGive(ctx.actor, ctx.self, ctx.object);\n"
              "  if (!give.ok && give.code === 'audit-rejected') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'The transfer is not approved.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    token.obj_flags.weight = 1;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    dispatch_options.helper_mutation_options.command_audit_user_data = &audit_calls;
    dispatch_options.helper_mutation_options.command_audit_callback =
        [](const JsTriggerCommandMutationAuditRequest &request, std::string *diagnostic,
           void *user_data) {
            ++*static_cast<int *>(user_data);
            if (request.operations_summary == "script.do_give") {
                if (diagnostic != nullptr)
                    *diagnostic = "private audit detail";
                return false;
            }
            return true;
        };

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 2);
    EXPECT_EQ(actor.carrying, &token);
    EXPECT_EQ(self.carrying, nullptr);
    EXPECT_EQ(token.carried_by, &actor);
    EXPECT_TRUE(contains(self_descriptor.output, "The transfer is not approved.\n\r"));
    EXPECT_FALSE(contains(self_descriptor.output, "private audit detail"));
}

TEST(JsTriggerDispatch, DoGiveInlineSuccessAuditsOnceAndTransfersAtCommit) {
    ObjectPrototypeGuard object_guard(5104);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5882, "function onEnter(ctx) {\n"
              "  const give = RotS.Script.doGive(ctx.actor, ctx.self, ctx.object);\n"
              "  return give.ok && give.code === 'ok'\n"
              "    && ctx.actor.inventory.length === 1\n"
              "    && ctx.self.inventory.length === 0;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    token.obj_flags.weight = 1;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(self.carrying, &token);
    EXPECT_EQ(token.carried_by, &self);
    EXPECT_EQ(actor.specials.carry_items, 0);
    EXPECT_EQ(actor.specials.carry_weight, 0);
    EXPECT_EQ(self.specials.carry_items, 1);
    EXPECT_EQ(self.specials.carry_weight, 1);
}

TEST(JsTriggerDispatch, BridgeAcceptedDoGiveFailsClosedWhenLiveStateNoLongerApplicable) {
    ObjectPrototypeGuard object_guard(5104);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    char_data interloper = make_character("Interloper");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &interloper;
    interloper.carrying = &token;
    interloper.specials.carry_items = 1;
    interloper.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor, &interloper};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Original Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 3, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation give = make_script_command_mutation(
        "script.do_give",
        "{\"giverId\":\"actor\",\"recipientId\":\"self\",\"objectId\":\"object\"}");
    give.command_result_bridge_accepted = true;
    JsTriggerHelperMutationTransactionOptions helper_options;

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, give}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_STREQ(zones[0].name, "Original Zone");
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(self.carrying, nullptr);
    EXPECT_EQ(interloper.carrying, &token);
    EXPECT_EQ(interloper.specials.carry_items, 1);
    EXPECT_EQ(interloper.specials.carry_weight, 1);
    EXPECT_EQ(token.carried_by, &interloper);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, BridgeAcceptedDoGiveStillTransfersExactlyOnceAtApply) {
    ObjectPrototypeGuard object_guard(5104);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    token.obj_flags.weight = 2;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 2;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsRuntimeMutation give = make_script_command_mutation(
        "script.do_give",
        "{\"giverId\":\"actor\",\"recipientId\":\"self\",\"objectId\":\"object\"}");
    give.command_result_bridge_accepted = true;
    JsTriggerHelperMutationTransactionOptions helper_options;

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {give}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_TRUE(result.ok) << result.diagnostic;
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(self.carrying, &token);
    EXPECT_EQ(token.carried_by, &self);
    EXPECT_EQ(actor.specials.carry_items, 0);
    EXPECT_EQ(actor.specials.carry_weight, 0);
    EXPECT_EQ(self.specials.carry_items, 1);
    EXPECT_EQ(self.specials.carry_weight, 2);
}

TEST(JsTriggerDispatch, BridgeAcceptedLoadObjFailsClosedWhenCapacityChangesBeforeApply) {
    ObjectPrototypeGuard object_guard(6201);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    actor.specials.carry_items = CAN_CARRY_N(&actor);
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Original Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6201,\"loadTargetId\":\"actor\"}");
    load.command_result_bridge_accepted = true;
    JsTriggerHelperMutationTransactionOptions helper_options;

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, load}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_STREQ(zones[0].name, "Original Zone");
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(actor.specials.carry_items, CAN_CARRY_N(&actor));
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, DoGiveResultClassifierReportsStableReasonCodes) {
    char_data giver = make_character("Giver");
    char_data recipient = make_character("Recipient");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &giver;
    token.obj_flags.weight = 1;
    giver.carrying = &token;
    giver.specials.carry_items = 1;
    giver.specials.carry_weight = 1;

    EXPECT_EQ(js_trigger_classify_do_give_result(nullptr, &giver, &recipient),
              JsTriggerCommandResultCode::InvalidTarget);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::InvalidTarget),
                 "invalid-target");

    obj_data unlinked = make_object("unlinked token", 0);
    unlinked.in_room = NOWHERE;
    unlinked.carried_by = &giver;
    EXPECT_EQ(js_trigger_classify_do_give_result(&unlinked, &giver, &recipient),
              JsTriggerCommandResultCode::NotCarried);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::NotCarried),
                 "not-carried");

    token.obj_flags.extra_flags = ITEM_NODROP;
    EXPECT_EQ(js_trigger_classify_do_give_result(&token, &giver, &recipient),
              JsTriggerCommandResultCode::NoDrop);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::NoDrop),
                 "no-drop");
    token.obj_flags.extra_flags = 0;

    recipient.specials.carry_items = CAN_CARRY_N(&recipient);
    EXPECT_EQ(js_trigger_classify_do_give_result(&token, &giver, &recipient),
              JsTriggerCommandResultCode::InventoryFull);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::InventoryFull),
                 "inventory-full");
    recipient.specials.carry_items = 0;

    recipient.specials.carry_weight = CAN_CARRY_W(&recipient);
    EXPECT_EQ(js_trigger_classify_do_give_result(&token, &giver, &recipient),
              JsTriggerCommandResultCode::TooHeavy);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::TooHeavy),
                 "too-heavy");
    recipient.specials.carry_weight = 0;

    EXPECT_EQ(js_trigger_classify_do_give_result(&token, &giver, &recipient),
              JsTriggerCommandResultCode::Ok);
    EXPECT_STREQ(js_trigger_command_result_code_name(JsTriggerCommandResultCode::Ok), "ok");
}

TEST(JsTriggerDispatch, DoGiveCommandHelperRejectsNoDropWithoutOutputOrTransfer) {
    DescriptorListGuard descriptor_guard;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data self_descriptor{};
    descriptor_data actor_descriptor{};
    attach_descriptor(self_descriptor, self);
    attach_descriptor(actor_descriptor, actor);
    self_descriptor.next = &actor_descriptor;
    actor_descriptor.next = nullptr;
    descriptor_list = &self_descriptor;
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    token.obj_flags.extra_flags = ITEM_NODROP;
    token.obj_flags.weight = 1;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.object = &token;
    JsTriggerHelperMutationTransactionOptions helper_options;
    int command_audit_calls = 0;
    add_accepting_command_audit(helper_options, &command_audit_calls);
    const JsRuntimeMutation give = make_script_command_mutation(
        "script.do_give",
        "{\"giverId\":\"actor\",\"recipientId\":\"self\",\"objectId\":\"object\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {give}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_EQ(actor.carrying, &token);
    EXPECT_EQ(self.carrying, nullptr);
    EXPECT_EQ(token.carried_by, &actor);
    EXPECT_EQ(actor.specials.carry_items, 1);
    EXPECT_EQ(self.specials.carry_items, 0);
    EXPECT_STREQ(self_descriptor.output, "");
    EXPECT_STREQ(actor_descriptor.output, "");
}

TEST(JsTriggerDispatch, CommandHelpersResolvePolymorphicCharacterAndRoomTargetsByKind) {
    DescriptorListGuard descriptor_guard;
    ObjectPrototypeGuard object_guard(6207);
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5868, "function onEnter(ctx) {\n"
              "  const tell = RotS.Script.send_to_char(ctx.target, 'Target notice.');\n"
              "  const room = RotS.Script.send_to_room(ctx.targ2, 'Room notice.');\n"
              "  const load = RotS.Script.load_obj(6207, ctx.targ2);\n"
              "  const give = RotS.Script.do_give(ctx.target, ctx.targ1, ctx.object);\n"
              "  return tell.ok && room.ok && load.ok && give.ok;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data giver = make_character("Target Giver");
    char_data recipient = make_character("Target Recipient");
    char_data observer = make_character("Observer");
    giver.abs_number = 7001;
    recipient.abs_number = 7002;
    observer.in_room = 1;
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &giver;
    giver.carrying = &token;
    giver.specials.carry_items = 1;
    giver.specials.carry_weight = 1;
    descriptor_data giver_descriptor{};
    descriptor_data recipient_descriptor{};
    descriptor_data observer_descriptor{};
    attach_descriptor(giver_descriptor, giver);
    attach_descriptor(recipient_descriptor, recipient);
    attach_descriptor(observer_descriptor, observer);
    giver_descriptor.next = &recipient_descriptor;
    recipient_descriptor.next = &observer_descriptor;
    observer_descriptor.next = nullptr;
    descriptor_list = &giver_descriptor;

    const char_data *live_characters[] = {&self, &giver, &recipient, &observer};
    const obj_data *live_objects[] = {&token};
    room_data world[2] = {make_room("Gate", 100, 0), make_room("Hall", 101, 0)};
    world[0].people = &self;
    world[1].people = &observer;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 4, live_objects, 1, world, 1, obj_index, 1, zones, 1);
    target_data targ1{};
    targ1.type = TARGET_CHAR;
    targ1.ptr.ch = &recipient;
    targ1.ch_num = GET_ABS_NUM(&recipient);
    target_data targ2{};
    targ2.type = TARGET_ROOM;
    targ2.ptr.room = &world[1];
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.target_character = &giver;
    request.context_input.targ1 = &targ1;
    request.context_input.targ2 = &targ2;
    request.context_input.object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_TRUE(contains(giver_descriptor.output, "Target notice.\n\r"));
    EXPECT_FALSE(contains(recipient_descriptor.output, "Target notice."));
    EXPECT_TRUE(contains(observer_descriptor.output, "Room notice.\n\r"));
    EXPECT_NE(object_list, nullptr);
    EXPECT_EQ(object_list->item_number, 0);
    EXPECT_EQ(object_list->in_room, 1);
    EXPECT_EQ(obj_index[0].number, 1);
    EXPECT_EQ(giver.carrying, nullptr);
    EXPECT_EQ(recipient.carrying, &token);
    EXPECT_EQ(token.carried_by, &recipient);
    EXPECT_EQ(giver.specials.carry_items, 0);
    EXPECT_EQ(recipient.specials.carry_items, 1);
}

TEST(JsTriggerDispatch, CommandHelpersResolvePolymorphicObjectTargetsByKind) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5869, "function onEnter(ctx) {\n"
              "  return RotS.Script.do_give(ctx.actor, ctx.self, ctx.target).ok;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    obj_data token = make_object("quest token", 0);
    token.in_room = NOWHERE;
    token.carried_by = &actor;
    actor.carrying = &token;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&token};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.target_object = &token;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    add_accepting_command_audit(dispatch_options);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(self.carrying, &token);
    EXPECT_EQ(token.carried_by, &self);
    EXPECT_EQ(actor.specials.carry_items, 0);
    EXPECT_EQ(self.specials.carry_items, 1);
}

TEST(JsTriggerDispatch, ObjectCommandHelpersRejectForgedLoadTargetsWithoutCreatingObjects) {
    ObjectPrototypeGuard object_guard(6201);
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation forged = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6201,\"loadTargetId\":\"mob:999\"}");

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction({forged}, request, adapter_options,
                                                               test_mutation_authority());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
}

TEST(JsTriggerDispatch, DoWaitCommandHelperAppliesBoundedWaitStateToLiveHost) {
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5856, "function onEnter(ctx) {\n"
                                           "  return RotS.Script.do_wait(4).ok;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 4);
    EXPECT_EQ(self.delay.cmd, 0);
    EXPECT_NE(self.delay.cmd, CMD_SCRIPT);
    EXPECT_EQ(self.delay.subcmd, 0);
    EXPECT_EQ(self.delay.priority, 50);
    EXPECT_EQ(self.delay.targ1.type, TARGET_IGNORE);
    EXPECT_EQ(self.delay.targ2.type, TARGET_IGNORE);
    EXPECT_EQ(self.delay.next, nullptr);
    EXPECT_EQ(waiting_list, &self);
}

TEST(JsTriggerDispatch, DoWaitInlineAuditRejectedResultDoesNotQueueWait) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    descriptor_list = nullptr;
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5891, "function onEnter(ctx) {\n"
              "  const wait = RotS.Script.doWait(4);\n"
              "  if (!wait.ok && wait.code === 'audit-rejected') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'The wait is not approved.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    std::vector<std::string> audit_operations;
    dispatch_options.helper_mutation_options.command_audit_user_data = &audit_operations;
    dispatch_options.helper_mutation_options.command_audit_callback =
        [](const JsTriggerCommandMutationAuditRequest &request, std::string *diagnostic,
           void *user_data) {
            static_cast<std::vector<std::string> *>(user_data)->push_back(
                request.operations_summary);
            if (request.operations_summary == "script.do_wait") {
                if (diagnostic != nullptr)
                    *diagnostic = "private audit detail";
                return false;
            }
            return true;
        };

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_operations,
              (std::vector<std::string>{"script.do_wait", "script.send_to_char"}));
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_EQ(waiting_list, nullptr);
    EXPECT_TRUE(contains(self_descriptor.output, "The wait is not approved.\n\r"));
    EXPECT_FALSE(contains(self_descriptor.output, "private audit detail"));
}

TEST(JsTriggerDispatch, DoWaitInlineNotAuthorizedResultDoesNotQueueWait) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    descriptor_list = nullptr;
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5893, "function onEnter(ctx) {\n"
              "  const wait = RotS.Script.doWait(4);\n"
              "  if (!wait.ok && wait.code === 'not-authorized') {\n"
              "    RotS.Script.sendToChar(ctx.self, 'The wait is not authorized.');\n"
              "  }\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_EQ(waiting_list, nullptr);
    EXPECT_TRUE(contains(self_descriptor.output, "The wait is not authorized.\n\r"));
}

TEST(JsTriggerDispatch, DoWaitInlineAlreadyWaitingResultDoesNotQueueWait) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data next = make_character("Next");
    SET_BIT(self.specials.affected_by, AFF_WAITING);
    self.delay.wait_value = 7;
    self.delay.cmd = CMD_HIDE;
    self.delay.subcmd = 1;
    self.delay.priority = 30;
    self.delay.targ1.type = TARGET_CHAR;
    self.delay.targ1.ch_num = 99;
    self.delay.targ2.type = TARGET_TEXT;
    self.delay.next = &next;
    waiting_list = &self;
    descriptor_data self_descriptor{};
    attach_descriptor(self_descriptor, self);
    descriptor_list = &self_descriptor;
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5892, "function onEnter(ctx) {\n"
                                           "  const wait = RotS.Script.doWait(4);\n"
                                           "  return !wait.ok && wait.code === 'already-waiting';\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 0);
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 7);
    EXPECT_EQ(self.delay.cmd, CMD_HIDE);
    EXPECT_EQ(self.delay.subcmd, 1);
    EXPECT_EQ(self.delay.priority, 30);
    EXPECT_EQ(self.delay.targ1.type, TARGET_CHAR);
    EXPECT_EQ(self.delay.targ1.ch_num, 99);
    EXPECT_EQ(self.delay.targ2.type, TARGET_TEXT);
    EXPECT_EQ(self.delay.next, &next);
    EXPECT_EQ(waiting_list, &self);
}

TEST(JsTriggerDispatch, DoWaitInlineSecondWaitReturnsAlreadyWaitingWithoutQueuing) {
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5894, "function onEnter() {\n"
              "  const first = RotS.Script.doWait(3);\n"
              "  const second = RotS.Script.doWait(4);\n"
              "  return first.ok && !second.ok && second.code === 'already-waiting';\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();
    int audit_calls = 0;
    add_accepting_command_audit(dispatch_options, &audit_calls);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(audit_calls, 1);
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 3);
    EXPECT_EQ(waiting_list, &self);
}

TEST(JsTriggerDispatch, DoWaitCommandHelperRequiresAuthorityWithoutChangingHost) {
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":4}");

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction({wait}, request, adapter_options,
                                                               {});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_EQ(waiting_list, nullptr);
}

TEST(JsTriggerDispatch, DoWaitCommandHelperRejectsDuplicateWaitsWithoutPartialState) {
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation first_wait =
        make_script_command_mutation("script.do_wait", "{\"pulses\":4}");
    const JsRuntimeMutation second_wait =
        make_script_command_mutation("script.do_wait", "{\"pulses\":5}");
    JsTriggerHelperMutationTransactionOptions helper_options;
    add_accepting_command_audit(helper_options);

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {first_wait, second_wait}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_EQ(waiting_list, nullptr);
}

TEST(JsTriggerDispatch, DoWaitCommandHelperRejectsStaleInvalidAndWrongZoneHosts) {
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":4}");

    {
        WaitingListGuard wait_guard;
        waiting_list = nullptr;
        char_data stale_self = make_character("Stale");
        char_data live_other = make_character("Live");
        const char_data *live_characters[] = {&live_other};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions adapter_options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&stale_self);

        JsTriggerRuntimeMutationTransactionApplyResult result =
            js_trigger_dispatch_apply_runtime_mutation_transaction({wait}, request, adapter_options,
                                                                   test_mutation_authority());

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
        EXPECT_FALSE(IS_SET(stale_self.specials.affected_by, AFF_WAITING));
        EXPECT_EQ(stale_self.delay.wait_value, 0);
        EXPECT_EQ(waiting_list, nullptr);
    }

    {
        WaitingListGuard wait_guard;
        waiting_list = nullptr;
        char_data self = make_character("Self");
        self.in_room = NOWHERE;
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions adapter_options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);

        JsTriggerRuntimeMutationTransactionApplyResult result =
            js_trigger_dispatch_apply_runtime_mutation_transaction({wait}, request, adapter_options,
                                                                   test_mutation_authority());

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
        EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
        EXPECT_EQ(self.delay.wait_value, 0);
        EXPECT_EQ(waiting_list, nullptr);
    }

    {
        WaitingListGuard wait_guard;
        waiting_list = nullptr;
        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions adapter_options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);

        JsTriggerRuntimeMutationTransactionApplyResult result =
            js_trigger_dispatch_apply_runtime_mutation_transaction({wait}, request, adapter_options,
                                                                   test_mutation_authority(31));

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
        EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
        EXPECT_EQ(self.delay.wait_value, 0);
        EXPECT_EQ(waiting_list, nullptr);
    }
}

TEST(JsTriggerDispatch, DoWaitCommandHelperLeavesAlreadyWaitingHostUnchanged) {
    WaitingListGuard wait_guard;
    char_data self = make_character("Self");
    char_data next = make_character("Next");
    SET_BIT(self.specials.affected_by, AFF_WAITING);
    self.delay.wait_value = 7;
    self.delay.cmd = CMD_HIDE;
    self.delay.subcmd = 1;
    self.delay.priority = 30;
    self.delay.targ1.type = TARGET_CHAR;
    self.delay.targ1.ch_num = 99;
    self.delay.targ2.type = TARGET_TEXT;
    self.delay.next = &next;
    waiting_list = &self;
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":4}");
    JsTriggerHelperMutationTransactionOptions helper_options;
    add_accepting_command_audit(helper_options);

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {wait}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command target rejected");
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 7);
    EXPECT_EQ(self.delay.cmd, CMD_HIDE);
    EXPECT_EQ(self.delay.subcmd, 1);
    EXPECT_EQ(self.delay.priority, 30);
    EXPECT_EQ(self.delay.targ1.type, TARGET_CHAR);
    EXPECT_EQ(self.delay.targ1.ch_num, 99);
    EXPECT_EQ(self.delay.targ2.type, TARGET_TEXT);
    EXPECT_EQ(self.delay.next, &next);
    EXPECT_EQ(waiting_list, &self);
}

TEST(JsTriggerDispatch, BridgeAcceptedDoWaitFailsClosedWhenHostStartsWaitingBeforeApply) {
    WaitingListGuard wait_guard;
    waiting_list = nullptr;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Original Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":4}");
    wait.command_result_bridge_accepted = true;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *) { return true; };
    helper_options.apply_precondition_user_data = &self;
    helper_options.apply_precondition_callback = [](std::size_t, void *user_data) {
        char_data *self = static_cast<char_data *>(user_data);
        SET_BIT(self->specials.affected_by, AFF_WAITING);
        self->delay.wait_value = 7;
        self->delay.cmd = CMD_HIDE;
        self->delay.subcmd = 1;
        self->delay.priority = 30;
        self->delay.targ1.type = TARGET_CHAR;
        self->delay.targ1.ch_num = 99;
        self->delay.targ2.type = TARGET_TEXT;
        self->delay.next = nullptr;
        waiting_list = self;
        return true;
    };

    JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, wait}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger wait command apply rejected");
    EXPECT_STREQ(zones[0].name, "Original Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 7);
    EXPECT_EQ(self.delay.cmd, CMD_HIDE);
    EXPECT_EQ(self.delay.subcmd, 1);
    EXPECT_EQ(self.delay.priority, 30);
    EXPECT_EQ(self.delay.targ1.type, TARGET_CHAR);
    EXPECT_EQ(self.delay.targ1.ch_num, 99);
    EXPECT_EQ(self.delay.targ2.type, TARGET_TEXT);
    EXPECT_EQ(self.delay.next, nullptr);
    EXPECT_EQ(waiting_list, &self);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchAppliesAllValidatedSideEffects) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6202);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    add_accepting_command_audit(helper_options);

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6202,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, load, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_TRUE(result.ok) << result.diagnostic;
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.applied_setter_count, 1U);
    EXPECT_EQ(result.applied_helper_count, 1U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_STREQ(zones[0].name, "Changed Zone");
    EXPECT_TRUE(IS_SET(world[0].room_flags, DARK));
    ASSERT_NE(actor.carrying, nullptr);
    EXPECT_EQ(actor.carrying->item_number, 0);
    EXPECT_TRUE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 3);
    EXPECT_TRUE(contains(actor_descriptor.output, "Batch complete.\n\r"));
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchRejectsCommandAuditWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6205);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &request,
                                               std::string *diagnostic, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        EXPECT_EQ(request.operations_summary, "script.do_wait,script.load_obj,script.send_to_char");
        if (diagnostic != nullptr)
            *diagnostic = "command audit denied";
        return false;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6205,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, load, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "command audit denied");
    EXPECT_EQ(command_audit_calls, 1);
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchRejectsObjectCommandWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6203);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation forged_load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6203,\"loadTargetId\":\"mob:999\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, forged_load, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch,
     MixedCommandHelperBatchRejectsWrongKindPolymorphicTargetWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6208);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    target_data room_target{};
    room_target.type = TARGET_ROOM;
    room_target.ptr.room = &world[0];
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.targ1 = &room_target;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation wrong_kind_tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"targ1\",\"text\":\"Batch complete.\"}");
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6208,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, wrong_kind_tell, load, wait}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger output command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch,
     MixedCommandHelperBatchRejectsStaleExplicitPolymorphicTargetWithoutFallback) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6209);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    char_data stale_target = make_character("Stale Target");
    char_data fallback_target = make_character("Fallback Target");
    stale_target.abs_number = 9001;
    fallback_target.abs_number = 9002;
    descriptor_data fallback_descriptor{};
    attach_descriptor(fallback_descriptor, fallback_target);
    descriptor_list = &fallback_descriptor;
    const char_data *live_characters[] = {&self, &actor, &fallback_target};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 3, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    target_data live_targ1{};
    live_targ1.type = TARGET_CHAR;
    live_targ1.ptr.ch = &fallback_target;
    live_targ1.ch_num = GET_ABS_NUM(&fallback_target);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.target_character = &stale_target;
    request.context_input.targ1 = &live_targ1;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation stale_tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"target\",\"text\":\"Batch complete.\"}");
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6209,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, stale_tell, load, wait}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger output command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(fallback_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch,
     MixedCommandHelperBatchRejectsWrongKindObjectCommandPolymorphicTargetWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6210);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    obj_data object_target = make_object("Target Object", 0);
    target_data object_targ2{};
    object_targ2.type = TARGET_OBJ;
    object_targ2.ptr.obj = &object_target;
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.targ2 = &object_targ2;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation wrong_kind_load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6210,\"loadTargetId\":\"targ2\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, wrong_kind_load, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchRejectsStaleObjectPolymorphicTargetWithoutFallback) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6211);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    obj_data stale_object = make_object("stale token", 0);
    obj_data fallback_object = make_object("fallback token", 0);
    stale_object.in_room = NOWHERE;
    stale_object.carried_by = &actor;
    fallback_object.in_room = NOWHERE;
    fallback_object.carried_by = &actor;
    actor.carrying = &fallback_object;
    actor.specials.carry_items = 1;
    actor.specials.carry_weight = 1;
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    const obj_data *live_objects[] = {&fallback_object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, live_objects, 1, world, 0, obj_index, 1, zones, 1);
    target_data live_targ2{};
    live_targ2.type = TARGET_OBJ;
    live_targ2.ptr.obj = &fallback_object;
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.target_object = &stale_object;
    request.context_input.targ2 = &live_targ2;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation stale_give = make_script_command_mutation(
        "script.do_give",
        "{\"giverId\":\"actor\",\"recipientId\":\"self\",\"objectId\":\"target\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, stale_give, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, &fallback_object);
    EXPECT_EQ(self.carrying, nullptr);
    EXPECT_EQ(fallback_object.carried_by, &actor);
    EXPECT_EQ(actor.specials.carry_items, 1);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch,
     MixedCommandHelperBatchRejectsDetachedRoomPolymorphicOutputWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6212);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    room_data detached_room = make_room("Detached", 999, 0);
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    target_data detached_targ1{};
    detached_targ1.type = TARGET_ROOM;
    detached_targ1.ptr.room = &detached_room;
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.targ1 = &detached_targ1;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation detached_room_output = make_script_command_mutation(
        "script.send_to_room", "{\"roomId\":\"targ1\",\"text\":\"Batch complete.\"}");
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6212,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, detached_room_output, load, wait}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger output command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch,
     MixedCommandHelperBatchRejectsDetachedRoomPolymorphicLoadWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6213);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    room_data detached_room = make_room("Detached", 999, 0);
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    target_data detached_targ1{};
    detached_targ1.type = TARGET_ROOM;
    detached_targ1.ptr.room = &detached_room;
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;
    request.context_input.targ1 = &detached_targ1;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.command_audit_user_data = &command_audit_calls;
    helper_options.command_audit_callback = [](const JsTriggerCommandMutationAuditRequest &,
                                               std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation detached_room_load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6213,\"loadTargetId\":\"targ1\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, room_flag, detached_room_load, wait, tell}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger object command target rejected");
    EXPECT_EQ(helper_audit_calls, 0);
    EXPECT_EQ(command_audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchRejectsHelperPreconditionWithoutPartialWrites) {
    DescriptorListGuard descriptor_guard;
    WaitingListGuard wait_guard;
    ObjectPrototypeGuard object_guard(6204);
    waiting_list = nullptr;
    descriptor_list = nullptr;
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    descriptor_data actor_descriptor{};
    attach_descriptor(actor_descriptor, actor);
    descriptor_list = &actor_descriptor;
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.apply_precondition_user_data = &apply_precondition_calls;
    helper_options.apply_precondition_callback = [](std::size_t mutation_index, void *user_data) {
        ++*static_cast<int *>(user_data);
        return mutation_index == 0;
    };
    add_accepting_command_audit(helper_options);

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation add_peace = make_helper_mutation("room.flags.add");
    add_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation remove_dark = make_helper_mutation("room.flags.remove");
    remove_dark.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6204,\"loadTargetId\":\"actor\"}");
    const JsRuntimeMutation wait = make_script_command_mutation("script.do_wait", "{\"pulses\":3}");
    const JsRuntimeMutation tell = make_script_command_mutation(
        "script.send_to_char", "{\"targetId\":\"actor\",\"text\":\"Batch complete.\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, add_peace, load, wait, tell, remove_dark}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation apply rejected");
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(apply_precondition_calls, 2);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, DARK);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_FALSE(IS_SET(self.specials.affected_by, AFF_WAITING));
    EXPECT_EQ(self.delay.wait_value, 0);
    EXPECT_STREQ(actor_descriptor.output, "");
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedCommandHelperBatchRollsBackLoadedObjectsWhenRoomFlagApplyFails) {
    ObjectPrototypeGuard object_guard(6214);
    char_data self = make_character("Self");
    char_data actor = make_character("Actor");
    const char_data *live_characters[] = {&self, &actor};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 2, nullptr, 0, world, 0, obj_index, 1, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.actor = &actor;

    int helper_audit_calls = 0;
    int command_audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &helper_audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    add_accepting_command_audit(helper_options, &command_audit_calls);
    struct ApplyFailureProbe {
        room_data *room = nullptr;
        int *calls = nullptr;
    } probe{&world[0], &apply_precondition_calls};
    helper_options.apply_precondition_user_data = &probe;
    helper_options.apply_precondition_callback = [](std::size_t, void *user_data) {
        ApplyFailureProbe *probe = static_cast<ApplyFailureProbe *>(user_data);
        ++*probe->calls;
        probe->room->number = 101;
        return true;
    };

    JsRuntimeMutation room_flag = make_helper_mutation("room.flags.add");
    room_flag.arguments_json = "{\"flag\":\"dark\"}";
    const JsRuntimeMutation load = make_script_command_mutation(
        "script.load_obj", "{\"vnum\":6214,\"loadTargetId\":\"actor\"}");

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {load, room_flag}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation apply rejected");
    EXPECT_EQ(helper_audit_calls, 1);
    EXPECT_EQ(command_audit_calls, 1);
    EXPECT_EQ(apply_precondition_calls, 1);
    EXPECT_EQ(actor.carrying, nullptr);
    EXPECT_EQ(object_list, nullptr);
    EXPECT_EQ(obj_index[0].number, 0);
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(world[0].number, 101);
}

TEST(JsTriggerDispatch, HelperTransactionRejectsUnsupportedEnvelopesBeforeAudit) {
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter;
    setter.kind = "setter";
    setter.target_type = "room";
    setter.target_id = "room";
    setter.property = "level";
    setter.value_kind = "number";
    setter.has_value = true;
    setter.value = "10";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({setter}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope);
    EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.status),
                 "unsupported-envelope");
    EXPECT_EQ(result.mutation_count, 0U);
    EXPECT_EQ(audit_calls, 0);
    EXPECT_EQ(result.diagnostic, "JavaScript helper mutation envelope rejected");

    JsRuntimeMutation missing_token;
    missing_token.kind = "helper";
    missing_token.operation = "room.flags.add";
    missing_token.arguments_json = "{\"flag\":\"peace\"}";
    const JsTriggerHelperMutationTransactionResult malformed =
        js_trigger_dispatch_prepare_helper_mutation_transaction({missing_token}, options);
    EXPECT_EQ(malformed.status, JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope);
    EXPECT_EQ(malformed.mutation_count, 0U);
    EXPECT_EQ(audit_calls, 0);
    EXPECT_EQ(malformed.diagnostic.find("fixture-token"), std::string::npos);
}

TEST(JsTriggerDispatch, HelperTransactionRejectsUnknownOperationsBeforeAudit) {
    const char *const allowed_operations[] = {"room.flags.add"};
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry.operation_names = allowed_operations;
    options.registry.operation_count = 1;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation helper;
    helper.kind = "helper";
    helper.operation = "setFlags";
    helper.target_token = "fixture-token";
    helper.arguments_json = "{\"flag\":\"peace\"}";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::UnknownOperation);
    EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.status),
                 "unknown-operation");
    EXPECT_EQ(result.mutation_count, 0U);
    EXPECT_EQ(audit_calls, 0);
    EXPECT_EQ(result.diagnostic, "JavaScript helper mutation operation is not supported");
}

TEST(JsTriggerDispatch, RoomFlagHelperRegistryMatchesServerCatalog) {
    const JsTriggerHelperMutationOperationRegistry registry =
        js_trigger_dispatch_room_flag_helper_operation_registry();
    ASSERT_NE(registry.operation_names, nullptr);
    ASSERT_EQ(registry.operation_count, js_api_room_flag_helper_operation_count());
    ASSERT_EQ(registry.operation_count, 2U);

    for (std::size_t index = 0; index < registry.operation_count; ++index) {
        ASSERT_STREQ(registry.operation_names[index],
                     js_api_room_flag_helper_operations()[index].operation_name);
    }
    EXPECT_STREQ(registry.operation_names[0], "room.flags.add");
    EXPECT_STREQ(registry.operation_names[1], "room.flags.remove");
}

TEST(JsTriggerDispatch, RoomFlagHelperDispatchAcceptedFlagsMatchServerPolicyCatalog) {
    const std::vector<std::string> dispatch_flags = parse_dispatch_room_flag_helper_names();
    ASSERT_FALSE(dispatch_flags.empty());

    for (std::size_t index = 0; index < js_api_room_flag_helper_operation_count(); ++index) {
        const JsApiRoomFlagHelperOperation &operation = js_api_room_flag_helper_operations()[index];
        SCOPED_TRACE(operation.operation_name);
        const std::vector<std::string> policy_flags = split_pipe_list(operation.allowed_flags);
        EXPECT_EQ(dispatch_flags, policy_flags);

        const std::vector<std::string> builder_zone_flags =
            split_pipe_list(operation.builder_zone_flags);
        const std::vector<std::string> admin_only_flags =
            split_pipe_list(operation.admin_only_flags);
        std::set<std::string> authority_classified_flags;
        for (const std::string &flag : builder_zone_flags)
            EXPECT_TRUE(authority_classified_flags.insert(flag).second) << flag;
        for (const std::string &flag : admin_only_flags)
            EXPECT_TRUE(authority_classified_flags.insert(flag).second) << flag;
        EXPECT_EQ(authority_classified_flags,
                  std::set<std::string>(dispatch_flags.begin(), dispatch_flags.end()));
    }
}

TEST(JsTriggerDispatch, RoomFlagHelperRegistryAcceptsCatalogOperationsThroughTransaction) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;

    int audit_calls = 0;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 2U);
        EXPECT_EQ(request.operations_summary, "room.flags.add,room.flags.remove");
        return true;
    };

    JsRuntimeMutation add_flag;
    add_flag.kind = "helper";
    add_flag.operation = js_api_room_flag_helper_operations()[0].operation_name;
    add_flag.target_token = "room-token:v1:30:100:test-room-target-secret";
    add_flag.arguments_json = "{\"flag\":\"peaceRoom\"}";

    JsRuntimeMutation remove_flag = add_flag;
    remove_flag.operation = js_api_room_flag_helper_operations()[1].operation_name;
    remove_flag.arguments_json = "{\"flag\":\"dark\"}";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({add_flag, remove_flag}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.mutation_count, 2U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsTriggerDispatch, RoomFlagHelperRegistryUsesSortedUniqueAuditSummary) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;

    int audit_calls = 0;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        EXPECT_EQ(request.operations_summary, "room.flags.add,room.flags.remove");
        return true;
    };

    JsRuntimeMutation add_flag;
    add_flag.kind = "helper";
    add_flag.operation = "room.flags.add";
    add_flag.target_token = "room-token:v1:30:100:test-room-target-secret";
    add_flag.arguments_json = "{\"flag\":\"peaceRoom\"}";

    JsRuntimeMutation remove_flag = add_flag;
    remove_flag.operation = "room.flags.remove";
    remove_flag.arguments_json = "{\"flag\":\"dark\"}";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({remove_flag, add_flag, add_flag},
                                                                options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.mutation_count, 3U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsTriggerDispatch, RoomFlagHelperRegistryRejectsRawOrUnknownOperationsBeforeAudit) {
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();

    int audit_calls = 0;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    for (const char *operation : {"room.flags.setRaw", "room.flags.replace", "setFlags",
                                  "Room.addFlag", "room.flags.permanentAffect", "__proto__",
                                  "constructor", "toString", "   ", "room.flags.add "}) {
        JsRuntimeMutation helper;
        helper.kind = "helper";
        helper.operation = operation;
        helper.target_token = "opaque-room-token:30:3001";
        helper.arguments_json = "{\"flag\":\"dark\"}";

        const JsTriggerHelperMutationTransactionResult result =
            js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

        EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::UnknownOperation)
            << operation;
        EXPECT_EQ(result.mutation_count, 0U) << operation;
        EXPECT_EQ(result.diagnostic, "JavaScript helper mutation operation is not supported")
            << operation;
    }

    JsRuntimeMutation empty_operation;
    empty_operation.kind = "helper";
    empty_operation.target_token = "opaque-room-token:30:3001";
    empty_operation.arguments_json = "{\"flag\":\"dark\"}";

    const JsTriggerHelperMutationTransactionResult empty_operation_result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({empty_operation}, options);

    EXPECT_EQ(empty_operation_result.status,
              JsTriggerHelperMutationTransactionStatus::UnsupportedEnvelope);
    EXPECT_EQ(empty_operation_result.mutation_count, 0U);
    EXPECT_EQ(empty_operation_result.diagnostic, "JavaScript helper mutation envelope rejected");
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsMissingContextBeforeAudit) {
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction(
            {make_helper_mutation("room.flags.add")}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidTarget);
    EXPECT_EQ(result.mutation_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript helper mutation target rejected");
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsNullContextMembersBeforeAudit) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    enum class NullContextMember {
        Request,
        AdapterOptions,
        Authority,
    };
    const NullContextMember null_members[] = {NullContextMember::Request,
                                              NullContextMember::AdapterOptions,
                                              NullContextMember::Authority};

    for (NullContextMember null_member : null_members) {
        JsTriggerHelperMutationValidationContext validation_context;
        validation_context.request = &request;
        validation_context.adapter_options = &adapter_options;
        validation_context.authority = &authority;
        switch (null_member) {
        case NullContextMember::Request:
            validation_context.request = nullptr;
            break;
        case NullContextMember::AdapterOptions:
            validation_context.adapter_options = nullptr;
            break;
        case NullContextMember::Authority:
            validation_context.authority = nullptr;
            break;
        }

        int audit_calls = 0;
        JsTriggerHelperMutationTransactionOptions options;
        options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
        options.validation_context = &validation_context;
        options.audit_user_data = &audit_calls;
        options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                    void *user_data) {
            ++*static_cast<int *>(user_data);
            return true;
        };

        const JsTriggerHelperMutationTransactionResult result =
            js_trigger_dispatch_prepare_helper_mutation_transaction(
                {make_helper_mutation("room.flags.add")}, options);

        EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidTarget);
        EXPECT_EQ(result.mutation_count, 0U);
        EXPECT_EQ(result.diagnostic, "JavaScript helper mutation target rejected");
        EXPECT_EQ(audit_calls, 0);
    }
}

TEST(JsTriggerDispatch, RoomFlagHelperRegistryWorksThroughMixedTransactionProbe) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                       std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 1U);
        EXPECT_EQ(request.operations_summary, "room.flags.add");
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
    helper.target_token = "room-token:v1:30:100:test-room-target-secret";
    helper.arguments_json = "{\"flag\":\"peaceRoom\"}";

    JsTriggerRuntimeMutationTransactionProbeResult result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {setter, helper}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_TRUE(result.ok) << result.diagnostic;
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.prepared_setter_count, 1U);
    EXPECT_EQ(result.prepared_helper_count, 1U);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(audit_calls, 1);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyMutatesFlagsAfterAuditAndPreparedSetters) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = PEACEROOM;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                       std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        EXPECT_EQ(request.operations_summary, "room.flags.add,room.flags.remove");
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation add_dark = make_helper_mutation("room.flags.add");
    add_dark.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation add_peace = make_helper_mutation("room.flags.add");
    add_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation remove_peace = make_helper_mutation("room.flags.remove");
    remove_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, add_dark, add_peace, remove_peace}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_TRUE(result.ok) << result.diagnostic;
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.applied_setter_count, 1U);
    EXPECT_EQ(result.applied_helper_count, 3U);
    EXPECT_EQ(audit_calls, 1);
    ASSERT_NE(zones[0].name, nullptr);
    EXPECT_STREQ(zones[0].name, "Changed Zone");
    EXPECT_TRUE(IS_SET(world[0].room_flags, DARK));
    EXPECT_FALSE(IS_SET(world[0].room_flags, PEACEROOM));
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyKeepsSettersAndFlagsUnchangedWhenAuditRejects) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return false;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
    helper.arguments_json = "{\"flag\":\"peaceRoom\"}";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, helper}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation rejected");
    EXPECT_EQ(audit_calls, 1);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, DARK);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyRollsBackFlagsAndSkipsSettersWhenApplyFails) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };
    helper_options.apply_precondition_user_data = &apply_precondition_calls;
    helper_options.apply_precondition_callback = [](std::size_t mutation_index, void *user_data) {
        ++*static_cast<int *>(user_data);
        return mutation_index == 0;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation add_peace = make_helper_mutation("room.flags.add");
    add_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation remove_dark = make_helper_mutation("room.flags.remove");
    remove_dark.arguments_json = "{\"flag\":\"dark\"}";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, add_peace, remove_dark}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.helper_status),
                 "apply-rejected");
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation apply rejected");
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(apply_precondition_calls, 2);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, DARK);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyRollsBackMultipleRoomsWhenApplyFails) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[2] = {make_room("Gate", 100, 0), make_room("Hall", 101, 0)};
    world[0].room_flags = 0;
    world[1].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 1, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                       std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        return true;
    };
    helper_options.apply_precondition_user_data = &apply_precondition_calls;
    helper_options.apply_precondition_callback = [](std::size_t mutation_index, void *user_data) {
        ++*static_cast<int *>(user_data);
        return mutation_index < 2;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation add_peace_to_gate = make_helper_mutation("room.flags.add");
    add_peace_to_gate.target_token = "room-token:v1:30:100:test-room-target-secret";
    add_peace_to_gate.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation remove_dark_from_hall = make_helper_mutation("room.flags.remove");
    remove_dark_from_hall.target_token = "room-token:v1:30:101:test-room-target-secret";
    remove_dark_from_hall.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation add_magic_to_gate = make_helper_mutation("room.flags.add");
    add_magic_to_gate.target_token = "room-token:v1:30:100:test-room-target-secret";
    add_magic_to_gate.arguments_json = "{\"flag\":\"noMagic\"}";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, add_peace_to_gate, remove_dark_from_hall, add_magic_to_gate}, request,
            adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(apply_precondition_calls, 3);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    EXPECT_EQ(world[1].room_flags, DARK);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyRollsBackMultipleStepsOnSameRoomWhenApplyFails) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    int apply_precondition_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                       std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        return true;
    };
    helper_options.apply_precondition_user_data = &apply_precondition_calls;
    helper_options.apply_precondition_callback = [](std::size_t mutation_index, void *user_data) {
        ++*static_cast<int *>(user_data);
        return mutation_index < 2;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation add_peace = make_helper_mutation("room.flags.add");
    add_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation remove_dark = make_helper_mutation("room.flags.remove");
    remove_dark.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation add_magic = make_helper_mutation("room.flags.add");
    add_magic.arguments_json = "{\"flag\":\"noMagic\"}";

    int probe_audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions probe_options;
    probe_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    probe_options.audit_user_data = &probe_audit_calls;
    probe_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                      std::string *, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 3U);
        return true;
    };

    const JsTriggerRuntimeMutationTransactionProbeResult probe_result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {setter, add_peace, remove_dark, add_magic}, request, adapter_options,
            test_mutation_authority(), probe_options);
    EXPECT_TRUE(probe_result.ok);
    EXPECT_EQ(probe_result.prepared_setter_count, 1U);
    EXPECT_EQ(probe_result.prepared_helper_count, 3U);
    EXPECT_EQ(probe_result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(probe_audit_calls, 1);

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, add_peace, remove_dark, add_magic}, request, adapter_options,
            test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(apply_precondition_calls, 3);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, DARK);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperApplyRejectsInvalidHelpersBeforeAnyWrite) {
    for (const char *bad_arguments :
         {"{\"flag\":\"BFS_MARK\"}", "{\"flag\":\"permanentAffect\"}"}) {
        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        world[0].room_flags = DARK;
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].name = str_dup("Zone");
        JsGameAdapterOptions adapter_options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);

        int audit_calls = 0;
        JsTriggerHelperMutationTransactionOptions helper_options;
        helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
        helper_options.audit_user_data = &audit_calls;
        helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &,
                                           std::string *, void *user_data) {
            ++*static_cast<int *>(user_data);
            return true;
        };

        JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
        JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
        helper.arguments_json = bad_arguments;

        const JsTriggerRuntimeMutationTransactionApplyResult result =
            js_trigger_dispatch_apply_runtime_mutation_transaction(
                {setter, helper}, request, adapter_options, test_mutation_authority(),
                helper_options);

        EXPECT_FALSE(result.ok) << bad_arguments;
        EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::InvalidArguments)
            << bad_arguments;
        EXPECT_EQ(result.applied_setter_count, 0U) << bad_arguments;
        EXPECT_EQ(result.applied_helper_count, 0U) << bad_arguments;
        EXPECT_EQ(audit_calls, 0) << bad_arguments;
        EXPECT_STREQ(zones[0].name, "Zone") << bad_arguments;
        EXPECT_EQ(world[0].room_flags, DARK) << bad_arguments;
        free(zones[0].name);
    }

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DARK;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation wrong_secret = make_helper_mutation("room.flags.add");
    wrong_secret.target_token = "room-token:v1:30:100:wrong-secret";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, wrong_secret}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::InvalidTarget);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(audit_calls, 0);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, DARK);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, HelperApplyFailsClosedWhenRegistryOperationHasNoApplier) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = 0;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    const char *const allowed_operations[] = {"fixture.helper.add"};
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry.operation_names = allowed_operations;
    helper_options.registry.operation_count = 1;
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation generic_helper = make_helper_mutation("fixture.helper.add");
    generic_helper.target_token = "fixture-token";
    generic_helper.arguments_json = "{\"fixture\":true}";

    const JsTriggerRuntimeMutationTransactionApplyResult result =
        js_trigger_dispatch_apply_runtime_mutation_transaction(
            {setter, generic_helper}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::ApplyRejected);
    EXPECT_EQ(result.applied_setter_count, 0U);
    EXPECT_EQ(result.applied_helper_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation apply rejected");
    EXPECT_EQ(audit_calls, 1);
    EXPECT_STREQ(zones[0].name, "Zone");
    EXPECT_EQ(world[0].room_flags, 0);
    free(zones[0].name);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsBadTargetTokensBeforeAudit) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    for (const char *token :
         {"room:100", "100", "room-token:v1:31:100:test-room-target-secret",
          "room-token:v1:30:999:test-room-target-secret", "room-token:v1:30",
          "room-token:v1:30:100", "room-token:v1:30:100:wrong-secret",
          "room-token:v1:30:100:test-room-target-secret:extra", "room-token:v1:-1:100:secret",
          "room-token:v1:9999999999:100:test-room-target-secret",
          "room-token:v1:30:9999999999:test-room-target-secret"}) {
        JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
        helper.target_token = token;

        const JsTriggerHelperMutationTransactionResult result =
            js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

        EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidTarget) << token;
        EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.status),
                     "invalid-target");
        EXPECT_EQ(result.mutation_count, 0U) << token;
        EXPECT_EQ(result.diagnostic, "JavaScript helper mutation target rejected") << token;
    }
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsForgedTokenForWrongZoneRoom) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 1)};
    zone_data zones[2] = {make_zone("Builder Zone", 30), make_zone("Other Zone", 31)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority(30);

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
    helper.target_token = "room-token:v1:30:100:test-room-target-secret";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidTarget);
    EXPECT_EQ(result.mutation_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript helper mutation target rejected");
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRequiresPersistentAuthorityBeforeAudit) {
    enum class MissingAuthorityField {
        Allow,
        BuilderAccount,
        EligibleCharacter,
        TargetZone,
        TargetTokenSecret,
        DecisionEvidence,
    };

    const MissingAuthorityField missing_fields[] = {MissingAuthorityField::Allow,
                                                    MissingAuthorityField::BuilderAccount,
                                                    MissingAuthorityField::EligibleCharacter,
                                                    MissingAuthorityField::TargetZone,
                                                    MissingAuthorityField::TargetTokenSecret,
                                                    MissingAuthorityField::DecisionEvidence};

    for (MissingAuthorityField missing_field : missing_fields) {
        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions adapter_options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerMutationAuthorityContext authority = test_mutation_authority();

        switch (missing_field) {
        case MissingAuthorityField::Allow:
            authority.allow_persistent_setter_mutations = false;
            break;
        case MissingAuthorityField::BuilderAccount:
            authority.builder_account_id.clear();
            break;
        case MissingAuthorityField::EligibleCharacter:
            authority.eligible_character_id = 0;
            break;
        case MissingAuthorityField::TargetZone:
            authority.target_zone = -1;
            break;
        case MissingAuthorityField::TargetTokenSecret:
            authority.target_token_secret.clear();
            break;
        case MissingAuthorityField::DecisionEvidence:
            authority.decision_evidence.clear();
            break;
        }

        JsTriggerHelperMutationValidationContext validation_context;
        validation_context.request = &request;
        validation_context.adapter_options = &adapter_options;
        validation_context.authority = &authority;

        int audit_calls = 0;
        JsTriggerHelperMutationTransactionOptions options;
        options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
        options.validation_context = &validation_context;
        options.audit_user_data = &audit_calls;
        options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                    void *user_data) {
            ++*static_cast<int *>(user_data);
            return true;
        };

        const JsTriggerHelperMutationTransactionResult result =
            js_trigger_dispatch_prepare_helper_mutation_transaction(
                {make_helper_mutation("room.flags.add")}, options);

        EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidTarget);
        EXPECT_EQ(result.mutation_count, 0U);
        EXPECT_EQ(result.diagnostic, "JavaScript helper mutation target rejected");
        EXPECT_EQ(audit_calls, 0);
    }
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationAcceptsBuilderZonePolicyFlags) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    const std::vector<std::string> builder_zone_flags =
        split_pipe_list(js_api_room_flag_helper_operations()[0].builder_zone_flags);
    ASSERT_EQ(builder_zone_flags.size(), 11U);

    const char *operations[] = {"room.flags.add", "room.flags.remove"};
    for (const char *operation : operations) {
        for (const std::string &flag : builder_zone_flags) {
            JsRuntimeMutation helper = make_helper_mutation(operation);
            helper.arguments_json = std::string("{\"flag\":\"") + flag + "\"}";

            const JsTriggerHelperMutationTransactionResult result =
                js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

            EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok)
                << operation << " " << flag;
            EXPECT_EQ(result.mutation_count, 1U) << operation << " " << flag;
            EXPECT_TRUE(result.diagnostic.empty()) << operation << " " << flag;
        }
    }
    EXPECT_EQ(audit_calls, 22);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsAdminOnlyFlagsWithoutOverrideBeforeAudit) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    const std::vector<std::string> admin_only_flags =
        split_pipe_list(js_api_room_flag_helper_operations()[0].admin_only_flags);
    ASSERT_EQ(admin_only_flags.size(), 5U);

    const char *operations[] = {"room.flags.add", "room.flags.remove"};
    for (const char *operation : operations) {
        for (const std::string &flag : admin_only_flags) {
            JsRuntimeMutation helper = make_helper_mutation(operation);
            helper.arguments_json = std::string("{\"flag\":\"") + flag + "\"}";

            const JsTriggerHelperMutationTransactionResult result =
                js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

            EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::AuthorityRejected)
                << operation << " " << flag;
            EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.status),
                         "authority-rejected");
            EXPECT_EQ(result.mutation_count, 0U) << operation << " " << flag;
            EXPECT_EQ(result.diagnostic, "JavaScript helper mutation authority rejected")
                << operation << " " << flag;
        }
    }
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRequiresOverrideFlagAndEvidence) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    for (const bool allow_override : {false, true}) {
        for (const char *evidence : {"", "immortal-admin-override:test"}) {
            if (allow_override && evidence[0] != '\0')
                continue;

            JsTriggerMutationAuthorityContext authority = test_mutation_authority();
            authority.allow_room_flag_admin_override = allow_override;
            authority.room_flag_admin_override_evidence = evidence;

            JsTriggerHelperMutationValidationContext validation_context;
            validation_context.request = &request;
            validation_context.adapter_options = &adapter_options;
            validation_context.authority = &authority;

            int audit_calls = 0;
            JsTriggerHelperMutationTransactionOptions options;
            options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
            options.validation_context = &validation_context;
            options.audit_user_data = &audit_calls;
            options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                        void *user_data) {
                ++*static_cast<int *>(user_data);
                return true;
            };

            JsRuntimeMutation helper = make_helper_mutation("room.flags.add");
            helper.arguments_json = "{\"flag\":\"private\"}";

            const JsTriggerHelperMutationTransactionResult result =
                js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

            EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::AuthorityRejected)
                << "allow_override=" << allow_override << " evidence=" << evidence;
            EXPECT_EQ(result.mutation_count, 0U);
            EXPECT_EQ(audit_calls, 0);
        }
    }
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationAuditsAdminOnlyFlagsWithOverrideEvidence) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = DEATH;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();
    authority.allow_room_flag_admin_override = true;
    authority.room_flag_admin_override_evidence = "immortal-admin-override:test";

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 2U);
        EXPECT_EQ(request.operations_summary, "room.flags.add,room.flags.remove");
        EXPECT_TRUE(request.requires_room_flag_admin_override);
        EXPECT_EQ(request.room_flag_admin_override_evidence, "immortal-admin-override:test");
        EXPECT_EQ(request.room_flag_authority_summary, "admin-only");
        EXPECT_EQ(
            request.room_flag_audit_summary,
            "add:private:admin-only:room=100:zoneVnum=30:zoneIndex=0:previous=false:new=true;"
            "remove:death:admin-only:room=100:zoneVnum=30:zoneIndex=0:previous=true:new=false");
        return true;
    };

    JsRuntimeMutation add_private = make_helper_mutation("room.flags.add");
    add_private.arguments_json = "{\"flag\":\"private\"}";
    JsRuntimeMutation remove_death = make_helper_mutation("room.flags.remove");
    remove_death.arguments_json = "{\"flag\":\"death\"}";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({add_private, remove_death},
                                                                options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.mutation_count, 2U);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(audit_calls, 1);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationAuditsMixedAuthorityWithStagedPreviousState) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].room_flags = PEACEROOM;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();
    authority.allow_room_flag_admin_override = true;
    authority.room_flag_admin_override_evidence = "immortal-admin-override:mixed";

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 4U);
        EXPECT_EQ(request.operations_summary, "room.flags.add,room.flags.remove");
        EXPECT_TRUE(request.requires_room_flag_admin_override);
        EXPECT_EQ(request.room_flag_admin_override_evidence, "immortal-admin-override:mixed");
        EXPECT_EQ(request.room_flag_authority_summary, "admin-only,builder-zone");
        EXPECT_EQ(
            request.room_flag_audit_summary,
            "add:dark:builder-zone:room=100:zoneVnum=30:zoneIndex=0:previous=false:new=true;"
            "remove:dark:builder-zone:room=100:zoneVnum=30:zoneIndex=0:previous=true:new=false;"
            "remove:peaceRoom:builder-zone:room=100:zoneVnum=30:zoneIndex=0:previous=true:new="
            "false;"
            "add:private:admin-only:room=100:zoneVnum=30:zoneIndex=0:previous=false:new=true");
        return true;
    };

    JsRuntimeMutation add_dark = make_helper_mutation("room.flags.add");
    add_dark.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation remove_dark = make_helper_mutation("room.flags.remove");
    remove_dark.arguments_json = "{\"flag\":\"dark\"}";
    JsRuntimeMutation remove_peace = make_helper_mutation("room.flags.remove");
    remove_peace.arguments_json = "{\"flag\":\"peaceRoom\"}";
    JsRuntimeMutation add_private = make_helper_mutation("room.flags.add");
    add_private.arguments_json = "{\"flag\":\"private\"}";

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction(
            {add_dark, remove_dark, remove_peace, add_private}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.mutation_count, 4U);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(audit_calls, 1);
}

TEST(JsTriggerDispatch, RoomFlagHelperValidationRejectsBadArgumentsBeforeAudit) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerMutationAuthorityContext authority = test_mutation_authority();

    JsTriggerHelperMutationValidationContext validation_context;
    validation_context.request = &request;
    validation_context.adapter_options = &adapter_options;
    validation_context.authority = &authority;

    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.registry = js_trigger_dispatch_room_flag_helper_operation_registry();
    options.validation_context = &validation_context;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    for (const char *arguments_json :
         {"{}", "{\"flag\":\"permanentAffect\"}", "{\"flag\":\"BFS_MARK\"}",
          "{\"flag\":\"PERMAFFECT\"}", "{\"flag\":\"peace\"}", "{\"flag\":\"Dark\"}",
          "{\"flag\":\"dark \"}", "{\"flag\":\" dark\"}", "{\"flag\":\"\"}", "{\"flag\":1}",
          "{\"flag\":null}", "{\"flag\":true}", "{\"flag\":\"dark\",\"flag\":\"death\"}",
          "{\"flag\":\"dark\",\"extra\":true}", "[]", "{not-json}",
          "{\"flag\":\"dark\",\"padding\":"
          "\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}"}) {
        JsRuntimeMutation helper = make_helper_mutation("room.flags.remove");
        helper.arguments_json = arguments_json;

        const JsTriggerHelperMutationTransactionResult result =
            js_trigger_dispatch_prepare_helper_mutation_transaction({helper}, options);

        EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::InvalidArguments)
            << arguments_json;
        EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.status),
                     "invalid-arguments");
        EXPECT_EQ(result.mutation_count, 0U) << arguments_json;
        EXPECT_EQ(result.diagnostic, "JavaScript helper mutation arguments rejected")
            << arguments_json;
    }
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, HelperTransactionRequiresAuditBeforeSuccessfulPrepare) {
    const char *const allowed_operations[] = {"fixture.helper.add", "fixture.helper.remove"};
    JsTriggerHelperMutationTransactionOptions options;
    options.registry.operation_names = allowed_operations;
    options.registry.operation_count = 2;

    JsRuntimeMutation add_flag;
    add_flag.kind = "helper";
    add_flag.operation = "fixture.helper.add";
    add_flag.target_token = "fixture-room-token";
    add_flag.arguments_json = "{\"flag\":\"peace\"}";
    JsRuntimeMutation remove_flag = add_flag;
    remove_flag.operation = "fixture.helper.remove";
    remove_flag.arguments_json = "{\"flag\":\"dark\"}";

    JsTriggerHelperMutationTransactionResult missing_audit =
        js_trigger_dispatch_prepare_helper_mutation_transaction({add_flag}, options);
    EXPECT_EQ(missing_audit.status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(missing_audit.mutation_count, 1U);
    EXPECT_EQ(missing_audit.diagnostic, "JavaScript helper mutation audit rejected");

    int audit_calls = 0;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &request,
                                std::string *diagnostic, void *user_data) {
        ++*static_cast<int *>(user_data);
        EXPECT_EQ(request.mutation_count, 2U);
        EXPECT_EQ(request.operations_summary, "fixture.helper.add,fixture.helper.remove");
        if (diagnostic != nullptr)
            *diagnostic = "backend path /tmp/secret should stay hidden";
        return false;
    };

    JsTriggerHelperMutationTransactionResult audit_rejected =
        js_trigger_dispatch_prepare_helper_mutation_transaction({add_flag, remove_flag}, options);
    EXPECT_EQ(audit_rejected.status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(audit_rejected.mutation_count, 2U);
    EXPECT_EQ(audit_calls, 1);
    EXPECT_EQ(audit_rejected.diagnostic, "JavaScript helper mutation audit rejected");
    EXPECT_EQ(audit_rejected.diagnostic.find("/tmp/secret"), std::string::npos);

    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *) { return true; };
    JsTriggerHelperMutationTransactionResult ok =
        js_trigger_dispatch_prepare_helper_mutation_transaction({add_flag, remove_flag}, options);
    EXPECT_EQ(ok.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(ok.status), "ok");
    EXPECT_EQ(ok.mutation_count, 2U);
    EXPECT_TRUE(ok.diagnostic.empty());
}

TEST(JsTriggerDispatch, HelperTransactionAllowsEmptyBatchWithoutAudit) {
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions options;
    options.audit_user_data = &audit_calls;
    options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                void *user_data) {
        ++*static_cast<int *>(user_data);
        return false;
    };

    const JsTriggerHelperMutationTransactionResult result =
        js_trigger_dispatch_prepare_helper_mutation_transaction({}, options);

    EXPECT_EQ(result.status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.mutation_count, 0U);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, MixedTransactionRejectsHelperWithoutKeepingPreparedSetters) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Original Zone");
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");

    JsTriggerRuntimeMutationTransactionProbeResult result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {setter, helper}, request, adapter_options, test_mutation_authority());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::UnknownOperation);
    EXPECT_EQ(result.prepared_setter_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation rejected");
    EXPECT_STREQ(zones[0].name, "Original Zone");

    free(zones[0].name);
}

TEST(JsTriggerDispatch, MixedTransactionSkipsHelperAuditWhenSetterValidationFails) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    const char *const allowed_operations[] = {"room.flags.add"};
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry.operation_names = allowed_operations;
    helper_options.registry.operation_count = 1;
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation invalid_setter = make_zone_name_setter("Changed Zone");
    invalid_setter.target_id = "zone:999";
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");

    JsTriggerRuntimeMutationTransactionProbeResult result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {invalid_setter, helper}, request, adapter_options, test_mutation_authority(),
            helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::NotEvaluated);
    EXPECT_STREQ(js_trigger_helper_mutation_transaction_status_name(result.helper_status),
                 "not-evaluated");
    EXPECT_EQ(result.prepared_setter_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger mutation target rejected");
    EXPECT_EQ(audit_calls, 0);
}

TEST(JsTriggerDispatch, MixedTransactionRejectsHelperAuditFailureWithoutKeepingPreparedSetters) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    const char *const allowed_operations[] = {"room.flags.add"};
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry.operation_names = allowed_operations;
    helper_options.registry.operation_count = 1;
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return false;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");

    JsTriggerRuntimeMutationTransactionProbeResult result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {setter, helper}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::AuditRejected);
    EXPECT_EQ(result.prepared_setter_count, 0U);
    EXPECT_EQ(result.diagnostic, "JavaScript trigger helper mutation rejected");
    EXPECT_EQ(audit_calls, 1);
}

TEST(JsTriggerDispatch, MixedTransactionKeepsPreparedSettersWhenHelperAuditPasses) {
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    const char *const allowed_operations[] = {"room.flags.add"};
    int audit_calls = 0;
    JsTriggerHelperMutationTransactionOptions helper_options;
    helper_options.registry.operation_names = allowed_operations;
    helper_options.registry.operation_count = 1;
    helper_options.audit_user_data = &audit_calls;
    helper_options.audit_callback = [](const JsTriggerHelperMutationAuditRequest &, std::string *,
                                       void *user_data) {
        ++*static_cast<int *>(user_data);
        return true;
    };

    JsRuntimeMutation setter = make_zone_name_setter("Changed Zone");
    JsRuntimeMutation helper = make_helper_mutation("room.flags.add");

    JsTriggerRuntimeMutationTransactionProbeResult result =
        js_trigger_dispatch_probe_runtime_mutation_transaction(
            {setter, helper}, request, adapter_options, test_mutation_authority(), helper_options);

    EXPECT_TRUE(result.ok) << result.diagnostic;
    EXPECT_EQ(result.helper_status, JsTriggerHelperMutationTransactionStatus::Ok);
    EXPECT_EQ(result.prepared_setter_count, 1U);
    EXPECT_TRUE(result.diagnostic.empty());
    EXPECT_EQ(audit_calls, 1);
}

TEST(JsTriggerDispatch, StartsWithExplicitNoMatchStatusForEmptyRegistry) {
    JsScriptPackageRegistry registry;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "no-match");
    EXPECT_EQ(result.matched_package_count, 0U);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(JsTriggerDispatch, PersistsFirstTextSettersToLiveGameRecords) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5825, "function onEnter(ctx) {\n"
              "  ctx.object.setName('lever keys');\n"
              "  ctx.object.setDescription('A brass lever has new glyphs.');\n"
              "  ctx.object.setShortDescription('a renamed lever');\n"
              "  ctx.object.setActionDescription(null);\n"
              "  ctx.object.setLevel(42);\n"
              "  ctx.object.setRarity(201);\n"
              "  ctx.room.setName('Changed Gate');\n"
              "  ctx.room.setDescription('The gate was changed by script.');\n"
              "  ctx.room.setLevel(42);\n"
              "  ctx.room.setSectorType('Water_noswim');\n"
              "  ctx.zone.setName('Changed Zone');\n"
              "  ctx.zone.setDescription(null);\n"
              "  ctx.zone.setMap('N-G-S-E');\n"
              "  ctx.zone.setSymbol('*');\n"
              "  ctx.zone.setX(25);\n"
              "  ctx.zone.setY(24);\n"
              "  ctx.zone.setResetMode(3);\n"
              "  ctx.zone.setLifespan(60);\n"
              "  ctx.zone.setLevel(42);\n"
              "  return RotS.ScriptResult.block();\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.description = str_dup("A lever is here.");
    object.short_description = str_dup("a lever");
    object.action_description = str_dup("Pulling the lever does nothing.");
    object.obj_flags.level = 5;
    object.obj_flags.rarity = 7;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].name = str_dup("Gate");
    world[0].description = str_dup("The old gate.");
    world[0].level = 5;
    world[0].sector_type = SECT_CITY;
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].name = str_dup("Zone");
    zones[0].description = str_dup("The old zone.");
    zones[0].map = str_dup("N-G-S");
    zones[0].symbol = 'Z';
    zones[0].x = 10;
    zones[0].y = 11;
    zones[0].reset_mode = 1;
    zones[0].lifespan = 30;
    zones[0].level = 5;
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
    EXPECT_EQ(object.obj_flags.level, 42);
    EXPECT_EQ(object.obj_flags.rarity, 201);
    EXPECT_STREQ(world[0].name, "Changed Gate");
    EXPECT_STREQ(world[0].description, "The gate was changed by script.");
    EXPECT_EQ(world[0].level, 42);
    EXPECT_EQ(world[0].sector_type, SECT_WATER_NOSWIM);
    EXPECT_STREQ(zones[0].name, "Changed Zone");
    EXPECT_EQ(zones[0].description, nullptr);
    EXPECT_STREQ(zones[0].map, "N-G-S-E");
    EXPECT_EQ(zones[0].symbol, '*');
    EXPECT_EQ(zones[0].x, 25);
    EXPECT_EQ(zones[0].y, 24);
    EXPECT_EQ(zones[0].reset_mode, 3);
    EXPECT_EQ(zones[0].lifespan, 60);
    EXPECT_EQ(zones[0].level, 42);

    free(object.name);
    free(object.description);
    free(object.short_description);
    free(object.action_description);
    free(world[0].name);
    free(world[0].description);
    free(zones[0].name);
    free(zones[0].description);
    free(zones[0].map);
}

TEST(JsTriggerDispatch, RejectsPersistentSettersWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5827, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('unauthorized changed name');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
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

TEST(JsTriggerDispatch, RejectsZoneMapSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5831, "function onEnter(ctx) {\n"
                                           "  ctx.zone.setMap('unauthorized map');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].map = str_dup("N-G-S");
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_STREQ(zones[0].map, "N-G-S");

    free(zones[0].map);
}

TEST(JsTriggerDispatch, RejectsZoneSymbolSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5839, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setSymbol('*');\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].symbol = 'Z';
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].symbol, 'Z');
}

TEST(JsTriggerDispatch, RejectsZoneXSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5844, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setX(25);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].x = 10;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].x, 10);
}

TEST(JsTriggerDispatch, RejectsZoneYSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5850, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setY(25);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].y = 10;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].y, 10);
}

TEST(JsTriggerDispatch, RejectsZoneResetModeSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5853, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setResetMode(3);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].reset_mode = 1;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].reset_mode, 1);
}

TEST(JsTriggerDispatch, PersistsNullableZoneMapSetterToLiveGameRecord) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5829, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setMap(null);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].map = str_dup("N-G-S");
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(zones[0].map, nullptr);

    free(zones[0].map);
}

TEST(JsTriggerDispatch, IgnoresInvalidZoneSymbolSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setSymbol(''); return true; }",
        "function onEnter(ctx) { ctx.zone.setSymbol(' '); return true; }",
        "function onEnter(ctx) { ctx.zone.setSymbol('**'); return true; }",
        "function onEnter(ctx) { ctx.zone.setSymbol(String.fromCharCode(0xe9)); return true; }",
        "function onEnter(ctx) { ctx.zone.setSymbol(42); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5840, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].symbol = 'Z';
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(zones[0].symbol, 'Z') << script;
    }
}

TEST(JsTriggerDispatch, IgnoresInvalidZoneCoordinateSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setX(-1); return true; }",
        "function onEnter(ctx) { ctx.zone.setX(26); return true; }",
        "function onEnter(ctx) { ctx.zone.setX(1.5); return true; }",
        "function onEnter(ctx) { ctx.zone.setX(NaN); return true; }",
        "function onEnter(ctx) { ctx.zone.setX(Infinity); return true; }",
        "function onEnter(ctx) { ctx.zone.setX('7'); return true; }",
        "function onEnter(ctx) { ctx.zone.setY(-1); return true; }",
        "function onEnter(ctx) { ctx.zone.setY(26); return true; }",
        "function onEnter(ctx) { ctx.zone.setY(1.5); return true; }",
        "function onEnter(ctx) { ctx.zone.setY(NaN); return true; }",
        "function onEnter(ctx) { ctx.zone.setY(Infinity); return true; }",
        "function onEnter(ctx) { ctx.zone.setY('7'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5845, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].x = 10;
        zones[0].y = 11;
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(zones[0].x, 10) << script;
        EXPECT_EQ(zones[0].y, 11) << script;
    }
}

TEST(JsTriggerDispatch, IgnoresInvalidZoneResetModeSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setResetMode(-1); return true; }",
        "function onEnter(ctx) { ctx.zone.setResetMode(4); return true; }",
        "function onEnter(ctx) { ctx.zone.setResetMode(1.5); return true; }",
        "function onEnter(ctx) { ctx.zone.setResetMode(NaN); return true; }",
        "function onEnter(ctx) { ctx.zone.setResetMode(Infinity); return true; }",
        "function onEnter(ctx) { ctx.zone.setResetMode('2'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5854, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].reset_mode = 1;
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(zones[0].reset_mode, 1) << script;
    }
}

TEST(JsTriggerDispatch, RejectsZoneLifespanSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5858, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setLifespan(60);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].lifespan = 30;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].lifespan, 30);
}

TEST(JsTriggerDispatch, IgnoresInvalidZoneLifespanSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setLifespan(0); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan(-1); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan(10081); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan(1.5); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan(NaN); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan(Infinity); return true; }",
        "function onEnter(ctx) { ctx.zone.setLifespan('60'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5859, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].lifespan = 30;
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(zones[0].lifespan, 30) << script;
    }
}

TEST(JsTriggerDispatch, RejectsZoneLevelSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5863, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].level = 5;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(zones[0].level, 5);
}

TEST(JsTriggerDispatch, IgnoresInvalidZoneLevelSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setLevel(-1); return true; }",
        "function onEnter(ctx) { ctx.zone.setLevel(101); return true; }",
        "function onEnter(ctx) { ctx.zone.setLevel(1.5); return true; }",
        "function onEnter(ctx) { ctx.zone.setLevel(NaN); return true; }",
        "function onEnter(ctx) { ctx.zone.setLevel(Infinity); return true; }",
        "function onEnter(ctx) { ctx.zone.setLevel('42'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5864, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].level = 5;
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(zones[0].level, 5) << script;
    }
}

TEST(JsTriggerDispatch, RejectsObjectLevelSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5865, "function onEnter(ctx) {\n"
                                                                 "  ctx.object.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.obj_flags.level = 5;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(object.obj_flags.level, 5);
}

TEST(JsTriggerDispatch, IgnoresInvalidObjectLevelSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.object.setLevel(-1); return true; }",
        "function onEnter(ctx) { ctx.object.setLevel(101); return true; }",
        "function onEnter(ctx) { ctx.object.setLevel(1.5); return true; }",
        "function onEnter(ctx) { ctx.object.setLevel(NaN); return true; }",
        "function onEnter(ctx) { ctx.object.setLevel(Infinity); return true; }",
        "function onEnter(ctx) { ctx.object.setLevel('42'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5866, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        obj_data object = make_object("lever");
        object.obj_flags.level = 5;
        const char_data *live_characters[] = {&self};
        const obj_data *live_objects[] = {&object};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions options =
            make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        request.context_input.object = &object;
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(object.obj_flags.level, 5) << script;
    }
}

TEST(JsTriggerDispatch, RejectsObjectRaritySetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5867, "function onEnter(ctx) {\n"
                                                                 "  ctx.object.setRarity(201);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.obj_flags.rarity = 7;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(object.obj_flags.rarity, 7);
}

TEST(JsTriggerDispatch, IgnoresInvalidObjectRaritySetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.object.setRarity(-1); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity(256); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity(1.5); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity(NaN); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity(Infinity); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity('42'); return true; }",
        "function onEnter(ctx) { ctx.object.setRarity(null); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5868, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        obj_data object = make_object("lever");
        object.obj_flags.rarity = 7;
        const char_data *live_characters[] = {&self};
        const obj_data *live_objects[] = {&object};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions options =
            make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        request.context_input.object = &object;
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(object.obj_flags.rarity, 7) << script;
    }
}

TEST(JsTriggerDispatch, PersistsNestedWeaponRaritySetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5869, "function onEnter(ctx) {\n"
                                                                 "  ctx.weapon.setRarity(201);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data weapon = make_object("blade");
    weapon.obj_flags.rarity = 7;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&weapon};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.weapon = &weapon;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
    EXPECT_EQ(weapon.obj_flags.rarity, 201);
}

TEST(JsTriggerDispatch, RejectsObjectRaritySetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5870, "function onEnter(ctx) {\n"
                                                                 "  ctx.object.setRarity(201);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.obj_flags.rarity = 7;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Wrong Zone", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 31)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(object.obj_flags.rarity, 7);
}

TEST(JsTriggerDispatch, RejectsMixedObjectRarityTargetFailureWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5871, "function onEnter(ctx) {\n"
                                                                 "  ctx.object.setRarity(201);\n"
                                                                 "  ctx.room.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    self.in_room = 1;
    obj_data object = make_object("lever");
    object.in_room = 0;
    object.obj_flags.rarity = 7;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {make_room("Authorized", 100, 0), make_room("Wrong Zone", 200, 1)};
    world[1].level = 5;
    zone_data zones[2] = {make_zone("Authorized Zone", 30), make_zone("Wrong Zone", 31)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.room = 1;
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(object.obj_flags.rarity, 7);
    EXPECT_EQ(world[1].level, 5);
}

TEST(JsTriggerDispatch, RejectsRoomLevelSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5868, "function onEnter(ctx) {\n"
                                                                 "  ctx.room.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].level = 5;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(world[0].level, 5);
}

TEST(JsTriggerDispatch, IgnoresInvalidRoomLevelSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.room.setLevel(-1); return true; }",
        "function onEnter(ctx) { ctx.room.setLevel(101); return true; }",
        "function onEnter(ctx) { ctx.room.setLevel(1.5); return true; }",
        "function onEnter(ctx) { ctx.room.setLevel(NaN); return true; }",
        "function onEnter(ctx) { ctx.room.setLevel(Infinity); return true; }",
        "function onEnter(ctx) { ctx.room.setLevel('42'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5869, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        world[0].level = 5;
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(world[0].level, 5) << script;
    }
}

TEST(JsTriggerDispatch, RejectsRoomSectorTypeSetterWithoutExplicitAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5873, "function onEnter(ctx) {\n"
                                           "  ctx.room.setSectorType('Underwater');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].sector_type = SECT_CITY;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "builder authority"));
    EXPECT_EQ(world[0].sector_type, SECT_CITY);
}

TEST(JsTriggerDispatch, IgnoresInvalidRoomSectorTypeSetterValues) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.room.setSectorType('Unknown'); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType('water_noswim'); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType(' Water_noswim'); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType('Water_noswim '); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType(''); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType(7); return true; }",
        "function onEnter(ctx) { ctx.room.setSectorType(null); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5874, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        world[0].sector_type = SECT_CITY;
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(world[0].sector_type, SECT_CITY) << script;
    }
}

TEST(JsTriggerDispatch, PersistsEveryCanonicalRoomSectorTypeToMatchingLiveIndex) {
    ASSERT_NE(sector_types, nullptr);
    ASSERT_GT(num_of_sector_types, 0);

    for (int sector = 0; sector < num_of_sector_types; ++sector) {
        ASSERT_NE(sector_types[sector], nullptr) << sector;
        const std::string sector_name = sector_types[sector];
        ASSERT_NE(sector_name, "Unknown") << sector;

        JsScriptPackageRegistry registry;
        const std::string script = "function onEnter(ctx) {\n"
                                   "  ctx.room.setSectorType('" +
                                   sector_name +
                                   "');\n"
                                   "  return true;\n"
                                   "}";
        JsScriptPackage package = make_character_enter_package(5877, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        world[0].sector_type = SECT_CITY;
        zone_data zones[1] = {make_zone("Zone", 30)};
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow)
            << sector_name << ": " << result.diagnostic;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << sector_name;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_EQ(world[0].sector_type, sector) << sector_name;
    }
}

TEST(JsTriggerDispatch,
     RejectsMixedRoomSectorTypeTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5875, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.room.setSectorType('Underwater');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    world[0].sector_type = SECT_CITY;
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(world[0].sector_type, SECT_CITY);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsMixedLaterObjectFailureAfterRoomSectorTypeWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5878, "function onEnter(ctx) {\n"
                                           "  ctx.room.setSectorType('Underwater');\n"
                                           "  ctx.object.setName('unauthorized object edit');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Authorized Room", 100, 0),
        make_room("Object Room", 200, 1),
    };
    world[0].sector_type = SECT_CITY;
    zone_data zones[2] = {
        make_zone("Authorized Zone", 30),
        make_zone("Object Zone", 31),
    };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(world[0].sector_type, SECT_CITY);
    EXPECT_STREQ(object.name, "old lever");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, PersistsNestedObjectRoomSectorTypeSetterToLiveGameRecord) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5876, "function onEnter(ctx) {\n"
                                           "  ctx.object.room.setSectorType('Underwater');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Object Room", 200, 1),
    };
    world[1].sector_type = SECT_CITY;
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Object Zone", 31),
    };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(world[1].sector_type, SECT_UNDERWATER);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RedrawsGlobalWorldMapAfterPersistedZoneSymbolSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5842, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setSymbol('*');\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].x = 10;
    zones[0].y = 10;
    zones[0].symbol = 'Z';
    zone_data *previous_zone_table = zone_table;
    const int previous_top_of_zone_table = top_of_zone_table;
    zone_table = zones;
    top_of_zone_table = 0;
    draw_map();
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, zones[0].y)], 'Z');

    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(zones[0].symbol, '*');
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, zones[0].y)], '*');

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
    if (zone_table != nullptr && top_of_zone_table >= 0)
        draw_map();
}

TEST(JsTriggerDispatch, RedrawsGlobalWorldMapAfterPersistedZoneXSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5846, "function onEnter(ctx) {\n"
              "  try { Number.isInteger = function() { return false; }; } catch (error) {}\n"
              "  try { globalThis.String = function() { return '25'; }; } catch (error) {}\n"
              "  ctx.zone.setX(0);\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[2] = {
        make_zone("Zone", 30),
        make_zone("Stale Bad Coordinates", 31),
    };
    zones[0].x = 10;
    zones[0].y = 10;
    zones[0].symbol = 'Z';
    zones[1].x = -1;
    zones[1].y = WORLD_SIZE_Y;
    zones[1].symbol = 'B';
    zone_data *previous_zone_table = zone_table;
    const int previous_top_of_zone_table = top_of_zone_table;
    zone_table = zones;
    top_of_zone_table = 1;
    draw_map();
    EXPECT_EQ(world_map[world_map_symbol_offset(10, zones[0].y)], 'Z');

    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(zones[0].x, 0);
    EXPECT_EQ(world_map[world_map_symbol_offset(10, zones[0].y)], ' ');
    EXPECT_EQ(world_map[world_map_symbol_offset(0, zones[0].y)], 'Z');

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
    if (zone_table != nullptr && top_of_zone_table >= 0)
        draw_map();
}

TEST(JsTriggerDispatch, RedrawsGlobalWorldMapAfterPersistedZoneYSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5849, "function onEnter(ctx) {\n"
              "  try { Number.isInteger = function() { return false; }; } catch (error) {}\n"
              "  try { globalThis.String = function() { return '25'; }; } catch (error) {}\n"
              "  ctx.zone.setY(0);\n"
              "  return true;\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[2] = {
        make_zone("Zone", 30),
        make_zone("Stale Bad Coordinates", 31),
    };
    zones[0].x = 10;
    zones[0].y = 10;
    zones[0].symbol = 'Z';
    zones[1].x = -1;
    zones[1].y = WORLD_SIZE_Y;
    zones[1].symbol = 'B';
    zone_data *previous_zone_table = zone_table;
    const int previous_top_of_zone_table = top_of_zone_table;
    zone_table = zones;
    top_of_zone_table = 1;
    draw_map();
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 10)], 'Z');

    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(zones[0].y, 0);
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 10)], ' ');
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 0)], 'Z');

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
    if (zone_table != nullptr && top_of_zone_table >= 0)
        draw_map();
}

TEST(JsTriggerDispatch, RedrawsGlobalWorldMapAfterPersistedNestedZoneYSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5851, "function onEnter(ctx) {\n"
                                                                 "  ctx.room.zone.setY(25);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].x = 10;
    zones[0].y = 10;
    zones[0].symbol = 'Z';
    zone_data *previous_zone_table = zone_table;
    const int previous_top_of_zone_table = top_of_zone_table;
    zone_table = zones;
    top_of_zone_table = 0;
    draw_map();
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 10)], 'Z');

    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(zones[0].y, 25);
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 10)], ' ');
    EXPECT_EQ(world_map[world_map_symbol_offset(zones[0].x, 25)], 'Z');

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
    if (zone_table != nullptr && top_of_zone_table >= 0)
        draw_map();
}

TEST(JsTriggerDispatch, PersistsNestedZoneResetModeSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5856, "function onEnter(ctx) {\n"
                                           "  ctx.room.zone.setResetMode(3);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].reset_mode = 1;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(zones[0].reset_mode, 3);
}

TEST(JsTriggerDispatch, PersistsNestedZoneLifespanSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5860, "function onEnter(ctx) {\n"
                                           "  ctx.room.zone.setLifespan(90);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].lifespan = 30;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(zones[0].lifespan, 90);
}

TEST(JsTriggerDispatch, PersistsNestedZoneLevelSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5865, "function onEnter(ctx) {\n"
                                                                 "  ctx.room.zone.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].level = 5;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(zones[0].level, 42);
}

TEST(JsTriggerDispatch, PersistsNestedRoomLevelSetter) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5870, "function onEnter(ctx) {\n"
                                                                 "  ctx.object.room.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 0;
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].level = 5;
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << result.diagnostic;
    EXPECT_EQ(world[0].level, 42);
}

TEST(JsTriggerDispatch, RejectsZoneLifespanSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5861, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setLifespan(90);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Other Zone", 31),
    };
    zones[0].lifespan = 30;
    zones[1].lifespan = 45;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].lifespan, 30);
    EXPECT_EQ(zones[1].lifespan, 45);
}

TEST(JsTriggerDispatch, RejectsZoneLevelSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5866, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Other Zone", 31),
    };
    zones[0].level = 5;
    zones[1].level = 7;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].level, 5);
    EXPECT_EQ(zones[1].level, 7);
}

TEST(JsTriggerDispatch, RejectsRoomLevelSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5871, "function onEnter(ctx) {\n"
                                                                 "  ctx.room.setLevel(42);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].level = 5;
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Other Zone", 31),
    };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(world[0].level, 5);
}

TEST(JsTriggerDispatch, RejectsMixedSymbolBatchWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5843, "function onEnter(ctx) {\n"
                                           "  ctx.zone.setSymbol('*');\n"
                                           "  ctx.object.setName('unauthorized object');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Authorized Gate", 100, 0),
        make_room("Other Gate", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Zone", 30),
        make_zone("Other Zone", 31),
    };
    zones[0].symbol = 'Z';
    zones[1].symbol = 'O';
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].symbol, 'Z');
    EXPECT_STREQ(object.name, "old lever");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsMixedZoneCoordinateBatchWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5847, "function onEnter(ctx) {\n"
                                           "  ctx.zone.setX(25);\n"
                                           "  ctx.zone.setY(24);\n"
                                           "  ctx.object.setName('unauthorized object');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Authorized Gate", 100, 0),
        make_room("Other Gate", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Zone", 30),
        make_zone("Other Zone", 31),
    };
    zones[0].x = 10;
    zones[0].y = 11;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].x, 10);
    EXPECT_EQ(zones[0].y, 11);
    EXPECT_STREQ(object.name, "old lever");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch,
     RejectsMixedZoneYTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5848, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.zone.setY(25);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    zones[0].x = 10;
    zones[0].y = 11;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(zones[0].x, 10);
    EXPECT_EQ(zones[0].y, 11);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch,
     RejectsMixedZoneResetModeTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5857, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.zone.setResetMode(3);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    zones[0].reset_mode = 1;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(zones[0].reset_mode, 1);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch,
     RejectsMixedZoneLifespanTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5862, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.zone.setLifespan(90);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    zones[0].lifespan = 30;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(zones[0].lifespan, 30);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch,
     RejectsMixedZoneLevelTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5867, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.zone.setLevel(42);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    zones[0].level = 5;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(zones[0].level, 5);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch,
     RejectsMixedRoomLevelTargetFailureAfterEarlierValidMutationWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5872, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized object edit');\n"
                                           "  ctx.room.setLevel(42);\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.in_room = 1;
    object.name = str_dup("old lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[2] = {
        make_room("Script Room", 100, 0),
        make_room("Authorized Object Room", 200, 1),
    };
    world[0].level = 5;
    zone_data zones[2] = {
        make_zone("Script Zone", 30),
        make_zone("Authorized Object Zone", 31),
    };
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 1, nullptr, 0, zones, 2);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old lever");
    EXPECT_EQ(world[0].level, 5);

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsZoneMapSetterWithZoneFileSyntaxMarkers) {
    const char *scripts[] = {
        "function onEnter(ctx) { ctx.zone.setMap('bad~map'); return true; }",
        "function onEnter(ctx) { ctx.zone.setMap('ok\\n  #31'); return true; }",
    };

    for (const char *script : scripts) {
        JsScriptPackageRegistry registry;
        JsScriptPackage package = make_character_enter_package(5832, script);
        ASSERT_TRUE(registry.replace_all({package}, internal_options()));

        char_data self = make_character("Self");
        const char_data *live_characters[] = {&self};
        room_data world[1] = {make_room("Gate", 100, 0)};
        zone_data zones[1] = {make_zone("Zone", 30)};
        zones[0].map = str_dup("N-G-S");
        JsGameAdapterOptions options =
            make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
        JsTriggerDispatchRequest request = character_request(&self);
        JsTriggerDispatchOptions dispatch_options;
        dispatch_options.mutation_authority = test_mutation_authority();

        JsTriggerDispatchResult result =
            js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << script;
        EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok) << script;
        EXPECT_TRUE(result.diagnostic.empty()) << result.diagnostic;
        EXPECT_STREQ(zones[0].map, "N-G-S") << script;

        free(zones[0].map);
    }
}

TEST(JsTriggerDispatch, RejectsZoneSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5830, "function onEnter(ctx) {\n"
                                           "  ctx.zone.setMap('unauthorized map');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].map = str_dup("N-G-S");
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(zones[0].map, "N-G-S");

    free(zones[0].map);
}

TEST(JsTriggerDispatch, RejectsZoneSymbolSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5841, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setSymbol('*');\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].symbol = 'Z';
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].symbol, 'Z');
}

TEST(JsTriggerDispatch, RejectsZoneXSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5848, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setX(25);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].x = 10;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].x, 10);
}

TEST(JsTriggerDispatch, RejectsZoneYSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5852, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setY(25);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].y = 10;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].y, 10);
}

TEST(JsTriggerDispatch, RejectsZoneResetModeSetterWhenAuthorityTargetsAnotherZone) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(5855, "function onEnter(ctx) {\n"
                                                                 "  ctx.zone.setResetMode(3);\n"
                                                                 "  return true;\n"
                                                                 "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].reset_mode = 1;
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_EQ(zones[0].reset_mode, 1);
}

TEST(JsTriggerDispatch, RejectsObjectRoomAndZoneSettersOutsideAuthorityTargetWithoutPartialWrites) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5833, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('changed object');\n"
                                           "  ctx.room.setName('Changed Room');\n"
                                           "  ctx.zone.setMap('changed map');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    world[0].name = str_dup("Gate");
    zone_data zones[1] = {make_zone("Zone", 30)};
    zones[0].map = str_dup("N-G-S");
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "lever");
    EXPECT_STREQ(world[0].name, "Gate");
    EXPECT_STREQ(zones[0].map, "N-G-S");

    free(object.name);
    free(object.short_description);
    free(world[0].name);
    free(zones[0].map);
}

TEST(JsTriggerDispatch, PersistsCarriedWeaponSetterWhenCarrierRoomMatchesAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5834, "function onEnter(ctx) {\n"
                                           "  ctx.weapon.setName('authorized blade');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data weapon = make_object("blade");
    weapon.in_room = NOWHERE;
    weapon.carried_by = &self;
    self.equipment[WIELD] = &weapon;
    weapon.name = str_dup("old blade");
    weapon.short_description = str_dup("a blade");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&weapon};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.weapon = &weapon;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_STREQ(weapon.name, "authorized blade");

    free(weapon.name);
    free(weapon.short_description);
}

TEST(JsTriggerDispatch, RejectsCarriedWeaponSetterWhenCarrierRoomIsOutsideAuthorityTarget) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5835, "function onEnter(ctx) {\n"
                                           "  ctx.weapon.setName('wrong zone blade');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data weapon = make_object("blade");
    weapon.in_room = NOWHERE;
    weapon.carried_by = &self;
    self.equipment[WIELD] = &weapon;
    weapon.name = str_dup("old blade");
    weapon.short_description = str_dup("a blade");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&weapon};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.weapon = &weapon;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority(31);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(weapon.name, "old blade");

    free(weapon.name);
    free(weapon.short_description);
}

TEST(JsTriggerDispatch, RejectsCarriedWeaponSetterWhenCarrierPointerIsStale) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5836, "function onEnter(ctx) {\n"
                                           "  ctx.weapon.setName('stale carrier blade');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data weapon = make_object("blade");
    weapon.in_room = NOWHERE;
    weapon.carried_by = &self;
    weapon.name = str_dup("old blade");
    weapon.short_description = str_dup("a blade");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&weapon};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 1, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.weapon = &weapon;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(weapon.name, "old blade");

    free(weapon.name);
    free(weapon.short_description);
}

TEST(JsTriggerDispatch, PersistsContainedObjectSetterWhenContainerRoomMatchesAuthority) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5837, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('authorized gem');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data container = make_object("box");
    obj_data object = make_object("gem");
    container.in_room = 0;
    container.contains = &object;
    object.in_room = NOWHERE;
    object.in_obj = &container;
    object.name = str_dup("old gem");
    object.short_description = str_dup("a gem");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object, &container};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 2, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_STREQ(object.name, "authorized gem");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsContainedObjectSetterWhenContainerPointerIsStale) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5838, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('stale container gem');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data container = make_object("box");
    obj_data object = make_object("gem");
    container.in_room = 0;
    object.in_room = NOWHERE;
    object.in_obj = &container;
    object.name = str_dup("old gem");
    object.short_description = str_dup("a gem");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object, &container};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, live_objects, 2, world, 0, nullptr, 0, zones, 1);
    JsTriggerDispatchRequest request = character_request(&self);
    request.context_input.object = &object;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.mutation_authority = test_mutation_authority();

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, request, options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_TRUE(contains(result.diagnostic, "mutation target"));
    EXPECT_STREQ(object.name, "old gem");

    free(object.name);
    free(object.short_description);
}

TEST(JsTriggerDispatch, RejectsPersistentSettersWhenAuthorityEvidenceIsIncomplete) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5828, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('incomplete authority name');\n"
                                           "  return true;\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
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

TEST(JsTriggerDispatch, DoesNotPersistSetterSnapshotsWhenHandlerFails) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5826, "function onEnter(ctx) {\n"
                                           "  ctx.object.setName('unsafe changed name');\n"
                                           "  throw new TypeError('boom');\n"
                                           "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    obj_data object = make_object("lever");
    object.name = str_dup("lever keys old");
    object.short_description = str_dup("a lever");
    const char_data *live_characters[] = {&self};
    const obj_data *live_objects[] = {&object};
    room_data world[1] = {make_room("Gate", 100, 0)};
    zone_data zones[1] = {make_zone("Zone", 30)};
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

TEST(JsScriptingRuntimePolicy, PinsLiveDispatchDefaults) {
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();

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

TEST(JsTriggerDispatch, NearMissHostKindAndValueDoNotExecutePackageSource) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5001, "function onEnter(ctx) { syntax error if this runs }\n"
                                           "function onDamage(ctx) { return false; }");
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage"});
    refresh_checksum(package);
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, InvokesOnlyBoundHandlerFromFullCompiledPackage) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5101,
        "function onDamage(ctx) { return false; }\n"
        "function onEnter(ctx) {\n"
        "  return ctx.self.name === 'Self' && ctx.trigger.legacyName === 'ON_ENTER'\n"
        "    && ctx.trigger.hostType === 'character' && ctx.trigger.blocksGameplay === true;\n"
        "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, DispatchesBuilderClientCommonJsExportsAndScriptResultHelpers) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5151, "\"use strict\";\n"
              "Object.defineProperty(exports, \"__esModule\", { value: true });\n"
              "exports.onEnter = onEnter;\n"
              "function onEnter(ctx) {\n"
              "  return ctx.self.name === 'Self' ? RotS.ScriptResult.block() : "
              "RotS.ScriptResult.allow();\n"
              "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5151);
    EXPECT_EQ(result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, PrefersCompiledExportOverGlobalHandlerFallback) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_character_enter_package(
        5152, "exports.onEnter = function(ctx) { return RotS.ScriptResult.allow(); };\n"
              "function onEnter(ctx) { return RotS.ScriptResult.block(); }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Allow) << result.diagnostic;
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
    EXPECT_EQ(result.package_vnum, 5152);
    EXPECT_EQ(result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, MapsFalseReturnToBlockForBlockingManifestTriggers) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5201, JsScriptPackageHost::Character,
                     JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER, "onBeforeEnter",
                     "function onBeforeEnter(ctx) {\n"
                     "  return ctx.trigger.blocksGameplay ? false : true;\n"
                     "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Guard");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request = character_request(&self);
    request.legacy_value = ON_BEFORE_ENTER;
    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_STREQ(js_trigger_dispatch_status_name(result.status), "block");
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Ok);
}

TEST(JsTriggerDispatch, UsesFirstMatchingPackageInRegistryOrder) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5301, "function onEnter(ctx) { return false; }");
    JsScriptPackage second =
        make_character_enter_package(5302, "function onEnter(ctx) { syntax error if this runs }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(result.package_vnum, 5301);
    EXPECT_EQ(result.matched_package_count, 2U);
}

TEST(JsTriggerDispatch, PackageVnumFilterDispatchesOnlyAttachedPackage) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5351, "function onEnter(ctx) { return false; }");
    JsScriptPackage second =
        make_character_enter_package(5352, "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, PackageVnumFilterDoesNotFallBackWhenAttachedPackageIsWrongTrigger) {
    JsScriptPackageRegistry registry;
    JsScriptPackage wrong_attached = make_package(
        5371, JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger,
        ON_DAMAGE, "onDamage", "function onDamage(ctx) { return true; }");
    JsScriptPackage global_match =
        make_character_enter_package(5372, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({wrong_attached, global_match}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchRequest request = character_request(&self);
    request.package_vnum = 5371;
    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(registry, request, options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(result.matched_package_count, 0U);
}

TEST(JsTriggerDispatch, DispatchesFromRefreshedLiveRegistryService) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsScriptPackage package = make_character_enter_package(
        5381, "function onEnter(ctx) { return ctx.self.name === 'Self'; }");
    JsStagedPackageRecord record =
        activate_live_package_for_dispatch(repository, live_store, package);
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, LiveRegistryBridgeNoMatchDoesNotExposePackageMetadata) {
    JsLiveRegistryReloadService empty_service;
    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, LiveRegistryDispatchUsesRefreshSnapshotUntilReloaded) {
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first = activate_live_package_for_dispatch(
        repository, live_store,
        make_character_enter_package(5382, "function onEnter(ctx) { return false; }"));
    JsLiveRegistryReloadService service;
    ASSERT_TRUE(service.refresh_from_live_store(live_store));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, FailedLiveRegistryRefreshKeepsPreviousDispatchSnapshot) {
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
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, RejectsMissingRequiredCharacterHostBeforeRuntimeExecution) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5401, "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_other = make_character("Live");
    const char_data *live_characters[] = {&live_other};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, RejectsMissingRequiredObjectHostBeforeRuntimeExecution) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(5501, JsScriptPackageHost::Object,
                                           JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
                                           "onDamage", "function onDamage(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    obj_data stale_object = make_object("stale object", 0);
    obj_data live_object = make_object("live object", 0);
    const obj_data *live_objects[] = {&live_object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, ObjectHostProvidesObjectContextAndNoCharacterSelfAlias) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5601, JsScriptPackageHost::Object, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
        "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.self !== null) throw new TypeError('self-alias');\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.id !== 'object') throw new TypeError(ctx.object.id);\n"
        "  if (ctx.object.name !== 'Blade') throw new TypeError(ctx.object.name);\n"
        "  if (ctx.object.vnum !== 300) throw new TypeError(String(ctx.object.vnum));\n"
        "  if (ctx.object.room.vnum !== 100) throw new TypeError(String(ctx.object.room && "
        "ctx.object.room.vnum));\n"
        "  if (ctx.object.room.zone.vnum !== 10) throw new TypeError(String(ctx.object.room.zone "
        "&& ctx.object.room.zone.vnum));\n"
        "  if (ctx.room.vnum !== 100) throw new TypeError(String(ctx.room.vnum));\n"
        "  return ctx.trigger.hostType === 'object';\n"
        "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    obj_data object = make_object("Blade", 0);
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
    zone_data zones[1] = {make_zone("Test Zone", 10)};
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

TEST(JsTriggerDispatch, ObjectHostProvidesCarriedBySnapshot) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5602, JsScriptPackageHost::Object, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
        "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.room !== null) throw new TypeError('unexpected-room');\n"
        "  if (ctx.object.wornBy !== null) throw new TypeError('unexpected-worn');\n"
        "  if (ctx.object.carriedBy === null) throw new TypeError('missing-carrier');\n"
        "  if (ctx.object.carriedBy.name !== 'Carrier') throw new "
        "TypeError(ctx.object.carriedBy.name);\n"
        "  if (ctx.object.carriedBy.room.vnum !== 100) throw new TypeError('carrier-room');\n"
        "  return ctx.object.carriedBy.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data carrier = make_character("Carrier");
    obj_data object = make_object("Blade", 0);
    object.in_room = -1;
    object.carried_by = &carrier;
    carrier.carrying = &object;
    const char_data *live_characters[] = {&carrier};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, ObjectHostProvidesWornBySnapshot) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5603, JsScriptPackageHost::Object, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
        "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.object === null) throw new TypeError('missing-object');\n"
        "  if (ctx.object.room !== null) throw new TypeError('unexpected-room');\n"
        "  if (ctx.object.carriedBy !== null) throw new TypeError('unexpected-carried');\n"
        "  if (ctx.object.wornBy === null) throw new TypeError('missing-wearer');\n"
        "  if (ctx.object.wornBy.name !== 'Wearer') throw new TypeError(ctx.object.wornBy.name);\n"
        "  if (ctx.object.wornBy.room.vnum !== 100) throw new TypeError('wearer-room');\n"
        "  return ctx.object.wornBy.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data wearer = make_character("Wearer");
    obj_data object = make_object("Blade", 0);
    object.in_room = -1;
    object.carried_by = &wearer;
    wearer.equipment[WIELD] = &object;
    const char_data *live_characters[] = {&wearer};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, CharacterDieProvidesKillerRoleSnapshot) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5608, JsScriptPackageHost::Character,
                     JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "onDie",
                     "function onDie(ctx) {\n"
                     "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
                     "  if (ctx.self.name !== 'Victim') throw new TypeError('self');\n"
                     "  if (ctx.killer.name !== 'Killer') throw new TypeError('killer');\n"
                     "  return ctx.killer.isValid();\n"
                     "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data victim = make_character("Victim");
    char_data killer = make_character("Killer");
    const char_data *live_characters[] = {&victim, &killer};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, CharacterDieModelsMissingKillerAsNull) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5609, JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE,
        "onDie",
        "function onDie(ctx) { return ctx.self.name === 'Victim' && ctx.killer === null; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data victim = make_character("Victim");
    const char_data *live_characters[] = {&victim};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, CharacterDamageProvidesAttackerAndVictimRoleSnapshots) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5610, JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger,
        ON_DAMAGE, "onDamage",
        "function onDamage(ctx) {\n"
        "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
        "  if (ctx.self.name !== 'Victim') throw new TypeError('self');\n"
        "  if (ctx.actor.name !== 'Attacker') throw new TypeError('actor');\n"
        "  if (ctx.attacker.name !== 'Attacker') throw new TypeError('attacker');\n"
        "  if (ctx.victim.name !== 'Victim') throw new TypeError('victim');\n"
        "  if (ctx.weapon.name !== 'Blade') throw new TypeError('weapon');\n"
        "  return ctx.attacker.isValid() && ctx.victim.isValid() && ctx.weapon.isValid();\n"
        "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    obj_data weapon = make_object("Blade", 0);
    const char_data *live_characters[] = {&victim, &attacker};
    const obj_data *live_objects[] = {&weapon};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, ObjectDamageProvidesAttackerAndVictimRoleSnapshots) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(
        5612, JsScriptPackageHost::Object, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
        "onDamage",
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
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    obj_data object = make_object("Blade", 0);
    const char_data *live_characters[] = {&victim, &attacker};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, CharacterDamageModelsMissingWeaponAsNull) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5613, JsScriptPackageHost::Character,
                     JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
                     "function onDamage(ctx) { return ctx.weapon === null && ctx.attacker.name === "
                     "'Attacker'; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data victim = make_character("Victim");
    char_data attacker = make_character("Attacker");
    const char_data *live_characters[] = {&victim, &attacker};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, ObjectWearProvidesWearSlot) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5614, JsScriptPackageHost::Object,
                     JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR, "onWear",
                     "function onWear(ctx) {\n"
                     "  if (ctx.hostType !== 'object') throw new TypeError('host');\n"
                     "  if (ctx.object.name !== 'Helm') throw new TypeError('object');\n"
                     "  if (ctx.actor.name !== 'Actor') throw new TypeError('actor');\n"
                     "  return ctx.wearSlot === 'head';\n"
                     "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data actor = make_character("Actor");
    obj_data object = make_object("Helm", 0);
    const char_data *live_characters[] = {&actor};
    const obj_data *live_objects[] = {&object};
    index_data object_index[1]{};
    object_index[0].virt = 300;
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, CharacterHearProvidesSpeakerRoleSnapshot) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5611, JsScriptPackageHost::Character,
                     JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY, "onHearSay",
                     "function onHearSay(ctx) {\n"
                     "  if (ctx.hostType !== 'character') throw new TypeError('host');\n"
                     "  if (ctx.self.name !== 'Listener') throw new TypeError('self');\n"
                     "  if (ctx.actor.name !== 'Speaker') throw new TypeError('actor');\n"
                     "  if (ctx.speaker.name !== 'Speaker') throw new TypeError('speaker');\n"
                     "  return ctx.text === 'hello there' && ctx.speaker.isValid();\n"
                     "}");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data listener = make_character("Listener");
    char_data speaker = make_character("Speaker");
    const char_data *live_characters[] = {&listener, &speaker};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, RejectsMudlleMobileDispatchWhenSelfIsNotNpc) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_package(5651, JsScriptPackageHost::MudlleMobile,
                     JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND, "onSpecialCommand",
                     "function onSpecialCommand(ctx) { return ctx.self.isNpc === true; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data player = make_character("Player");
    char_data mobile = make_character("Mobile", 1, 10, true);
    const char_data *live_characters[] = {&player, &mobile};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, RuntimeErrorsKeepSafeMetadataAndRedactContextText) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5701, "function onEnter(ctx) { throw ctx.text; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, TopLevelPackageReturnCannotPreemptBoundHandler) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5751, "return false;\n"
                                           "function onEnter(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);

    JsTriggerDispatchResult result =
        js_trigger_dispatch_first_match(registry, character_request(&self), options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(result.runtime_status, JsRuntimeStatus::Error);
    EXPECT_NE(result.status, JsTriggerDispatchStatus::Block);
}

TEST(JsTriggerDispatch, RuntimeLimitsPropagateThroughFacade) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5771, "function onEnter(ctx) { while (true) {} }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, SamePulsePerPackageBudgetSkipsRuntimeExecution) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5772, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 90;

    JsTriggerDispatchResult first = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);
    JsTriggerDispatchResult second = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

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

TEST(JsTriggerDispatch, BudgetResetsWhenPulseChanges) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5773, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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
    JsTriggerDispatchResult next_pulse = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(next_pulse.status, JsTriggerDispatchStatus::Block) << next_pulse.diagnostic;
    EXPECT_EQ(next_pulse.package_vnum, 5773);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&self), adapter_options,
                                              dispatch_options)
                  .status,
              JsTriggerDispatchStatus::BudgetExceeded);

    dispatch_options.current_pulse = 0;
    JsTriggerDispatchResult wrapped_pulse = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(wrapped_pulse.status, JsTriggerDispatchStatus::Block) << wrapped_pulse.diagnostic;
    EXPECT_EQ(wrapped_pulse.package_vnum, 5773);
}

TEST(JsTriggerDispatch, TotalPulseBudgetAppliesAcrossPackages) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5774, "function onEnter(ctx) { return true; }");
    JsScriptPackage second =
        make_character_enter_package(5775, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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
    JsTriggerDispatchResult second_result = js_trigger_dispatch_first_match(
        registry, second_request, adapter_options, dispatch_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Allow) << first_result.diagnostic;
    EXPECT_EQ(first_result.package_vnum, 5774);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::BudgetExceeded);
    EXPECT_EQ(second_result.package_vnum, 5775);
    EXPECT_EQ(second_result.handler_name, "onEnter");
}

TEST(JsTriggerDispatch, BudgetExceededAttemptDoesNotConsumeTotalBudget) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5777, "function onEnter(ctx) { return true; }");
    JsScriptPackage second =
        make_character_enter_package(5778, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

    EXPECT_EQ(
        js_trigger_dispatch_first_match(registry, first_request, adapter_options, dispatch_options)
            .status,
        JsTriggerDispatchStatus::Allow);
    EXPECT_EQ(
        js_trigger_dispatch_first_match(registry, first_request, adapter_options, dispatch_options)
            .status,
        JsTriggerDispatchStatus::BudgetExceeded);
    JsTriggerDispatchResult second_package = js_trigger_dispatch_first_match(
        registry, second_request, adapter_options, dispatch_options);

    EXPECT_EQ(second_package.status, JsTriggerDispatchStatus::Block) << second_package.diagnostic;
    EXPECT_EQ(second_package.package_vnum, 5778);
}

TEST(JsTriggerDispatch, ZeroBudgetLimitsAreUnlimitedWhenBudgetIsProvided) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5779, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.current_pulse = 96;

    for (int invocation = 0; invocation < 4; ++invocation) {
        JsTriggerDispatchResult result = js_trigger_dispatch_first_match(
            registry, character_request(&self), adapter_options, dispatch_options);
        EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block)
            << invocation << ": " << result.diagnostic;
    }
}

TEST(JsTriggerDispatch, DepthExceededSkipsRuntimeExecution) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5780, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchDepthLimits depth_limits;
    depth_limits.max_dispatch_depth = 1;
    ASSERT_TRUE(depth_guard.try_enter(depth_limits));
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits = depth_limits;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

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

TEST(JsTriggerDispatch, SuccessfulDispatchLeavesDepthForNextInvocation) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5781, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;

    JsTriggerDispatchResult first = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);
    JsTriggerDispatchResult second = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(first.status, JsTriggerDispatchStatus::Block) << first.diagnostic;
    EXPECT_EQ(second.status, JsTriggerDispatchStatus::Block) << second.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, ZeroDepthLimitIsUnlimitedWhenGuardIsProvided) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5782, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchDepthLimits unlimited_limits;
    ASSERT_TRUE(depth_guard.try_enter(unlimited_limits));
    ASSERT_TRUE(depth_guard.try_enter(unlimited_limits));
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits = unlimited_limits;

    JsTriggerDispatchResult result = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(result.status, JsTriggerDispatchStatus::Block) << result.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 2U);
    depth_guard.leave();
    depth_guard.leave();
}

TEST(JsTriggerDispatch, RuntimeErrorReleasesTriggerDepthForNextDispatch) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5783, "function onEnter(ctx) { throw new Error('boom'); }");
    JsScriptPackage second =
        make_character_enter_package(5784, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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
    JsTriggerDispatchResult second_result = js_trigger_dispatch_first_match(
        registry, second_request, adapter_options, dispatch_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::Block) << second_result.diagnostic;
}

TEST(JsTriggerDispatch, InterruptedRuntimeReleasesTriggerDepthForNextDispatch) {
    JsScriptPackageRegistry registry;
    JsScriptPackage first =
        make_character_enter_package(5785, "function onEnter(ctx) { while (true) {} }");
    JsScriptPackage second =
        make_character_enter_package(5786, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({first, second}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

    JsTriggerDispatchResult first_result = js_trigger_dispatch_first_match(
        registry, first_request, adapter_options, interrupt_options);
    JsTriggerDispatchResult second_result =
        js_trigger_dispatch_first_match(registry, second_request, adapter_options, normal_options);

    EXPECT_EQ(first_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_EQ(first_result.runtime_status, JsRuntimeStatus::Interrupted);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(second_result.status, JsTriggerDispatchStatus::Block) << second_result.diagnostic;
}

TEST(JsTriggerDispatch, DepthExceededAttemptDoesNotConsumeDepth) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5787, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

    JsTriggerDispatchResult after_release = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(after_release.status, JsTriggerDispatchStatus::Block) << after_release.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, DepthExceededDoesNotConsumeBudget) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5788, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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
    JsTriggerDispatchResult after_release = js_trigger_dispatch_first_match(
        registry, character_request(&self), adapter_options, dispatch_options);

    EXPECT_EQ(after_release.status, JsTriggerDispatchStatus::Block) << after_release.diagnostic;
}

TEST(JsTriggerDispatch, NoMatchAndMissingLiveContextDoNotAcquireTriggerDepth) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5789, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_self = make_character("Live");
    const char_data *live_characters[] = {&live_self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchDepthGuard depth_guard;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.depth_guard = &depth_guard;
    dispatch_options.depth_limits.max_dispatch_depth = 1;

    JsTriggerDispatchRequest no_match = character_request(&live_self);
    no_match.legacy_value = ON_DAMAGE;
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, no_match, adapter_options, dispatch_options)
                  .status,
              JsTriggerDispatchStatus::NoMatch);
    EXPECT_EQ(depth_guard.current_depth(), 0U);
    EXPECT_EQ(js_trigger_dispatch_first_match(registry, character_request(&stale_self),
                                              adapter_options, dispatch_options)
                  .status,
              JsTriggerDispatchStatus::Error);
    EXPECT_EQ(depth_guard.current_depth(), 0U);

    JsTriggerDispatchResult live_result = js_trigger_dispatch_first_match(
        registry, character_request(&live_self), adapter_options, dispatch_options);

    EXPECT_EQ(live_result.status, JsTriggerDispatchStatus::Block) << live_result.diagnostic;
    EXPECT_EQ(depth_guard.current_depth(), 0U);
}

TEST(JsTriggerDispatch, MissingLiveContextDoesNotConsumeBudget) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5776, "function onEnter(ctx) { return false; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data stale_self = make_character("Stale");
    char_data live_self = make_character("Live");
    const char_data *live_characters[] = {&live_self};
    room_data world[1] = {make_room("Room", 100, 0)};
    JsGameAdapterOptions adapter_options =
        make_options(live_characters, 1, nullptr, 0, world, 0, nullptr, 0, nullptr, 0);
    JsTriggerDispatchBudget budget;
    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.budget = &budget;
    dispatch_options.budget_limits.max_invocations_per_package_per_pulse = 1;
    dispatch_options.current_pulse = 94;

    JsTriggerDispatchResult stale_result = js_trigger_dispatch_first_match(
        registry, character_request(&stale_self), adapter_options, dispatch_options);
    JsTriggerDispatchResult live_result = js_trigger_dispatch_first_match(
        registry, character_request(&live_self), adapter_options, dispatch_options);

    EXPECT_EQ(stale_result.status, JsTriggerDispatchStatus::Error);
    EXPECT_TRUE(contains(stale_result.diagnostic, "missing live character"));
    EXPECT_EQ(live_result.status, JsTriggerDispatchStatus::Block) << live_result.diagnostic;
    EXPECT_EQ(live_result.package_vnum, 5776);
}

TEST(JsTriggerDispatch, MissingHandlerFailsClosedInsteadOfAllowing) {
    JsScriptPackageRegistry registry;
    JsScriptPackage package =
        make_character_enter_package(5801, "function onDamage(ctx) { return true; }");
    ASSERT_TRUE(registry.replace_all({package}, internal_options()));

    char_data self = make_character("Self");
    const char_data *live_characters[] = {&self};
    room_data world[1] = {make_room("Room", 100, 0)};
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

TEST(JsTriggerDispatch, BuildFilesReferenceDispatchSourcesAndTests) {
    const std::string cmake =
        read_first_available_file({"src/CMakeLists.txt", "../src/CMakeLists.txt"});
    const std::string server_makefile =
        read_first_available_file({"src/Makefile", "../src/Makefile"});
    const std::string test_makefile =
        read_first_available_file({"src/tests/Makefile", "../src/tests/Makefile"});

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
