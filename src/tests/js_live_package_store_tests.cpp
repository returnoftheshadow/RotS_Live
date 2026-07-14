#include "../js_live_package_store.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001, const std::string &body = "return true") {
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
    options.identity_options.zone = 30;
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

JsStagedPackageRecord stage_record(int vnum = 3001, const std::string &body = "return true",
                                   const std::string &base_live = "live:old") {
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(vnum, body), make_stage_options(base_live));
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

std::string digest_body(const std::string &digest) {
    const std::string::size_type colon = digest.find(':');
    return colon == std::string::npos ? digest : digest.substr(colon + 1);
}

std::string live_checksum_for_record(const JsStagedPackageRecord &record) {
    return std::string("live:") + record.identity.digest_algorithm + ":" +
           digest_body(record.identity.canonical_digest);
}

JsLivePackagePointer make_pointer(const JsStagedPackageRecord &record) {
    JsLivePackagePointer pointer;
    pointer.zone = record.identity.zone;
    pointer.vnum = record.identity.vnum;
    pointer.host = record.identity.host;
    pointer.package_id = record.identity.package_id;
    pointer.package_version_id = record.identity.package_version_id;
    pointer.staged_digest = record.identity.canonical_digest;
    pointer.current_live_checksum = live_checksum_for_record(record);
    pointer.loaded_at_epoch_seconds = 200000;
    pointer.load_audit_id = "audit:load";
    return pointer;
}

JsScriptRegistryReplaceOptions internal_registry_options() {
    JsScriptRegistryReplaceOptions options;
    options.validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    return options;
}

bool has_code(const JsLivePackageStoreRecordResult &result, JsLivePackageStoreDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsLivePackageStoreDiagnostic &diagnostic) { return diagnostic.code == code; });
}

bool has_code(const JsLivePackagePointerResult &result, JsLivePackageStoreDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsLivePackageStoreDiagnostic &diagnostic) { return diagnostic.code == code; });
}

bool has_code(const JsLivePackageStoreHydrationResult &result,
              JsLivePackageStoreDiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsLivePackageStoreDiagnostic &diagnostic) { return diagnostic.code == code; });
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

} // namespace

TEST(JsLivePackageStore, StartsEmptyAndReportsMissingLookups) {
    JsLivePackageStore store;

    EXPECT_TRUE(store.empty());
    EXPECT_EQ(0u, store.package_record_count());
    EXPECT_EQ(0u, store.live_pointer_count());
    EXPECT_TRUE(has_code(store.find_record("missing", "version"),
                         JsLivePackageStoreDiagnosticCode::PackageRecordNotFound));
    EXPECT_TRUE(has_code(store.find_live_pointer("missing"),
                         JsLivePackageStoreDiagnosticCode::LivePointerNotFound));
}

TEST(JsLivePackageStore, StoresImmutablePackageRecordFromStagedRecord) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();

    JsLivePackageStoreRecordResult result = store.store_staged_record(staged);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.inserted);
    EXPECT_EQ(1u, store.package_record_count());
    EXPECT_EQ(staged.identity.package_id, result.record.identity.package_id);
    EXPECT_EQ(staged.audit.audit_id, result.record.staged_audit.audit_id);
    EXPECT_EQ(staged.package.compiled_javascript, result.record.package.compiled_javascript);
}

TEST(JsLivePackageStore, RepeatedStoreOfSameRecordIsIdempotent) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();

    JsLivePackageStoreRecordResult first = store.store_staged_record(staged);
    JsLivePackageStoreRecordResult second = store.store_staged_record(staged);

    ASSERT_TRUE(first.ok);
    ASSERT_TRUE(second.ok);
    EXPECT_TRUE(first.inserted);
    EXPECT_TRUE(second.idempotent);
    EXPECT_EQ(1u, store.package_record_count());
}

