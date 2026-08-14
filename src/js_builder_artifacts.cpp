#include "js_builder_artifacts.h"

#include "js_api_contract.h"
#include "js_api_enum_catalog.h"
#include "js_api_struct_mapping.h"
#include "js_manifest_export.h"
#include "js_scripting_manifest.h"
#include "js_scripting_runtime_policy.h"
#include "json_utils.h"

#include <sstream>
#include <string>

namespace {

void append_quoted_json(std::ostringstream &out, const char *value) {
    std::string escaped;
    json_utils::append_escaped_json_string(escaped, value ? value : "");
    out << '"' << escaped << '"';
}

std::string docs(const char *value) { return value ? value : ""; }

std::string markdown_cell(const char *value) {
    std::string escaped;
    for (char ch : docs(value)) {
        if (ch == '|')
            escaped += "\\|";
        else if (ch == '`')
            escaped += "'";
        else if (ch == '\n' || ch == '\r')
            escaped += ' ';
        else
            escaped += ch;
    }
    return escaped;
}

std::string markdown_inline_code(const char *value) { return "`" + markdown_cell(value) + "`"; }

std::string ts_string_literal(const char *value) {
    std::string escaped;
    for (char ch : docs(value)) {
        if (ch == '\\' || ch == '\'')
            escaped += '\\';
        escaped += ch;
    }
    return escaped;
}

std::string ts_value_literal(const JsApiEnumCatalog &catalog, const JsApiEnumValue &value) {
    if (catalog.value_kind == JsApiEnumValueKind::String)
        return "'" + ts_string_literal(value.string_value) + "'";
    return std::to_string(value.number_value);
}

std::string ts_doc_text(const char *value) {
    std::string escaped;
    const std::string text = docs(value);
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\n' || ch == '\r') {
            escaped += ' ';
            continue;
        }
        if (ch == '*' && index + 1 < text.size() && text[index + 1] == '/') {
            escaped += "* /";
            ++index;
            continue;
        }
        escaped += ch;
    }
    return escaped;
}

std::string markdown_host_names(unsigned flags) {
    std::string names;
    auto append = [&](unsigned flag, const char *name) {
        if ((flags & flag) == 0)
            return;
        if (!names.empty())
            names += ", ";
        names += name;
    };
    append(JS_SCRIPTING_HOST_CHARACTER, "character");
    append(JS_SCRIPTING_HOST_OBJECT, "object");
    append(JS_SCRIPTING_HOST_ROOM, "room");
    append(JS_SCRIPTING_HOST_MUDLLE_MOBILE, "mudlleMobile");
    return names.empty() ? "none" : names;
}

const char *markdown_struct_owner_name(JsApiStructOwner owner) {
    switch (owner) {
    case JsApiStructOwner::CharData:
        return "Character";
    case JsApiStructOwner::ObjData:
        return "GameObject";
    case JsApiStructOwner::RoomData:
        return "Room";
    case JsApiStructOwner::ZoneData:
        return "Zone";
    }
    return "Unknown";
}

bool markdown_mapping_is_public(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.getter_status) != "internal-only";
}

bool mapping_setter_is_callable(const JsApiStructFieldMapping &mapping) {
    return std::string(mapping.setter_status) == "implemented-validated-setter";
}

constexpr const char *GameObjectLevelSetterDocs =
    "Updates the invocation snapshot object flags level after integer and 0 through 100 "
    "inclusive bounds checks, rejects negative values, values above 100, and fractional or "
    "other non-integer values, and applies to live owned memory only when dispatch provides "
    "target-scoped persistent setter authority. This changes the persisted object-file scalar "
    "level value visible as flags.level.";

constexpr const char *GameObjectRaritySetterDocs =
    "Updates the invocation snapshot object flags rarity after integer and 0 through 255 "
    "inclusive bounds checks, rejects negative values, values above 255, and fractional or "
    "other non-integer values, and applies to live owned memory only when dispatch provides "
    "target-scoped persistent setter authority. This changes the persisted object-file scalar "
    "rarity value visible as flags.rarity.";

std::string markdown_mapping_field_id(const JsApiStructFieldMapping &mapping) {
    return std::string(markdown_struct_owner_name(mapping.owner)) + "." + mapping.js_property;
}

