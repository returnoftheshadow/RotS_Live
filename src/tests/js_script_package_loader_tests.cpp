#include "../js_script_package_loader.h"

#include "../interpre.h"
#include "../json_utils.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::string quote(const std::string& value)
{
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

void refresh_checksum(JsScriptPackage& package)
{
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
}

JsScriptPackage make_package(int vnum = 6101, JsScriptPackageHost host = JsScriptPackageHost::Character,
    JsScriptingManifestKind kind = JsScriptingManifestKind::LegacyScriptTrigger,
    int legacy_value = ON_ENTER, const char* handler = "onEnter",
    const std::string& source = "function onEnter(ctx) { return true; }")
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
    package.compiled_javascript = source;
    package.trigger_bindings.push_back({ kind, legacy_value, handler });
    refresh_checksum(package);
    return package;
}

std::string binding_json(const JsScriptTriggerBinding& binding)
{
    std::ostringstream out;
    out << "{"
        << "\"kind\":" << quote(js_scripting_manifest_kind_name(binding.kind)) << ","
        << "\"legacyValue\":" << binding.legacy_value << ","
        << "\"handlerName\":" << quote(binding.handler_name) << "}";
    return out.str();
}

std::string package_json(const JsScriptPackage& package, const std::string& extra_field = "")
{
    std::ostringstream out;
    out << "{"
        << "\"vnum\":" << package.vnum << ","
        << "\"packageId\":" << quote(package.package_id) << ","
        << "\"host\":" << quote(js_script_package_host_name(package.host)) << ","
        << "\"packageFormatVersion\":" << package.package_format_version << ","
        << "\"manifestSchemaVersion\":" << package.manifest_schema_version << ","
        << "\"triggerCatalogRevision\":" << package.trigger_catalog_revision << ","
        << "\"manifestChecksum\":" << quote(package.manifest_checksum) << ","
        << "\"runtimeName\":" << quote(package.runtime_name) << ","
        << "\"runtimeVersion\":" << quote(package.runtime_version) << ","
        << "\"generatedTypingsVersion\":" << quote(package.generated_typings_version) << ","
        << "\"compiledJavaScriptChecksum\":" << quote(package.compiled_javascript_checksum) << ","
        << "\"compiledJavaScript\":" << quote(package.compiled_javascript) << ","
        << "\"triggerBindings\":[";
    for (std::size_t index = 0; index < package.trigger_bindings.size(); ++index) {
        if (index > 0)
            out << ",";
        out << binding_json(package.trigger_bindings[index]);
    }
    out << "]";
    if (!extra_field.empty())
        out << "," << extra_field;
    out << "}";
    return out.str();
}

std::string bundle_json(const std::vector<JsScriptPackage>& packages)
{
    std::ostringstream out;
    out << "{\"packages\":[";
    for (std::size_t index = 0; index < packages.size(); ++index) {
        if (index > 0)
            out << ",";
        out << package_json(packages[index]);
    }
    out << "]}";
    return out.str();
}

JsScriptRegistryReplaceOptions internal_options()
{
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    options.allow_empty_replacement = false;
    return options;
}

std::string temp_path(const char* name)
{
    return "/tmp/rots-js-package-loader-" + std::to_string(static_cast<long long>(getpid())) + "-" + name;
}

void write_file(const std::string& path, const std::string& contents)
{
    std::ofstream file(path, std::ios::binary);
    file << contents;
}

std::string read_first_available_file(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths) {
        std::ifstream file(path);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
    return "";
}

bool diagnostic_contains(const JsScriptPackageBundleLoadResult& result, const std::string& needle)
{
    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void expect_sanitized_diagnostics(const JsScriptPackageBundleLoadResult& result)
{
    ASSERT_FALSE(result.diagnostics.empty());
    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        EXPECT_EQ(diagnostic.code, JsScriptPackageDiagnosticCode::InvalidMetadata);
        EXPECT_LE(diagnostic.message.size(), 240U);
        EXPECT_LE(diagnostic.package_id.size(), 240U);
        for (unsigned char ch : diagnostic.message)
            EXPECT_GE(ch, 0x20);
        for (unsigned char ch : diagnostic.package_id)
            EXPECT_GE(ch, 0x20);
        EXPECT_EQ(diagnostic.message.find("function onEnter"), std::string::npos);
    }
}

} // namespace

