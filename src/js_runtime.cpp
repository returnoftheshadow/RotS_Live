#include "js_runtime.h"

extern "C" {
#include "../third_party/quickjs/quickjs.h"
}

#include <algorithm>
#include <cstring>
#include <string>

namespace {

constexpr std::size_t MaxDiagnosticLength = 240;

struct InterruptState {
    std::uint64_t remaining_budget = 0;
    bool interrupted = false;
};

int interrupt_handler(JSRuntime *, void *opaque) {
    InterruptState *state = static_cast<InterruptState *>(opaque);
    if (state == nullptr)
        return 1;
    if (state->remaining_budget == 0) {
        state->interrupted = true;
        return 1;
    }
    --state->remaining_budget;
    return 0;
}

std::string clamp_diagnostic(std::string diagnostic) {
    diagnostic.erase(std::remove(diagnostic.begin(), diagnostic.end(), '\r'), diagnostic.end());
    diagnostic.erase(std::remove(diagnostic.begin(), diagnostic.end(), '\n'), diagnostic.end());
    if (diagnostic.size() > MaxDiagnosticLength)
        diagnostic.resize(MaxDiagnosticLength);
    return diagnostic;
}

std::string exception_to_string(JSContext *context) {
    JSValue exception = JS_GetException(context);
    const char *exception_text = JS_ToCString(context, exception);
    std::string diagnostic = exception_text != nullptr ? exception_text : "JavaScript exception";
    if (exception_text != nullptr)
        JS_FreeCString(context, exception_text);
    JS_FreeValue(context, exception);
    return clamp_diagnostic(diagnostic);
}

bool contains_dynamic_import(const std::string &source) {
    return source.find("import(") != std::string::npos ||
           source.find("import (") != std::string::npos;
}

JSModuleDef *deny_module_load(JSContext *context, const char *module_name, void *) {
    JS_ThrowReferenceError(context, "module loading is not supported: %s", module_name);
    return nullptr;
}

void delete_global_property(JSContext *context, const char *name) {
    JSValue global = JS_GetGlobalObject(context);
    JSAtom atom = JS_NewAtom(context, name);
    if (atom != JS_ATOM_NULL) {
        JS_DeleteProperty(context, global, atom, 0);
        JS_FreeAtom(context, atom);
    }
    JS_FreeValue(context, global);
}

void configure_context(JSContext *context) {
    delete_global_property(context, "eval");
    delete_global_property(context, "Function");
}

} // namespace

struct JsRuntime::Impl {
    JsRuntimeLimits limits;
};

JsRuntime::JsRuntime(const JsRuntimeLimits &limits) : m_impl(new Impl) { m_impl->limits = limits; }

JsRuntime::~JsRuntime() { delete m_impl; }

JsRuntimeEvalResult JsRuntime::evaluate(const std::string &source, const char *filename) {
    JsRuntimeEvalResult result;
    if (contains_dynamic_import(source)) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "dynamic import is not supported";
        return result;
    }

    InterruptState interrupt_state;
    interrupt_state.remaining_budget = m_impl->limits.instruction_budget;

    JSRuntime *runtime = JS_NewRuntime();
    if (runtime == nullptr) {
        result.status = JsRuntimeStatus::OutOfMemory;
        result.diagnostic = "JavaScript runtime initialization failed";
        return result;
    }

    JS_SetMemoryLimit(runtime, m_impl->limits.memory_limit_bytes);
    JS_SetMaxStackSize(runtime, m_impl->limits.stack_limit_bytes);
    JS_SetCanBlock(runtime, false);
    JS_SetStripInfo(runtime, JS_STRIP_DEBUG);
    JS_SetInterruptHandler(runtime, interrupt_handler, &interrupt_state);
    JS_SetModuleLoaderFunc(runtime, nullptr, deny_module_load, nullptr);

    JSContext *context = JS_NewContext(runtime);
    if (context == nullptr) {
        JS_FreeRuntime(runtime);
        result.status = JsRuntimeStatus::OutOfMemory;
        result.diagnostic = "JavaScript runtime initialization failed";
        return result;
    }
    configure_context(context);

    JSValue value = JS_Eval(context, source.c_str(), source.size(), filename,
                            JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(value)) {
        result.status =
            interrupt_state.interrupted ? JsRuntimeStatus::Interrupted : JsRuntimeStatus::Error;
        result.diagnostic = exception_to_string(context);
        if (result.diagnostic.find("out of memory") != std::string::npos)
            result.status = JsRuntimeStatus::OutOfMemory;
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return result;
    }

    if (JS_IsUndefined(value)) {
        result.status = JsRuntimeStatus::Ok;
        result.value = JsRuntimeValue::Allow;
        JS_FreeValue(context, value);
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return result;
    }

    if (JS_PromiseState(context, value) != static_cast<JSPromiseStateEnum>(-1)) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "promise results are not supported";
        JS_FreeValue(context, value);
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return result;
    }

    const int boolean_value = JS_ToBool(context, value);
    JS_FreeValue(context, value);
    if (boolean_value < 0) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = exception_to_string(context);
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return result;
    }

    result.status = JsRuntimeStatus::Ok;
    result.value = boolean_value ? JsRuntimeValue::Allow : JsRuntimeValue::Block;
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return result;
}

const char *js_runtime_status_name(JsRuntimeStatus status) {
    switch (status) {
    case JsRuntimeStatus::Ok:
        return "ok";
    case JsRuntimeStatus::Error:
        return "error";
    case JsRuntimeStatus::Interrupted:
        return "interrupted";
    case JsRuntimeStatus::OutOfMemory:
        return "out-of-memory";
    }
    return "unknown";
}

const char *js_runtime_value_name(JsRuntimeValue value) {
    switch (value) {
    case JsRuntimeValue::Allow:
        return "allow";
    case JsRuntimeValue::Block:
        return "block";
    }
    return "unknown";
}
