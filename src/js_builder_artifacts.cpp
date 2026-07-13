#include "js_builder_artifacts.h"

#include "js_api_contract.h"
#include "js_manifest_export.h"
#include "js_scripting_manifest.h"
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

bool member_is_active_typing(const JsApiMember &member) {
    return member.status == JsApiMemberStatus::PlannedReadOnly ||
           member.status == JsApiMemberStatus::PlannedPureHelper;
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
    append_trigger_handler_union(out);
    out << "\n";

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        append_ts_doc_comment(out, "", type.docs);
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
        out << "\n";
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
