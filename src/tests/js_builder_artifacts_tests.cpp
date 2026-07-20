#include "js_builder_artifacts.h"

#include "js_api_contract.h"
#include "js_scripting_manifest.h"
#include "js_scripting_runtime_policy.h"
#include "json_utils.h"

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

namespace {

void expect_contains(const std::string &text, const std::string &needle) {
    EXPECT_NE(text.find(needle), std::string::npos) << needle;
}

bool contains_raw_cpp_type_text(const std::string &text) {
    std::string lower_text;
    lower_text.reserve(text.size());
    for (char ch : text)
        lower_text += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    const char *forbidden[] = {"char_data",   "char_data*",  "obj_data",  "obj_data*",
                               "room_data",   "room_data*",  "zone_data", "zone_data*",
                               "script_data", "script_head", "room_rnum", "obj_rnum",
                               "std::",       "::",          "void*",     "char*",
                               "struct ",     ".h",          ".cpp"};

    for (const char *value : forbidden) {
        if (lower_text.find(value) != std::string::npos)
            return true;
    }
    return false;
}

bool member_is_active_typing(const JsApiMember &member) {
    return member.status == JsApiMemberStatus::PlannedReadOnly ||
           member.status == JsApiMemberStatus::PlannedPureHelper;
}

bool trigger_is_authorable_typing(const JsScriptingManifestEntry &entry) {
    return entry.support_status == JsScriptingSupportStatus::Deferred &&
           entry.builder_status == JsScriptingBuilderStatus::Deferred &&
           std::string(entry.javascript_handler_name).empty() == false;
}

std::string expected_trigger_return_type(const JsScriptingManifestEntry &entry) {
    if (entry.blocks_gameplay || entry.consumes_special_result)
        return "boolean | void";
    return "void";
}

std::string markdown_cell(const char *value) {
    std::string escaped;
    for (char ch : std::string(value ? value : "")) {
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

std::string declaration_block(const std::string &declarations, const std::string &start) {
    const std::size_t start_index = declarations.find(start);
    if (start_index == std::string::npos)
        return "";
    const std::size_t open_brace = declarations.find('{', start_index);
    if (open_brace == std::string::npos)
        return "";

    int depth = 0;
    bool in_block_comment = false;
    for (std::size_t index = open_brace; index < declarations.size(); ++index) {
        const char ch = declarations[index];
        const char next = index + 1 < declarations.size() ? declarations[index + 1] : '\0';

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++index;
            }
            continue;
        }
        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++index;
            continue;
        }
        if (ch == '{')
            ++depth;
        else if (ch == '}') {
            --depth;
            if (depth == 0)
                return declarations.substr(start_index, index - start_index + 1);
        }
    }
    return "";
}

void expect_balanced_typescript_shape(const std::string &declarations) {
    int brace_depth = 0;
    bool in_block_comment = false;
    for (std::size_t index = 0; index < declarations.size(); ++index) {
        const char ch = declarations[index];
        const char next = index + 1 < declarations.size() ? declarations[index + 1] : '\0';

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++index;
            }
            continue;
        }

        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++index;
            continue;
        }
        if (ch == '{')
            ++brace_depth;
        else if (ch == '}')
            --brace_depth;
        EXPECT_GE(brace_depth, 0) << declarations;
    }

    EXPECT_FALSE(in_block_comment) << declarations;
    EXPECT_EQ(brace_depth, 0) << declarations;
    EXPECT_EQ(declarations.find("export function ;"), std::string::npos);
    EXPECT_EQ(declarations.find("readonly :"), std::string::npos);
}

