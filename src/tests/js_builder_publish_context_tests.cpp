#include "../js_builder_publish_context.h"

#include "../json_utils.h"
#include "../script.h"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string quote(const std::string &value) {
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

JsScriptPackage make_package(int vnum = 3001,
                             JsScriptPackageHost host = JsScriptPackageHost::Character,
                             const std::string &body = "return true") {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "client-pkg-" + std::to_string(vnum);
    package.host = host;
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

std::string package_json(const JsScriptPackage &package) {
    return "{"
           "\"vnum\":" +
           std::to_string(package.vnum) + ",\"packageId\":" + quote(package.package_id) +
           ",\"host\":" + quote(js_script_package_host_name(package.host)) +
           ",\"packageFormatVersion\":" + std::to_string(package.package_format_version) +
           ",\"manifestSchemaVersion\":" + std::to_string(package.manifest_schema_version) +
           ",\"triggerCatalogRevision\":" + std::to_string(package.trigger_catalog_revision) +
           ",\"manifestChecksum\":" + quote(package.manifest_checksum) +
           ",\"runtimeName\":" + quote(package.runtime_name) +
           ",\"runtimeVersion\":" + quote(package.runtime_version) +
           ",\"generatedTypingsVersion\":" + quote(package.generated_typings_version) +
           ",\"compiledJavaScriptChecksum\":" + quote(package.compiled_javascript_checksum) +
           ",\"compiledJavaScript\":" + quote(package.compiled_javascript) +
           ",\"triggerBindings\":[{\"kind\":\"legacy-script-trigger\",\"legacyValue\":11,"
           "\"handlerName\":\"onEnter\"}]}";
}

std::string stage_json(const JsScriptPackage &package) {
    return "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:old\",\"package\":" +
           package_json(package) + "}";
}

std::string package_id_json(const std::string &operation, const std::string &id,
                            const std::string &extra = "") {
    std::string json = "{\"operation\":" + quote(operation) + ",\"packageId\":" + quote(id);
    if (!extra.empty())
        json += "," + extra;
    json += "}";
    return json;
}

std::string package_id(int zone, JsScriptPackageHost host, int vnum) {
    return js_staged_package_logical_package_id(zone, host, vnum);
}

JsBuilderPublishTargetCatalog make_catalog() {
    JsBuilderPublishTargetCatalog catalog;
    catalog.mobile_vnums = {3001, 4101};
    catalog.object_vnums = {3002};
    catalog.room_vnums = {3003};
    catalog.zones.push_back({30, 3999, {42, 77}});
    catalog.zones.push_back({41, 4999, {0}});
    return catalog;
}

JsBuilderPublishContextOptions make_options(const JsBuilderPublishTargetCatalog &catalog) {
    static JsLivePackageStore empty_live_store;
    JsBuilderPublishContextOptions options;
    options.target_catalog = &catalog;
    options.live_store = &empty_live_store;
    return options;
}

JsPublishEndpointTransportContext make_base_context() {
    JsPublishEndpointTransportContext context;
    context.request_id = "request:base";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    return context;
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
    options.audit.transport_source_identifier = "transport:builder";
    return options;
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

} // namespace

TEST(JsBuilderPublishContext, ResolvesStageTargetFromPackage) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();

    JsBuilderPublishContextResult result = js_builder_publish_context_resolve(
        stage_json(make_package()), make_base_context(), make_options(catalog));

    ASSERT_TRUE(result.ok);
    EXPECT_EQ("builder.context.accepted", result.reason_code);
    EXPECT_EQ(30, result.context.zone);
    EXPECT_TRUE(result.context.target_zone_resolved);
    EXPECT_EQ(30, result.context.server_resolved_target_zone);
    EXPECT_EQ(JsScriptPackageHost::Character, result.context.server_resolved_target_host);
    EXPECT_TRUE(result.context.zone_exists);
    EXPECT_FALSE(result.context.zone_allows_all_builders);
    ASSERT_EQ(2u, result.context.zone_owner_character_ids.size());
    EXPECT_EQ(42, result.context.zone_owner_character_ids[0]);
    EXPECT_TRUE(result.context.current_live_checksum.empty());
    EXPECT_EQ("request:base", result.context.request_id);
}

