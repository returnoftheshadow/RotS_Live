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
    std::string kind;
    std::string target_type;
    std::string target_id;
    std::string property;
    std::string value_kind;
    bool has_value = false;
    std::string value;
    std::string operation;
    std::string target_token;
    std::string arguments_json;
    bool command_result_bridge_accepted = false;
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

using JsRuntimeNativeCommandResultCallback = std::string (*)(const std::string &operation,
                                                             const std::string &arguments_json,
                                                             void *user_data);

struct JsRuntimeNativeCommandResultOptions {
    JsRuntimeNativeCommandResultCallback callback = nullptr;
    void *user_data = nullptr;
};

class JsRuntime {
  public:
    explicit JsRuntime(const JsRuntimeLimits &limits = {});
    ~JsRuntime();

    JsRuntime(const JsRuntime &) = delete;
    JsRuntime &operator=(const JsRuntime &) = delete;

    JsRuntimeEvalResult evaluate(const std::string &source, const char *filename = "script.js");
    JsRuntimeEvalResult evaluate_trusted_wrapped_source(const std::string &source,
                                                        const char *filename = "script.js");
    JsRuntimeEvalResult evaluate_trusted_wrapped_source(
        const std::string &source, const char *filename,
        const JsRuntimeNativeCommandResultOptions &command_result_options);

  private:
    struct Impl;
    Impl *m_impl;
};

const char *js_runtime_status_name(JsRuntimeStatus status);
const char *js_runtime_value_name(JsRuntimeValue value);

#endif
