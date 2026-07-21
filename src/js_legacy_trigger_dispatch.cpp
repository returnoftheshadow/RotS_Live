#include "js_legacy_trigger_dispatch.h"

#include <cctype>
#include <sstream>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 220;

std::string bounded_single_line(std::string message) {
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

JsLegacyTriggerDispatchStatus facade_status_from_dispatch(JsTriggerDispatchStatus status) {
    switch (status) {
    case JsTriggerDispatchStatus::NoMatch:
        return JsLegacyTriggerDispatchStatus::NoMatch;
    case JsTriggerDispatchStatus::Allow:
        return JsLegacyTriggerDispatchStatus::Allow;
    case JsTriggerDispatchStatus::Block:
        return JsLegacyTriggerDispatchStatus::Block;
    case JsTriggerDispatchStatus::Error:
        return JsLegacyTriggerDispatchStatus::Error;
    case JsTriggerDispatchStatus::BudgetExceeded:
        return JsLegacyTriggerDispatchStatus::BudgetExceeded;
    case JsTriggerDispatchStatus::DepthExceeded:
        return JsLegacyTriggerDispatchStatus::DepthExceeded;
    }
    return JsLegacyTriggerDispatchStatus::Error;
}

bool is_identifier_start(unsigned char ch) {
    return std::isalpha(ch) || ch == '_' || ch == '$';
}

bool is_identifier_continue(unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '$';
}

bool is_safe_handler_identifier(const std::string &handler_name) {
    if (handler_name.empty())
        return false;
    if (!is_identifier_start(static_cast<unsigned char>(handler_name[0])))
        return false;
    for (std::size_t index = 1; index < handler_name.size(); ++index) {
        if (!is_identifier_continue(static_cast<unsigned char>(handler_name[index])))
            return false;
    }
    return true;
}

std::string safe_log_diagnostic(const JsLegacyTriggerDispatchResult &result) {
    switch (result.status) {
    case JsLegacyTriggerDispatchStatus::RegistryNotReady:
        return "registry-not-ready";
    case JsLegacyTriggerDispatchStatus::StaleRegistry:
        return "stale-registry";
    case JsLegacyTriggerDispatchStatus::Error:
        return "runtime-error";
    case JsLegacyTriggerDispatchStatus::BudgetExceeded:
        return "budget-exceeded";
    case JsLegacyTriggerDispatchStatus::DepthExceeded:
        return "depth-exceeded";
    case JsLegacyTriggerDispatchStatus::Disabled:
    case JsLegacyTriggerDispatchStatus::NoMatch:
    case JsLegacyTriggerDispatchStatus::Allow:
    case JsLegacyTriggerDispatchStatus::Block:
        return "";
    }
    return "unknown";
}

} // namespace

JsLegacyTriggerReloadGeneration::JsLegacyTriggerReloadGeneration() = default;

JsLegacyTriggerReloadGeneration::JsLegacyTriggerReloadGeneration(
    std::size_t successful_reload_count)
    : m_successful_reload_count(successful_reload_count), m_valid(true) {}

bool JsLegacyTriggerReloadGeneration::valid() const { return m_valid; }

std::size_t JsLegacyTriggerReloadGeneration::successful_reload_count() const {
    return m_successful_reload_count;
}

const char *js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus status) {
    switch (status) {
    case JsLegacyTriggerDispatchStatus::Disabled:
        return "disabled";
    case JsLegacyTriggerDispatchStatus::RegistryNotReady:
        return "registry-not-ready";
    case JsLegacyTriggerDispatchStatus::StaleRegistry:
        return "stale-registry";
    case JsLegacyTriggerDispatchStatus::NoMatch:
        return "no-match";
    case JsLegacyTriggerDispatchStatus::Allow:
        return "allow";
    case JsLegacyTriggerDispatchStatus::Block:
        return "block";
    case JsLegacyTriggerDispatchStatus::Error:
        return "error";
    case JsLegacyTriggerDispatchStatus::BudgetExceeded:
        return "budget-exceeded";
    case JsLegacyTriggerDispatchStatus::DepthExceeded:
        return "depth-exceeded";
    }
    return "unknown";
}