TEST(JsLivePackageStore, RejectsDuplicateRecordWithChangedMetadata) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    JsStagedPackageRecord changed = staged;
    changed.audit.audit_id = "audit:changed";

    ASSERT_TRUE(store.store_staged_record(staged).ok);
    JsLivePackageStoreRecordResult result = store.store_staged_record(changed);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict));
    EXPECT_EQ(1u, store.package_record_count());
}

TEST(JsLivePackageStore, NormalizesStoredPackageIdToServerOwnedIdentity) {
    JsLivePackageStore store;
    JsStagedPackageRecord first = stage_record(3001);
    JsStagedPackageRecord second = stage_record(3002);
    first.package.package_id = "client-duplicate";
    second.package.package_id = "client-duplicate";

    ASSERT_TRUE(store.store_staged_record(first).ok);
    ASSERT_TRUE(store.store_staged_record(second).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(first)).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(second)).ok);

    JsLivePackageRegistrySnapshotResult snapshot =
        store.build_live_registry_snapshot(internal_registry_options());

    ASSERT_TRUE(snapshot.ok);
    EXPECT_NE(nullptr, snapshot.registry.find_package_by_id(first.identity.package_id));
    EXPECT_NE(nullptr, snapshot.registry.find_package_by_id(second.identity.package_id));
}

TEST(JsLivePackageStore, RejectsMalformedRecordWhoseIdentityDoesNotMatchPackage) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    staged.package.vnum = 9999;

    JsLivePackageStoreRecordResult result = store.store_staged_record(staged);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
    EXPECT_EQ(0u, store.package_record_count());
}

TEST(JsLivePackageStore, RejectsMalformedRecordWithStalePackageChecksum) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    staged.package.compiled_javascript = "function onEnter(ctx) { return false; }";

    JsLivePackageStoreRecordResult result = store.store_staged_record(staged);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, RejectsRecordWithForgedCanonicalDigestAndVersion) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    staged.identity.canonical_digest =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    staged.identity.package_version_id = js_staged_package_version_id(staged.identity);

    JsLivePackageStoreRecordResult result = store.store_staged_record(staged);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
    EXPECT_EQ(0u, store.package_record_count());
}

TEST(JsLivePackageStore, StoredRecordsAreCopied) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    JsLivePackageStoreRecordResult stored = store.store_staged_record(staged);
    ASSERT_TRUE(stored.ok);

    staged.package.compiled_javascript = "mutated";
    stored.record.package.compiled_javascript = "mutated copy";

    JsLivePackageStoreRecordResult lookup = store.find_record(
        stored.record.identity.package_id, stored.record.identity.package_version_id);

    ASSERT_TRUE(lookup.ok);
    EXPECT_EQ("function onEnter(ctx) { return true; }", lookup.record.package.compiled_javascript);
}

TEST(JsLivePackageStore, EnforcesPackageRecordLimitAfterIdempotentCheck) {
    JsLivePackageStoreOptions options;
    options.maximum_package_records = 1;
    JsLivePackageStore store(options);
    JsStagedPackageRecord first = stage_record(3001);
    JsStagedPackageRecord second = stage_record(3002);

    ASSERT_TRUE(store.store_staged_record(first).ok);
    EXPECT_TRUE(store.store_staged_record(first).ok);
    JsLivePackageStoreRecordResult over_limit = store.store_staged_record(second);

    EXPECT_FALSE(over_limit.ok);
    EXPECT_TRUE(has_code(over_limit, JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded));
}

TEST(JsLivePackageStore, LoadsLivePointerForStoredRecord) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);

    JsLivePackagePointerResult result = store.load_live_pointer(make_pointer(staged));

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.inserted);
    EXPECT_EQ(1u, store.live_pointer_count());
    EXPECT_EQ(staged.identity.package_version_id, result.pointer.package_version_id);
}