bool member_is_active_typing(const JsApiMember &member) {
    return member.status == JsApiMemberStatus::PlannedReadOnly ||
           member.status == JsApiMemberStatus::PlannedPureHelper ||
           member.status == JsApiMemberStatus::ImplementedSideEffectHelper;
}

bool trigger_is_authorable_typing(const JsScriptingManifestEntry &entry) {
    return entry.support_status == JsScriptingSupportStatus::Deferred &&
           entry.builder_status == JsScriptingBuilderStatus::Deferred &&
           docs(entry.javascript_handler_name)[0] != '\0';
}

const char *trigger_handler_return_type(const JsScriptingManifestEntry &entry) {
    if (entry.blocks_gameplay || entry.consumes_special_result)
        return "boolean | void";
    return "void";
}

std::string ts_member_signature(const JsApiMember &member) {
    std::ostringstream out;
    if (member.kind == JsApiMemberKind::Property) {
        out << "readonly " << member.name << ": " << docs(member.type_name) << ";";
        return out.str();
    }

    std::string type_name = docs(member.type_name);
    const std::size_t arrow = type_name.find("=>");
    if (arrow != std::string::npos) {
        std::string args = type_name.substr(0, arrow);
        while (!args.empty() && args[args.size() - 1] == ' ')
            args.erase(args.size() - 1);
        out << member.name << args << ": " << docs(member.return_type) << ";";
    } else {
        out << member.name << "(): " << docs(member.return_type) << ";";
    }
    return out.str();
}

void append_ts_doc_comment(std::ostringstream &out, const std::string &indent, const char *text) {
    out << indent << "/** " << ts_doc_text(text) << " */\n";
}

void append_enum_typescript_declarations(std::ostringstream &out) {
    out << "/* Script-visible constants for common comparison domains. */\n";
    for (std::size_t catalog_index = 0; catalog_index < js_api_enum_catalog_count();
         ++catalog_index) {
        const JsApiEnumCatalog &catalog = js_api_enum_catalogs()[catalog_index];
        append_ts_doc_comment(out, "", catalog.docs);
        out << "export type " << catalog.type_name << " =\n";
        for (std::size_t value_index = 0; value_index < catalog.value_count; ++value_index) {
            out << "    | " << ts_value_literal(catalog, catalog.values[value_index])
                << (value_index + 1 == catalog.value_count ? ";\n" : "\n");
        }
        append_ts_doc_comment(out, "", catalog.docs);
        out << "export const " << catalog.name << ": Readonly<{\n";
        for (std::size_t value_index = 0; value_index < catalog.value_count; ++value_index) {
            const JsApiEnumValue &value = catalog.values[value_index];
            append_ts_doc_comment(out, "    ", value.docs);
            out << "    readonly " << value.key << ": " << ts_value_literal(catalog, value)
                << ";\n";
        }
        out << "}>;\n\n";
    }
    append_ts_doc_comment(out, "",
        "Room flag names accepted by Room.addFlag and Room.removeFlag from ordinary "
        "builder-zone scripts. High-impact room flags are readable through RoomFlagName but "
        "must use reviewed server/admin override flows, not the public helper.");
    out << "export type MutableRoomFlagName =\n";
    out << "    | 'dark'\n";
    out << "    | 'noMob'\n";
    out << "    | 'indoors'\n";
    out << "    | 'noRide'\n";
    out << "    | 'shadowy'\n";
    out << "    | 'noMagic'\n";
    out << "    | 'tunnel'\n";
    out << "    | 'drinkWater'\n";
    out << "    | 'drinkPoison'\n";
    out << "    | 'peaceRoom'\n";
    out << "    | 'hideVnum';\n\n";
}

