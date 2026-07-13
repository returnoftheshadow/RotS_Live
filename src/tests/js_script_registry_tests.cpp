#include "../js_script_registry.h"

#include "../interpre.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

JsScriptPackage make_package(int vnum = 3001, JsScriptPackageHost host = JsScriptPackageHost::Character,
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
    package.compiled_javascript = "function onEnter(ctx) { return true; }\n"
                                  "function onDamage(ctx) { return true; }\n"
                                  "function onSpecialCommand(ctx) { return true; }";
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, trigger, handler });
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

void refresh_checksum(JsScriptPackage& package)
{
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
}

JsScriptRegistryReplaceOptions internal_options()
{
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

bool has_code(const JsScriptPackageValidationResult& result, JsScriptPackageDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsScriptPackageDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

} // namespace

TEST(JsScriptRegistry, StartsEmptyAndLookupMisses)
{
    JsScriptPackageRegistry registry;

    EXPECT_TRUE(registry.empty());
    EXPECT_EQ(registry.package_count(), 0U);
    EXPECT_EQ(registry.find_package_by_vnum(1), nullptr);
    EXPECT_EQ(registry.find_package_by_id("missing"), nullptr);
    EXPECT_EQ(registry.find_trigger_binding(1, JsScriptPackageHost::Character,
                  JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER),
        nullptr);
    EXPECT_TRUE(registry.find_packages_for_trigger(JsScriptPackageHost::Character,
                            JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER)
            .empty());
}

TEST(JsScriptRegistry, AtomicReplaceLoadsValidInternalPackages)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage first = make_package(3101);
    JsScriptPackage second = make_package(3102, JsScriptPackageHost::Object, ON_DAMAGE, "onDamage");
    JsScriptPackageValidationResult result;

    EXPECT_TRUE(registry.replace_all({ first, second }, internal_options(), &result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(registry.package_count(), 2U);
    ASSERT_NE(registry.find_package_by_vnum(3101), nullptr);
    EXPECT_EQ(registry.find_package_by_vnum(3101)->package_id, "pkg-3101");
    ASSERT_NE(registry.find_package_by_id("pkg-3102"), nullptr);
    EXPECT_EQ(registry.find_package_by_id("pkg-3102")->host, JsScriptPackageHost::Object);
}

TEST(JsScriptRegistry, AtomicReplaceKeepsPreviousSnapshotWhenValidationFails)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage original = make_package(3201);
    ASSERT_TRUE(registry.replace_all({ original }, internal_options()));

    JsScriptPackage invalid = make_package(3202);
    invalid.compiled_javascript = "function onEnter(ctx) { eval('1'); }";
    refresh_checksum(invalid);

    JsScriptPackageValidationResult result;
    EXPECT_FALSE(registry.replace_all({ invalid }, internal_options(), &result));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation));

    EXPECT_EQ(registry.package_count(), 1U);
    ASSERT_NE(registry.find_package_by_vnum(3201), nullptr);
    EXPECT_EQ(registry.find_package_by_vnum(3202), nullptr);
    ASSERT_NE(registry.find_trigger_binding(3201, JsScriptPackageHost::Character,
                  JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER),
        nullptr);
}

TEST(JsScriptRegistry, RejectsDuplicateVnumsAndPackageIdsWithoutMutation)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(3301) }, internal_options()));

    JsScriptPackage first = make_package(3302);
    JsScriptPackage duplicate_vnum = make_package(3302);
    duplicate_vnum.package_id = "pkg-3302b";
    refresh_checksum(duplicate_vnum);

    JsScriptPackageValidationResult result;
    EXPECT_FALSE(registry.replace_all({ first, duplicate_vnum }, internal_options(), &result));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::DuplicateVnum));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(3301), nullptr);

    JsScriptPackage duplicate_id = make_package(3303);
    JsScriptPackage duplicate_id_second = make_package(3304);
    duplicate_id_second.package_id = duplicate_id.package_id;
    refresh_checksum(duplicate_id_second);

    result = {};
    EXPECT_FALSE(registry.replace_all({ duplicate_id, duplicate_id_second }, internal_options(), &result));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::DuplicatePackageId));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(3301), nullptr);
}

TEST(JsScriptRegistry, RejectsLegacyScriptVnumConflictWithoutMutation)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(3401) }, internal_options()));

    JsScriptRegistryReplaceOptions options = internal_options();
    options.legacy_script_vnums = { 3402, 9999 };

    JsScriptPackageValidationResult result;
    EXPECT_FALSE(registry.replace_all({ make_package(3402) }, options, &result));

    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::LegacyVnumConflict));
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].vnum, 3402);
    EXPECT_EQ(result.diagnostics[0].package_id, "pkg-3402");
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(3401), nullptr);
    EXPECT_EQ(registry.find_package_by_vnum(3402), nullptr);
}

TEST(JsScriptRegistry, AllowsVnumWhenLegacyConflictSetDoesNotContainIt)
{
    JsScriptPackageRegistry registry;
    JsScriptRegistryReplaceOptions options = internal_options();
    options.legacy_script_vnums = { 1111, 2222 };

    EXPECT_TRUE(registry.replace_all({ make_package(3501) }, options));
    EXPECT_NE(registry.find_package_by_vnum(3501), nullptr);
}