TEST(JsLivePackageStore, ReplacesLivePointerForSameSlot) {
    JsLivePackageStore store;
    JsStagedPackageRecord first = stage_record(3001, "return true", "live:first");
    JsStagedPackageRecord second = stage_record(3001, "return false", "live:second");
    ASSERT_TRUE(store.store_staged_record(first).ok);
    ASSERT_TRUE(store.store_staged_record(second).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(first)).ok);

    JsLivePackagePointer second_pointer = make_pointer(second);
    second_pointer.expected_previous_live_checksum = live_checksum_for_record(first);
    JsLivePackagePointerResult result = store.load_live_pointer(second_pointer);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.replaced);
    EXPECT_EQ(1u, store.live_pointer_count());
    JsLivePackagePointerResult current =
        store.find_live_pointer(first.identity.zone, first.identity.host, first.identity.vnum);
    ASSERT_TRUE(current.ok);
    EXPECT_EQ(second.identity.package_version_id, current.pointer.package_version_id);
}

TEST(JsLivePackageStore, RejectsStaleLivePointerReplacement) {
    JsLivePackageStore store;
    JsStagedPackageRecord first = stage_record(3001, "return true", "live:first");
    JsStagedPackageRecord second = stage_record(3001, "return false", "live:second");
    ASSERT_TRUE(store.store_staged_record(first).ok);
    ASSERT_TRUE(store.store_staged_record(second).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(first)).ok);

    JsLivePackagePointer stale = make_pointer(second);
    stale.expected_previous_live_checksum = "live:stale";
    JsLivePackagePointerResult result = store.load_live_pointer(stale);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::LivePointerConflict));
    JsLivePackagePointerResult current =
        store.find_live_pointer(first.identity.zone, first.identity.host, first.identity.vnum);
    ASSERT_TRUE(current.ok);
    EXPECT_EQ(first.identity.package_version_id, current.pointer.package_version_id);
}

TEST(JsLivePackageStore, RejectsLivePointerForMissingRecord) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();

    JsLivePackagePointerResult result = store.load_live_pointer(make_pointer(staged));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::PackageRecordNotFound));
    EXPECT_EQ(0u, store.live_pointer_count());
}

TEST(JsLivePackageStore, RejectsLivePointerDigestMismatch) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);
    JsLivePackagePointer pointer = make_pointer(staged);
    pointer.staged_digest = "sha256:wrong";

    JsLivePackagePointerResult result = store.load_live_pointer(pointer);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, RejectsLivePointerChecksumMismatch) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);
    JsLivePackagePointer pointer = make_pointer(staged);
    pointer.current_live_checksum = "live:wrong";

    JsLivePackagePointerResult result = store.load_live_pointer(pointer);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, RejectsInvalidLivePointerShape) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);
    JsLivePackagePointer pointer = make_pointer(staged);
    pointer.current_live_checksum.clear();

    JsLivePackagePointerResult result = store.load_live_pointer(pointer);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, RejectsInvalidLivePointerSlotAndWhitespaceFields) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);

    JsLivePackagePointer bad_slot = make_pointer(staged);
    bad_slot.vnum = 0;
    EXPECT_TRUE(has_code(store.load_live_pointer(bad_slot),
                         JsLivePackageStoreDiagnosticCode::InvalidRequest));

    JsLivePackagePointer blank_id = make_pointer(staged);
    blank_id.package_id = " \t";
    EXPECT_TRUE(has_code(store.load_live_pointer(blank_id),
                         JsLivePackageStoreDiagnosticCode::InvalidRequest));

    JsLivePackagePointer missing_timestamp = make_pointer(staged);
    missing_timestamp.loaded_at_epoch_seconds = 0;
    EXPECT_TRUE(has_code(store.load_live_pointer(missing_timestamp),
                         JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, RejectsUnsafeLivePointerMetadataText) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);
    JsLivePackagePointer pointer = make_pointer(staged);
    pointer.load_audit_id = "audit\nload";

    JsLivePackagePointerResult result = store.load_live_pointer(pointer);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
}

