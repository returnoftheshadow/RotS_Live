#include "../js_script_package.h"

#include "../interpre.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 2001, JsScriptPackageHost host = JsScriptPackageHost::Character,
    int trigger = ON_ENTER, const char* handler = "onEnter")
{
    const JsScriptingManifestMetadata& metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "pkg-" + std::to_string(vnum);
    package.host = host;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = "function onEnter(ctx) { return true; }";
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, trigger, handler });
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsScriptPackageValidationOptions internal_options()
{
    JsScriptPackageValidationOptions options;
    options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

bool has_code(const JsScriptPackageValidationResult& result, JsScriptPackageDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsScriptPackageDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

void refresh_checksum(JsScriptPackage& package)
{
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
}

} // namespace

TEST(JsScriptPackage, AcceptsWellFormedInternalValidationPackageWithoutPublishing)
{
    JsScriptPackage package = make_package();

    JsScriptPackageValidationResult internal_result = js_script_package_validate(package, internal_options());
    EXPECT_TRUE(internal_result.ok);
    EXPECT_TRUE(internal_result.diagnostics.empty());

    JsScriptPackageValidationResult publish_result = js_script_package_validate(package);
    EXPECT_FALSE(publish_result.ok);
    EXPECT_TRUE(has_code(publish_result, JsScriptPackageDiagnosticCode::UnsupportedTrigger));
}

TEST(JsScriptPackage, RejectsAllCurrentManifestEntriesInPublishMode)
{
    for (std::size_t index = 0; index < js_scripting_manifest_entry_count(); ++index) {
        const JsScriptingManifestEntry& entry = js_scripting_manifest_entries()[index];
        if (std::string(entry.javascript_handler_name).empty())
            continue;

        JsScriptPackage package = make_package(2100 + static_cast<int>(index), JsScriptPackageHost::Character,
            entry.legacy_value, entry.javascript_handler_name);
        package.trigger_bindings[0].kind = entry.kind;
        if (entry.host_flags & JS_SCRIPTING_HOST_OBJECT)
            package.host = JsScriptPackageHost::Object;
        if (entry.host_flags & JS_SCRIPTING_HOST_MUDLLE_MOBILE)
            package.host = JsScriptPackageHost::MudlleMobile;
        refresh_checksum(package);

        JsScriptPackageValidationResult result = js_script_package_validate(package);
        EXPECT_FALSE(result.ok) << entry.legacy_name;
        EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::UnsupportedTrigger))
            << entry.legacy_name;
    }
}

TEST(JsScriptPackage, RejectsReservedAndUnsupportedEntriesEvenForInternalValidation)
{
    JsScriptPackage before_die = make_package(2201, JsScriptPackageHost::Character, ON_BEFORE_DIE,
        "onBeforeDie");
    EXPECT_FALSE(js_script_package_validate(before_die, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(before_die, internal_options()),
        JsScriptPackageDiagnosticCode::UnsupportedTrigger));

    JsScriptPackage delay = make_package(2202, JsScriptPackageHost::MudlleMobile, SPECIAL_DELAY,
        "onSpecialDelay");
    delay.trigger_bindings[0].kind = JsScriptingManifestKind::MudlleCallFlag;
    refresh_checksum(delay);
    EXPECT_FALSE(js_script_package_validate(delay, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(delay, internal_options()),
        JsScriptPackageDiagnosticCode::UnsupportedTrigger));
}

