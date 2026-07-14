#include "../interpre.h"
#include "../js_scripting_manifest.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

bool has_host(const JsScriptingManifestEntry &entry, JsScriptingHostFlag host_flag) {
    return (entry.host_flags & host_flag) != 0;
}

void expect_context_fields(int legacy_value, const std::initializer_list<const char *> fields) {
    const JsScriptingManifestEntry *entry = find_js_scripting_manifest_entry(
        JsScriptingManifestKind::LegacyScriptTrigger, legacy_value);
    ASSERT_NE(entry, nullptr) << legacy_value;
    const std::string context = entry->context_fields;
    for (const char *field : fields)
        EXPECT_NE(context.find(field), std::string::npos) << entry->legacy_name << " " << field;
}

void expect_deferred_handler(JsScriptingManifestKind kind, int legacy_value,
                             const char *handler_name) {
    const JsScriptingManifestEntry *entry = find_js_scripting_manifest_entry(kind, legacy_value);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->support_status, JsScriptingSupportStatus::Deferred);
    EXPECT_EQ(entry->builder_status, JsScriptingBuilderStatus::Deferred);
    EXPECT_STREQ(entry->javascript_handler_name, handler_name);
    EXPECT_NE(std::string(entry->context_fields).find("trigger"), std::string::npos)
        << entry->legacy_name;
}

} // namespace

TEST(JsScriptingManifest, ContainsEveryLegacyScriptTriggerWithNoDuplicateIds) {
    const int expected_triggers[] = {
        ON_ENTER,          ON_BEFORE_ENTER, ON_BEFORE_DIE, ON_DIE, ON_RECEIVE,
        ON_EXAMINE_OBJECT, ON_HEAR_SAY,     ON_DAMAGE,     ON_EAT, ON_DRINK,
        ON_WEAR,           ON_PULL,         ON_HEAR_YELL,
    };

    std::set<int> seen_triggers;
    std::size_t trigger_count = 0;
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (entry.kind != JsScriptingManifestKind::LegacyScriptTrigger)
            continue;

        ++trigger_count;
        EXPECT_TRUE(seen_triggers.insert(entry.legacy_value).second) << entry.legacy_name;
        EXPECT_GE(entry.legacy_value, 10) << entry.legacy_name;
        EXPECT_LT(entry.legacy_value, 30) << entry.legacy_name;
        EXPECT_NE(entry.legacy_name, nullptr);
        EXPECT_NE(std::string(entry.legacy_name).find("ON_"), std::string::npos);
    }

    EXPECT_EQ(trigger_count, sizeof(expected_triggers) / sizeof(expected_triggers[0]));
    for (int trigger : expected_triggers)
        EXPECT_NE(
            find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, trigger),
            nullptr)
            << trigger;
}

TEST(JsScriptingManifest, ExposesStableMetadataForGeneratedConsumers) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();

    EXPECT_EQ(metadata.schema_version, 1);
    EXPECT_EQ(metadata.package_format_version, 1);
    EXPECT_EQ(metadata.trigger_catalog_revision, 1);
    EXPECT_STREQ(metadata.manifest_checksum, "rots-js-manifest-v1-trigger-catalog-1");
    EXPECT_STREQ(metadata.api_version, "unpublished");
    EXPECT_STREQ(metadata.selected_runtime_name, "QuickJS");
    EXPECT_STREQ(metadata.selected_runtime_version, "2026-06-04");
    EXPECT_STREQ(metadata.minimum_supported_runtime_version, "2026-06-04");
    EXPECT_STREQ(metadata.runtime_feature_flags,
                 "no-modules,no-fs,no-process,no-network,no-persistence,no-async");
    EXPECT_STREQ(metadata.generated_typings_version, "unpublished");
}