bool js_legacy_trigger_dispatch_status_should_log(JsLegacyTriggerDispatchStatus status) {
    switch (status) {
    case JsLegacyTriggerDispatchStatus::RegistryNotReady:
    case JsLegacyTriggerDispatchStatus::StaleRegistry:
    case JsLegacyTriggerDispatchStatus::Error:
    case JsLegacyTriggerDispatchStatus::BudgetExceeded:
    case JsLegacyTriggerDispatchStatus::DepthExceeded:
        return true;
    case JsLegacyTriggerDispatchStatus::Disabled:
    case JsLegacyTriggerDispatchStatus::NoMatch:
    case JsLegacyTriggerDispatchStatus::Allow:
    case JsLegacyTriggerDispatchStatus::Block:
        return false;
    }
    return true;
}

std::string js_legacy_trigger_dispatch_log_message(
    const JsLegacyTriggerDispatchResult &result, const JsTriggerDispatchRequest &request) {
    std::ostringstream message;
    message << "JavaScript trigger dispatch failed"
            << " status=" << js_legacy_trigger_dispatch_status_name(result.status)
            << " host=" << js_script_package_host_name(request.host)
            << " kind=" << js_scripting_manifest_kind_name(request.kind)
            << " legacy_value=" << request.legacy_value;
    if (request.package_vnum > 0)
        message << " requested_vnum=" << request.package_vnum;
    if (result.dispatch_result.package_vnum > 0)
        message << " package_vnum=" << result.dispatch_result.package_vnum;
    if (!result.dispatch_result.handler_name.empty()) {
        message << " handler="
                << (is_safe_handler_identifier(result.dispatch_result.handler_name)
                        ? result.dispatch_result.handler_name
                        : "[redacted-handler]");
    }
    const std::string diagnostic = safe_log_diagnostic(result);
    if (!diagnostic.empty())
        message << " diagnostic=" << diagnostic;
    return bounded_single_line(message.str());
}

JsLegacyTriggerReloadGeneration
js_legacy_trigger_reload_generation(const JsLiveRegistryReloadService &service) {
    if (service.successful_reload_count() == 0)
        return {};
    return JsLegacyTriggerReloadGeneration(service.successful_reload_count());
}

JsLegacyTriggerDispatchResult js_legacy_trigger_dispatch(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options, const JsLegacyTriggerDispatchOptions &options) {
    JsLegacyTriggerDispatchResult result;

    if (!options.enabled) {
        result.status = JsLegacyTriggerDispatchStatus::Disabled;
        result.diagnostic = "JavaScript legacy trigger dispatch is disabled.";
        return result;
    }

    const std::size_t current_reload_count = service.successful_reload_count();
    if (current_reload_count == 0) {
        result.status = JsLegacyTriggerDispatchStatus::RegistryNotReady;
        result.diagnostic = "JavaScript live registry has not loaded successfully.";
        return result;
    }

    if (options.require_fresh_reload &&
        (!options.expected_reload_generation.valid() ||
         options.expected_reload_generation.successful_reload_count() != current_reload_count)) {
        result.status = JsLegacyTriggerDispatchStatus::StaleRegistry;
        result.diagnostic = "JavaScript live registry reload generation is stale.";
        return result;
    }

    JsTriggerDispatchOptions dispatch_options;
    dispatch_options.runtime_limits = options.runtime_limits;
    dispatch_options.budget = options.budget;
    dispatch_options.budget_limits = options.budget_limits;
    dispatch_options.depth_guard = options.depth_guard;
    dispatch_options.depth_limits = options.depth_limits;
    dispatch_options.current_pulse = options.current_pulse;
    dispatch_options.mutation_authority = options.mutation_authority;

    result.dispatch_result = js_trigger_dispatch_live_first_match(
        service, request, adapter_options, dispatch_options);
    result.status = facade_status_from_dispatch(result.dispatch_result.status);
    result.diagnostic = bounded_single_line(result.dispatch_result.diagnostic);
    return result;
}
