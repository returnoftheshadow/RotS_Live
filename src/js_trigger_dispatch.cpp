#include "js_trigger_dispatch.h"

#include "db.h"
#include "structs.h"
#include "utils.h"
#include "zone.h"

#include <cctype>
#include <cstdlib>
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

struct PendingTextMutation {
    char** target = nullptr;
    bool has_value = false;
    std::string value;
};

bool is_blank_text(const std::string& value)
{
    for (char ch : value) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

bool is_nullable_text_property(const JsRuntimeMutation& mutation)
{
    return (mutation.target_type == "object" && mutation.property == "actionDescription") || (mutation.target_type == "zone" && mutation.property == "description");
}

bool validate_text_mutation_value(const JsRuntimeMutation& mutation)
{
    if (!mutation.has_value)
        return is_nullable_text_property(mutation);

    const bool is_name = mutation.property == "name";
    const std::size_t max_length = is_name ? 256 : 8192;
    if (mutation.value.size() > max_length)
        return false;
    if (mutation.value.find('\0') != std::string::npos)
        return false;
    if (is_name && is_blank_text(mutation.value))
        return false;
    return true;
}

bool parse_id_number(const std::string& id, const char* prefix, int* number)
{
    if (number == nullptr)
        return false;
    const std::string prefix_text(prefix);
    if (id.find(prefix_text) != 0)
        return false;
    const std::string suffix = id.substr(prefix_text.size());
    if (suffix.empty())
        return false;
    int parsed = 0;
    for (char ch : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        parsed = parsed * 10 + (ch - '0');
    }
    *number = parsed;
    return true;
}

obj_data* mutable_live_object_for_id(const JsRuntimeMutation& mutation,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& options)
{
    if (mutation.target_id == "object")
        return const_cast<obj_data*>(request.context_input.object);
    if (mutation.target_id == "weapon")
        return const_cast<obj_data*>(request.context_input.weapon);

    int object_vnum = -1;
    if (!parse_id_number(mutation.target_id, "object:", &object_vnum) || options.live_objects == nullptr || options.object_index == nullptr) {
        return nullptr;
    }

    for (std::size_t index = 0; index < options.live_object_count; ++index) {
        const obj_data* object = options.live_objects[index];
        if (object == nullptr || object->item_number < 0 || static_cast<std::size_t>(object->item_number) >= options.object_index_count) {
            continue;
        }
        if (options.object_index[object->item_number].virt == object_vnum)
            return const_cast<obj_data*>(object);
    }
    return nullptr;
}

room_data* mutable_live_room_for_id(const JsRuntimeMutation& mutation,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& options)
{
    if (mutation.target_id == "room" && js_game_adapter_room_is_valid(request.context_input.room, options))
        return const_cast<room_data*>(&options.world[request.context_input.room]);

    int room_vnum = -1;
    if (!parse_id_number(mutation.target_id, "room:", &room_vnum) || options.world == nullptr)
        return nullptr;

    const std::size_t room_count = options.world_count > 0 ? options.world_count : static_cast<std::size_t>(options.top_of_world + 1);
    for (std::size_t index = 0; index < room_count; ++index) {
        if (options.world[index].number == room_vnum)
            return const_cast<room_data*>(&options.world[index]);
    }
    return nullptr;
}

zone_data* mutable_live_zone_for_id(const JsRuntimeMutation& mutation,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& options)
{
    if (mutation.target_id == "zone" && js_game_adapter_room_is_valid(request.context_input.room, options)) {
        const int zone_index = options.world[request.context_input.room].zone;
        if (options.zones != nullptr && zone_index >= 0 && static_cast<std::size_t>(zone_index) < options.zone_count) {
            return const_cast<zone_data*>(&options.zones[zone_index]);
        }
    }

    int zone_vnum = -1;
    if (!parse_id_number(mutation.target_id, "zone:", &zone_vnum) || options.zones == nullptr)
        return nullptr;

    for (std::size_t index = 0; index < options.zone_count; ++index) {
        if (options.zones[index].number == zone_vnum)
            return const_cast<zone_data*>(&options.zones[index]);
    }
    return nullptr;
}

char** resolve_text_mutation_target(const JsRuntimeMutation& mutation,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& options)
{
    if (mutation.target_type == "object") {
        obj_data* object = mutable_live_object_for_id(mutation, request, options);
        if (object == nullptr)
            return nullptr;
        if (mutation.property == "name")
            return &object->name;
        if (mutation.property == "description")
            return &object->description;
        if (mutation.property == "shortDescription")
            return &object->short_description;
        if (mutation.property == "actionDescription")
            return &object->action_description;
        return nullptr;
    }

    if (mutation.target_type == "room") {
        room_data* room = mutable_live_room_for_id(mutation, request, options);
        if (room == nullptr)
            return nullptr;
        if (mutation.property == "name")
            return &room->name;
        if (mutation.property == "description")
            return &room->description;
        return nullptr;
    }

    if (mutation.target_type == "zone") {
        zone_data* zone = mutable_live_zone_for_id(mutation, request, options);
        if (zone == nullptr)
            return nullptr;
        if (mutation.property == "name")
            return &zone->name;
        if (mutation.property == "description")
            return &zone->description;
        return nullptr;
    }

    return nullptr;
}

bool prepare_text_mutations(const std::vector<JsRuntimeMutation>& mutations,
    const JsTriggerDispatchRequest& request, const JsGameAdapterOptions& options,
    std::vector<PendingTextMutation>* pending)
{
    if (pending == nullptr)
        return false;
    pending->clear();
    for (const JsRuntimeMutation& mutation : mutations) {
        if (!validate_text_mutation_value(mutation))
            return false;
        char** target = resolve_text_mutation_target(mutation, request, options);
        if (target == nullptr)
            return false;
        pending->push_back({ target, mutation.has_value, mutation.value });
    }
    return true;
}

void apply_text_mutations(const std::vector<PendingTextMutation>& mutations)
{
    for (const PendingTextMutation& mutation : mutations) {
        free(*mutation.target);
        *mutation.target = mutation.has_value ? str_dup(mutation.value.c_str()) : nullptr;
    }
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

    if (limits.max_invocations_per_pulse > 0 && m_pulse_invocations >= limits.max_invocations_per_pulse)
        return false;

    std::size_t& package_invocations = m_package_invocations[package_vnum];
    if (limits.max_invocations_per_package_per_pulse > 0 && package_invocations >= limits.max_invocations_per_package_per_pulse)
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

    if (options.budget != nullptr && !options.budget->try_consume(options.current_pulse, package.vnum, options.budget_limits)) {
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
    } else {
        std::vector<PendingTextMutation> pending_mutations;
        if (!prepare_text_mutations(evaluation.mutations, request, adapter_options, &pending_mutations)) {
            result.status = JsTriggerDispatchStatus::Error;
            result.runtime_status = JsRuntimeStatus::Error;
            result.diagnostic = "JavaScript trigger mutation target rejected";
            return result;
        }
        apply_text_mutations(pending_mutations);
        if (evaluation.value == JsRuntimeValue::Block) {
            result.status = JsTriggerDispatchStatus::Block;
        } else {
            result.status = JsTriggerDispatchStatus::Allow;
        }
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
