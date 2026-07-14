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
    }
    return "unknown";
}

JsTriggerDispatchResult js_trigger_dispatch_first_match(const JsScriptPackageRegistry& registry,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& adapter_options,
    const JsRuntimeLimits& limits)
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

    JsGameRuntime runtime(limits);
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
    return js_trigger_dispatch_first_match(service.m_registry, request, adapter_options, limits);
}