std::string enum_example(const JsApiEnumCatalog &catalog) {
    const std::string name = catalog.name;
    if (name == "Race")
        return "if (ctx.actor && ctx.actor.race === RotS.Race.Human) { return RotS.ScriptResult.allow(); }";
    if (name == "RaceIds")
        return "if (ctx.actor && ctx.actor.profile.raceId === RotS.RaceIds.Human) { return RotS.ScriptResult.allow(); }";
    if (name == "RoomSector")
        return "if (ctx.room && ctx.room.sectorType === RotS.RoomSector.Forest) { return RotS.ScriptResult.allow(); }";
    if (name == "ObjectExtraFlag")
        return "if (ctx.object && ctx.object.flags.extraFlags.includes(RotS.ObjectExtraFlag.NoDrop)) { return RotS.ScriptResult.block(); }";
    if (name == "RoomFlag")
        return "if (ctx.room && ctx.room.flags.includes(RotS.RoomFlag.PeaceRoom)) { return RotS.ScriptResult.allow(); }";
    if (name == "ExitFlag")
        return "const locked = ctx.room?.exits.some((exit) => exit.flags.includes(RotS.ExitFlag.Locked));";
    if (name == "Direction")
        return "if (ctx.direction === RotS.Direction.North) { return RotS.ScriptResult.allow(); }";
    return std::string("const value = RotS.") + catalog.name + "." + catalog.values[0].key + ";";
}

void append_enum_markdown_reference(std::ostringstream &out) {
    out << "## API Enums\n\n";
    out << "These frozen `RotS.*` constants are available in live scripts and offline fixtures. "
           "Use the string constants for comparing snapshot fields such as `ctx.actor.race`, "
           "`ctx.room.sectorType`, or flag-name arrays. Use the `Ids` or `Bits` catalogs only "
           "when a field stores the numeric legacy id or bit value.\n\n";
    for (std::size_t catalog_index = 0; catalog_index < js_api_enum_catalog_count();
         ++catalog_index) {
        const JsApiEnumCatalog &catalog = js_api_enum_catalogs()[catalog_index];
        out << "### RotS." << catalog.name << "\n\n";
        out << docs(catalog.docs) << "\n\n";
        out << "- Type: `" << catalog.type_name << "`\n";
        out << "- Value kind: `" << js_api_enum_value_kind_name(catalog.value_kind) << "`\n";
        out << "- Compare with: `" << markdown_cell(catalog.comparable_fields) << "`\n";
        out << "- Example: `" << markdown_cell(enum_example(catalog).c_str()) << "`\n\n";
        out << "| Key | Value | Docs |\n";
        out << "| --- | --- | --- |\n";
        for (std::size_t value_index = 0; value_index < catalog.value_count; ++value_index) {
            const JsApiEnumValue &value = catalog.values[value_index];
            const std::string literal = catalog.value_kind == JsApiEnumValueKind::String
                ? value.string_value
                : std::to_string(value.number_value);
            out << "| " << markdown_inline_code(value.key) << " | "
                << markdown_inline_code(literal.c_str()) << " | " << markdown_cell(value.docs)
                << " |\n";
        }
        out << "\n";
    }
}

void append_mutation_result_type(std::ostringstream &out) {
    out << "export type MutationResult =\n";
    out << "    | {\n";
    out << "        /** True when a validated setter or command helper accepts the requested "
           "change. */\n";
    out << "        readonly ok: true;\n";
    out << "        /** Stable machine-readable result code for successful mutations. */\n";
    out << "        readonly code: 'ok';\n";
    out << "        /** Sanitized builder-facing detail text, or null when no safe detail is "
           "available. */\n";
    out << "        readonly message: string | null;\n";
    out << "        /** Public API field or setter argument name related to the result, or null "
           "for whole-operation results. */\n";
    out << "        readonly field: string | null;\n";
    out << "      }\n";
    out << "    | {\n";
    out << "        /** False when a validated setter or command helper rejects or defers the "
           "requested change. */\n";
    out << "        readonly ok: false;\n";
    out << "        /** Stable machine-readable result code for rejected or deferred mutations. "
           "*/\n";
    out << "        readonly code:\n";
    out << "          | 'invalid-value'\n";
    out << "          | 'out-of-range'\n";
    out << "          | 'not-authorized'\n";
    out << "          | 'authority-rejected'\n";
    out << "          | 'stale-handle'\n";
    out << "          | 'unsupported'\n";
    out << "          | 'deferred'\n";
    out << "          | 'invalid-target'\n";
    out << "          | 'not-carried'\n";
    out << "          | 'no-drop'\n";
    out << "          | 'inventory-full'\n";
    out << "          | 'too-heavy'\n";
    out << "          | 'audit-rejected'\n";
    out << "          | 'not-found'\n";
    out << "          | 'already-waiting'\n";
    out << "          | 'no-recipient'\n";
    out << "          | 'blocked-room'\n";
    out << "          | 'no-teleport';\n";
    out << "        /** Sanitized builder-facing detail text, or null when no safe detail is "
           "available. */\n";
    out << "        readonly message: string | null;\n";
    out << "        /** Public API field or setter argument name related to the result, or null "
           "for whole-operation results. */\n";
    out << "        readonly field: string | null;\n";
    out << "      };\n\n";
}

