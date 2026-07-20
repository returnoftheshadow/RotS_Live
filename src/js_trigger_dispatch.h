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
#include <unordered_map>

enum class JsTriggerDispatchStatus {
    NoMatch,
    Allow,
    Block,
    Error,
    BudgetExceeded,
    DepthExceeded,
};

struct JsTriggerDispatchBudgetLimits {
    std::size_t max_invocations_per_pulse = 0;
    std::size_t max_invocations_per_package_per_pulse = 0;
};

class JsTriggerDispatchBudget {
  public:
    bool try_consume(int pulse, int package_vnum, const JsTriggerDispatchBudgetLimits& limits);
    void reset();

  private:
    int m_current_pulse = 0;
    bool m_has_current_pulse = false;
    std::size_t m_pulse_invocations = 0;
    std::unordered_map<int, std::size_t> m_package_invocations;
};

struct JsTriggerDispatchDepthLimits {
    std::size_t max_dispatch_depth = 0;
};

class JsTriggerDispatchDepthGuard {
  public:
    bool would_exceed(const JsTriggerDispatchDepthLimits& limits) const;
    bool try_enter(const JsTriggerDispatchDepthLimits& limits);
    void leave();
    void reset();
    std::size_t current_depth() const;

  private:
    std::size_t m_current_depth = 0;
};

struct JsTriggerDispatchOptions {
    JsRuntimeLimits runtime_limits;
    JsTriggerDispatchBudget* budget = nullptr;
    JsTriggerDispatchBudgetLimits budget_limits;
    JsTriggerDispatchDepthGuard* depth_guard = nullptr;
    JsTriggerDispatchDepthLimits depth_limits;
    int current_pulse = 0;
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
JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& adapter_options,
    const JsTriggerDispatchOptions& options);

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService& service, const JsTriggerDispatchRequest& request,
    const JsGameAdapterOptions& adapter_options, const JsRuntimeLimits& limits = {});
JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService& service, const JsTriggerDispatchRequest& request,
    const JsGameAdapterOptions& adapter_options, const JsTriggerDispatchOptions& options);

#endif