TEST(JsScriptPackage, RejectsHostMismatchesAndRoomOwnedPublishing)
{
    JsScriptPackage wear_on_character = make_package(2301, JsScriptPackageHost::Character, ON_WEAR, "onWear");
    EXPECT_FALSE(js_script_package_validate(wear_on_character, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(wear_on_character, internal_options()),
        JsScriptPackageDiagnosticCode::WrongHost));

    JsScriptPackage before_enter_on_object = make_package(2302, JsScriptPackageHost::Object, ON_BEFORE_ENTER, "onBeforeEnter");
    EXPECT_FALSE(js_script_package_validate(before_enter_on_object, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(before_enter_on_object, internal_options()),
        JsScriptPackageDiagnosticCode::WrongHost));

    JsScriptPackage special_on_character = make_package(2303, JsScriptPackageHost::Character, SPECIAL_COMMAND, "onSpecialCommand");
    special_on_character.trigger_bindings[0].kind = JsScriptingManifestKind::MudlleCallFlag;
    refresh_checksum(special_on_character);
    EXPECT_FALSE(js_script_package_validate(special_on_character, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(special_on_character, internal_options()),
        JsScriptPackageDiagnosticCode::WrongHost));

    JsScriptPackage room_enter = make_package(2304, JsScriptPackageHost::Room);
    EXPECT_FALSE(js_script_package_validate(room_enter, internal_options()).ok);
    EXPECT_TRUE(has_code(js_script_package_validate(room_enter, internal_options()),
        JsScriptPackageDiagnosticCode::WrongHost));
}

TEST(JsScriptPackage, RejectsManifestRuntimeTypingsAndChecksumDrift)
{
    JsScriptPackage package = make_package();

    JsScriptPackage changed = package;
    changed.package_format_version++;
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::ManifestMismatch));

    changed = package;
    changed.manifest_schema_version++;
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::ManifestMismatch));

    changed = package;
    changed.trigger_catalog_revision++;
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::ManifestMismatch));

    changed = package;
    changed.manifest_checksum = "stale";
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::ManifestMismatch));

    changed = package;
    changed.runtime_name = "Node";
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::RuntimeMismatch));

    changed = package;
    changed.generated_typings_version = "stale";
    refresh_checksum(changed);
    EXPECT_TRUE(has_code(js_script_package_validate(changed, internal_options()),
        JsScriptPackageDiagnosticCode::TypingsMismatch));
}

TEST(JsScriptPackage, RejectsUnknownTriggersAndHandlerMismatches)
{
    JsScriptPackage package = make_package();
    package.trigger_bindings[0].legacy_value = 9999;
    refresh_checksum(package);

    JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::UnknownTrigger));

    package = make_package();
    package.trigger_bindings[0].handler_name = "wrongHandler";
    refresh_checksum(package);
    result = js_script_package_validate(package, internal_options());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::MissingHandler));
}

TEST(JsScriptPackage, RejectsDuplicateBindingsAndRegistryVnums)
{
    JsScriptPackage package = make_package();
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter" });
    refresh_checksum(package);

    JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::DuplicateTrigger));

    JsScriptPackage first = make_package(2501);
    JsScriptPackage second = make_package(2501);
    second.package_id = "pkg-2501b";
    refresh_checksum(second);
    result = js_script_package_registry_validate({ first, second }, internal_options());
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::DuplicateVnum));
}

TEST(JsScriptPackage, RejectsCompiledJavaScriptChecksumMismatch)
{
    JsScriptPackage package = make_package();
    package.compiled_javascript += "\nfunction extra() { return true; }";

    JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::SourceChecksumMismatch));
}

TEST(JsScriptPackage, RejectsUnsafeSourcePolicyWithoutExecutingRuntime)
{
    const char* bad_sources[] = {
        "function onEnter(ctx) { import('fs'); }",
        "function onEnter(ctx) { import/**/('fs'); }",
        "function onEnter(ctx) { import\n('fs'); }",
        "import value from 'fs'; function onEnter(ctx) { return true; }",
        "function onEnter(ctx) { eval('1'); }",
        "function onEnter(ctx) { globalThis['eval']('1'); }",
        "function onEnter(ctx) { globalThis[ 'constructor' ].constructor('return true')(); }",
        "function onEnter(ctx) { globalThis['Function']('return true')(); }",
        R"(function onEnter(ctx) { return globalThis['con\x73tructor']; })",
        "function onEnter(ctx) { Function('return true')(); }",
        "function onEnter(ctx) { return ({}).constructor.constructor('return true')(); }",
        "function onEnter(ctx) { return `${Function('return true')()}`; }",
        "function* onEnter(ctx) { yield true; }",
        "function/**/ * onEnter(ctx) { yield true; }",
        R"(function onEnter(ctx) { const r = /\/*/; const A = (async () => {})['con' + 'structor']; A('globalThis.pwned = true')(); return true; })",
        "async function onEnter(ctx) { return true; }",
        "function onEnter(ctx) { return Promise.resolve(true); }",
        "function onEnter(ctx) { return true; }\n//# sourceMappingURL=data:application/json;base64,ZXZhbA==",
    };

    for (const char* source : bad_sources) {
        JsScriptPackage package = make_package();
        package.compiled_javascript = source;
        refresh_checksum(package);

        JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());
        EXPECT_FALSE(result.ok) << source;
        EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation))
            << source;
    }
}