void append_literal_union_start(std::ostringstream &out, const char *name) {
    out << "export type " << name << " =\n";
}

void append_string_literal_line(std::ostringstream &out, const char *value, bool last) {
    out << "    | '" << ts_string_literal(value) << "'";
    out << (last ? ";\n" : "\n");
}

std::size_t non_empty_handler_count() {
    std::size_t count = 0;
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        if (trigger_is_authorable_typing(js_scripting_manifest_entries()[index]))
            ++count;
    }
    return count;
}

void append_trigger_handler_union(std::ostringstream &out) {
    append_literal_union_start(out, "TriggerHandlerName");
    std::size_t emitted = 0;
    const std::size_t total = non_empty_handler_count();
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (!trigger_is_authorable_typing(entry))
            continue;
        ++emitted;
        append_string_literal_line(out, entry.javascript_handler_name, emitted == total);
    }
}

void append_runtime_safety_json(std::ostringstream &out) {
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();
    out << "\"runtimeSafety\":{";
    out << "\"memoryLimitBytes\":" << policy.runtime_limits.memory_limit_bytes;
    out << ",\"stackLimitBytes\":" << policy.runtime_limits.stack_limit_bytes;
    out << ",\"instructionBudget\":" << policy.runtime_limits.instruction_budget;
    out << ",\"maxInvocationsPerPulse\":" << policy.budget_limits.max_invocations_per_pulse;
    out << ",\"maxInvocationsPerPackagePerPulse\":"
        << policy.budget_limits.max_invocations_per_package_per_pulse;
    out << ",\"maxDispatchDepth\":" << policy.depth_limits.max_dispatch_depth;
    out << ",\"maxDispatchFailureLogsPerPulse\":" << policy.max_dispatch_failure_logs_per_pulse;
    out << ",\"failureLoggingPolicy\":";
    append_quoted_json(out, policy.failure_logging_policy);
    out << '}';
}

} // namespace