TEST(JsScriptPackageLoader, ParsesCompleteBundleIntoPackageFields)
{
    JsScriptPackage object_package = make_package(6101, JsScriptPackageHost::Object,
        JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage",
        "function onDamage(ctx) { return ctx.object !== null; }");
    JsScriptPackage mudlle_package = make_package(6102, JsScriptPackageHost::MudlleMobile,
        JsScriptingManifestKind::MudlleCallFlag, SPECIAL_COMMAND, "onSpecialCommand",
        "function onSpecialCommand(ctx) { return true; }");

    JsScriptPackageBundleLoadResult result;
    ASSERT_TRUE(js_script_package_bundle_parse_json(
        bundle_json({ object_package, mudlle_package }), &result));

    ASSERT_EQ(result.packages.size(), 2U);
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.packages[0].vnum, 6101);
    EXPECT_EQ(result.packages[0].package_id, "pkg-6101");
    EXPECT_EQ(result.packages[0].host, JsScriptPackageHost::Object);
    EXPECT_EQ(result.packages[0].package_format_version, object_package.package_format_version);
    EXPECT_EQ(result.packages[0].manifest_schema_version, object_package.manifest_schema_version);
    EXPECT_EQ(result.packages[0].trigger_catalog_revision, object_package.trigger_catalog_revision);
    EXPECT_EQ(result.packages[0].manifest_checksum, object_package.manifest_checksum);
    EXPECT_EQ(result.packages[0].runtime_name, object_package.runtime_name);
    EXPECT_EQ(result.packages[0].runtime_version, object_package.runtime_version);
    EXPECT_EQ(result.packages[0].generated_typings_version,
        object_package.generated_typings_version);
    EXPECT_EQ(result.packages[0].compiled_javascript, object_package.compiled_javascript);
    EXPECT_EQ(result.packages[0].compiled_javascript_checksum,
        object_package.compiled_javascript_checksum);
    ASSERT_EQ(result.packages[0].trigger_bindings.size(), 1U);
    EXPECT_EQ(result.packages[0].trigger_bindings[0].kind,
        JsScriptingManifestKind::LegacyScriptTrigger);
    EXPECT_EQ(result.packages[0].trigger_bindings[0].legacy_value, ON_DAMAGE);
    EXPECT_EQ(result.packages[0].trigger_bindings[0].handler_name, "onDamage");

    EXPECT_EQ(result.packages[1].host, JsScriptPackageHost::MudlleMobile);
    EXPECT_EQ(result.packages[1].trigger_bindings[0].kind, JsScriptingManifestKind::MudlleCallFlag);
}

TEST(JsScriptPackageLoader, RejectsMissingUnknownDuplicateAndWrongTypedFields)
{
    JsScriptPackage package = make_package();
    const std::vector<std::pair<std::string, std::string>> bad_bundles = {
        { "{\"packages\":[]}", "empty" },
        { "{\"unexpected\":true,\"packages\":[]}", "unknown package bundle field 'unexpected'" },
        { "{\"packages\":[],\"packages\":[]}", "duplicate JSON field 'packages'" },
        { "{\"packages\":[{\"packageId\":\"pkg\"}]}", "missing required package field 'vnum'" },
        { "{\"packages\":[{\"vnum\":\"not-int\"}]}", "Expected integer value" },
        { "{\"packages\":{}}", "Expected array value" },
        { "{\"packages\":[{\"vnum\":1,\"packageId\":7}]}", "Expected string value" },
        { "{\"packages\":[{\"vnum\":1,\"packageId\":\"pkg\",\"host\":7}]}",
            "Expected string value" },
        { "{\"packages\":[{\"vnum\":1,\"packageId\":\"pkg\",\"host\":\"character\","
          "\"packageFormatVersion\":\"1\"}]}",
            "Expected integer value" },
        { "{\"packages\":[" + package_json(package, "\"extra\":true") + "]}",
            "unknown package field 'extra'" },
        { "{\"packages\":[" + package_json(package, "\"vnum\":9999") + "]}",
            "duplicate JSON field 'vnum'" },
        { "{\"packages\":[{\"vnum\":1,\"packageId\":\"pkg\",\"host\":\"mudlleMobile\"}]}",
            "unknown JavaScript package host" },
    };

    for (const auto& bad_bundle : bad_bundles) {
        JsScriptPackageBundleLoadResult result;
        EXPECT_FALSE(js_script_package_bundle_parse_json(bad_bundle.first, &result))
            << bad_bundle.second;
        EXPECT_TRUE(diagnostic_contains(result, bad_bundle.second)) << bad_bundle.first;
        expect_sanitized_diagnostics(result);
    }
}