TEST(JsScriptingManifest, ContainsEveryMudlleCallFlagWithNoDuplicateIds) {
    const int expected_call_flags[] = {
        SPECIAL_NONE,  SPECIAL_COMMAND, SPECIAL_SELF,   SPECIAL_ENTER,
        SPECIAL_DELAY, SPECIAL_TARGET,  SPECIAL_DAMAGE, SPECIAL_DEATH,
    };

    std::set<int> seen_call_flags;
    std::size_t call_flag_count = 0;
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (entry.kind != JsScriptingManifestKind::MudlleCallFlag)
            continue;

        ++call_flag_count;
        EXPECT_TRUE(seen_call_flags.insert(entry.legacy_value).second) << entry.legacy_name;
        EXPECT_NE(entry.legacy_name, nullptr);
        EXPECT_NE(std::string(entry.legacy_name).find("SPECIAL_"), std::string::npos);
    }

    EXPECT_EQ(call_flag_count, sizeof(expected_call_flags) / sizeof(expected_call_flags[0]));
    for (int call_flag : expected_call_flags)
        EXPECT_NE(
            find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, call_flag),
            nullptr)
            << call_flag;
}

TEST(JsScriptingManifest, HasNoDuplicateJavaScriptHandlerNames) {
    std::set<std::string> seen_handler_names;
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (std::string(entry.javascript_handler_name).empty())
            continue;

        EXPECT_TRUE(seen_handler_names.insert(entry.javascript_handler_name).second)
            << entry.javascript_handler_name;
    }
}

TEST(JsScriptingManifest, DoesNotMarkAnyTriggerPublishableBeforeRuntimeExists) {
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        EXPECT_NE(std::string(js_scripting_support_status_name(entry.support_status)), "supported")
            << entry.legacy_name;
        EXPECT_NE(std::string(js_scripting_builder_status_name(entry.builder_status)), "supported")
            << entry.legacy_name;
        EXPECT_FALSE(js_scripting_manifest_entry_publishable(entry)) << entry.legacy_name;
    }

    EXPECT_FALSE(js_scripting_manifest_host_publishable(
        JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, JS_SCRIPTING_HOST_ROOM));
}

TEST(JsScriptingManifest, RecordsPlannedJavaScriptHandlersForActiveLegacyTriggers) {
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER,
                            "onBeforeEnter");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, "onDie");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_RECEIVE, "onReceive");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_EXAMINE_OBJECT,
                            "onExamineObject");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY, "onHearSay");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_EAT, "onEat");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_DRINK, "onDrink");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR, "onWear");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_PULL, "onPull");
    expect_deferred_handler(JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_YELL,
                            "onHearYell");
}

TEST(JsScriptingManifest, KeepsUndispatchedBeforeDieReserved) {
    const JsScriptingManifestEntry *entry = find_js_scripting_manifest_entry(
        JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_DIE);

    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->support_status, JsScriptingSupportStatus::Reserved);
    EXPECT_EQ(entry->builder_status, JsScriptingBuilderStatus::Reserved);
    EXPECT_EQ(entry->exception_policy, JsScriptingExceptionPolicy::RejectAtPublish);
    EXPECT_TRUE(entry->blocks_gameplay);
    EXPECT_STREQ(entry->javascript_handler_name, "onBeforeDie");
    EXPECT_NE(std::string(entry->notes).find("not currently dispatched"), std::string::npos);
}

TEST(JsScriptingManifest, RecordsLegacyBlockingAndHostEligibility) {
    const JsScriptingManifestEntry *before_enter = find_js_scripting_manifest_entry(
        JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER);
    const JsScriptingManifestEntry *damage =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE);
    const JsScriptingManifestEntry *wear =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR);
    const JsScriptingManifestEntry *hear_say =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY);

    ASSERT_NE(before_enter, nullptr);
    ASSERT_NE(damage, nullptr);
    ASSERT_NE(wear, nullptr);
    ASSERT_NE(hear_say, nullptr);

    EXPECT_TRUE(before_enter->blocks_gameplay);
    EXPECT_EQ(before_enter->exception_policy, JsScriptingExceptionPolicy::FailClosed);
    EXPECT_TRUE(has_host(*before_enter, JS_SCRIPTING_HOST_CHARACTER));
    EXPECT_FALSE(has_host(*before_enter, JS_SCRIPTING_HOST_OBJECT));

    EXPECT_TRUE(damage->blocks_gameplay);
    EXPECT_EQ(damage->exception_policy, JsScriptingExceptionPolicy::FailClosed);
    EXPECT_TRUE(has_host(*damage, JS_SCRIPTING_HOST_CHARACTER));
    EXPECT_TRUE(has_host(*damage, JS_SCRIPTING_HOST_OBJECT));
    EXPECT_NE(std::string(damage->dispatch_order).find("victim character script first"),
              std::string::npos);

    EXPECT_TRUE(wear->blocks_gameplay);
    EXPECT_EQ(wear->exception_policy, JsScriptingExceptionPolicy::FailClosed);
    EXPECT_TRUE(has_host(*wear, JS_SCRIPTING_HOST_OBJECT));

    EXPECT_FALSE(hear_say->blocks_gameplay);
    EXPECT_NE(std::string(hear_say->notes).find("say/yell compatibility"), std::string::npos);
}