std::string js_generate_typescript_declarations() {
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();
    std::ostringstream out;

    out << "/*\n";
    out << " * Generated RotS JavaScript builder API declarations.\n";
    out << " * Trigger manifest checksum: " << manifest.manifest_checksum << "\n";
    out << " * API contract checksum: " << api.contract_checksum << "\n";
    out << " * Generated typings version: " << manifest.generated_typings_version << "\n";
    out << " */\n\n";
    out << "declare namespace RotS {\n";
    out << "export type HostType = 'character' | 'object' | 'room' | 'mudlleMobile';\n";
    out << "export type TriggerKind = 'legacy-script-trigger' | 'mudlle-call-flag';\n\n";
    out << "export type DispatchStatus =\n";
    out << "    | 'no-match'\n";
    out << "    | 'allow'\n";
    out << "    | 'block'\n";
    out << "    | 'error'\n";
    out << "    | 'budget-exceeded'\n";
    out << "    | 'depth-exceeded';\n\n";
    out << "export interface RuntimeSafetyPolicy {\n";
    out << "    /** Maximum QuickJS heap bytes allowed for one trigger invocation. */\n";
    out << "    readonly memoryLimitBytes: number;\n";
    out << "    /** Maximum QuickJS stack bytes allowed for one trigger invocation. */\n";
    out << "    readonly stackLimitBytes: number;\n";
    out << "    /** Approximate QuickJS instruction budget before interruption. */\n";
    out << "    readonly instructionBudget: number;\n";
    out << "    /** Maximum JavaScript trigger invocations allowed in one server pulse. */\n";
    out << "    readonly maxInvocationsPerPulse: number;\n";
    out << "    /** Maximum JavaScript trigger invocations for one package in one server pulse. "
           "*/\n";
    out << "    readonly maxInvocationsPerPackagePerPulse: number;\n";
    out << "    /** Maximum nested JavaScript trigger-entry depth before dispatch is denied. */\n";
    out << "    readonly maxDispatchDepth: number;\n";
    out << "    /** Maximum repeated dispatch failure logs emitted in one server pulse. */\n";
    out << "    readonly maxDispatchFailureLogsPerPulse: number;\n";
    out << "    /** Sanitized server logging policy for runtime, budget, and depth failures. */\n";
    out << "    readonly failureLoggingPolicy: string;\n";
    out << "}\n\n";
    append_trigger_handler_union(out);
    out << "\n";
    out << "export type TriggerResult = boolean | void;\n\n";
    append_enum_typescript_declarations(out);

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        append_ts_doc_comment(out, "", type.docs);
        if (std::string(type.name) == "MutationResult") {
            append_mutation_result_type(out);
            continue;
        }
        if (type.kind == JsApiTypeKind::Namespace) {
            out << "export namespace " << type.name << " {\n";
            for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
                const JsApiMember &member = type.members[member_index];
                if (!member_is_active_typing(member))
                    continue;
                append_ts_doc_comment(out, "    ", member.docs);
                out << "    export function " << ts_member_signature(member) << "\n";
            }
            out << "}\n\n";
            continue;
        }

        out << "export interface " << type.name;
        if (docs(type.extends)[0] != '\0')
            out << " extends " << type.extends;
        out << " {\n";
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            if (!member_is_active_typing(member))
                continue;
            append_ts_doc_comment(out, "    ", member.docs);
            out << "    " << ts_member_signature(member) << "\n";
        }
        for (std::size_t mapping_index = 0; mapping_index < js_api_struct_field_mapping_count();
             ++mapping_index) {
            const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[mapping_index];
            if (!markdown_mapping_is_public(mapping) || !mapping_setter_is_callable(mapping))
                continue;
            if (std::string(markdown_struct_owner_name(mapping.owner)) != type.name)
                continue;
            append_ts_doc_comment(out, "    ", mapping.setter_docs);
            out << "    " << mapping.setter_name << "(value: " << mapping.type_name
                << "): MutationResult;\n";
        }
        if (std::string(type.name) == "GameObject") {
            append_ts_doc_comment(out, "    ", GameObjectLevelSetterDocs);
            out << "    setLevel(value: number): MutationResult;\n";
            append_ts_doc_comment(out, "    ", GameObjectRaritySetterDocs);
            out << "    setRarity(value: number): MutationResult;\n";
        }
        if (std::string(type.name) == "Room") {
            append_ts_doc_comment(out, "    ",
                "Adds a supported named room flag to the invocation snapshot and queues an audited "
                "room flag helper mutation when dispatch provides target-scoped room flag "
                "authority. Only MutableRoomFlagName builder-zone flags are accepted by this "
                "public helper; death, private, godRoom, securityRoom, noTeleport, "
                "PERMAFFECT/permanentAffect, unnamed bits, and raw setFlags remain unavailable "
                "without reviewed server/admin override flows.");
            out << "    addFlag(name: MutableRoomFlagName): MutationResult;\n";
            append_ts_doc_comment(out, "    ",
                "Removes a supported named room flag from the invocation snapshot and queues an "
                "audited room flag helper mutation when dispatch provides target-scoped room flag "
                "authority. Only MutableRoomFlagName builder-zone flags are accepted by this "
                "public helper; high-impact and internal room flags return rejected results or "
                "stay unavailable through server/admin flows. Raw setFlags remains blocked.");
            out << "    removeFlag(name: MutableRoomFlagName): MutationResult;\n";
        }
        out << "}\n\n";
    }

    out << "export interface ScriptHandlers {\n";
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (!trigger_is_authorable_typing(entry))
            continue;
        append_ts_doc_comment(out, "    ", entry.notes);
        out << "    " << entry.javascript_handler_name
            << "?(ctx: ScriptContext): " << trigger_handler_return_type(entry) << ";\n";
    }
    out << "}\n";
    out << "}\n\n";
    out << "export = RotS;\n";
    out << "export as namespace RotS;\n";
    return out.str();
}