TEST(JsScriptPackageLoader, RejectsMalformedBindingFields)
{
    JsScriptPackage package = make_package();
    const std::string base = package_json(package);
    (void)base;
    const std::vector<std::pair<std::string, std::string>> bad_bundles = {
        { "{\"packages\":[{\"vnum\":6201,\"packageId\":\"pkg-6201\",\"host\":\"character\","
          "\"packageFormatVersion\":1,\"manifestSchemaVersion\":1,\"triggerCatalogRevision\":1,"
          "\"manifestChecksum\":\"rots-js-manifest-v1-trigger-catalog-1\","
          "\"runtimeName\":\"QuickJS\",\"runtimeVersion\":\"2026-06-04\","
          "\"generatedTypingsVersion\":\"unpublished\","
          "\"compiledJavaScriptChecksum\":\"x\",\"compiledJavaScript\":\"x\","
          "\"triggerBindings\":[{\"legacyValue\":11,\"handlerName\":\"onEnter\"}]}]}",
            "missing required trigger binding field 'kind'" },
        { "{\"packages\":[{\"vnum\":6202,\"packageId\":\"pkg-6202\",\"host\":\"character\","
          "\"packageFormatVersion\":1,\"manifestSchemaVersion\":1,\"triggerCatalogRevision\":1,"
          "\"manifestChecksum\":\"rots-js-manifest-v1-trigger-catalog-1\","
          "\"runtimeName\":\"QuickJS\",\"runtimeVersion\":\"2026-06-04\","
          "\"generatedTypingsVersion\":\"unpublished\","
          "\"compiledJavaScriptChecksum\":\"x\",\"compiledJavaScript\":\"x\","
          "\"triggerBindings\":[{\"kind\":\"legacy_script_trigger\",\"legacyValue\":11,"
          "\"handlerName\":\"onEnter\"}]}]}",
            "unknown JavaScript trigger binding kind" },
        { "{\"packages\":[{\"vnum\":6203,\"packageId\":\"pkg-6203\",\"host\":\"character\","
          "\"packageFormatVersion\":1,\"manifestSchemaVersion\":1,\"triggerCatalogRevision\":1,"
          "\"manifestChecksum\":\"rots-js-manifest-v1-trigger-catalog-1\","
          "\"runtimeName\":\"QuickJS\",\"runtimeVersion\":\"2026-06-04\","
          "\"generatedTypingsVersion\":\"unpublished\","
          "\"compiledJavaScriptChecksum\":\"x\",\"compiledJavaScript\":\"x\","
          "\"triggerBindings\":{}}]}",
            "Expected array value" },
        { "{\"packages\":[{\"vnum\":6204,\"packageId\":\"pkg-6204\",\"host\":\"character\","
          "\"packageFormatVersion\":1,\"manifestSchemaVersion\":1,\"triggerCatalogRevision\":1,"
          "\"manifestChecksum\":\"rots-js-manifest-v1-trigger-catalog-1\","
          "\"runtimeName\":\"QuickJS\",\"runtimeVersion\":\"2026-06-04\","
          "\"generatedTypingsVersion\":\"unpublished\","
          "\"compiledJavaScriptChecksum\":\"x\",\"compiledJavaScript\":\"x\","
          "\"triggerBindings\":[7]}]}",
            "Expected JSON object" },
        { "{\"packages\":[{\"vnum\":6205,\"packageId\":\"pkg-6205\",\"host\":\"character\","
          "\"packageFormatVersion\":1,\"manifestSchemaVersion\":1,\"triggerCatalogRevision\":1,"
          "\"manifestChecksum\":\"rots-js-manifest-v1-trigger-catalog-1\","
          "\"runtimeName\":\"QuickJS\",\"runtimeVersion\":\"2026-06-04\","
          "\"generatedTypingsVersion\":\"unpublished\","
          "\"compiledJavaScriptChecksum\":\"x\",\"compiledJavaScript\":\"x\","
          "\"triggerBindings\":[{\"kind\":\"legacy-script-trigger\",\"kind\":\"mudlle-call-flag\","
          "\"legacyValue\":11,\"handlerName\":\"onEnter\"}]}]}",
            "duplicate JSON field 'kind'" },
        { "{\"packages\":[" + package_json(package) + "]} trailing", "Unexpected trailing" },
        { "{\"packages\":[" + package_json(package) + ",", "Expected JSON object" },
    };

    for (const auto& bad_bundle : bad_bundles) {
        JsScriptPackageBundleLoadResult result;
        EXPECT_FALSE(js_script_package_bundle_parse_json(bad_bundle.first, &result))
            << bad_bundle.second;
        EXPECT_TRUE(diagnostic_contains(result, bad_bundle.second)) << bad_bundle.first;
        expect_sanitized_diagnostics(result);
    }
}

