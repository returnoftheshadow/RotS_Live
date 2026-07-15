#include "../js_live_package_store_persistence.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

JsScriptPackage make_package(int vnum = 9101, const std::string &body = "return true") {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "client-pkg-" + std::to_string(vnum);
    package.host = JsScriptPackageHost::Character;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = "function onEnter(ctx) { " + body + "; }";
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter"});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageStageOptions make_stage_options(const std::string &base_live = "live:old") {
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 91;
    options.identity_options.builder_account_id = "account:builder";
    options.identity_options.base_live_checksum = base_live;
    options.identity_options.server_instance_id = "server:main";
    options.audit.staged_at_epoch_seconds = 123456;
    options.audit.request_id = "request:stage";
    options.audit.actor_id = "actor:42";
    options.audit.permission_snapshot_id = "permission:snapshot";
    options.audit.audit_id = "audit:stage";
    options.audit.source_policy_decision = "source-policy:accepted";
    options.audit.validation_report_digest = "validation:sha256:abc";
    options.audit.transport_source_identifier = "transport:tls";
    return options;
}

JsStagedPackageRecord stage_record(int vnum = 9101, const std::string &body = "return true") {
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(vnum, body), make_stage_options());
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

std::string digest_body(const std::string &digest) {
    const std::string::size_type colon = digest.find(':');
    return colon == std::string::npos ? digest : digest.substr(colon + 1);
}

JsLivePackagePointer make_pointer(const JsStagedPackageRecord &record) {
    JsLivePackagePointer pointer;
    pointer.zone = record.identity.zone;
    pointer.vnum = record.identity.vnum;
    pointer.host = record.identity.host;
    pointer.package_id = record.identity.package_id;
    pointer.package_version_id = record.identity.package_version_id;
    pointer.staged_digest = record.identity.canonical_digest;
    pointer.current_live_checksum =
        std::string("live:") + record.identity.digest_algorithm + ":" +
        digest_body(record.identity.canonical_digest);
    pointer.loaded_at_epoch_seconds = 200000;
    pointer.load_audit_id = "audit:load";
    return pointer;
}

JsLivePackageStoreSnapshot make_snapshot(const std::string &body = "return true") {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record(9101, body);
    EXPECT_TRUE(store.store_staged_record(staged).ok);
    EXPECT_TRUE(store.load_live_pointer(make_pointer(staged)).ok);
    return store.export_snapshot();
}

std::string temp_file_path(const std::string &name) {
    return "build/rots-js-live-store-persistence-" + std::to_string(static_cast<long>(getpid())) +
        "-" + name + ".json";
}

std::string read_first_available_file(std::initializer_list<const char *> paths) {
    for (const char *path : paths) {
        std::ifstream file(path);
        if (file.good())
            return std::string((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    }
    return {};
}

void replace_once(std::string *text, const std::string &from, const std::string &to) {
    const std::string::size_type pos = text->find(from);
    ASSERT_NE(std::string::npos, pos);
    text->replace(pos, from.size(), to);
}

void insert_after_once(std::string *text, const std::string &needle, const std::string &insert) {
    const std::string::size_type pos = text->find(needle);
    ASSERT_NE(std::string::npos, pos);
    text->insert(pos + needle.size(), insert);
}

std::string repeat_json_value(const std::string &value, std::size_t count) {
    std::string repeated;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0)
            repeated += ",";
        repeated += value;
    }
    return repeated;
}

std::string slice_between(const std::string &text, const std::string &start_marker,
                          const std::string &end_marker) {
    const std::string::size_type start_marker_pos = text.find(start_marker);
    EXPECT_NE(std::string::npos, start_marker_pos);
    if (start_marker_pos == std::string::npos)
        return {};
    const std::string::size_type start = start_marker_pos + start_marker.size();
    const std::string::size_type end = text.find(end_marker, start);
    EXPECT_NE(std::string::npos, end);
    if (end == std::string::npos)
        return {};
    return text.substr(start, end - start);
}