std::string js_generate_api_markdown_reference() {
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();
    std::ostringstream out;

    out << "# RotS JavaScript Builder API\n\n";
    out << "- Trigger manifest checksum: `" << manifest.manifest_checksum << "`\n";
    out << "- API contract checksum: `" << api.contract_checksum << "`\n";
    out << "- Generated typings version: `" << manifest.generated_typings_version << "`\n";
    out << "- Documentation version: `" << api.documentation_version << "`\n\n";
    out << "The generated TypeScript declarations expose only deferred authorable trigger "
           "handlers and active read-only or pure helper API members. Reserved, unsupported, "
           "and deferred side-effect APIs are documented here for compatibility planning but are "
           "not callable from builder scripts.\n\n";
    append_enum_markdown_reference(out);
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();
    out << "## Runtime Safety\n\n";
    out << "| Limit | Value |\n";
    out << "| --- | --- |\n";
    out << "| QuickJS memory limit bytes | `" << policy.runtime_limits.memory_limit_bytes
        << "` |\n";
    out << "| QuickJS stack limit bytes | `" << policy.runtime_limits.stack_limit_bytes << "` |\n";
    out << "| QuickJS instruction budget | `" << policy.runtime_limits.instruction_budget
        << "` |\n";
    out << "| Max invocations per server pulse | `"
        << policy.budget_limits.max_invocations_per_pulse << "` |\n";
    out << "| Max invocations per package per server pulse | `"
        << policy.budget_limits.max_invocations_per_package_per_pulse << "` |\n";
    out << "| Max nested dispatch depth | `" << policy.depth_limits.max_dispatch_depth << "` |\n";
    out << "| Max dispatch failure logs per pulse | `" << policy.max_dispatch_failure_logs_per_pulse
        << "` |\n\n";
    out << markdown_cell(policy.failure_logging_policy) << "\n\n";
    out << "## Trigger Handlers\n\n";
    out << "| Legacy name | Handler | Status | Hosts | Blocks gameplay | Context fields | Dispatch "
           "order | Notes |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        out << "| " << markdown_inline_code(entry.legacy_name) << " | "
            << markdown_inline_code(entry.javascript_handler_name) << " | "
            << markdown_inline_code(js_scripting_support_status_name(entry.support_status)) << " | "
            << markdown_inline_code(markdown_host_names(entry.host_flags).c_str()) << " | "
            << markdown_inline_code(entry.blocks_gameplay ? "yes" : "no") << " | "
            << markdown_inline_code(entry.context_fields) << " | "
            << markdown_cell(entry.dispatch_order) << " | " << markdown_cell(entry.notes) << " |\n";
    }

    out << "\n## API Types\n\n";
    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        out << "### " << type.name << "\n\n";
        out << docs(type.docs) << "\n\n";
        out << "| Member | Kind | Type | Return | Status | Permission | Side effect | Docs |\n";
        out << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            out << "| " << markdown_inline_code(member.name) << " | "
                << markdown_inline_code(js_api_member_kind_name(member.kind)) << " | "
                << markdown_inline_code(member.type_name) << " | "
                << markdown_inline_code(member.return_type) << " | "
                << markdown_inline_code(js_api_member_status_name(member.status)) << " | "
                << markdown_inline_code(member.permission) << " | "
                << markdown_inline_code(js_api_side_effect_name(member.side_effect)) << " | "
                << markdown_cell(member.docs) << " |\n";
        }
        if (std::string(type.name) == "GameObject") {
            out << "| " << markdown_inline_code("setLevel") << " | "
                << markdown_inline_code("function") << " | "
                << markdown_inline_code("(value: number)") << " | "
                << markdown_inline_code("MutationResult") << " | "
                << markdown_inline_code("implemented") << " | "
                << markdown_inline_code("validated-setter") << " | "
                << markdown_inline_code("mutation") << " | "
                << markdown_cell(GameObjectLevelSetterDocs) << " |\n";
            out << "| " << markdown_inline_code("setRarity") << " | "
                << markdown_inline_code("function") << " | "
                << markdown_inline_code("(value: number)") << " | "
                << markdown_inline_code("MutationResult") << " | "
                << markdown_inline_code("implemented") << " | "
                << markdown_inline_code("validated-setter") << " | "
                << markdown_inline_code("mutation") << " | "
                << markdown_cell(GameObjectRaritySetterDocs) << " |\n";
        }
        out << "\n";
    }
    out << "## Public Field Accessor Mapping\n\n";
    out << "This catalog maps legacy server fields onto the documented JavaScript handle API. "
           "Implemented read-only getters may appear in TypeScript declarations when the live "
           "runtime and offline fixture runner both support them. Deferred and unsupported setter "
           "policies are documented for review but are not callable from builder scripts until "
           "the generated API contract exposes a validated setter. Internal server fields are "
           "omitted from this public artifact.\n\n";
    out << "| Owner | Field id | Property | Getter | Setter | Type | Nullable | Getter status | "
           "Setter status | Getter callable | Setter callable | Documentation only | Side effect | "
           "Getter docs | Setter docs | Notes |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | "
           "--- | --- |\n";
    for (std::size_t index = 0; index < js_api_struct_field_mapping_count(); ++index) {
        const JsApiStructFieldMapping &mapping = js_api_struct_field_mappings()[index];
        if (!markdown_mapping_is_public(mapping))
            continue;
        out << "| " << markdown_inline_code(markdown_struct_owner_name(mapping.owner)) << " | "
            << markdown_inline_code(markdown_mapping_field_id(mapping).c_str()) << " | "
            << markdown_inline_code(mapping.js_property) << " | "
            << markdown_inline_code(mapping.getter_name) << " | "
            << markdown_inline_code(mapping.setter_name) << " | "
            << markdown_inline_code(mapping.type_name) << " | "
            << markdown_inline_code(mapping.nullable ? "yes" : "no") << " | "
            << markdown_inline_code(mapping.getter_status) << " | "
            << markdown_inline_code(mapping.setter_status) << " | "
            << markdown_inline_code(
                   std::string(mapping.getter_status) == "implemented-read-only-getter" ? "yes"
                                                                                        : "no")
            << " | " << markdown_inline_code(mapping_setter_is_callable(mapping) ? "yes" : "no")
            << " | " << markdown_inline_code(mapping_setter_is_callable(mapping) ? "no" : "yes")
            << " | " << markdown_inline_code(mapping.side_effect) << " | "
            << markdown_cell(mapping.getter_docs) << " | " << markdown_cell(mapping.setter_docs)
            << " | " << markdown_cell(mapping.notes) << " |\n";
    }
    return out.str();
}