TEST(JsBuilderPublishContext, ResolvesStatusTargetFromCanonicalPackageId) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    const std::string body = "{\"operation\":\"status\",\"packageId\":" +
                             quote(package_id(30, JsScriptPackageHost::Object, 3002)) + "}";

    JsBuilderPublishContextResult result =
        js_builder_publish_context_resolve(body, make_base_context(), make_options(catalog));

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(30, result.context.zone);
    EXPECT_EQ(JsScriptPackageHost::Object, result.context.server_resolved_target_host);
    EXPECT_EQ(std::vector<int>({42, 77}), result.context.zone_owner_character_ids);
}

TEST(JsBuilderPublishContext, ResolvesActivateAndRollbackTargetsFromCanonicalPackageId) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    const std::string id = package_id(30, JsScriptPackageHost::Character, 3001);

    JsBuilderPublishContextResult activate =
        js_builder_publish_context_resolve(package_id_json("activate", id,
                                                           "\"stagedDigest\":\"sha256:abc\","
                                                           "\"baseLiveChecksum\":\"live:old\""),
                                           make_base_context(), make_options(catalog));
    JsBuilderPublishContextResult rollback =
        js_builder_publish_context_resolve(package_id_json("rollback", id,
                                                           "\"targetLiveChecksum\":\"live:old\","
                                                           "\"reason\":\"builder rollback\""),
                                           make_base_context(), make_options(catalog));

    ASSERT_TRUE(activate.ok);
    EXPECT_EQ(30, activate.context.zone);
    EXPECT_EQ(JsScriptPackageHost::Character, activate.context.server_resolved_target_host);
    ASSERT_TRUE(rollback.ok);
    EXPECT_EQ(30, rollback.context.zone);
    EXPECT_EQ(JsScriptPackageHost::Character, rollback.context.server_resolved_target_host);
}

TEST(JsBuilderPublishContext, MarksAllBuildersSentinel) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();

    JsBuilderPublishContextResult result = js_builder_publish_context_resolve(
        stage_json(make_package(4101)), make_base_context(), make_options(catalog));

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(41, result.context.zone);
    EXPECT_TRUE(result.context.zone_allows_all_builders);
    ASSERT_EQ(1u, result.context.zone_owner_character_ids.size());
    EXPECT_EQ(0, result.context.zone_owner_character_ids[0]);
}

TEST(JsBuilderPublishContext, LoadsCurrentLiveChecksumWhenPointerExists) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(), make_stage_options());
    ASSERT_TRUE(staged.ok);
    JsLivePackageStore live_store;
    ASSERT_TRUE(
        live_store.activate_staged_record_pointer(staged.record, make_pointer(staged.record)).ok);

    JsBuilderPublishContextOptions options = make_options(catalog);
    options.live_store = &live_store;
    JsBuilderPublishContextResult result = js_builder_publish_context_resolve(
        stage_json(make_package()), make_base_context(), options);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(live_checksum_for_record(staged.record), result.context.current_live_checksum);
}

TEST(JsBuilderPublishContext, ClearsBaseChecksumWhenLiveStoreMisses) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsPublishEndpointTransportContext base = make_base_context();
    base.current_live_checksum = "live:stale";
    JsLivePackageStore live_store;
    JsBuilderPublishContextOptions options = make_options(catalog);
    options.live_store = &live_store;

    JsBuilderPublishContextResult result =
        js_builder_publish_context_resolve(stage_json(make_package()), base, options);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.context.current_live_checksum.empty());
}

TEST(JsBuilderPublishContext, OverwritesHostileBaseTargetContext) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsPublishEndpointTransportContext base = make_base_context();
    base.zone = 99;
    base.target_zone_resolved = true;
    base.server_resolved_target_zone = 99;
    base.server_resolved_target_host = JsScriptPackageHost::Room;
    base.zone_exists = true;
    base.zone_allows_all_builders = true;
    base.zone_owner_character_ids = {0, 999};
    base.current_live_checksum = "live:client";

    JsBuilderPublishContextResult result =
        js_builder_publish_context_resolve(stage_json(make_package()), base, make_options(catalog));

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(30, result.context.zone);
    EXPECT_TRUE(result.context.target_zone_resolved);
    EXPECT_EQ(30, result.context.server_resolved_target_zone);
    EXPECT_EQ(JsScriptPackageHost::Character, result.context.server_resolved_target_host);
    EXPECT_TRUE(result.context.zone_exists);
    EXPECT_FALSE(result.context.zone_allows_all_builders);
    EXPECT_EQ(std::vector<int>({42, 77}), result.context.zone_owner_character_ids);
    EXPECT_TRUE(result.context.current_live_checksum.empty());
}