void expect_failed_empty_snapshot(const JsLivePackageStorePersistenceLoadResult &result,
                                  const std::string &forbidden_diagnostic_text = "") {
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.snapshot.records.empty());
    EXPECT_TRUE(result.snapshot.live_pointers.empty());
    EXPECT_TRUE(result.snapshot.prior_live_pointers.empty());
    if (!forbidden_diagnostic_text.empty()) {
        for (const JsLivePackageStorePersistenceDiagnostic &diagnostic : result.diagnostics)
            EXPECT_EQ(std::string::npos, diagnostic.message.find(forbidden_diagnostic_text));
    }
}

} // namespace

TEST(JsLivePackageStorePersistence, JsonRoundTripHydratesEquivalentLiveStore) {
    JsLivePackageStoreSnapshot snapshot = make_snapshot("return 'roundtrip'");

    JsLivePackageStorePersistenceLoadResult decoded =
        js_live_package_store_snapshot_from_json(js_live_package_store_snapshot_to_json(snapshot));

    ASSERT_TRUE(decoded.ok);
    ASSERT_EQ(1u, decoded.snapshot.records.size());
    ASSERT_EQ(1u, decoded.snapshot.live_pointers.size());
    JsLivePackageStore hydrated;
    JsLivePackageStoreHydrationResult hydrated_result =
        hydrated.hydrate_from_snapshot(decoded.snapshot);
    ASSERT_TRUE(hydrated_result.ok);
    EXPECT_EQ(1u, hydrated.package_record_count());
    EXPECT_EQ(1u, hydrated.live_pointer_count());
    const JsLivePackageRecord &original = snapshot.records.front();
    EXPECT_TRUE(hydrated.find_record(original.identity.package_id,
                    original.identity.package_version_id)
                    .ok);
    EXPECT_TRUE(hydrated.find_live_pointer(original.identity.zone, original.identity.host,
                    original.identity.vnum)
                    .ok);
}

TEST(JsLivePackageStorePersistence, JsonRoundTripPreservesSourceBearingFields) {
    const std::string source_sentinel = "PERSISTENCE_SOURCE_SENTINEL";
    JsLivePackageStoreSnapshot snapshot =
        make_snapshot("return '" + source_sentinel + "\\nwith escape'");

    std::string json = js_live_package_store_snapshot_to_json(snapshot);
    JsLivePackageStorePersistenceLoadResult decoded =
        js_live_package_store_snapshot_from_json(json);

    ASSERT_TRUE(decoded.ok);
    ASSERT_EQ(1u, decoded.snapshot.records.size());
    const JsLivePackageRecord &record = decoded.snapshot.records.front();
    EXPECT_NE(std::string::npos, record.package.compiled_javascript.find(source_sentinel));
    EXPECT_EQ(snapshot.records.front().identity.package_version_id,
              record.identity.package_version_id);
    EXPECT_EQ(snapshot.records.front().identity.canonical_digest,
              record.identity.canonical_digest);
    EXPECT_EQ(snapshot.records.front().identity.base_live_checksum,
              record.identity.base_live_checksum);
    EXPECT_EQ(snapshot.records.front().identity.server_instance_id,
              record.identity.server_instance_id);
    EXPECT_EQ(snapshot.records.front().staged_audit.audit_id,
              record.staged_audit.audit_id);
    ASSERT_EQ(1u, record.package.trigger_bindings.size());
    EXPECT_EQ(snapshot.records.front().package.trigger_bindings.front().handler_name,
              record.package.trigger_bindings.front().handler_name);
}

TEST(JsLivePackageStorePersistence, RejectsMalformedJsonAndUnsupportedSchema) {
    JsLivePackageStorePersistenceLoadResult malformed =
        js_live_package_store_snapshot_from_json("{\"schema_version\":");
    JsLivePackageStorePersistenceLoadResult unsupported =
        js_live_package_store_snapshot_from_json(
            "{\"schema_version\":999,\"records\":[],\"live_pointers\":[]}");

    EXPECT_FALSE(malformed.ok);
    EXPECT_FALSE(malformed.diagnostics.empty());
    EXPECT_FALSE(unsupported.ok);
    EXPECT_FALSE(unsupported.diagnostics.empty());
}