std::string js_generate_editor_lsp_config_json() {
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();
    std::ostringstream out;

    out << '{';
    out << "\"schemaVersion\":1,";
    out << "\"editorProfile\":\"vscode-compatible\",";
    out << "\"language\":\"typescript\",";
    out << "\"tsconfig\":{";
    out << "\"compilerOptions\":{";
    out << "\"target\":\"ES2020\",";
    out << "\"module\":\"preserve\",";
    out << "\"strict\":true,";
    out << "\"noEmit\":true,";
    out << "\"allowUmdGlobalAccess\":true,";
    out << "\"types\":[]";
    out << "},";
    out << "\"files\":[\"generated/rots.d.ts\"],";
    out << "\"include\":[\"scripts/**/*.ts\",\"fixtures/**/*.ts\",\"generated/**/*.d.ts\"]";
    out << "},";
    out << "\"lsp\":{";
    out << "\"server\":\"typescript-language-service\",";
    out << "\"intelliSense\":true,";
    out << "\"hover\":true,";
    out << "\"signatureHelp\":true,";
    out << "\"semanticTokens\":true,";
    out << "\"diagnostics\":true";
    out << "},";
    out << "\"generatedArtifacts\":{";
    out << "\"declarations\":\"generated/rots.d.ts\",";
    out << "\"apiReference\":\"generated/rots-api.md\",";
    out << "\"builderManifest\":\"generated/rots-builder-manifest.json\"";
    out << "},";
    append_runtime_safety_json(out);
    out << ',';
    out << "\"compatibility\":{";
    out << "\"triggerManifestChecksum\":";
    append_quoted_json(out, manifest.manifest_checksum);
    out << ",\"apiContractChecksum\":";
    append_quoted_json(out, api.contract_checksum);
    out << ",\"generatedTypingsVersion\":";
    append_quoted_json(out, manifest.generated_typings_version);
    out << ",\"documentationVersion\":";
    append_quoted_json(out, api.documentation_version);
    out << '}';
    out << '}';
    return out.str();
}
