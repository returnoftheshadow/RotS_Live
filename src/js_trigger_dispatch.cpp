#include "js_trigger_dispatch.h"

#include <cctype>
#include <utility>
#include <vector>

namespace {

bool is_identifier_start(unsigned char ch)
{
    return std::isalpha(ch) || ch == '_' || ch == '$';
}

bool is_identifier_continue(unsigned char ch)
{
    return std::isalnum(ch) || ch == '_' || ch == '$';
}

bool is_safe_handler_identifier(const std::string& handler_name)
{
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

JsGameTriggerFixture make_trigger_fixture(
    const JsScriptingManifestEntry& entry, JsScriptPackageHost host)
{
    JsGameTriggerFixture trigger;
    trigger.name = entry.javascript_handler_name ? entry.javascript_handler_name : "";
    trigger.legacy_name = entry.legacy_name ? entry.legacy_name : "";
    trigger.host_type = js_script_package_host_name(host);
    trigger.legacy_value = entry.legacy_value;
    trigger.blocks_gameplay = entry.blocks_gameplay;
    return trigger;
}

bool required_host_context_is_present(
    JsScriptPackageHost host, const JsGameTriggerContextFixture& context)
{
    switch (host) {
    case JsScriptPackageHost::Character:
        return context.has_self;
    case JsScriptPackageHost::MudlleMobile:
        return context.has_self && context.self.is_npc;
    case JsScriptPackageHost::Object:
        return context.has_object;
    case JsScriptPackageHost::Room:
        return context.has_room;
    }
    return false;
}

JsTriggerDispatchResult make_error_result(const JsScriptPackage& package,
    const JsScriptTriggerBinding& binding, JsRuntimeStatus runtime_status, std::string diagnostic,
    std::size_t matched_package_count)
{
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::Error;
    result.runtime_status = runtime_status;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = std::move(diagnostic);
    result.matched_package_count = matched_package_count;
    return result;
}

JsTriggerDispatchResult make_budget_exceeded_result(const JsScriptPackage& package,
    const JsScriptTriggerBinding& binding, std::size_t matched_package_count)
{
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::BudgetExceeded;
    result.runtime_status = JsRuntimeStatus::Ok;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = "JavaScript trigger execution budget exceeded";
    result.matched_package_count = matched_package_count;
    return result;
}

JsTriggerDispatchResult make_depth_exceeded_result(const JsScriptPackage& package,
    const JsScriptTriggerBinding& binding, std::size_t matched_package_count)
{
    JsTriggerDispatchResult result;
    result.status = JsTriggerDispatchStatus::DepthExceeded;
    result.runtime_status = JsRuntimeStatus::Ok;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding.handler_name;
    result.diagnostic = "JavaScript trigger recursion depth exceeded";
    result.matched_package_count = matched_package_count;
    return result;
}

std::vector<const JsScriptPackage*> find_requested_packages(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request)
{
    if (request.package_vnum <= 0)
        return registry.find_packages_for_trigger(request.host, request.kind, request.legacy_value);

    const JsScriptPackage* package = registry.find_package_by_vnum(request.package_vnum);
    if (!package)
        return {};
    if (!registry.find_trigger_binding(
            package->vnum, request.host, request.kind, request.legacy_value)) {
        return {};
    }
    return { package };
}

class JsTriggerDispatchDepthScope {
  public:
    JsTriggerDispatchDepthScope(
        JsTriggerDispatchDepthGuard* guard, const JsTriggerDispatchDepthLimits& limits)
        : m_guard(guard)
    {
        if (m_guard != nullptr)
            m_entered = m_guard->try_enter(limits);
    }

    ~JsTriggerDispatchDepthScope()
    {
        if (m_guard != nullptr && m_entered)
            m_guard->leave();
    }

    bool exceeded() const
    {
        return m_guard != nullptr && !m_entered;
    }

  private:
    JsTriggerDispatchDepthGuard* m_guard = nullptr;
    bool m_entered = false;
};

} // namespace

const char* js_trigger_dispatch_status_name(JsTriggerDispatchStatus status)
{
    switch (status) {
    case JsTriggerDispatchStatus::NoMatch:
        return "no-match";
    case JsTriggerDispatchStatus::Allow:
        return "allow";
    case JsTriggerDispatchStatus::Block:
        return "block";
    case JsTriggerDispatchStatus::Error:
        return "error";
    case JsTriggerDispatchStatus::BudgetExceeded:
        return "budget-exceeded";
    case JsTriggerDispatchStatus::DepthExceeded:
        return "depth-exceeded";
    }
    return "unknown";
}

bool JsTriggerDispatchBudget::try_consume(
    int pulse, int package_vnum, const JsTriggerDispatchBudgetLimits& limits)
{
    if (!m_has_current_pulse || m_current_pulse != pulse) {
        m_current_pulse = pulse;
        m_has_current_pulse = true;
        m_pulse_invocations = 0;
        m_package_invocations.clear();
    }

    if (limits.max_invocations_per_pulse > 0 &&
        m_pulse_invocations >= limits.max_invocations_per_pulse)
        return false;

    std::size_t& package_invocations = m_package_invocations[package_vnum];
    if (limits.max_invocations_per_package_per_pulse > 0 &&
        package_invocations >= limits.max_invocations_per_package_per_pulse)
        return false;

    ++m_pulse_invocations;
    ++package_invocations;
    return true;
}

void JsTriggerDispatchBudget::reset()
{
    m_current_pulse = 0;
    m_has_current_pulse = false;
    m_pulse_invocations = 0;
    m_package_invocations.clear();
}

bool JsTriggerDispatchDepthGuard::would_exceed(const JsTriggerDispatchDepthLimits& limits) const
{
    if (limits.max_dispatch_depth > 0 && m_current_depth >= limits.max_dispatch_depth)
        return true;
    return false;
}

bool JsTriggerDispatchDepthGuard::try_enter(const JsTriggerDispatchDepthLimits& limits)
{
    if (would_exceed(limits))
        return false;
    ++m_current_depth;
    return true;
}

void JsTriggerDispatchDepthGuard::leave()
{
    if (m_current_depth > 0)
        --m_current_depth;
}

void JsTriggerDispatchDepthGuard::reset()
{
    m_current_depth = 0;
}

std::size_t JsTriggerDispatchDepthGuard::current_depth() const
{
    return m_current_depth;
}

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& adapter_options,
    const JsRuntimeLimits& limits)
{
    JsTriggerDispatchOptions options;
    options.runtime_limits = limits;
    return js_trigger_dispatch_first_match(registry, request, adapter_options, options);
}

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& adapter_options,
    const JsTriggerDispatchOptions& options)
{
    const std::vector<const JsScriptPackage*> matches = find_requested_packages(registry, request);
    if (matches.empty())
        return {};

    const JsScriptPackage& package = *matches.front();
    const JsScriptTriggerBinding* binding = registry.find_trigger_binding(
        package.vnum, request.host, request.kind, request.legacy_value);
    if (!binding) {
        JsTriggerDispatchResult result;
        result.status = JsTriggerDispatchStatus::Error;
        result.runtime_status = JsRuntimeStatus::Error;
        result.package_vnum = package.vnum;
        result.package_id = package.package_id;
        result.diagnostic = "JavaScript trigger binding disappeared during dispatch";
        result.matched_package_count = matches.size();
        return result;
    }

    const JsScriptingManifestEntry* entry = find_js_scripting_manifest_entry(request.kind, request.legacy_value);
    if (!entry) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
            "JavaScript trigger manifest entry missing", matches.size());
    }
    if (!is_safe_handler_identifier(binding->handler_name)) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
            "JavaScript trigger handler name is not a safe identifier", matches.size());
    }

    JsGameAdapterContextInput context_input = request.context_input;
    context_input.trigger = make_trigger_fixture(*entry, request.host);
    const JsGameTriggerContextFixture context = js_game_adapter_context_fixture(context_input, adapter_options);
    if (!required_host_context_is_present(request.host, context)) {
        return make_error_result(package, *binding, JsRuntimeStatus::Error,
            "JavaScript trigger context rejected: missing live "
                + std::string(js_script_package_host_name(request.host)),
            matches.size());
    }

    JsTriggerDispatchDepthScope depth_scope(options.depth_guard, options.depth_limits);
    if (depth_scope.exceeded())
        return make_depth_exceeded_result(package, *binding, matches.size());

    if (options.budget != nullptr &&
        !options.budget->try_consume(options.current_pulse, package.vnum, options.budget_limits)) {
        return make_budget_exceeded_result(package, *binding, matches.size());
    }

    JsGameRuntime runtime(options.runtime_limits);
    const std::string filename = "js-package-" + std::to_string(package.vnum) + ".js";
    const JsRuntimeEvalResult evaluation = runtime.evaluate_trigger_package_handler(
        package.compiled_javascript, binding->handler_name, context, filename.c_str());

    JsTriggerDispatchResult result;
    result.runtime_status = evaluation.status;
    result.package_vnum = package.vnum;
    result.package_id = package.package_id;
    result.handler_name = binding->handler_name;
    result.diagnostic = evaluation.diagnostic;
    result.matched_package_count = matches.size();

    if (evaluation.status != JsRuntimeStatus::Ok) {
        result.status = JsTriggerDispatchStatus::Error;
    } else if (evaluation.value == JsRuntimeValue::Block) {
        result.status = JsTriggerDispatchStatus::Block;
    } else {
        result.status = JsTriggerDispatchStatus::Allow;
    }

    return result;
}

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService& service, const JsTriggerDispatchRequest& request,
    const JsGameAdapterOptions& adapter_options, const JsRuntimeLimits& limits)
{
    JsTriggerDispatchOptions options;
    options.runtime_limits = limits;
    return js_trigger_dispatch_live_first_match(service, request, adapter_options, options);
}

JsTriggerDispatchResult js_trigger_dispatch_live_first_match(
    const JsLiveRegistryReloadService& service, const JsTriggerDispatchRequest& request,
    const JsGameAdapterOptions& adapter_options, const JsTriggerDispatchOptions& options)
{
    return js_trigger_dispatch_first_match(service.m_registry, request, adapter_options, options);
}