TEST(JsLivePackageStorePersistence, RejectsMissingDuplicateAndUnknownTopLevelFields) {
    JsLivePackageStorePersistenceLoadResult missing_arrays =
        js_live_package_store_snapshot_from_json("{\"schema_version\":1}");
    JsLivePackageStorePersistenceLoadResult duplicate_records =
        js_live_package_store_snapshot_from_json(
            "{\"schema_version\":1,\"records\":[],\"records\":[],\"live_pointers\":[]}");
    JsLivePackageStorePersistenceLoadResult unknown =
        js_live_package_store_snapshot_from_json(
            "{\"schema_version\":1,\"records\":[],\"live_pointers\":[],\"extra\":true}");

    EXPECT_FALSE(missing_arrays.ok);
    EXPECT_FALSE(duplicate_records.ok);
    EXPECT_FALSE(unknown.ok);
}

TEST(JsLivePackageStorePersistence, RejectsMissingNestedFieldsBeforeReturningSuccess) {
    std::string missing_source = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&missing_source, "\"compiled_javascript\":\"function onEnter(ctx) { return true; }\",",
                 "");
    JsLivePackageStoreSnapshot snapshot = make_snapshot();
    std::string missing_pointer_digest = js_live_package_store_snapshot_to_json(snapshot);
    replace_once(&missing_pointer_digest,
                 "\"staged_digest\":\"" + snapshot.live_pointers.front().staged_digest + "\",",
                 "");

    JsLivePackageStorePersistenceLoadResult source_result =
        js_live_package_store_snapshot_from_json(missing_source);
    JsLivePackageStorePersistenceLoadResult pointer_result =
        js_live_package_store_snapshot_from_json(missing_pointer_digest);

    EXPECT_FALSE(source_result.ok);
    EXPECT_FALSE(pointer_result.ok);
}

TEST(JsLivePackageStorePersistence, RejectsUnknownNestedFields) {
    std::string record_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&record_unknown, "{\"identity\":",
                 "{\"unexpected_record_field\":true,\"identity\":");
    std::string identity_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&identity_unknown, "\"identity\":{\"zone\":91,",
                      "\"unexpected_identity_field\":true,");
    std::string audit_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&audit_unknown, "\"staged_audit\":{\"staged_at_epoch_seconds\":123456,",
                      "\"unexpected_audit_field\":true,");
    std::string package_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&package_unknown, "\"package\":{\"vnum\":9101,",
                      "\"unexpected_package_field\":true,");
    std::string binding_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&binding_unknown, "\"trigger_bindings\":[{\"kind\":",
                 "\"trigger_bindings\":[{\"unexpected_binding_field\":true,\"kind\":");
    std::string pointer_unknown = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&pointer_unknown, "\"live_pointers\":[{\"zone\":91,",
                      "\"unexpected_pointer_field\":true,");

    EXPECT_FALSE(js_live_package_store_snapshot_from_json(record_unknown).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(identity_unknown).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(audit_unknown).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(package_unknown).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(binding_unknown).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(pointer_unknown).ok);
}