TEST(JsScriptPackageLoader, EnforcesFileAndStringLimitsBeforeRegistryReplacement)
{
    const std::string path = temp_path("limits.json");
    write_file(path, bundle_json({ make_package() }));

    JsScriptPackageBundleLoadOptions options;
    options.maximum_file_bytes = 4;
    JsScriptPackageBundleLoadResult result;
    EXPECT_FALSE(js_script_package_bundle_load_file(path, options, &result));
    EXPECT_TRUE(diagnostic_contains(result, "exceeds maximum size"));
    expect_sanitized_diagnostics(result);

    options = {};
    options.maximum_string_bytes = 4;
    EXPECT_FALSE(js_script_package_bundle_load_file(path, options, &result));
    EXPECT_TRUE(diagnostic_contains(result, "string field 'packageId' exceeds maximum size"));
    expect_sanitized_diagnostics(result);

    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(6251) }, internal_options()));
    EXPECT_FALSE(js_script_package_registry_load_file(path, options, internal_options(), &registry,
        &result, nullptr));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(6251), nullptr);

    std::remove(path.c_str());
}

TEST(JsScriptPackageLoader, EnforcesPackageAndBindingCountLimits)
{
    JsScriptPackage first = make_package(6261);
    JsScriptPackage second = make_package(6262);
    JsScriptPackageBundleLoadOptions options;
    options.maximum_package_count = 2;
    JsScriptPackageBundleLoadResult result;
    EXPECT_TRUE(js_script_package_bundle_parse_json(bundle_json({ first, second }), options, &result));
    EXPECT_EQ(result.packages.size(), 2U);

    options.maximum_package_count = 1;
    EXPECT_FALSE(js_script_package_bundle_parse_json(bundle_json({ first, second }), options, &result));
    EXPECT_TRUE(result.packages.empty());
    EXPECT_TRUE(diagnostic_contains(result, "too many JavaScript packages"));

    JsScriptPackage two_bindings = make_package(6263);
    two_bindings.compiled_javascript += "\nfunction onDamage(ctx) { return true; }";
    two_bindings.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_DAMAGE, "onDamage" });
    refresh_checksum(two_bindings);

    options = {};
    options.maximum_trigger_binding_count = 2;
    EXPECT_TRUE(js_script_package_bundle_parse_json(bundle_json({ two_bindings }), options, &result));
    EXPECT_EQ(result.packages[0].trigger_bindings.size(), 2U);

    options.maximum_trigger_binding_count = 1;
    EXPECT_FALSE(js_script_package_bundle_parse_json(bundle_json({ two_bindings }), options, &result));
    EXPECT_TRUE(result.packages.empty());
    EXPECT_TRUE(diagnostic_contains(result, "too many JavaScript trigger bindings"));
}

