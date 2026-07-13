#include "../js_script_package_reload_service.h"

#include "../interpre.h"
#include "../json_utils.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
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

JsScriptPackage make_package(int vnum = 6801, JsScriptPackageHost host = JsScriptPackageHost::Character,
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

std::string package_json(const JsScriptPackage& package)
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
    out << "]}";
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

std::string temp_root()
{
    static int counter = 0;
    const std::string root = "/tmp/rots-js-reload-service-"
        + std::to_string(static_cast<long long>(getpid())) + "-" + std::to_string(++counter);
    mkdir(root.c_str(), 0700);
    return root;
}

void make_dir(const std::string& path)
{
    mkdir(path.c_str(), 0700);
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

JsScriptPackageReloadOptions reload_options(const std::string& root)
{
    JsScriptPackageReloadOptions options;
    options.package_root = root;
    options.replace_options.validation_options.mode
        = JsScriptPackageValidationMode::InternalValidationOnly;
    options.replace_options.allow_empty_replacement = false;
    return options;
}

bool diagnostic_contains(const JsScriptPackageReloadResult& result, const std::string& needle)
{
    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void expect_bounded_diagnostics(const JsScriptPackageReloadResult& result)
{
    ASSERT_FALSE(result.diagnostics.empty());
    for (const JsScriptPackageDiagnostic& diagnostic : result.diagnostics) {
        EXPECT_LE(diagnostic.message.size(), 240U);
        for (unsigned char ch : diagnostic.message)
            EXPECT_GE(ch, 0x20);
        EXPECT_EQ(diagnostic.message.find("/tmp/rots-js-reload-service"), std::string::npos);
        EXPECT_EQ(diagnostic.message.find("function onEnter"), std::string::npos);
    }
}

void expect_package_loaded(const JsScriptPackageReloadService& service, int vnum)
{
    const JsScriptPackage* package = service.find_package_by_vnum(vnum);
    ASSERT_NE(package, nullptr);
    EXPECT_EQ(package->package_id, "pkg-" + std::to_string(vnum));
}

} // namespace

TEST(JsScriptPackageReloadService, LoadsBundleFromCanonicalRootAndExposesLookups)
{
    const std::string root = temp_root();
    make_dir(root + "/compiled");
    JsScriptPackage package = make_package(6801);
    write_file(root + "/compiled/live.json", bundle_json({ package }));

    JsScriptPackageReloadService service(reload_options(root));
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("compiled/live.json", &result));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Success);
    EXPECT_EQ(result.package_count, 1U);
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.last_successful_request_path(), "compiled/live.json");
    EXPECT_EQ(service.last_successful_package_count(), 1U);
    expect_package_loaded(service, 6801);
    const JsScriptTriggerBinding* binding = service.find_trigger_binding(6801,
        JsScriptPackageHost::Character, JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->handler_name, "onEnter");
}

TEST(JsScriptPackageReloadService, RejectsUntrustedRequestPathsBeforeLoading)
{
    const std::string root = temp_root();
    make_dir(root + "/compiled");
    write_file(root + "/compiled/live.json", bundle_json({ make_package(6811) }));
    const std::string long_request(400, 'x');

    JsScriptPackageReloadService service(reload_options(root));
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("compiled/live.json", &result));

    const std::vector<std::string> bad_paths = {
        "",
        root + "/compiled/live.json",
        "../compiled/live.json",
        "compiled/../../live.json",
        "./compiled/live.json",
        "compiled/../compiled/live.json",
        "compiled/bad\nname.json",
        long_request,
    };

    for (const std::string& path : bad_paths) {
        SCOPED_TRACE(path);
        EXPECT_FALSE(service.reload_bundle(path, &result));
        EXPECT_EQ(result.status, JsScriptPackageReloadStatus::InvalidRequestPath);
        expect_bounded_diagnostics(result);
        EXPECT_EQ(service.package_count(), 1U);
        EXPECT_EQ(service.last_successful_request_path(), "compiled/live.json");
        EXPECT_EQ(service.last_successful_package_count(), 1U);
        expect_package_loaded(service, 6811);
    }
}

