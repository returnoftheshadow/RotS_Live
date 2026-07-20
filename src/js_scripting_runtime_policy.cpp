#include "js_scripting_runtime_policy.h"

const JsScriptingRuntimeSafetyPolicy& js_scripting_runtime_safety_policy()
{
    static const JsScriptingRuntimeSafetyPolicy policy = {
        {},
        { 1024, 256 },
        { 8 },
        16,
        "Server logs only actionable JavaScript dispatch failures with bounded metadata and "
        "generic diagnostics. Source text, actor speech, tokens, raw pointers, stack details, and "
        "builder-controlled package ids are omitted; repeated failures are capped per server pulse."
    };
    return policy;
}