TEST(JsScriptingManifest, RecordsExactHostFlagsAndSemanticsForEveryEntry) {
    struct ExpectedPolicy {
        JsScriptingManifestKind kind;
        int legacy_value;
        unsigned host_flags;
        bool room_owned_publishable;
        bool mudlle_call_mask_required;
        bool blocks_gameplay;
        bool consumes_special_result;
        JsScriptingExceptionPolicy exception_policy;
    };

    const ExpectedPolicy expected_policies[] = {
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER,
         JS_SCRIPTING_HOST_CHARACTER | JS_SCRIPTING_HOST_OBJECT | JS_SCRIPTING_HOST_ROOM, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_ENTER, JS_SCRIPTING_HOST_CHARACTER,
         false, false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_BEFORE_DIE, JS_SCRIPTING_HOST_CHARACTER,
         false, false, true, false, JsScriptingExceptionPolicy::RejectAtPublish},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_DIE, JS_SCRIPTING_HOST_CHARACTER, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_RECEIVE, JS_SCRIPTING_HOST_CHARACTER,
         false, false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_EXAMINE_OBJECT, JS_SCRIPTING_HOST_OBJECT,
         false, false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY, JS_SCRIPTING_HOST_CHARACTER,
         false, false, false, false, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE,
         JS_SCRIPTING_HOST_CHARACTER | JS_SCRIPTING_HOST_OBJECT, false, false, true, false,
         JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_EAT, JS_SCRIPTING_HOST_OBJECT, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_DRINK, JS_SCRIPTING_HOST_OBJECT, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_WEAR, JS_SCRIPTING_HOST_OBJECT, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_PULL, JS_SCRIPTING_HOST_OBJECT, false,
         false, true, false, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_YELL, JS_SCRIPTING_HOST_CHARACTER,
         false, false, false, false, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, false, true, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_SELF, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, false, true, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_ENTER, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, false, true, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DELAY, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, false, false, true, JsScriptingExceptionPolicy::RejectAtPublish},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_TARGET, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, false, true, JsScriptingExceptionPolicy::FailOpen},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DAMAGE, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, true, true, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DEATH, JS_SCRIPTING_HOST_MUDLLE_MOBILE,
         false, true, true, true, JsScriptingExceptionPolicy::FailClosed},
        {JsScriptingManifestKind::MudlleCallFlag, SPECIAL_NONE, 0, false, false, false, false,
         JsScriptingExceptionPolicy::RejectAtPublish},
    };

    EXPECT_EQ(js_scripting_manifest_entry_count(),
              sizeof(expected_policies) / sizeof(expected_policies[0]));
    for (const ExpectedPolicy &expected : expected_policies) {
        const JsScriptingManifestEntry *entry =
            find_js_scripting_manifest_entry(expected.kind, expected.legacy_value);
        ASSERT_NE(entry, nullptr) << expected.legacy_value;
        EXPECT_EQ(entry->host_flags, expected.host_flags) << entry->legacy_name;
        EXPECT_EQ(entry->room_owned_scripts_publishable, expected.room_owned_publishable)
            << entry->legacy_name;
        EXPECT_EQ(entry->mudlle_call_mask_required, expected.mudlle_call_mask_required)
            << entry->legacy_name;
        EXPECT_EQ(entry->blocks_gameplay, expected.blocks_gameplay) << entry->legacy_name;
        EXPECT_EQ(entry->consumes_special_result, expected.consumes_special_result)
            << entry->legacy_name;
        EXPECT_EQ(entry->exception_policy, expected.exception_policy) << entry->legacy_name;
    }
}