TEST(JsScriptPackageReloadService, RejectsMissingDirectoriesAndSymlinks)
{
    const std::string root = temp_root();
    make_dir(root + "/compiled");
    make_dir(root + "/compiled/directory.json");
    write_file(root + "/compiled/live.json", bundle_json({ make_package(6821) }));
    symlink("compiled/live.json", (root + "/link.json").c_str());
    symlink("compiled", (root + "/compiled-link").c_str());
    const std::string outside = temp_root();
    write_file(outside + "/outside.json", bundle_json({ make_package(6822) }));
    symlink(outside.c_str(), (root + "/outside-link").c_str());

    JsScriptPackageReloadService service(reload_options(root));
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("compiled/live.json", &result));

    EXPECT_FALSE(service.reload_bundle("compiled/missing.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::MissingFile);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    expect_package_loaded(service, 6821);

    EXPECT_FALSE(service.reload_bundle("compiled/directory.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Directory);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    expect_package_loaded(service, 6821);

    EXPECT_FALSE(service.reload_bundle("link.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Symlink);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    expect_package_loaded(service, 6821);

    EXPECT_FALSE(service.reload_bundle("compiled-link/live.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Symlink);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    expect_package_loaded(service, 6821);

    EXPECT_FALSE(service.reload_bundle("outside-link/outside.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Symlink);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.last_successful_request_path(), "compiled/live.json");
    expect_package_loaded(service, 6821);
}

TEST(JsScriptPackageReloadService, InvalidRootsNeverLoadRequests)
{
    const std::string root = temp_root();
    write_file(root + "/file-root", "not a directory");
    make_dir(root + "/real-root");
    symlink("real-root", (root + "/symlink-root").c_str());

    const std::vector<std::string> roots = {
        root + "/missing-root",
        root + "/file-root",
        root + "/symlink-root",
    };

    for (const std::string& bad_root : roots) {
        SCOPED_TRACE(bad_root);
        JsScriptPackageReloadService service(reload_options(bad_root));
        JsScriptPackageReloadResult result;
        EXPECT_FALSE(service.reload_bundle("live.json", &result));
        EXPECT_EQ(result.status, JsScriptPackageReloadStatus::InvalidRoot);
        expect_bounded_diagnostics(result);
        EXPECT_TRUE(service.empty());
    }
}

TEST(JsScriptPackageReloadService, CanCreateConfiguredRootWhenExplicitlyEnabled)
{
    const std::string root = temp_root() + "/created/root";
    JsScriptPackageReloadOptions options = reload_options(root);
    options.create_package_root = true;
    JsScriptPackageReloadService service(options);
    write_file(root + "/live.json", bundle_json({ make_package(6831) }));

    JsScriptPackageReloadResult result;
    EXPECT_TRUE(service.reload_bundle("live.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::Success);
    expect_package_loaded(service, 6831);
}

TEST(JsScriptPackageReloadService, RootCreationRejectsSymlinkedIntermediateParent)
{
    const std::string root = temp_root();
    const std::string outside = temp_root();
    symlink(outside.c_str(), (root + "/link-parent").c_str());

    JsScriptPackageReloadOptions options = reload_options(root + "/link-parent/created");
    options.create_package_root = true;
    JsScriptPackageReloadService service(options);

    JsScriptPackageReloadResult result;
    EXPECT_FALSE(service.reload_bundle("live.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::InvalidRoot);
    expect_bounded_diagnostics(result);
    EXPECT_TRUE(service.empty());
}

TEST(JsScriptPackageReloadService, ReloadRejectsRootSwappedToSymlinkAfterConstruction)
{
    const std::string root = temp_root();
    JsScriptPackageReloadService service(reload_options(root));

    const std::string outside = temp_root();
    write_file(outside + "/live.json", bundle_json({ make_package(6836) }));
    ASSERT_EQ(rmdir(root.c_str()), 0);
    ASSERT_EQ(symlink(outside.c_str(), root.c_str()), 0);

    JsScriptPackageReloadResult result;
    EXPECT_FALSE(service.reload_bundle("live.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::OutsideRoot);
    expect_bounded_diagnostics(result);
    EXPECT_TRUE(service.empty());
    EXPECT_EQ(service.find_package_by_vnum(6836), nullptr);
}

TEST(JsScriptPackageReloadService, ParseFailurePreservesRegistryAndLastSuccess)
{
    const std::string root = temp_root();
    write_file(root + "/good.json", bundle_json({ make_package(6841) }));
    write_file(root + "/bad.json", "{\"packages\":[");

    JsScriptPackageReloadService service(reload_options(root));
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("good.json", &result));

    EXPECT_FALSE(service.reload_bundle("bad.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::LoadFailed);
    EXPECT_TRUE(diagnostic_contains(result, "failed to parse"));
    expect_bounded_diagnostics(result);
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.last_successful_request_path(), "good.json");
    EXPECT_EQ(service.last_successful_package_count(), 1U);
    expect_package_loaded(service, 6841);
}

TEST(JsScriptPackageReloadService, FileSizeLimitFailurePreservesRegistryAndLastSuccess)
{
    const std::string root = temp_root();
    const std::string good_bundle = bundle_json({ make_package(6846) });
    const std::string oversized_bundle = bundle_json({ make_package(6847) }) + std::string(128, ' ');
    ASSERT_GT(oversized_bundle.size(), good_bundle.size());
    write_file(root + "/good.json", good_bundle);
    write_file(root + "/oversized.json", oversized_bundle);

    JsScriptPackageReloadOptions limited_options = reload_options(root);
    limited_options.load_options.maximum_file_bytes = good_bundle.size();
    JsScriptPackageReloadService limited_service(limited_options);
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(limited_service.reload_bundle("good.json", &result));

    ASSERT_FALSE(limited_service.reload_bundle("oversized.json", &result));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::LoadFailed);
    expect_bounded_diagnostics(result);
    EXPECT_EQ(limited_service.package_count(), 1U);
    EXPECT_EQ(limited_service.last_successful_request_path(), "good.json");
    EXPECT_EQ(limited_service.last_successful_package_count(), 1U);
    expect_package_loaded(limited_service, 6846);
    EXPECT_EQ(limited_service.find_package_by_vnum(6847), nullptr);
}

TEST(JsScriptPackageReloadService, ValidationFailurePreservesRegistryAndLastSuccess)
{
    const std::string root = temp_root();
    write_file(root + "/good.json", bundle_json({ make_package(6851) }));
    JsScriptPackage invalid = make_package(6852);
    invalid.compiled_javascript += "\nfunction changed(ctx) { return true; }";
    write_file(root + "/invalid.json", bundle_json({ invalid }));

    JsScriptPackageReloadService service(reload_options(root));
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("good.json", &result));

    EXPECT_FALSE(service.reload_bundle("invalid.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::ValidationFailed);
    ASSERT_FALSE(result.validation_result.diagnostics.empty());
    EXPECT_EQ(result.validation_result.diagnostics[0].code,
        JsScriptPackageDiagnosticCode::SourceChecksumMismatch);
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.last_successful_request_path(), "good.json");
    expect_package_loaded(service, 6851);
    EXPECT_EQ(service.find_package_by_vnum(6852), nullptr);
}

TEST(JsScriptPackageReloadService, EmptyBundleRequiresExplicitLoaderAndRegistryClear)
{
    const std::string root = temp_root();
    write_file(root + "/good.json", bundle_json({ make_package(6861) }));
    write_file(root + "/empty.json", "{\"packages\":[]}");

    JsScriptPackageReloadOptions options = reload_options(root);
    JsScriptPackageReloadService service(options);
    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("good.json", &result));

    EXPECT_FALSE(service.reload_bundle("empty.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::LoadFailed);
    EXPECT_EQ(service.package_count(), 1U);
    EXPECT_EQ(service.last_successful_request_path(), "good.json");

    options.load_options.allow_empty_bundle = true;
    options.replace_options.allow_empty_replacement = true;
    JsScriptPackageReloadService clearing_service(options);
    ASSERT_TRUE(clearing_service.reload_bundle("good.json", &result));
    EXPECT_TRUE(clearing_service.reload_bundle("empty.json", &result));
    EXPECT_TRUE(clearing_service.empty());
    EXPECT_EQ(clearing_service.last_successful_request_path(), "empty.json");
    EXPECT_EQ(clearing_service.last_successful_package_count(), 0U);
}

TEST(JsScriptPackageReloadService, LegacyVnumConflictPreservesRegistry)
{
    const std::string root = temp_root();
    write_file(root + "/good.json", bundle_json({ make_package(6871) }));
    write_file(root + "/conflict.json", bundle_json({ make_package(6872) }));

    JsScriptPackageReloadOptions options = reload_options(root);
    options.replace_options.legacy_script_vnums.push_back(6872);
    JsScriptPackageReloadService service(options);

    JsScriptPackageReloadResult result;
    ASSERT_TRUE(service.reload_bundle("good.json", &result));
    EXPECT_FALSE(service.reload_bundle("conflict.json", &result));
    EXPECT_EQ(result.status, JsScriptPackageReloadStatus::ValidationFailed);
    EXPECT_EQ(service.package_count(), 1U);
    expect_package_loaded(service, 6871);
    EXPECT_EQ(service.find_package_by_vnum(6872), nullptr);
}

TEST(JsScriptPackageReloadService, StatusNamesAreStable)
{
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::Success),
        "success");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::InvalidRoot),
        "invalid-root");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::InvalidRequestPath),
        "invalid-request-path");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::MissingFile),
        "missing-file");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::Directory),
        "directory");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::Symlink),
        "symlink");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::OutsideRoot),
        "outside-root");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::LoadFailed),
        "load-failed");
    EXPECT_STREQ(js_script_package_reload_status_name(JsScriptPackageReloadStatus::ValidationFailed),
        "validation-failed");
}

TEST(JsScriptPackageReloadService, BuildFilesReferenceReloadServiceSourcesAndTests)
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

    EXPECT_NE(cmake.find("js_script_package_reload_service.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("tests/js_script_package_reload_service_tests.cpp"), std::string::npos);
    EXPECT_NE(server_makefile.find("js_script_package_reload_service.o"), std::string::npos);
    EXPECT_NE(server_makefile.find("js_script_package_reload_service.cpp"), std::string::npos);
    EXPECT_NE(test_makefile.find("js_script_package_reload_service.o"), std::string::npos);
    EXPECT_NE(test_makefile.find("js_script_package_reload_service_tests.cpp"), std::string::npos);
}