TEST(JsBuilderPublishContext, ResolvesObjectRoomAndMudlleMobileHosts) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextResult object_stage = js_builder_publish_context_resolve(
        stage_json(make_package(3002, JsScriptPackageHost::Object)), make_base_context(),
        make_options(catalog));
    JsBuilderPublishContextResult room_status = js_builder_publish_context_resolve(
        package_id_json("status", package_id(30, JsScriptPackageHost::Room, 3003)),
        make_base_context(), make_options(catalog));
    JsBuilderPublishContextResult mudlle_status = js_builder_publish_context_resolve(
        package_id_json("status", package_id(41, JsScriptPackageHost::MudlleMobile, 4101)),
        make_base_context(), make_options(catalog));

    ASSERT_TRUE(object_stage.ok);
    EXPECT_EQ(JsScriptPackageHost::Object, object_stage.context.server_resolved_target_host);
    ASSERT_TRUE(room_status.ok);
    EXPECT_EQ(JsScriptPackageHost::Room, room_status.context.server_resolved_target_host);
    ASSERT_TRUE(mudlle_status.ok);
    EXPECT_EQ(JsScriptPackageHost::MudlleMobile, mudlle_status.context.server_resolved_target_host);
    EXPECT_TRUE(mudlle_status.context.zone_allows_all_builders);
}

TEST(JsBuilderPublishContext, RejectsMalformedOrUnsupportedRequests) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions options = make_options(catalog);

    EXPECT_EQ("builder.context.invalid-request",
              js_builder_publish_context_resolve("{\"operation\":", make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.invalid-target",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"garbageCollect\",\"packageId\":\"js:30:character:3001\"}",
                  make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.invalid-target",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"stage\",\"packageId\":\"js:30:character:3001\",\"package\":" +
                      package_json(make_package()) + "}",
                  make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.invalid-request",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"status\",\"packageId\":\"js:30:character:3001\","
                  "\"serverResolvedTargetZone\":30}",
                  make_base_context(), options)
                  .reason_code);
}

TEST(JsBuilderPublishContext, RejectsUnknownTargetAndZoneMismatch) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions options = make_options(catalog);

    EXPECT_EQ("builder.context.target-not-found",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"status\",\"packageId\":\"js:30:character:3998\"}",
                  make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.zone-mismatch",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"status\",\"packageId\":\"js:41:character:3001\"}",
                  make_base_context(), options)
                  .reason_code);
}

TEST(JsBuilderPublishContext, RejectsDuplicateFields) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions options = make_options(catalog);

    EXPECT_EQ(
        "builder.context.invalid-request",
        js_builder_publish_context_resolve("{\"operation\":\"status\",\"operation\":\"activate\","
                                           "\"packageId\":\"js:30:character:3001\"}",
                                           make_base_context(), options)
            .reason_code);
    EXPECT_EQ("builder.context.invalid-request",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"status\",\"packageId\":\"js:30:character:3001\","
                  "\"packageId\":\"js:30:object:3002\"}",
                  make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.invalid-request",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"stage\",\"package\":" + package_json(make_package()) +
                      ",\"package\":" + package_json(make_package()) + "}",
                  make_base_context(), options)
                  .reason_code);
}

TEST(JsBuilderPublishContext, RejectsPackageObjectSmugglingForActivateAndRollback) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions options = make_options(catalog);

    EXPECT_EQ("builder.context.invalid-target",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"activate\",\"packageId\":\"js:30:character:3001\","
                  "\"package\":" +
                      package_json(make_package()) + "}",
                  make_base_context(), options)
                  .reason_code);
    EXPECT_EQ("builder.context.invalid-target",
              js_builder_publish_context_resolve(
                  "{\"operation\":\"rollback\",\"packageId\":\"js:30:character:3001\","
                  "\"package\":" +
                      package_json(make_package()) + "}",
                  make_base_context(), options)
                  .reason_code);
}