TEST(JsScriptingManifest, KeepsMudlleDelayAndNoneUnsupported) {
    const JsScriptingManifestEntry *delay =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DELAY);
    const JsScriptingManifestEntry *none =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_NONE);

    ASSERT_NE(delay, nullptr);
    ASSERT_NE(none, nullptr);

    EXPECT_EQ(delay->support_status, JsScriptingSupportStatus::Unsupported);
    EXPECT_EQ(delay->builder_status, JsScriptingBuilderStatus::Unsupported);
    EXPECT_EQ(delay->exception_policy, JsScriptingExceptionPolicy::RejectAtPublish);
    EXPECT_TRUE(delay->consumes_special_result);
    EXPECT_NE(std::string(delay->notes).find("unsupported"), std::string::npos);

    EXPECT_EQ(none->support_status, JsScriptingSupportStatus::Unsupported);
    EXPECT_EQ(none->builder_status, JsScriptingBuilderStatus::Unsupported);
    EXPECT_EQ(none->exception_policy, JsScriptingExceptionPolicy::RejectAtPublish);
    EXPECT_EQ(none->host_flags, 0u);
    EXPECT_STREQ(none->javascript_handler_name, "");
}

TEST(JsScriptingManifest, RecordsMudlleCallFlagHandlersAsDeferredExceptUnsupportedFlags) {
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND,
                            "onSpecialCommand");
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_SELF, "onSpecialSelf");
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_ENTER,
                            "onSpecialEnter");
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_TARGET,
                            "onSpecialTarget");
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DAMAGE,
                            "onSpecialDamage");
    expect_deferred_handler(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DEATH,
                            "onSpecialDeath");

    const JsScriptingManifestEntry *command =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND);
    const JsScriptingManifestEntry *self =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_SELF);
    const JsScriptingManifestEntry *damage =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::MudlleCallFlag, SPECIAL_DAMAGE);

    ASSERT_NE(command, nullptr);
    ASSERT_NE(self, nullptr);
    ASSERT_NE(damage, nullptr);

    EXPECT_TRUE(command->consumes_special_result);
    EXPECT_TRUE(command->mudlle_call_mask_required);
    EXPECT_EQ(command->exception_policy, JsScriptingExceptionPolicy::FailOpen);
    EXPECT_TRUE(has_host(*command, JS_SCRIPTING_HOST_MUDLLE_MOBILE));
    EXPECT_TRUE(has_host(*self, JS_SCRIPTING_HOST_MUDLLE_MOBILE));
    EXPECT_TRUE(damage->blocks_gameplay);
    EXPECT_EQ(damage->exception_policy, JsScriptingExceptionPolicy::FailClosed);
    EXPECT_NE(std::string(damage->notes).find(".scr ON_DAMAGE"), std::string::npos);
}

TEST(JsScriptingManifest, KeepsRoomOwnedJavaScriptPublishingDeferred) {
    const JsScriptingManifestEntry *enter =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER);

    ASSERT_NE(enter, nullptr);
    EXPECT_TRUE(has_host(*enter, JS_SCRIPTING_HOST_ROOM));
    EXPECT_FALSE(enter->room_owned_scripts_publishable);
    EXPECT_NE(std::string(enter->notes).find("room-owned JavaScript publishing is deferred"),
              std::string::npos);
}

TEST(JsScriptingManifest, RecordsRequiredContextFieldsForLegacyTriggers) {
    expect_context_fields(ON_ENTER, {"self", "actor", "room", "trigger", "hostType"});
    expect_context_fields(ON_BEFORE_ENTER, {"self", "actor", "room", "trigger", "hostType"});
    expect_context_fields(ON_BEFORE_DIE, {"self", "killer", "trigger", "hostType"});
    expect_context_fields(ON_DIE, {"self", "killer", "trigger", "hostType"});
    expect_context_fields(ON_RECEIVE, {"self", "actor", "object", "trigger", "hostType"});
    expect_context_fields(ON_EXAMINE_OBJECT, {"object", "actor", "trigger", "hostType"});
    expect_context_fields(ON_HEAR_SAY, {"self", "speaker", "text", "trigger", "hostType"});
    expect_context_fields(ON_DAMAGE, {"self", "object", "actor", "trigger", "hostType"});
    expect_context_fields(ON_EAT, {"object", "actor", "trigger", "hostType"});
    expect_context_fields(ON_DRINK, {"object", "actor", "trigger", "hostType"});
    expect_context_fields(ON_WEAR, {"object", "actor", "wearSlot", "trigger", "hostType"});
    expect_context_fields(ON_PULL, {"object", "actor", "trigger", "hostType"});
    expect_context_fields(ON_HEAR_YELL, {"self", "speaker", "text", "trigger", "hostType"});
}