TEST(JsLivePackageStorePersistence, RejectsDuplicateNestedFields) {
    std::string record_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&record_duplicate, "{\"identity\":", "{\"identity\":{},\"identity\":");
    std::string identity_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&identity_duplicate, "\"identity\":{\"zone\":91,", "\"zone\":92,");
    std::string audit_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&audit_duplicate, "\"staged_audit\":{\"staged_at_epoch_seconds\":123456,",
                      "\"staged_at_epoch_seconds\":123457,");
    std::string package_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&package_duplicate, "\"package\":{\"vnum\":9101,", "\"vnum\":9102,");
    std::string binding_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&binding_duplicate, "\"trigger_bindings\":[{\"kind\":\"legacy-script-trigger\",",
                 "\"trigger_bindings\":[{\"kind\":\"legacy-script-trigger\","
                 "\"kind\":\"mudlle-call-flag\",");
    std::string pointer_duplicate = js_live_package_store_snapshot_to_json(make_snapshot());
    insert_after_once(&pointer_duplicate, "\"live_pointers\":[{\"zone\":91,", "\"zone\":92,");

    EXPECT_FALSE(js_live_package_store_snapshot_from_json(record_duplicate).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(identity_duplicate).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(audit_duplicate).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(package_duplicate).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(binding_duplicate).ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(pointer_duplicate).ok);
}

TEST(JsLivePackageStorePersistence, RejectsWrongPrimitiveTypes) {
    JsLivePackageStorePersistenceLoadResult bad_schema =
        js_live_package_store_snapshot_from_json(
            "{\"schema_version\":\"1\",\"records\":[],\"live_pointers\":[]}");
    std::string bad_source_type = js_live_package_store_snapshot_to_json(make_snapshot());
    replace_once(&bad_source_type, "\"compiled_javascript\":\"function onEnter(ctx) { return true; }\"",
                 "\"compiled_javascript\":42");

    EXPECT_FALSE(bad_schema.ok);
    EXPECT_FALSE(js_live_package_store_snapshot_from_json(bad_source_type).ok);
}

TEST(JsLivePackageStorePersistence, RejectsUnknownEnumValuesBeforeHydration) {
    std::string json = js_live_package_store_snapshot_to_json(make_snapshot());
    const std::string needle = "\"host\":\"character\"";
    const std::string::size_type pos = json.find(needle);
    ASSERT_NE(std::string::npos, pos);
    json.replace(pos, needle.size(), "\"host\":\"bad-host\"");

    JsLivePackageStorePersistenceLoadResult decoded =
        js_live_package_store_snapshot_from_json(json);

    EXPECT_FALSE(decoded.ok);
    EXPECT_FALSE(decoded.diagnostics.empty());
}

TEST(JsLivePackageStorePersistence, RejectsPersistenceResourceLimitExcesses) {
    JsLivePackageStorePersistenceLoadResult oversized_json =
        js_live_package_store_snapshot_from_json(std::string(2 * 1024 * 1024 + 1, ' '));
    expect_failed_empty_snapshot(oversized_json);

    const std::string source_sentinel = "LIMIT_SOURCE_SENTINEL";
    std::string oversized_source =
        js_live_package_store_snapshot_to_json(make_snapshot("return '" + source_sentinel + "'"));
    replace_once(&oversized_source,
                 "\"compiled_javascript\":\"function onEnter(ctx) { return '" +
                     source_sentinel + "'; }\"",
                 "\"compiled_javascript\":\"" + std::string(300 * 1024, 'x') + "\"");
    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(oversized_source),
                                 source_sentinel);

    std::string json =
        js_live_package_store_snapshot_to_json(make_snapshot("return '" + source_sentinel + "'"));
    const std::string record = slice_between(json, "\"records\":[", "],\"live_pointers\"");
    const std::string pointer = slice_between(json, "\"live_pointers\":[", "],\"prior_live_pointers\"");
    std::string too_many_records = "{\"schema_version\":1,\"records\":[" +
        repeat_json_value(record, 130) + "],\"live_pointers\":[" + pointer + "]}";
    std::string too_many_pointers = "{\"schema_version\":1,\"records\":[" + record +
        "],\"live_pointers\":[" + repeat_json_value(pointer, 130) + "]}";
    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(too_many_records),
                                 source_sentinel);
    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(too_many_pointers),
                                 source_sentinel);

    std::string too_many_bindings = json;
    const std::string binding = slice_between(json, "\"trigger_bindings\":[", "]}");
    replace_once(&too_many_bindings, "\"trigger_bindings\":[" + binding + "]",
                 "\"trigger_bindings\":[" + repeat_json_value(binding, 520) + "]");
    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(too_many_bindings),
                                 source_sentinel);
}