TEST(JsLivePackageStore, EnforcesLivePointerLimit) {
    JsLivePackageStoreOptions options;
    options.maximum_live_pointers = 1;
    JsLivePackageStore store(options);
    JsStagedPackageRecord first = stage_record(3001);
    JsStagedPackageRecord second = stage_record(3002);
    ASSERT_TRUE(store.store_staged_record(first).ok);
    ASSERT_TRUE(store.store_staged_record(second).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(first)).ok);

    JsLivePackagePointerResult result = store.load_live_pointer(make_pointer(second));

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded));
    EXPECT_EQ(1u, store.live_pointer_count());
}

TEST(JsLivePackageStore, EmptyStoreBuildsEmptyRegistrySnapshot) {
    JsLivePackageStore store;

    JsLivePackageRegistrySnapshotResult result =
        store.build_live_registry_snapshot(internal_registry_options());

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.registry.empty());
    EXPECT_EQ(0u, result.registry.package_count());
}

TEST(JsLivePackageStore, BuildsRegistrySnapshotFromLivePointersOnly) {
    JsLivePackageStore store;
    JsStagedPackageRecord live = stage_record(3001);
    JsStagedPackageRecord stored_only = stage_record(3002);
    ASSERT_TRUE(store.store_staged_record(live).ok);
    ASSERT_TRUE(store.store_staged_record(stored_only).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(live)).ok);

    JsLivePackageRegistrySnapshotResult result =
        store.build_live_registry_snapshot(internal_registry_options());

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(1u, result.registry.package_count());
    EXPECT_NE(nullptr, result.registry.find_package_by_vnum(3001));
    EXPECT_EQ(nullptr, result.registry.find_package_by_vnum(3002));
}

TEST(JsLivePackageStore, RegistrySnapshotReportsValidationFailureWithoutMutatingStore) {
    JsLivePackageStore store;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(store.store_staged_record(staged).ok);
    ASSERT_TRUE(store.load_live_pointer(make_pointer(staged)).ok);
    JsScriptRegistryReplaceOptions options = internal_registry_options();
    options.legacy_script_vnums = {3001};

    JsLivePackageRegistrySnapshotResult result = store.build_live_registry_snapshot(options);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.package_validation.ok);
    EXPECT_EQ(1u, store.package_record_count());
    EXPECT_EQ(1u, store.live_pointer_count());
}

TEST(JsLivePackageStore, ExportsAndHydratesSnapshotAtomically) {
    JsLivePackageStore source;
    JsStagedPackageRecord staged = stage_record();
    ASSERT_TRUE(source.store_staged_record(staged).ok);
    ASSERT_TRUE(source.load_live_pointer(make_pointer(staged)).ok);

    JsLivePackageStore target;
    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(source.export_snapshot());

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(1u, result.records_loaded);
    EXPECT_EQ(1u, result.live_pointers_loaded);
    EXPECT_EQ(1u, target.package_record_count());
    EXPECT_EQ(1u, target.live_pointer_count());
    EXPECT_TRUE(
        target.find_record(staged.identity.package_id, staged.identity.package_version_id).ok);
    EXPECT_TRUE(
        target.find_live_pointer(staged.identity.zone, staged.identity.host, staged.identity.vnum)
            .ok);
}

TEST(JsLivePackageStore, HydrationFailureLeavesExistingStateUnchanged) {
    JsLivePackageStore source;
    JsStagedPackageRecord existing = stage_record(3011);
    ASSERT_TRUE(source.store_staged_record(existing).ok);
    ASSERT_TRUE(source.load_live_pointer(make_pointer(existing)).ok);

    JsLivePackageStore target;
    ASSERT_TRUE(target.hydrate_from_snapshot(source.export_snapshot()).ok);

    JsLivePackageStoreSnapshot invalid = source.export_snapshot();
    invalid.records.front().identity.package_version_id = "version:forged";
    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(invalid);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
    EXPECT_EQ(1u, target.package_record_count());
    EXPECT_EQ(1u, target.live_pointer_count());
    EXPECT_TRUE(
        target.find_record(existing.identity.package_id, existing.identity.package_version_id).ok);
}

