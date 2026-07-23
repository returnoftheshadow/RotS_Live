#include "js_runtime.h"
#include "js_source_policy.h"

extern "C" {
#include "../third_party/quickjs/quickjs.h"
}

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace {

constexpr std::size_t MaxDiagnosticLength = 240;

struct InterruptState {
    std::uint64_t remaining_budget = 0;
    bool interrupted = false;
};

struct RuntimeContextData {
    InterruptState *interrupt_state = nullptr;
    const JsRuntimeNativeCommandResultOptions *command_result_options = nullptr;
};

int interrupt_handler(JSRuntime *, void *opaque) {
    RuntimeContextData *context_data = static_cast<RuntimeContextData *>(opaque);
    InterruptState *state = context_data != nullptr ? context_data->interrupt_state : nullptr;
    if (state == nullptr)
        return 1;
    if (state->remaining_budget == 0) {
        state->interrupted = true;
        return 1;
    }
    --state->remaining_budget;
    return 0;
}

JSValue native_command_result(JSContext *context, JSValueConst, int argc, JSValueConst *argv) {
    RuntimeContextData *context_data =
        static_cast<RuntimeContextData *>(JS_GetContextOpaque(context));
    if (context_data == nullptr || context_data->command_result_options == nullptr ||
        context_data->command_result_options->callback == nullptr) {
        return JS_UNDEFINED;
    }
    if (argc != 2 || !JS_IsString(argv[0]) || !JS_IsString(argv[1])) {
        return JS_ThrowTypeError(context, "native command result expects operation and arguments");
    }

    const char *operation_chars = JS_ToCString(context, argv[0]);
    if (operation_chars == nullptr)
        return JS_EXCEPTION;
    std::string operation = operation_chars;
    JS_FreeCString(context, operation_chars);

    const char *arguments_chars = JS_ToCString(context, argv[1]);
    if (arguments_chars == nullptr)
        return JS_EXCEPTION;
    std::string arguments_json = arguments_chars;
    JS_FreeCString(context, arguments_chars);

    const std::string result_json = context_data->command_result_options->callback(
        operation, arguments_json, context_data->command_result_options->user_data);
    return JS_NewStringLen(context, result_json.data(), result_json.size());
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
    static const char *hardening_source =
        "(function() {\n"
        "  Object.defineProperty(Object.prototype, 'constructor', { value: undefined, "
        "writable: false, configurable: false });\n"
        "  const functionPrototype = Object.getPrototypeOf(function() {});\n"
        "  Object.defineProperty(functionPrototype, 'constructor', { value: undefined, "
        "writable: false, configurable: false });\n"
        "  const asyncFunctionPrototype = Object.getPrototypeOf(async function () {});\n"
        "  Object.defineProperty(asyncFunctionPrototype, 'constructor', { value: undefined, "
        "writable: false, configurable: false });\n"
        "  const generatorFunctionPrototype = Object.getPrototypeOf(function* () {});\n"
        "  Object.defineProperty(generatorFunctionPrototype, 'constructor', { value: undefined, "
        "writable: false, configurable: false });\n"
        "  const asyncGeneratorFunctionPrototype = Object.getPrototypeOf(async function* () {});\n"
        "  Object.defineProperty(asyncGeneratorFunctionPrototype, 'constructor', { value: "
        "undefined, "
        "writable: false, configurable: false });\n"
        "  Object.defineProperty(Array.prototype, 'constructor', { value: undefined, "
        "writable: false, configurable: false });\n"
        "}());\n";
    JSValue value = JS_Eval(context, hardening_source, std::strlen(hardening_source),
                            "<rots-runtime-hardening>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    JS_FreeValue(context, value);
}

bool install_native_command_result(
    JSContext *context, const JsRuntimeNativeCommandResultOptions &command_result_options) {
    if (command_result_options.callback == nullptr)
        return true;

    JSValue global = JS_GetGlobalObject(context);
    JSValue function =
        JS_NewCFunction(context, native_command_result, "__rotsNativeCommandResult", 2);
    if (JS_IsException(function)) {
        JS_FreeValue(context, global);
        return false;
    }

    const int define_status =
        JS_DefinePropertyValueStr(context, global, "__rotsNativeCommandResult", function,
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_FreeValue(context, global);
    return define_status >= 0;
}

} // namespace

struct JsRuntime::Impl {
    JsRuntimeLimits limits;
};

JsRuntime::JsRuntime(const JsRuntimeLimits &limits) : m_impl(new Impl) { m_impl->limits = limits; }

JsRuntime::~JsRuntime() { delete m_impl; }

JsRuntimeEvalResult JsRuntime::evaluate(const std::string &source, const char *filename) {
    JsRuntimeEvalResult result;
    const std::vector<JsSourcePolicyViolation> policy_violations =
        js_source_policy_validate(source);
    if (!policy_violations.empty()) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = clamp_diagnostic(policy_violations.front().message);
        return result;
    }

    return evaluate_trusted_wrapped_source(source, filename);
}

JsRuntimeEvalResult JsRuntime::evaluate_trusted_wrapped_source(const std::string &source,
                                                               const char *filename) {
    return evaluate_trusted_wrapped_source(source, filename, {});
}

JsRuntimeEvalResult JsRuntime::evaluate_trusted_wrapped_source(
    const std::string &source, const char *filename,
    const JsRuntimeNativeCommandResultOptions &command_result_options) {
    JsRuntimeEvalResult result;

    InterruptState interrupt_state;
    interrupt_state.remaining_budget = m_impl->limits.instruction_budget;
    RuntimeContextData context_data;
    context_data.interrupt_state = &interrupt_state;
    context_data.command_result_options = &command_result_options;

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
    JS_SetInterruptHandler(runtime, interrupt_handler, &context_data);
    JS_SetModuleLoaderFunc(runtime, nullptr, deny_module_load, nullptr);

    JSContext *context = JS_NewContext(runtime);
    if (context == nullptr) {
        JS_FreeRuntime(runtime);
        result.status = JsRuntimeStatus::OutOfMemory;
        result.diagnostic = "JavaScript runtime initialization failed";
        return result;
    }
    configure_context(context);
    JS_SetContextOpaque(context, &context_data);
    if (!install_native_command_result(context, command_result_options)) {
        result.status = JsRuntimeStatus::Error;
        result.diagnostic = "JavaScript runtime initialization failed";
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return result;
    }

    JSValue value = JS_Eval(context, source.c_str(), source.size(), filename,
                            JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    if (JS_IsException(value)) {
        result.status =
            interrupt_state.interrupted ? JsRuntimeStatus::Interrupted : JsRuntimeStatus::Error;
        result.diagnostic = exception_to_string(context);
        if (interrupt_state.interrupted)
            result.status = JsRuntimeStatus::Interrupted;
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

    if (JS_IsString(value)) {
        const char *string_value = JS_ToCString(context, value);
        if (string_value == nullptr) {
            result.status = JsRuntimeStatus::Error;
            result.diagnostic = exception_to_string(context);
            JS_FreeValue(context, value);
            JS_FreeContext(context);
            JS_FreeRuntime(runtime);
            return result;
        }
        result.has_string_value = true;
        result.string_value = string_value;
        JS_FreeCString(context, string_value);
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