void expect_valid_json_object(const std::string &json) {
    json_utils::JsonReader reader(json);
    std::string error_message;
    EXPECT_TRUE(reader.parse_root_object(
        [](const std::string &, json_utils::JsonReader *nested_reader,
           std::string *nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
}

void expect_editor_config_structure(const std::string &json) {
    json_utils::JsonReader reader(json);
    std::string error_message;
    bool saw_tsconfig = false;
    bool saw_lsp = false;
    bool saw_generated_artifacts = false;
    bool saw_compatibility = false;

    ASSERT_TRUE(reader.parse_root_object(
        [&](const std::string &key, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            if (key == "tsconfig") {
                saw_tsconfig = true;
                bool saw_compiler_options = false;
                bool saw_files = false;
                bool saw_include = false;
                return nested_reader->parse_object(
                           [&](const std::string &tsconfig_key,
                               json_utils::JsonReader *tsconfig_reader,
                               std::string *tsconfig_error) {
                               if (tsconfig_key == "compilerOptions") {
                                   saw_compiler_options = true;
                                   return tsconfig_reader->parse_object(
                                       [](const std::string &,
                                          json_utils::JsonReader *option_reader,
                                          std::string *option_error) {
                                           return option_reader->skip_value(option_error);
                                       },
                                       tsconfig_error);
                               }
                               if (tsconfig_key == "files") {
                                   std::vector<std::string> values;
                                   const bool parsed =
                                       tsconfig_reader->parse_string_array(&values, tsconfig_error);
                                   EXPECT_EQ(values.size(), 1U);
                                   if (!values.empty()) {
                                       EXPECT_EQ(values[0], "generated/rots.d.ts");
                                   }
                                   saw_files = true;
                                   return parsed;
                               }
                               if (tsconfig_key == "include") {
                                   std::vector<std::string> values;
                                   const bool parsed =
                                       tsconfig_reader->parse_string_array(&values, tsconfig_error);
                                   EXPECT_EQ(values.size(), 3U);
                                   saw_include = true;
                                   return parsed;
                               }
                               return tsconfig_reader->skip_value(tsconfig_error);
                           },
                           nested_error_message) &&
                       saw_compiler_options && saw_files && saw_include;
            }
            if (key == "lsp")
                saw_lsp = true;
            if (key == "generatedArtifacts")
                saw_generated_artifacts = true;
            if (key == "compatibility")
                saw_compatibility = true;
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;

    EXPECT_TRUE(saw_tsconfig);
    EXPECT_TRUE(saw_lsp);
    EXPECT_TRUE(saw_generated_artifacts);
    EXPECT_TRUE(saw_compatibility);
}

} // namespace

TEST(JsBuilderArtifacts, GeneratesTypescriptDeclarationsWithCompatibilityHeader) {
    const std::string declarations = js_generate_typescript_declarations();
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();

    expect_contains(declarations, "declare namespace RotS");
    expect_contains(declarations, "export = RotS;");
    expect_contains(declarations, "export as namespace RotS;");
    expect_contains(declarations, manifest.manifest_checksum);
    expect_contains(declarations, api.contract_checksum);
    expect_contains(declarations, manifest.generated_typings_version);
    expect_contains(declarations, "export type HostType");
    expect_contains(declarations, "export type DispatchStatus");
    expect_contains(declarations, "export interface RuntimeSafetyPolicy");
    expect_contains(declarations, "readonly memoryLimitBytes: number;");
    expect_contains(declarations, "readonly stackLimitBytes: number;");
    expect_contains(declarations, "readonly instructionBudget: number;");
    expect_contains(declarations, "readonly maxInvocationsPerPulse: number;");
    expect_contains(declarations, "readonly maxInvocationsPerPackagePerPulse: number;");
    expect_contains(declarations, "readonly maxDispatchDepth: number;");
    expect_contains(declarations, "readonly maxDispatchFailureLogsPerPulse: number;");
    expect_contains(declarations, "readonly failureLoggingPolicy: string;");
    expect_contains(declarations, "budget-exceeded");
    expect_contains(declarations, "depth-exceeded");
    expect_contains(declarations, "export interface ScriptHandlers");
    expect_balanced_typescript_shape(declarations);
    EXPECT_EQ(declarations.find("*/ */"), std::string::npos);
    EXPECT_FALSE(contains_raw_cpp_type_text(declarations));
}

TEST(JsBuilderArtifacts, TypescriptDeclarationsCoverEveryApiTypeAndMember) {
    const std::string declarations = js_generate_typescript_declarations();

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        const std::string start = type.kind == JsApiTypeKind::Namespace
                                      ? "export namespace " + std::string(type.name)
                                      : "export interface " + std::string(type.name);
        expect_contains(declarations, start);
        expect_contains(declarations, "/** " + std::string(type.docs) + " */");
        const std::string block = declaration_block(declarations, start);
        ASSERT_FALSE(block.empty()) << type.name;

        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            if (member_is_active_typing(member)) {
                expect_contains(block, std::string(member.name));
                expect_contains(block, "/** " + std::string(member.docs) + " */");
                if (member.kind == JsApiMemberKind::Property)
                    expect_contains(block, "readonly " + std::string(member.name) + ": ");
                else
                    expect_contains(block, std::string(member.name) + "(");
                continue;
            }

            EXPECT_EQ(block.find("readonly " + std::string(member.name) + ": "), std::string::npos)
                << member.name;
            EXPECT_EQ(block.find("export function " + std::string(member.name) + "("),
                      std::string::npos)
                << member.name;
        }
    }

    EXPECT_EQ(declarations.find("accountName"), std::string::npos);
    EXPECT_EQ(declarations.find("sendToCharacter"), std::string::npos);
    EXPECT_EQ(declarations.find("sendToRoom"), std::string::npos);
    EXPECT_EQ(declarations.find("loadMob"), std::string::npos);
    EXPECT_EQ(declarations.find("extractCharacter"), std::string::npos);
    EXPECT_EQ(declarations.find("runtimeSafety:"), std::string::npos);
}

TEST(JsBuilderArtifacts, TypescriptDeclarationsExposeTypedTargetContext)
{
    const std::string declarations = js_generate_typescript_declarations();
    const std::string block = declaration_block(declarations, "export interface ScriptContext");

    ASSERT_FALSE(block.empty());
    expect_contains(block, "readonly target: Character | GameObject | Room | null;");
    expect_contains(block, "readonly targ1: Character | GameObject | Room | null;");
    expect_contains(block, "readonly targ2: Character | GameObject | Room | null;");
    expect_contains(block, "readonly targetTypes: readonly string[];");
    expect_contains(block, "readonly dying: Character | null;");
}

TEST(JsBuilderArtifacts, TypescriptDeclarationsCoverEveryTriggerHandler) {
    const std::string declarations = js_generate_typescript_declarations();

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        if (std::string(entry.javascript_handler_name).empty())
            continue;
        const std::string signature =
            std::string(entry.javascript_handler_name) +
            "?(ctx: ScriptContext): " + expected_trigger_return_type(entry) + ";";
        if (trigger_is_authorable_typing(entry)) {
            expect_contains(declarations, "'" + std::string(entry.javascript_handler_name) + "'");
            expect_contains(declarations, signature);
            expect_contains(declarations, "/** " + std::string(entry.notes) + " */");
            continue;
        }

        EXPECT_EQ(declarations.find("'" + std::string(entry.javascript_handler_name) + "'"),
                  std::string::npos)
            << entry.javascript_handler_name;
        EXPECT_EQ(declarations.find(signature), std::string::npos) << entry.javascript_handler_name;
    }
}

TEST(JsBuilderArtifacts, GeneratesMarkdownReferenceFromManifestAndContract) {
    const std::string markdown = js_generate_api_markdown_reference();
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();

    expect_contains(markdown, "# RotS JavaScript Builder API");
    expect_contains(markdown, manifest.manifest_checksum);
    expect_contains(markdown, api.contract_checksum);
    expect_contains(markdown, "## Trigger Handlers");
    expect_contains(markdown, "## Runtime Safety");
    expect_contains(markdown, "QuickJS memory limit bytes");
    expect_contains(markdown, std::to_string(policy.runtime_limits.memory_limit_bytes));
    expect_contains(markdown, std::to_string(policy.budget_limits.max_invocations_per_pulse));
    expect_contains(markdown, std::to_string(policy.depth_limits.max_dispatch_depth));
    expect_contains(markdown, markdown_cell(policy.failure_logging_policy));
    expect_contains(markdown, "## API Types");
    expect_contains(markdown, "Context fields");
    expect_contains(markdown, "Dispatch order");
    expect_contains(markdown, "Notes");
    expect_contains(markdown, "`character");
    EXPECT_FALSE(contains_raw_cpp_type_text(markdown));

    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry &entry = js_scripting_manifest_entries()[index];
        expect_contains(
            markdown,
            "| " + markdown_inline_code(entry.legacy_name) + " | " +
                markdown_inline_code(entry.javascript_handler_name) + " | " +
                markdown_inline_code(js_scripting_support_status_name(entry.support_status)) +
                " | " + markdown_inline_code(markdown_host_names(entry.host_flags).c_str()) +
                " | " + markdown_inline_code(entry.blocks_gameplay ? "yes" : "no") + " | " +
                markdown_inline_code(entry.context_fields) + " | " +
                markdown_cell(entry.dispatch_order) + " | " + markdown_cell(entry.notes) + " |");
    }

    for (std::size_t type_index = 0; type_index < js_api_contract_type_count(); ++type_index) {
        const JsApiType &type = js_api_contract_types()[type_index];
        expect_contains(markdown, "### " + std::string(type.name));
        expect_contains(markdown, std::string(type.docs));
        for (std::size_t member_index = 0; member_index < type.member_count; ++member_index) {
            const JsApiMember &member = type.members[member_index];
            expect_contains(markdown,
                            "| " + markdown_inline_code(member.name) + " | " +
                                markdown_inline_code(js_api_member_kind_name(member.kind)) + " | " +
                                markdown_inline_code(member.type_name) + " | " +
                                markdown_inline_code(member.return_type) + " | " +
                                markdown_inline_code(js_api_member_status_name(member.status)) +
                                " | " + markdown_inline_code(member.permission) + " | " +
                                markdown_inline_code(js_api_side_effect_name(member.side_effect)) +
                                " | " + markdown_cell(member.docs) + " |");
        }
    }
}

TEST(JsBuilderArtifacts, GeneratesValidVscodeStyleLspConfig) {
    const std::string json = js_generate_editor_lsp_config_json();
    const JsScriptingManifestMetadata &manifest = js_scripting_manifest_metadata();
    const JsApiContractMetadata &api = js_api_contract_metadata();
    const JsScriptingRuntimeSafetyPolicy &policy = js_scripting_runtime_safety_policy();

    expect_valid_json_object(json);
    expect_editor_config_structure(json);
    expect_contains(json, "\"editorProfile\":\"vscode-compatible\"");
    expect_contains(json, "\"server\":\"typescript-language-service\"");
    expect_contains(json, "\"intelliSense\":true");
    expect_contains(json, "\"tsconfig\":{");
    expect_contains(json, "\"module\":\"preserve\"");
    expect_contains(json, "\"files\":[\"generated/rots.d.ts\"]");
    expect_contains(
        json, "\"include\":[\"scripts/**/*.ts\",\"fixtures/**/*.ts\",\"generated/**/*.d.ts\"]");
    expect_contains(json, "\"builderManifest\":\"generated/rots-builder-manifest.json\"");
    expect_contains(json, "\"runtimeSafety\":{");
    expect_contains(json, "\"memoryLimitBytes\":" +
            std::to_string(policy.runtime_limits.memory_limit_bytes));
    expect_contains(json, "\"stackLimitBytes\":" +
            std::to_string(policy.runtime_limits.stack_limit_bytes));
    expect_contains(json, "\"instructionBudget\":" +
            std::to_string(policy.runtime_limits.instruction_budget));
    expect_contains(json, "\"maxInvocationsPerPulse\":" +
            std::to_string(policy.budget_limits.max_invocations_per_pulse));
    expect_contains(json, "\"maxInvocationsPerPackagePerPulse\":" +
            std::to_string(policy.budget_limits.max_invocations_per_package_per_pulse));
    expect_contains(json, "\"maxDispatchDepth\":" +
            std::to_string(policy.depth_limits.max_dispatch_depth));
    expect_contains(json, "\"maxDispatchFailureLogsPerPulse\":" +
            std::to_string(policy.max_dispatch_failure_logs_per_pulse));
    expect_contains(json, policy.failure_logging_policy);
    expect_contains(json, manifest.manifest_checksum);
    expect_contains(json, api.contract_checksum);
    EXPECT_EQ(json.find("declarationRoots"), std::string::npos);
    EXPECT_EQ(json.find("\"module\":\"none\""), std::string::npos);
    EXPECT_FALSE(contains_raw_cpp_type_text(json));
}
