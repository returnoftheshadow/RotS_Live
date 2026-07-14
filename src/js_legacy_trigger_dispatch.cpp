#include "js_legacy_trigger_dispatch.h"

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
    }
    return JsLegacyTriggerDispatchStatus::Error;
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
    }
    return "unknown";
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

    result.dispatch_result = js_trigger_dispatch_live_first_match(
        service, request, adapter_options, options.runtime_limits);
    result.status = facade_status_from_dispatch(result.dispatch_result.status);
    result.diagnostic = bounded_single_line(result.dispatch_result.diagnostic);
    return result;
}
