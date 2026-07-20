#ifndef JS_SCRIPTING_RUNTIME_POLICY_H
#define JS_SCRIPTING_RUNTIME_POLICY_H

#include "js_runtime.h"
#include "js_trigger_dispatch.h"

#include <cstddef>

struct JsScriptingRuntimeSafetyPolicy {
    JsRuntimeLimits runtime_limits;
    JsTriggerDispatchBudgetLimits budget_limits;
    JsTriggerDispatchDepthLimits depth_limits;
    std::size_t max_dispatch_failure_logs_per_pulse = 0;
    const char* failure_logging_policy = "";
};

const JsScriptingRuntimeSafetyPolicy& js_scripting_runtime_safety_policy();

#endif