TEST(JsBuilderPublishContext, RejectsNonCanonicalPackageIds) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions options = make_options(catalog);
    const char *ids[] = {
        "js:030:character:3001", "js:30:Character:3001", "js:-30:character:3001",
        "js:0:character:3001",   "js:30:character:0",    "js:30:character:3001:extra",
        "pkg:30:character:3001",
    };

    for (const char *id : ids) {
        EXPECT_EQ("builder.context.invalid-target",
                  js_builder_publish_context_resolve(package_id_json("status", id),
                                                     make_base_context(), options)
                      .reason_code)
            << id;
    }
}

TEST(JsBuilderPublishContext, ResolvesZoneBoundaries) {
    JsBuilderPublishTargetCatalog catalog;
    catalog.mobile_vnums = {3999, 4000};
    catalog.zones.push_back({30, 3999, {42}});
    catalog.zones.push_back({40, 4999, {84}});

    JsBuilderPublishContextResult zone_top = js_builder_publish_context_resolve(
        stage_json(make_package(3999)), make_base_context(), make_options(catalog));
    JsBuilderPublishContextResult next_zone_first = js_builder_publish_context_resolve(
        stage_json(make_package(4000)), make_base_context(), make_options(catalog));

    ASSERT_TRUE(zone_top.ok);
    EXPECT_EQ(30, zone_top.context.zone);
    ASSERT_TRUE(next_zone_first.ok);
    EXPECT_EQ(40, next_zone_first.context.zone);
}

TEST(JsBuilderPublishContext, RejectsAmbiguousOrUnorderedZones) {
    JsBuilderPublishTargetCatalog catalog = make_catalog();
    catalog.zones[1].top = 3500;

    JsBuilderPublishContextResult result = js_builder_publish_context_resolve(
        stage_json(make_package(4101)), make_base_context(), make_options(catalog));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ("builder.context.catalog-invalid", result.reason_code);
}

TEST(JsBuilderPublishContext, RejectsMissingCatalogAndOversizedBody) {
    JsBuilderPublishContextOptions missing_catalog;
    static JsLivePackageStore empty_live_store;
    missing_catalog.live_store = &empty_live_store;
    EXPECT_EQ("builder.context.catalog-unavailable",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 missing_catalog)
                  .reason_code);

    JsBuilderPublishTargetCatalog catalog = make_catalog();
    JsBuilderPublishContextOptions missing_live_store = make_options(catalog);
    missing_live_store.live_store = nullptr;
    EXPECT_EQ("builder.context.live-store-unavailable",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 missing_live_store)
                  .reason_code);

    JsBuilderPublishContextOptions options = make_options(catalog);
    options.maximum_request_bytes = 8;
    EXPECT_EQ(
        "builder.context.invalid-request",
        js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(), options)
            .reason_code);
}

TEST(JsBuilderPublishContext, RejectsInvalidCatalogState) {
    JsBuilderPublishTargetCatalog duplicate_mobile = make_catalog();
    duplicate_mobile.mobile_vnums.push_back(3001);
    EXPECT_EQ("builder.context.catalog-invalid",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 make_options(duplicate_mobile))
                  .reason_code);

    JsBuilderPublishTargetCatalog mixed_all_builders = make_catalog();
    mixed_all_builders.zones[0].owner_character_ids = {0, 42};
    EXPECT_EQ("builder.context.catalog-invalid",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 make_options(mixed_all_builders))
                  .reason_code);

    JsBuilderPublishTargetCatalog negative_owner = make_catalog();
    negative_owner.zones[0].owner_character_ids = {-1};
    EXPECT_EQ("builder.context.catalog-invalid",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 make_options(negative_owner))
                  .reason_code);

    JsBuilderPublishTargetCatalog duplicate_zone_number = make_catalog();
    duplicate_zone_number.zones[1].number = 30;
    EXPECT_EQ("builder.context.catalog-invalid",
              js_builder_publish_context_resolve(stage_json(make_package()), make_base_context(),
                                                 make_options(duplicate_zone_number))
                  .reason_code);
}