TEST(JsLivePackageStore, HydrationPointerFailureLeavesExistingStateUnchanged) {
    JsLivePackageStore existing_source;
    JsStagedPackageRecord existing = stage_record(3031);
    ASSERT_TRUE(existing_source.store_staged_record(existing).ok);
    ASSERT_TRUE(existing_source.load_live_pointer(make_pointer(existing)).ok);

    JsLivePackageStore target;
    ASSERT_TRUE(target.hydrate_from_snapshot(existing_source.export_snapshot()).ok);

    JsLivePackageStore new_source;
    JsStagedPackageRecord newer = stage_record(3032);
    ASSERT_TRUE(new_source.store_staged_record(newer).ok);
    ASSERT_TRUE(new_source.load_live_pointer(make_pointer(newer)).ok);
    JsLivePackageStoreSnapshot invalid = new_source.export_snapshot();
    invalid.live_pointers.front().staged_digest = "sha256:forged";

    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(invalid);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::InvalidRequest));
    EXPECT_EQ(1u, target.package_record_count());
    EXPECT_EQ(1u, target.live_pointer_count());
    EXPECT_TRUE(
        target.find_record(existing.identity.package_id, existing.identity.package_version_id).ok);
    EXPECT_TRUE(target.find_live_pointer(existing.identity.zone, existing.identity.host,
                    existing.identity.vnum)
                    .ok);
    EXPECT_FALSE(target.find_live_pointer(newer.identity.zone, newer.identity.host,
                       newer.identity.vnum)
                     .ok);
}

TEST(JsLivePackageStore, HydrationReplacesExistingStateInsteadOfMerging) {
    JsLivePackageStore first_source;
    JsStagedPackageRecord first = stage_record(3041);
    ASSERT_TRUE(first_source.store_staged_record(first).ok);
    ASSERT_TRUE(first_source.load_live_pointer(make_pointer(first)).ok);

    JsLivePackageStore second_source;
    JsStagedPackageRecord second = stage_record(3042);
    ASSERT_TRUE(second_source.store_staged_record(second).ok);
    ASSERT_TRUE(second_source.load_live_pointer(make_pointer(second)).ok);

    JsLivePackageStore target;
    ASSERT_TRUE(target.hydrate_from_snapshot(first_source.export_snapshot()).ok);
    JsLivePackageStoreHydrationResult result =
        target.hydrate_from_snapshot(second_source.export_snapshot());

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(1u, target.package_record_count());
    EXPECT_EQ(1u, target.live_pointer_count());
    EXPECT_FALSE(target.find_live_pointer(first.identity.zone, first.identity.host,
                       first.identity.vnum)
                     .ok);
    EXPECT_TRUE(target.find_live_pointer(second.identity.zone, second.identity.host,
                    second.identity.vnum)
                    .ok);
}

TEST(JsLivePackageStore, HydrationEnforcesConfiguredLimitsBeforeMutating) {
    JsLivePackageStore source;
    ASSERT_TRUE(source.store_staged_record(stage_record(3021)).ok);
    ASSERT_TRUE(source.store_staged_record(stage_record(3022)).ok);

    JsLivePackageStoreOptions options;
    options.maximum_package_records = 1;
    JsLivePackageStore target(options);

    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(source.export_snapshot());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded));
    EXPECT_TRUE(target.empty());
}

TEST(JsLivePackageStore, HydrationEnforcesLivePointerLimitBeforeMutating) {
    JsLivePackageStore source;
    JsStagedPackageRecord first = stage_record(3051);
    JsStagedPackageRecord second = stage_record(3052);
    ASSERT_TRUE(source.store_staged_record(first).ok);
    ASSERT_TRUE(source.store_staged_record(second).ok);
    ASSERT_TRUE(source.load_live_pointer(make_pointer(first)).ok);
    ASSERT_TRUE(source.load_live_pointer(make_pointer(second)).ok);

    JsLivePackageStoreOptions options;
    options.maximum_live_pointers = 1;
    JsLivePackageStore target(options);

    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(source.export_snapshot());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded));
    EXPECT_TRUE(target.empty());
}