TEST(JsLivePackageStorePersistence, FailureDoesNotExposePartiallyDecodedSnapshot) {
    const std::string source_sentinel = "PARTIAL_FAILURE_SOURCE_SENTINEL";
    JsLivePackageStoreSnapshot snapshot = make_snapshot("return '" + source_sentinel + "'");
    std::string unsupported_schema = js_live_package_store_snapshot_to_json(snapshot);
    replace_once(&unsupported_schema, "\"schema_version\":1", "\"schema_version\":999");

    std::string hydration_failure = js_live_package_store_snapshot_to_json(snapshot);
    replace_once(&hydration_failure,
                 "\"current_live_checksum\":\"" +
                     snapshot.live_pointers.front().current_live_checksum + "\"",
                 "\"current_live_checksum\":\"live:sha256:bad\"");

    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(unsupported_schema),
                                 source_sentinel);
    expect_failed_empty_snapshot(js_live_package_store_snapshot_from_json(hydration_failure),
                                 source_sentinel);
}

TEST(JsLivePackageStorePersistence, SavesAndLoadsSnapshotFileRoundTrip) {
    const std::string path = temp_file_path("roundtrip");
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
    JsLivePackageStoreSnapshot snapshot = make_snapshot("return 'file-roundtrip'");

    JsLivePackageStorePersistenceFileResult saved =
        js_live_package_store_snapshot_save_file(path, snapshot);
    JsLivePackageStorePersistenceLoadResult loaded =
        js_live_package_store_snapshot_load_file(path);

    EXPECT_TRUE(saved.ok);
    EXPECT_TRUE(saved.target_replaced);
    ASSERT_TRUE(loaded.ok);
    ASSERT_EQ(1u, loaded.snapshot.records.size());
    EXPECT_EQ(snapshot.records.front().identity.package_version_id,
              loaded.snapshot.records.front().identity.package_version_id);
    EXPECT_EQ(std::string::npos,
              read_first_available_file({(path + ".tmp").c_str()}).find("file-roundtrip"));
    struct stat st;
    ASSERT_EQ(0, stat(path.c_str(), &st));
    EXPECT_EQ(0u, static_cast<unsigned>(st.st_mode) & 0077u);
    std::remove(path.c_str());
}

TEST(JsLivePackageStorePersistence, LoadFileFailuresReturnEmptySnapshots) {
    JsLivePackageStorePersistenceLoadResult missing =
        js_live_package_store_snapshot_load_file(temp_file_path("missing"));
    JsLivePackageStorePersistenceLoadResult empty_path =
        js_live_package_store_snapshot_load_file("");
    const std::string corrupt_path = temp_file_path("corrupt");
    {
        std::ofstream file(corrupt_path, std::ios::binary | std::ios::trunc);
        file << "{\"schema_version\":1,\"records\":[";
    }

    expect_failed_empty_snapshot(missing);
    expect_failed_empty_snapshot(empty_path);
    expect_failed_empty_snapshot(js_live_package_store_snapshot_load_file(corrupt_path));
    std::remove(corrupt_path.c_str());
}

TEST(JsLivePackageStorePersistence, SaveFileFailurePreservesExistingSnapshot) {
    const std::string path = temp_file_path("preserve-existing");
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
    JsLivePackageStoreSnapshot original = make_snapshot("return 'original-live-store'");
    ASSERT_TRUE(js_live_package_store_snapshot_save_file(path, original).ok);

    JsLivePackageStoreSnapshot invalid = original;
    invalid.records.clear();
    JsLivePackageStorePersistenceFileResult failed_save =
        js_live_package_store_snapshot_save_file(path, invalid);
    JsLivePackageStorePersistenceLoadResult loaded =
        js_live_package_store_snapshot_load_file(path);

    EXPECT_FALSE(failed_save.ok);
    EXPECT_FALSE(failed_save.target_replaced);
    ASSERT_TRUE(loaded.ok);
    ASSERT_EQ(1u, loaded.snapshot.records.size());
    EXPECT_NE(std::string::npos,
              loaded.snapshot.records.front().package.compiled_javascript.find(
                  "original-live-store"));
    EXPECT_TRUE(read_first_available_file({(path + ".tmp").c_str()}).empty());
    std::remove(path.c_str());
}

