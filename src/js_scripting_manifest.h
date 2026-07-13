#ifndef JS_SCRIPTING_MANIFEST_H
#define JS_SCRIPTING_MANIFEST_H

#include <cstddef>

enum class JsScriptingManifestKind {
    LegacyScriptTrigger,
    MudlleCallFlag,
};

enum class JsScriptingSupportStatus {
    Deferred,
    Reserved,
    Unsupported,
};

enum class JsScriptingBuilderStatus {
    Deferred,
    Reserved,
    Unsupported,
};

enum class JsScriptingExceptionPolicy {
    FailClosed,
    FailOpen,
    RejectAtPublish,
};

enum class JsScriptingApiPermissionStatus {
    Deferred,
    Unsupported,
};

enum JsScriptingHostFlag {
    JS_SCRIPTING_HOST_CHARACTER = 1 << 0,
    JS_SCRIPTING_HOST_OBJECT = 1 << 1,
    JS_SCRIPTING_HOST_ROOM = 1 << 2,
    JS_SCRIPTING_HOST_MUDLLE_MOBILE = 1 << 3,
};

struct JsScriptingManifestEntry {
    JsScriptingManifestKind kind;
    int legacy_value;
    const char* legacy_name;
    const char* javascript_handler_name;
    JsScriptingSupportStatus support_status;
    JsScriptingBuilderStatus builder_status;
    unsigned host_flags;
    bool room_owned_scripts_publishable;
    bool mudlle_call_mask_required;
    bool blocks_gameplay;
    bool consumes_special_result;
    JsScriptingExceptionPolicy exception_policy;
    const char* dispatch_order;
    const char* context_fields;
    const char* notes;
};

struct JsScriptingApiPermissionEntry {
    int legacy_command;
    const char* legacy_name;
    const char* javascript_api_name;
    JsScriptingApiPermissionStatus status;
    const char* reason;
};

struct JsScriptingManifestMetadata {
    int schema_version;
    int package_format_version;
    int trigger_catalog_revision;
    const char* manifest_checksum;
    const char* api_version;
    const char* selected_runtime_name;
    const char* selected_runtime_version;
    const char* minimum_supported_runtime_version;
    const char* runtime_feature_flags;
    const char* generated_typings_version;
};

const JsScriptingManifestMetadata& js_scripting_manifest_metadata();

const JsScriptingManifestEntry* js_scripting_manifest_entries();
std::size_t js_scripting_manifest_entry_count();
const JsScriptingManifestEntry* find_js_scripting_manifest_entry(JsScriptingManifestKind kind,
    int legacy_value);
bool js_scripting_manifest_entry_publishable(const JsScriptingManifestEntry& entry);
bool js_scripting_manifest_host_publishable(JsScriptingManifestKind kind, int legacy_value,
    JsScriptingHostFlag host_flag);

const JsScriptingApiPermissionEntry* js_scripting_api_permission_entries();
std::size_t js_scripting_api_permission_entry_count();
const JsScriptingApiPermissionEntry* find_js_scripting_api_permission_entry(int legacy_command);
bool js_scripting_api_permission_is_allowed(int legacy_command);

const char* js_scripting_manifest_kind_name(JsScriptingManifestKind kind);
const char* js_scripting_support_status_name(JsScriptingSupportStatus status);
const char* js_scripting_builder_status_name(JsScriptingBuilderStatus status);
const char* js_scripting_exception_policy_name(JsScriptingExceptionPolicy policy);
const char* js_scripting_api_permission_status_name(JsScriptingApiPermissionStatus status);

#endif