TEST(JsScriptRegistry, FindTriggerBindingUsesVnumHostKindAndLegacyValue)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(3601);
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage" });
    refresh_checksum(package);
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    const JsScriptTriggerBinding* enter = registry.find_trigger_binding(3601,
        JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER);
    ASSERT_NE(enter, nullptr);
    EXPECT_EQ(enter->handler_name, "onEnter");

    const JsScriptTriggerBinding* damage = registry.find_trigger_binding(3601,
        JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE);
    ASSERT_NE(damage, nullptr);
    EXPECT_EQ(damage->handler_name, "onDamage");

    EXPECT_EQ(registry.find_trigger_binding(3601, JsScriptPackageHost::Object,
                  JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER),
        nullptr);
    EXPECT_EQ(registry.find_trigger_binding(3601, JsScriptPackageHost::Character,
                  JsScriptingManifestKind::MudlleCallFlag, ON_ENTER),
        nullptr);
    EXPECT_EQ(registry.find_trigger_binding(9999, JsScriptPackageHost::Character,
                  JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER),
        nullptr);
}

TEST(JsScriptRegistry, FindPackagesForTriggerIsHostAwareAndStable)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage character_first = make_package(3701);
    JsScriptPackage character_second = make_package(3702);
    JsScriptPackage object_damage = make_package(3703, JsScriptPackageHost::Object, ON_DAMAGE, "onDamage");
    JsScriptPackage mudlle_command = make_package(3704, JsScriptPackageHost::MudlleMobile,
        SPECIAL_COMMAND, "onSpecialCommand");
    mudlle_command.trigger_bindings[0].kind = JsScriptingManifestKind::MudlleCallFlag;
    refresh_checksum(mudlle_command);
    ASSERT_TRUE(registry.replace_all(
        { character_first, object_damage, character_second, mudlle_command }, internal_options()));

    std::vector<const JsScriptPackage*> character_matches = registry.find_packages_for_trigger(
        JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER);
    ASSERT_EQ(character_matches.size(), 2U);
    EXPECT_EQ(character_matches[0]->vnum, 3701);
    EXPECT_EQ(character_matches[1]->vnum, 3702);

    std::vector<const JsScriptPackage*> object_matches = registry.find_packages_for_trigger(
        JsScriptPackageHost::Object, JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE);
    ASSERT_EQ(object_matches.size(), 1U);
    EXPECT_EQ(object_matches[0]->vnum, 3703);

    std::vector<const JsScriptPackage*> mudlle_matches = registry.find_packages_for_trigger(
        JsScriptPackageHost::MudlleMobile, JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND);
    ASSERT_EQ(mudlle_matches.size(), 1U);
    EXPECT_EQ(mudlle_matches[0]->vnum, 3704);

    EXPECT_TRUE(registry.find_packages_for_trigger(JsScriptPackageHost::Character,
                            JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND)
            .empty());
}

TEST(JsScriptRegistry, StoresSnapshotUnaffectedByCallerMutation)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage package = make_package(3801);
    ASSERT_TRUE(registry.replace_all({ package }, internal_options()));

    package.package_id = "changed-after-replace";
    package.compiled_javascript = "function onEnter(ctx) { eval('1'); }";
    package.trigger_bindings.clear();

    const JsScriptPackage* stored = registry.find_package_by_vnum(3801);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->package_id, "pkg-3801");
    ASSERT_EQ(stored->trigger_bindings.size(), 1U);
    EXPECT_EQ(stored->compiled_javascript_checksum,
        js_script_package_compiled_javascript_checksum(*stored));
}

TEST(JsScriptRegistry, EmptyReplacementPolicyIsExplicit)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(3901) }, internal_options()));

    JsScriptRegistryReplaceOptions disallow_empty = internal_options();
    disallow_empty.allow_empty_replacement = false;
    JsScriptPackageValidationResult result;
    EXPECT_FALSE(registry.replace_all({}, disallow_empty, &result));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::InvalidMetadata));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(3901), nullptr);

    EXPECT_TRUE(registry.replace_all({}, internal_options(), &result));
    EXPECT_TRUE(registry.empty());
}

TEST(JsScriptRegistry, ReportsAggregatedDiagnosticsWithoutPartialMutation)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(4001) }, internal_options()));

    JsScriptRegistryReplaceOptions options = internal_options();
    options.legacy_script_vnums = { 4002 };

    JsScriptPackage duplicate = make_package(4002);
    JsScriptPackage duplicate_id = make_package(4003);
    duplicate_id.package_id = duplicate.package_id;
    duplicate_id.compiled_javascript = "function onEnter(ctx) { Function('return true')(); }";
    refresh_checksum(duplicate_id);

    JsScriptPackageValidationResult result;
    EXPECT_FALSE(registry.replace_all({ duplicate, duplicate_id }, options, &result));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::DuplicatePackageId));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::LegacyVnumConflict));
    EXPECT_TRUE(has_code(result, JsScriptPackageDiagnosticCode::SourcePolicyViolation));

    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        EXPECT_STRNE(js_script_package_diagnostic_code_name(diagnostic.code), "unknown");
        EXPECT_LE(diagnostic.message.size(), 240U);
        EXPECT_EQ(diagnostic.message.find('\n'), std::string::npos);
        EXPECT_EQ(diagnostic.message.find('\r'), std::string::npos);
    }
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(4001), nullptr);
}