TEST(JsLivePackageStorePersistence, SaveFileRejectsInvalidPathsWithoutWritingTempFile) {
    JsLivePackageStorePersistenceFileResult empty_path =
        js_live_package_store_snapshot_save_file("", make_snapshot());
    JsLivePackageStorePersistenceFileResult absolute_path =
        js_live_package_store_snapshot_save_file("/tmp/rots-live-store.json", make_snapshot());
    JsLivePackageStorePersistenceFileResult traversal_path =
        js_live_package_store_snapshot_save_file("build/../rots-live-store.json", make_snapshot());
    JsLivePackageStorePersistenceFileResult missing_directory =
        js_live_package_store_snapshot_save_file(
            "build/rots-missing-live-store-dir/snapshot.json", make_snapshot());

    EXPECT_FALSE(empty_path.ok);
    EXPECT_FALSE(empty_path.target_replaced);
    EXPECT_FALSE(absolute_path.ok);
    EXPECT_FALSE(absolute_path.target_replaced);
    EXPECT_FALSE(traversal_path.ok);
    EXPECT_FALSE(traversal_path.target_replaced);
    EXPECT_FALSE(missing_directory.ok);
    EXPECT_FALSE(missing_directory.target_replaced);
    EXPECT_TRUE(
        read_first_available_file({"build/rots-missing-live-store-dir/snapshot.json.tmp"}).empty());
    std::remove("build/../rots-live-store.json");
}

TEST(JsLivePackageStorePersistence, RejectsSymlinkLoadAndTemporarySaveTargets) {
    const std::string real_path = temp_file_path("real");
    const std::string load_link = temp_file_path("load-link");
    const std::string save_path = temp_file_path("save-symlink-temp");
    std::remove(real_path.c_str());
    std::remove(load_link.c_str());
    std::remove(save_path.c_str());
    std::remove((save_path + ".tmp").c_str());
    ASSERT_TRUE(js_live_package_store_snapshot_save_file(real_path, make_snapshot()).ok);
    ASSERT_EQ(0, symlink(real_path.c_str(), load_link.c_str()));
    ASSERT_EQ(0, symlink(real_path.c_str(), (save_path + ".tmp").c_str()));

    expect_failed_empty_snapshot(js_live_package_store_snapshot_load_file(load_link));
    EXPECT_FALSE(js_live_package_store_snapshot_save_file(save_path, make_snapshot()).ok);

    std::remove(real_path.c_str());
    std::remove(load_link.c_str());
    std::remove((save_path + ".tmp").c_str());
}

TEST(JsLivePackageStorePersistence, BuildFilesIncludePersistenceSourcesAndTests) {
    const std::string cmake = read_first_available_file({"src/CMakeLists.txt", "../CMakeLists.txt"});
    const std::string raw_make = read_first_available_file({"src/Makefile", "../Makefile"});
    const std::string tests_make =
        read_first_available_file({"src/tests/Makefile", "Makefile"});

    EXPECT_NE(std::string::npos, cmake.find("js_live_package_store_persistence.cpp"));
    EXPECT_NE(std::string::npos,
              cmake.find("tests/js_live_package_store_persistence_tests.cpp"));
    EXPECT_NE(std::string::npos, raw_make.find("js_live_package_store_persistence.o"));
    EXPECT_NE(std::string::npos, tests_make.find("js_live_package_store_persistence.o"));
    EXPECT_NE(std::string::npos,
              tests_make.find("js_live_package_store_persistence_tests.cpp"));
}