TEST(JsScriptPackageLoader, MissingAndMalformedFilesDoNotReplaceRegistry)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage original = make_package(6301);
    ASSERT_TRUE(registry.replace_all({ original }, internal_options()));

    JsScriptPackageBundleLoadOptions load_options;
    JsScriptPackageBundleLoadResult load_result;
    JsScriptPackageValidationResult validation_result;
    EXPECT_FALSE(js_script_package_registry_load_file(temp_path("missing.json"), load_options,
        internal_options(), &registry, &load_result, &validation_result));
    EXPECT_TRUE(diagnostic_contains(load_result, "failed to open"));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(6301), nullptr);
    EXPECT_TRUE(validation_result.diagnostics.empty());

    const std::string malformed_path = temp_path("malformed.json");
    write_file(malformed_path, "{\"packages\":[");
    load_result = {};
    validation_result = {};
    EXPECT_FALSE(js_script_package_registry_load_file(malformed_path, load_options,
        internal_options(), &registry, &load_result, &validation_result));
    EXPECT_TRUE(diagnostic_contains(load_result, "failed to parse"));
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(6301), nullptr);
    EXPECT_TRUE(validation_result.diagnostics.empty());

    std::remove(malformed_path.c_str());
}

TEST(JsScriptPackageLoader, LoadFailureClearsStaleValidationResult)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(6351) }, internal_options()));

    JsScriptPackageValidationResult validation_result;
    validation_result.ok = true;
    validation_result.diagnostics.push_back({});
    JsScriptPackageBundleLoadResult load_result;

    EXPECT_FALSE(js_script_package_registry_load_file(temp_path("missing-stale-validation.json"),
        {}, internal_options(), &registry, &load_result, &validation_result));

    EXPECT_FALSE(validation_result.ok);
    EXPECT_TRUE(validation_result.diagnostics.empty());
    EXPECT_EQ(registry.package_count(), 1U);
}

TEST(JsScriptPackageLoader, ValidationFailuresAfterParseDoNotReplaceRegistry)
{
    JsScriptPackageRegistry registry;
    JsScriptPackage original = make_package(6401);
    ASSERT_TRUE(registry.replace_all({ original }, internal_options()));

    JsScriptPackage invalid = make_package(6402);
    invalid.compiled_javascript += "\nfunction extra(ctx) { return true; }";
    const std::string path = temp_path("invalid-validation.json");
    write_file(path, bundle_json({ invalid }));

    JsScriptPackageBundleLoadResult load_result;
    JsScriptPackageValidationResult validation_result;
    EXPECT_FALSE(js_script_package_registry_load_file(path, {}, internal_options(), &registry,
        &load_result, &validation_result));

    EXPECT_TRUE(load_result.ok);
    ASSERT_FALSE(validation_result.diagnostics.empty());
    EXPECT_EQ(validation_result.diagnostics[0].code,
        JsScriptPackageDiagnosticCode::SourceChecksumMismatch);
    EXPECT_EQ(registry.package_count(), 1U);
    EXPECT_NE(registry.find_package_by_vnum(6401), nullptr);
    EXPECT_EQ(registry.find_package_by_vnum(6402), nullptr);

    std::remove(path.c_str());
}