TEST(JsScriptingManifest, RecordsRequiredContextFieldsForMudlleCallFlags) {
    struct ExpectedContext {
        int legacy_value;
        std::initializer_list<const char *> fields;
    };

    const ExpectedContext expected_contexts[] = {
        {SPECIAL_COMMAND,
         {"self", "actor", "command", "args", "target", "room", "trigger", "hostType"}},
        {SPECIAL_SELF, {"self", "room", "tick", "trigger", "hostType"}},
        {SPECIAL_ENTER,
         {"self", "actor", "direction", "reverseDirection", "room", "trigger", "hostType"}},
        {SPECIAL_DELAY, {"self", "continuation", "trigger", "hostType"}},
        {SPECIAL_TARGET,
         {"self", "actor", "command", "args", "targ1", "targ2", "targetTypes", "trigger",
          "hostType"}},
        {SPECIAL_DAMAGE, {"self", "attacker", "victim", "target", "trigger", "hostType"}},
        {SPECIAL_DEATH, {"self", "actor", "dying", "target", "trigger", "hostType"}},
    };

    for (const ExpectedContext &expected : expected_contexts) {
        const JsScriptingManifestEntry *entry = find_js_scripting_manifest_entry(
            JsScriptingManifestKind::MudlleCallFlag, expected.legacy_value);
        ASSERT_NE(entry, nullptr) << expected.legacy_value;
        const std::string context = entry->context_fields;
        for (const char *field : expected.fields)
            EXPECT_NE(context.find(field), std::string::npos) << entry->legacy_name << " " << field;
    }
}

TEST(JsScriptingManifest, DocumentsSayYellCompatibilityPolicy) {
    const JsScriptingManifestEntry *hear_say =
        find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_SAY);
    const JsScriptingManifestEntry *hear_yell = find_js_scripting_manifest_entry(
        JsScriptingManifestKind::LegacyScriptTrigger, ON_HEAR_YELL);

    ASSERT_NE(hear_say, nullptr);
    ASSERT_NE(hear_yell, nullptr);

    EXPECT_NE(std::string(hear_say->dispatch_order).find("ON_HEAR_YELL"), std::string::npos);
    EXPECT_NE(std::string(hear_yell->dispatch_order).find("ON_HEAR_SAY"), std::string::npos);
    EXPECT_NE(std::string(hear_say->notes).find("Preserve the say/yell compatibility quirk"),
              std::string::npos);
    EXPECT_NE(std::string(hear_yell->notes).find("Preserve the say/yell compatibility quirk"),
              std::string::npos);
}

TEST(JsScriptingManifest, DeniesDangerousLegacyMutationApisByDefault) {
    const int dangerous_commands[] = {
        SCRIPT_DO_HIT,        SCRIPT_DO_FLEE,        SCRIPT_LOAD_MOB,    SCRIPT_TELEPORT_CHAR,
        SCRIPT_EXTRACT_CHAR,  SCRIPT_SET_EXIT_STATE, SCRIPT_RAW_KILL,    SCRIPT_DO_GIVE,
        SCRIPT_LOAD_OBJ,      SCRIPT_OBJ_TO_CHAR,    SCRIPT_OBJ_TO_ROOM, SCRIPT_CHANGE_EXIT_TO,
        SCRIPT_EXTRACT_OBJ,   SCRIPT_DO_DROP,        SCRIPT_DO_REMOVE,   SCRIPT_DO_WEAR,
        SCRIPT_DO_SOCIAL,     SCRIPT_DO_WAIT,        SCRIPT_GAIN_EXP,    SCRIPT_TELEPORT_CHAR_X,
        SCRIPT_OBJ_FROM_ROOM, SCRIPT_OBJ_FROM_CHAR,  SCRIPT_EQUIP_CHAR,  SCRIPT_TELEPORT_CHAR_XL,
        SCRIPT_LOAD_OBJ_X,
    };

    EXPECT_EQ(js_scripting_api_permission_entry_count(),
              sizeof(dangerous_commands) / sizeof(dangerous_commands[0]));
    std::set<int> seen_commands;
    for (int command : dangerous_commands) {
        const JsScriptingApiPermissionEntry *entry =
            find_js_scripting_api_permission_entry(command);
        ASSERT_NE(entry, nullptr) << command;
        EXPECT_TRUE(seen_commands.insert(entry->legacy_command).second) << entry->legacy_name;
        EXPECT_EQ(entry->status, JsScriptingApiPermissionStatus::Unsupported) << entry->legacy_name;
        EXPECT_FALSE(js_scripting_api_permission_is_allowed(command)) << entry->legacy_name;
        EXPECT_FALSE(std::string(entry->reason).empty())
            << entry->legacy_name << ": " << entry->reason;
    }

    EXPECT_FALSE(js_scripting_api_permission_is_allowed(SCRIPT_DO_SAY))
        << "Missing API permission entries must fail closed until explicitly allowed.";
    EXPECT_EQ(find_js_scripting_api_permission_entry(SCRIPT_DO_SAY), nullptr);
    EXPECT_FALSE(js_scripting_api_permission_is_allowed(SCRIPT_SEND_TO_CHAR));
    EXPECT_FALSE(js_scripting_api_permission_is_allowed(SCRIPT_DO_FOLLOW));
    EXPECT_FALSE(js_scripting_api_permission_is_allowed(SCRIPT_SET_INT_WAR_STATUS));
    EXPECT_FALSE(js_scripting_api_permission_is_allowed(SCRIPT_PAGE_ZONE_MAP));
    EXPECT_FALSE(js_scripting_api_permission_is_allowed(9999));
}