TEST(JsScriptPackage, IgnoresForbiddenWordsInsideStringsAndComments)
{
    JsScriptPackage package = make_package();
    package.compiled_javascript = "function onEnter(ctx) { const text = 'import eval Function Promise'; return true; }\n"
                                  "function onReceive(ctx) { const bracketedText = \"globalThis['eval'] and ['constructor']\"; return !!bracketedText; }\n"
                                  "function onDrink(ctx) { const staticTemplate = `import eval Function Promise constructor`; return !!staticTemplate; }\n"
                                  "// import eval Function Promise\n"
                                  "// globalThis['eval'] and ['constructor']\n"
                                  "/* import eval Function Promise */";
    refresh_checksum(package);

    JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());

    EXPECT_TRUE(result.ok);
}

TEST(JsScriptPackage, DiagnosticsAreSingleLineBoundedAndCoded)
{
    JsScriptPackage package = make_package();
    package.package_id = std::string(400, 'x');
    package.compiled_javascript.clear();
    refresh_checksum(package);

    JsScriptPackageValidationResult result = js_script_package_validate(package, internal_options());

    ASSERT_FALSE(result.ok);
    ASSERT_FALSE(result.diagnostics.empty());
    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        EXPECT_LE(diagnostic.message.size(), 240U);
        EXPECT_EQ(diagnostic.message.find('\n'), std::string::npos);
        EXPECT_EQ(diagnostic.message.find('\r'), std::string::npos);
        EXPECT_STRNE(js_script_package_diagnostic_code_name(diagnostic.code), "unknown");
        EXPECT_EQ(diagnostic.vnum, package.vnum);
    }
}

TEST(JsScriptPackage, ExposesStablePublicStringNames)
{
    EXPECT_STREQ(js_script_package_host_name(JsScriptPackageHost::Character), "character");
    EXPECT_STREQ(js_script_package_host_name(JsScriptPackageHost::Object), "object");
    EXPECT_STREQ(js_script_package_host_name(JsScriptPackageHost::Room), "room");
    EXPECT_STREQ(js_script_package_host_name(JsScriptPackageHost::MudlleMobile), "mudlle-mobile");
    EXPECT_STREQ(js_script_package_host_name(static_cast<JsScriptPackageHost>(999)), "unknown");
    EXPECT_STREQ(js_script_package_validation_mode_name(JsScriptPackageValidationMode::Publish),
        "publish");
    EXPECT_STREQ(js_script_package_validation_mode_name(
                     JsScriptPackageValidationMode::InternalValidationOnly),
        "internal-validation-only");
    EXPECT_STREQ(js_script_package_validation_mode_name(
                     static_cast<JsScriptPackageValidationMode>(999)),
        "unknown");
    EXPECT_STREQ(js_script_package_diagnostic_code_name(JsScriptPackageDiagnosticCode::DuplicateVnum),
        "duplicate-vnum");
    EXPECT_STREQ(js_script_package_diagnostic_code_name(JsScriptPackageDiagnosticCode::DuplicatePackageId),
        "duplicate-package-id");
    EXPECT_STREQ(js_script_package_diagnostic_code_name(JsScriptPackageDiagnosticCode::LegacyVnumConflict),
        "legacy-vnum-conflict");
    EXPECT_STREQ(js_script_package_diagnostic_code_name(
                     static_cast<JsScriptPackageDiagnosticCode>(999)),
        "unknown");
}
