#ifndef JS_TRIGGER_DISPATCH_H
#define JS_TRIGGER_DISPATCH_H

#include "js_game_adapter.h"
#include "js_live_registry_reload_service.h"
#include "js_runtime.h"
#include "js_script_package.h"
#include "js_script_registry.h"
#include "js_scripting_manifest.h"

#include <cstddef>
#include <string>

enum class JsTriggerDispatchStatus {
    NoMatch,
    Allow,
    Block,
    Error,
};

struct JsTriggerDispatchRequest {
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    JsScriptingManifestKind kind = JsScriptingManifestKind::LegacyScriptTrigger;
    int legacy_value = 0;
    int package_vnum = 0;
    JsGameAdapterContextInput context_input;
};

struct JsTriggerDispatchResult {
    JsTriggerDispatchStatus status = JsTriggerDispatchStatus::NoMatch;
    JsRuntimeStatus runtime_status = JsRuntimeStatus::Ok;
    int package_vnum = 0;
    std::string package_id;
    std::string handler_name;
    std::string diagnostic;
    std::size_t matched_package_count = 0;
};

const char* js_trigger_dispatch_status_name(JsTriggerDispatchStatus status);

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& adapter_options,
    const JsRuntimeLimits& limits = {});

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService& service, const JsTriggerDispatchRequest& request,
    const JsGameAdapterOptions& adapter_options, const JsRuntimeLimits& limits = {});

#endif
