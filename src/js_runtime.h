#ifndef JS_RUNTIME_H
#define JS_RUNTIME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class JsRuntimeStatus {
    Ok,
    Error,
    Interrupted,
    OutOfMemory,
};

enum class JsRuntimeValue {
    Allow,
    Block,
};

struct JsRuntimeMutation {
    std::string target_type;
    std::string target_id;
    std::string property;
    bool has_value = false;
    std::string value;
};

struct JsRuntimeLimits {
    std::size_t memory_limit_bytes = 1024 * 1024;
    std::size_t stack_limit_bytes = 256 * 1024;
    std::uint64_t instruction_budget = 100000;
};

struct JsRuntimeEvalResult {
    JsRuntimeStatus status = JsRuntimeStatus::Error;
    JsRuntimeValue value = JsRuntimeValue::Allow;
    std::string diagnostic;
    bool has_string_value = false;
    std::string string_value;
    std::vector<JsRuntimeMutation> mutations;
};

class JsRuntime {
public:
    explicit JsRuntime(const JsRuntimeLimits& limits = {});
    ~JsRuntime();

    JsRuntime(const JsRuntime&) = delete;
    JsRuntime& operator=(const JsRuntime&) = delete;

    JsRuntimeEvalResult evaluate(const std::string& source, const char* filename = "script.js");

private:
    struct Impl;
    Impl* m_impl;
};

const char* js_runtime_status_name(JsRuntimeStatus status);
const char* js_runtime_value_name(JsRuntimeValue value);

#endif