TEST(JsLivePackageStore, HydrationCollapsesIdenticalRecordDuplicates) {
    JsLivePackageStore source;
    JsStagedPackageRecord staged = stage_record(3061);
    ASSERT_TRUE(source.store_staged_record(staged).ok);
    JsLivePackageStoreSnapshot snapshot = source.export_snapshot();
    snapshot.records.push_back(snapshot.records.front());

    JsLivePackageStore target;
    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(snapshot);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(1u, result.records_loaded);
    EXPECT_EQ(1u, target.package_record_count());
}

TEST(JsLivePackageStore, HydrationRejectsConflictingRecordDuplicates) {
    JsLivePackageStore source;
    JsStagedPackageRecord staged = stage_record(3062);
    ASSERT_TRUE(source.store_staged_record(staged).ok);
    JsLivePackageStoreSnapshot snapshot = source.export_snapshot();
    snapshot.records.push_back(snapshot.records.front());
    snapshot.records.back().staged_audit.audit_id = "audit:changed";

    JsLivePackageStore target;
    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(snapshot);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(
        has_code(result, JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict));
    EXPECT_TRUE(target.empty());
}

TEST(JsLivePackageStore, HydrationRejectsDuplicateLivePointerSlots) {
    JsLivePackageStore source;
    JsStagedPackageRecord staged = stage_record(3063);
    ASSERT_TRUE(source.store_staged_record(staged).ok);
    ASSERT_TRUE(source.load_live_pointer(make_pointer(staged)).ok);
    JsLivePackageStoreSnapshot snapshot = source.export_snapshot();
    snapshot.live_pointers.push_back(snapshot.live_pointers.front());

    JsLivePackageStore target;
    JsLivePackageStoreHydrationResult result = target.hydrate_from_snapshot(snapshot);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsLivePackageStoreDiagnosticCode::LivePointerConflict));
    EXPECT_TRUE(target.empty());
}

TEST(JsLivePackageStore, PublicDiagnosticNamesAreStable) {
    EXPECT_STREQ("invalid-request", js_live_package_store_diagnostic_code_name(
                                        JsLivePackageStoreDiagnosticCode::InvalidRequest));
    EXPECT_STREQ("duplicate-package-record-conflict",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict));
    EXPECT_STREQ("package-record-limit-exceeded",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded));
    EXPECT_STREQ("live-pointer-limit-exceeded",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded));
    EXPECT_STREQ("package-record-not-found",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::PackageRecordNotFound));
    EXPECT_STREQ("live-pointer-not-found",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::LivePointerNotFound));
    EXPECT_STREQ("live-pointer-conflict",
                 js_live_package_store_diagnostic_code_name(
                     JsLivePackageStoreDiagnosticCode::LivePointerConflict));
}

TEST(JsLivePackageStore, BuildFilesIncludeLivePackageStoreSourcesAndTests) {
    const std::string cmake_text =
        read_first_available_file({"src/CMakeLists.txt", "../src/CMakeLists.txt"});
    const std::string src_make_text =
        read_first_available_file({"src/Makefile", "../src/Makefile"});
    const std::string test_make_text =
        read_first_available_file({"src/tests/Makefile", "../src/tests/Makefile"});
    ASSERT_FALSE(cmake_text.empty());
    ASSERT_FALSE(src_make_text.empty());
    ASSERT_FALSE(test_make_text.empty());

    EXPECT_NE(std::string::npos, cmake_text.find("js_live_package_store.cpp"));
    EXPECT_NE(std::string::npos, cmake_text.find("tests/js_live_package_store_tests.cpp"));
    EXPECT_NE(std::string::npos, src_make_text.find("js_live_package_store.o"));
    EXPECT_NE(std::string::npos, test_make_text.find("js_live_package_store.o"));
    EXPECT_NE(std::string::npos, test_make_text.find("js_live_package_store_tests.cpp"));
}