TEST(JsScriptPackageLoader, PartialMultiPackageParseFailureExposesNoPackages)
{
    JsScriptPackage valid = make_package(6451);
    const std::string json = "{\"packages\":[" + package_json(valid)
        + ",{\"vnum\":6452,\"packageId\":\"pkg-6452\",\"host\":\"character\"}]}";

    JsScriptPackageBundleLoadResult result;
    EXPECT_FALSE(js_script_package_bundle_parse_json(json, &result));

    EXPECT_TRUE(result.packages.empty());
    EXPECT_TRUE(diagnostic_contains(result, "missing required package field"));
    expect_sanitized_diagnostics(result);
}

TEST(JsScriptPackageLoader, EmptyBundleRequiresExplicitLoaderAndRegistryClear)
{
    JsScriptPackageRegistry registry;
    ASSERT_TRUE(registry.replace_all({ make_package(6501) }, internal_options()));
    const std::string path = temp_path("empty.json");
    write_file(path, "{\"packages\":[]}");

    JsScriptPackageBundleLoadOptions load_options;
    JsScriptPackageBundleLoadResult load_result;
    EXPECT_FALSE(js_script_package_registry_load_file(path, load_options, internal_options(),
        &registry, &load_result, nullptr));
    EXPECT_TRUE(diagnostic_contains(load_result, "bundle is empty"));
    EXPECT_EQ(registry.package_count(), 1U);

    load_options.allow_empty_bundle = true;
    JsScriptRegistryReplaceOptions replace_options = internal_options();
    replace_options.allow_empty_replacement = true;
    EXPECT_TRUE(js_script_package_registry_load_file(path, load_options, replace_options, &registry,
        &load_result, nullptr));
    EXPECT_TRUE(registry.empty());

    std::remove(path.c_str());
}

TEST(JsScriptPackageLoader, DiagnosticPackageIdsAreSanitizedAndDoNotIncludeSource)
{
    JsScriptPackage package = make_package(6601);
    package.package_id = std::string(260, 'x') + "\npackage";
    refresh_checksum(package);
    std::string json = package_json(package);
    const std::string needle = "\"compiledJavaScript\":";
    json.erase(json.find(needle), needle.size() + quote(package.compiled_javascript).size() + 1);

    JsScriptPackageBundleLoadResult result;
    EXPECT_FALSE(js_script_package_bundle_parse_json("{\"packages\":[" + json + "]}", &result));
    expect_sanitized_diagnostics(result);
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].package_id.find('\n'), std::string::npos);
}

TEST(JsScriptPackageLoader, RejectsUnsupportedUnicodeEscapesInCompiledSource)
{
    JsScriptPackage package = make_package(6701);
    std::string json = package_json(package);
    const std::string source_value = quote(package.compiled_javascript);
    json.replace(json.find(source_value), source_value.size(), "\"function onEnter(ctx) { return \\u00e9; }\"");

    JsScriptPackageBundleLoadResult result;
    EXPECT_FALSE(js_script_package_bundle_parse_json("{\"packages\":[" + json + "]}", &result));
    EXPECT_TRUE(diagnostic_contains(result, "Unsupported unicode escape sequence"));
}

TEST(JsScriptPackageLoader, BuildFilesReferenceLoaderSourcesAndTests)
{
    const std::string cmake =
        read_first_available_file({ "src/CMakeLists.txt", "../src/CMakeLists.txt" });
    const std::string server_makefile =
        read_first_available_file({ "src/Makefile", "../src/Makefile" });
    const std::string test_makefile =
        read_first_available_file({ "src/tests/Makefile", "../src/tests/Makefile" });

    ASSERT_FALSE(cmake.empty());
    ASSERT_FALSE(server_makefile.empty());
    ASSERT_FALSE(test_makefile.empty());

    EXPECT_NE(cmake.find("js_script_package_loader.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("tests/js_script_package_loader_tests.cpp"), std::string::npos);
    EXPECT_NE(server_makefile.find("js_script_package_loader.o"), std::string::npos);
    EXPECT_NE(server_makefile.find("js_script_package_loader.cpp"), std::string::npos);
    EXPECT_NE(test_makefile.find("js_script_package_loader.o"), std::string::npos);
    EXPECT_NE(test_makefile.find("js_script_package_loader_tests.cpp"), std::string::npos);
}