TEST(JsScriptingManifest, ObjectHostContextsExposeObjectAlias) {
    const int object_triggers[] = {
        ON_EXAMINE_OBJECT, ON_EAT, ON_DRINK, ON_WEAR, ON_PULL,
    };

    for (int trigger : object_triggers) {
        const JsScriptingManifestEntry *entry =
            find_js_scripting_manifest_entry(JsScriptingManifestKind::LegacyScriptTrigger, trigger);
        ASSERT_NE(entry, nullptr) << trigger;
        EXPECT_TRUE(has_host(*entry, JS_SCRIPTING_HOST_OBJECT)) << entry->legacy_name;
        EXPECT_NE(std::string(entry->context_fields).find("object"), std::string::npos)
            << entry->legacy_name;
        EXPECT_EQ(std::string(entry->context_fields).find("self"), std::string::npos)
            << entry->legacy_name;
    }
}

TEST(JsScriptingManifest, HasStablePublicStringNames) {
    EXPECT_STREQ(js_scripting_manifest_kind_name(JsScriptingManifestKind::LegacyScriptTrigger),
                 "legacy-script-trigger");
    EXPECT_STREQ(js_scripting_manifest_kind_name(JsScriptingManifestKind::MudlleCallFlag),
                 "mudlle-call-flag");
    EXPECT_STREQ(js_scripting_support_status_name(JsScriptingSupportStatus::Deferred), "deferred");
    EXPECT_STREQ(js_scripting_support_status_name(JsScriptingSupportStatus::Reserved), "reserved");
    EXPECT_STREQ(js_scripting_support_status_name(JsScriptingSupportStatus::Unsupported),
                 "unsupported");
    EXPECT_STREQ(js_scripting_builder_status_name(JsScriptingBuilderStatus::Deferred), "deferred");
    EXPECT_STREQ(js_scripting_builder_status_name(JsScriptingBuilderStatus::Reserved), "reserved");
    EXPECT_STREQ(js_scripting_builder_status_name(JsScriptingBuilderStatus::Unsupported),
                 "unsupported");
    EXPECT_STREQ(js_scripting_exception_policy_name(JsScriptingExceptionPolicy::FailClosed),
                 "fail-closed");
    EXPECT_STREQ(js_scripting_exception_policy_name(JsScriptingExceptionPolicy::FailOpen),
                 "fail-open");
    EXPECT_STREQ(js_scripting_exception_policy_name(JsScriptingExceptionPolicy::RejectAtPublish),
                 "reject-at-publish");
    EXPECT_STREQ(js_scripting_api_permission_status_name(JsScriptingApiPermissionStatus::Deferred),
                 "deferred");
    EXPECT_STREQ(
        js_scripting_api_permission_status_name(JsScriptingApiPermissionStatus::Unsupported),
        "unsupported");
}
