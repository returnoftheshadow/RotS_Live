#include "../js_builder_http_server_transport.h"

#include "../db.h"
#include "../js_live_registry_admin.h"
#include "../js_script_package_loader.h"
#include "../js_publish_staging.h"
#include "../json_utils.h"
#include "../script.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

extern room_data world;
extern int top_of_world;
extern index_data *mob_index;
extern int top_of_mobt;
extern index_data *obj_index;
extern int top_of_objt;
extern char_data *character_list;
extern obj_data *object_list;

int trigger_char_enter(char_data *ch, char_data *vict, room_data *room);

namespace {

const char *kProxySecret = "server-transport-secret";

std::string quote(const std::string &value) {
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

JsScriptPackage make_package_with_source(const std::string &source, int vnum = 3001) {
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "client-pkg-3001";
    package.host = JsScriptPackageHost::Character;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = source;
    package.trigger_bindings.push_back(
        {JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter"});
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsScriptPackage make_package() {
    return make_package_with_source("function onEnter(ctx) { return true; }");
}

JsScriptPackage make_runtime_blocking_package(int vnum = 3001) {
    return make_package_with_source(
        std::string("function onEnter(ctx) { ") +
            "return !(ctx.self.name === 'Http Guard' && ctx.actor.name === 'Http Actor' && "
            "ctx.room.vnum === " +
            std::to_string(vnum) + "); }",
        vnum);
}

std::string package_json(const JsScriptPackage &package) {
    return "{\"packageFormatVersion\":" + std::to_string(package.package_format_version) +
           ",\"packageId\":" + quote(package.package_id) +
           ",\"host\":" + quote(js_script_package_host_name(package.host)) +
           ",\"vnum\":" + std::to_string(package.vnum) +
           ",\"manifestSchemaVersion\":" + std::to_string(package.manifest_schema_version) +
           ",\"triggerCatalogRevision\":" + std::to_string(package.trigger_catalog_revision) +
           ",\"manifestChecksum\":" + quote(package.manifest_checksum) +
           ",\"runtimeName\":" + quote(package.runtime_name) +
           ",\"runtimeVersion\":" + quote(package.runtime_version) +
           ",\"generatedTypingsVersion\":" + quote(package.generated_typings_version) +
           ",\"compiledJavaScript\":" + quote(package.compiled_javascript) +
           ",\"compiledJavaScriptChecksum\":" + quote(package.compiled_javascript_checksum) +
           ",\"triggerBindings\":[{\"kind\":\"legacy-script-trigger\",\"legacyValue\":11,"
           "\"handlerName\":\"onEnter\"}]}";
}

std::string stage_body() {
    return "{\"baseLiveChecksum\":\"live:initial\",\"package\":" + package_json(make_package()) +
           "}";
}

std::string stage_body(const JsScriptPackage &package) {
    return "{\"baseLiveChecksum\":\"live:initial\",\"package\":" + package_json(package) + "}";
}

std::string stage_body_with_operation() {
    return "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:initial\",\"package\":" +
           package_json(make_package()) + "}";
}

std::string status_body() { return "{\"packageId\":\"js:30:character:3001\"}"; }

std::string activate_body(const std::string &package_id, const std::string &staged_digest,
                          const std::string &base_live_checksum) {
    return "{\"packageId\":" + quote(package_id) + ",\"stagedDigest\":" + quote(staged_digest) +
           ",\"baseLiveChecksum\":" + quote(base_live_checksum) + "}";
}

std::string raw_request(const std::string &method, const std::string &path,
                        const std::string &body = "", bool trusted = true,
                        const std::string &content_type = "application/json",
                        const std::string &bearer_token = "") {
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "host: 127.0.0.1:4802\r\n";
    if (trusted) {
        request += "x-rots-builder-proxy-secret: ";
        request += kProxySecret;
        request += "\r\n";
    }
    if (!content_type.empty()) {
        request += "content-type: ";
        request += content_type;
        request += "\r\n";
    }
    if (!bearer_token.empty()) {
        request += "authorization: Bearer ";
        request += bearer_token;
        request += "\r\n";
    }
    request += "content-length: ";
    request += std::to_string(body.size());
    request += "\r\n\r\n";
    request += body;
    return request;
}

std::string
raw_request_with_headers(const std::string &method, const std::string &path,
                         const std::vector<std::pair<std::string, std::string>> &headers,
                         const std::string &body = "") {
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "host: 127.0.0.1:4802\r\n";
    for (const auto &header : headers) {
        request += header.first;
        request += ": ";
        request += header.second;
        request += "\r\n";
    }
    request += "content-length: ";
    request += std::to_string(body.size());
    request += "\r\n\r\n";
    request += body;
    return request;
}

std::size_t rendered_content_length(const std::string &response) {
    const std::string needle = "\r\ncontent-length: ";
    const std::size_t start = response.find(needle);
    if (start == std::string::npos)
        return std::string::npos;
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = response.find("\r\n", value_start);
    if (value_end == std::string::npos)
        return std::string::npos;
    return static_cast<std::size_t>(
        std::strtoul(response.substr(value_start, value_end - value_start).c_str(), nullptr, 10));
}

void expect_response_contains(const JsBuilderHttpServerTransportResult &result,
                              const std::string &needle) {
    EXPECT_NE(std::string::npos, result.http_response.find(needle)) << needle;
}

void expect_content_length_matches_body(const std::string &response) {
    const std::size_t body_start = response.find("\r\n\r\n");
    ASSERT_NE(std::string::npos, body_start);
    const std::string body = response.substr(body_start + 4);
    EXPECT_EQ(body.size(), rendered_content_length(response));
}

std::string read_first_available_file(const std::vector<std::string> &paths) {
    for (const std::string &path : paths) {
        std::ifstream file(path);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
    return "";
}

JsBuilderPublishTargetCatalog make_catalog() {
    JsBuilderPublishTargetCatalog catalog;
    catalog.mobile_vnums = {3001};
    catalog.zones.push_back({30, 3999, {1001}});
    return catalog;
}

JsBuilderSessionStoreOptions session_store_options() {
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = 100;
    options.server_audience = "server:main";
    options.workspace_id = "workspace:main";
    return options;
}

JsBuilderHttpServerTransportOptions
make_options(JsLivePackageStore *live_store = nullptr,
             JsPublishEndpointService *publish_service = nullptr,
             JsBuilderSessionStore *session_store = nullptr) {
    JsBuilderHttpServerTransportOptions options;
    options.ingress_options.expected_proxy_secret = kProxySecret;
    options.ingress_options.publish_service = publish_service;
    options.ingress_options.publish_context.request_id = "request:server-transport";
    options.ingress_options.publish_context.audit_id = "audit:server-transport";
    options.ingress_options.publish_context.transport.secure_channel = true;
    options.ingress_options.publish_context.transport.server_identity_verified = true;
    options.ingress_options.publish_context.transport.server_audience = "server:main";
    options.ingress_options.publish_context.transport.source_identifier = "transport:local-proxy";
    options.ingress_options.publish_context.now_epoch_seconds = 100;
    options.ingress_options.publish_context.expected_server_audience = "server:main";
    options.ingress_options.publish_context.expected_workspace_id = "workspace:main";
    options.ingress_options.publish_context.expected_server_instance_id = "server:main";
    options.ingress_options.publish_context.allow_mutating_operations = true;
    options.ingress_options.publish_context.allow_live_pointer_update = true;
    options.ingress_options.publish_context.applied_at_epoch_seconds = 200;
    options.ingress_options.publish_context.publish_audit_log_path =
        "build/js_builder_http_server_transport_audit.jsonl";
    options.ingress_options.publish_options.session_store = session_store;
    options.ingress_options.publish_options.session_store_options = session_store_options();
    static JsBuilderPublishTargetCatalog catalog = make_catalog();
    options.publish_context_options.target_catalog = &catalog;
    options.publish_context_options.live_store = live_store;
    return options;
}

JsPublishEndpointServiceOptions service_options() {
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

JsBuilderSessionLoginResult make_login(unsigned int scopes) {
    JsBuilderSessionLoginResult login;
    login.ok = true;
    login.session_token = "session-token";
    login.account_id = "builder-account";
    login.expires_at_epoch_seconds = 200;
    login.token_metadata.claims_verified = true;
    login.token_metadata.token_id = js_builder_session_token_id(login.session_token);
    login.token_metadata.actor_id = "Builderone";
    login.token_metadata.builder_account_id = "builder-account";
    login.token_metadata.scopes = scopes;
    login.token_metadata.issued_at_epoch_seconds = 90;
    login.token_metadata.expires_at_epoch_seconds = 200;
    login.builder_eligibility.ok = true;
    login.builder_eligibility.builder_account_id = "builder-account";
    login.builder_eligibility.eligible_character_id = 1001;
    login.builder_eligibility.eligible_character_name = "Builderone";
    login.builder_eligibility.eligible_character_level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    return login;
}

void insert_session(JsBuilderSessionStore *store, unsigned int scopes) {
    ASSERT_TRUE(store->insert_login_result(make_login(scopes), session_store_options()).ok);
}

char_data make_character(const char *name, bool npc = false) {
    char_data character = {};
    character.player.name = const_cast<char *>(name);
    character.player.short_descr = const_cast<char *>(name);
    character.player.race = 0;
    character.player.level = 1;
    character.tmpabilities.hit = 10;
    character.abilities.hit = 10;
    character.in_room = 0;
    character.nr = npc ? 0 : -1;
    if (npc)
        character.specials2.act = MOB_ISNPC;
    return character;
}

room_data make_room(const char *name, int number, int zone) {
    room_data room = {};
    room.name = const_cast<char *>(name);
    room.number = number;
    room.zone = zone;
    return room;
}

struct RuntimeGlobalStateGuard {
    room_data saved_world = world;
    int saved_top_of_world = top_of_world;
    index_data *saved_mob_index = mob_index;
    int saved_top_of_mobt = top_of_mobt;
    char_data *saved_character_list = character_list;
    obj_data *saved_object_list = object_list;
    JsLiveRegistryAdminService &service = js_live_registry_admin_service();

    RuntimeGlobalStateGuard() {
        EXPECT_TRUE(service.live_store().hydrate_from_snapshot({}).ok);
        EXPECT_TRUE(service.refresh().ok);
        js_script_invalidate_live_registry_generation();
        js_script_set_legacy_trigger_dispatch_enabled(false);
    }

    ~RuntimeGlobalStateGuard() {
        service.live_store().hydrate_from_snapshot({});
        service.refresh();
        js_script_invalidate_live_registry_generation();
        world = saved_world;
        top_of_world = saved_top_of_world;
        mob_index = saved_mob_index;
        top_of_mobt = saved_top_of_mobt;
        character_list = saved_character_list;
        object_list = saved_object_list;
        js_script_set_legacy_trigger_dispatch_enabled(false);
    }
};

} // namespace

TEST(JsBuilderHttpServerTransport, RendersParserErrorsAsHttpJson) {
    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        "POST /api/builder/login\r\n\r\n", make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("builder.http.invalid-start-line", result.reason_code);
    expect_response_contains(result, "HTTP/1.1 400 Bad Request");
    expect_response_contains(result, "\"reasonCode\":\"builder.http.invalid-start-line\"");
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, RoutesTrustedManifestThroughIngress) {
    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("GET", "/api/builder/js/manifest", "", true, ""), make_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.manifest.current", result.reason_code);
    expect_response_contains(result, "HTTP/1.1 200 OK");
    expect_response_contains(result, "\"exportKind\":\"builderManifest\"");
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, LoadedWorldCatalogDropsOwnerTerminatorOnlyForOwnedZones) {
    struct GlobalCatalogStateGuard {
        zone_data *saved_zone_table = zone_table;
        int saved_top_of_zone_table = top_of_zone_table;
        int saved_top_of_world = top_of_world;
        index_data *saved_mob_index = mob_index;
        int saved_top_of_mobt = top_of_mobt;
        index_data *saved_obj_index = obj_index;
        int saved_top_of_objt = top_of_objt;

        ~GlobalCatalogStateGuard()
        {
            zone_table = saved_zone_table;
            top_of_zone_table = saved_top_of_zone_table;
            top_of_world = saved_top_of_world;
            mob_index = saved_mob_index;
            top_of_mobt = saved_top_of_mobt;
            obj_index = saved_obj_index;
            top_of_objt = saved_top_of_objt;
        }
    } guard;

    owner_list owned_first {42, nullptr};
    owner_list owned_second {77, nullptr};
    owner_list owned_duplicate {42, nullptr};
    owner_list owned_terminator {0, nullptr};
    owned_first.next = &owned_second;
    owned_second.next = &owned_duplicate;
    owned_duplicate.next = &owned_terminator;
    owner_list common_owner {0, nullptr};
    zone_data zones[2] {};
    zones[0].number = 0;
    zones[0].top = 1199;
    zones[0].owners = &common_owner;
    zones[1].number = 30;
    zones[1].top = 3999;
    zones[1].owners = &owned_first;

    zone_table = zones;
    top_of_zone_table = 1;
    top_of_world = -1;
    mob_index = nullptr;
    top_of_mobt = -1;
    obj_index = nullptr;
    top_of_objt = -1;

    JsBuilderPublishTargetCatalog catalog =
        js_builder_http_server_transport_catalog_from_loaded_world();

    ASSERT_EQ(2u, catalog.zones.size());
    EXPECT_EQ(0, catalog.zones[0].number);
    EXPECT_EQ(std::vector<int>({0}), catalog.zones[0].owner_character_ids);
    EXPECT_EQ(30, catalog.zones[1].number);
    EXPECT_EQ(std::vector<int>({42, 77}), catalog.zones[1].owner_character_ids);
}

TEST(JsBuilderHttpServerTransport, RejectsUntrustedPublishBeforeContextResolution) {
    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), false), make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("builder.ingress.untrusted", result.reason_code);
    expect_response_contains(result, "\"reasonCode\":\"builder.ingress.untrusted\"");
    EXPECT_EQ(std::string::npos, result.http_response.find(kProxySecret));
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, RejectsDuplicateProxySecretsBeforeContextResolution) {
    const std::vector<std::vector<std::pair<std::string, std::string>>> headers = {
        {{"x-rots-builder-proxy-secret", kProxySecret},
         {"x-rots-builder-proxy-secret", kProxySecret}},
        {{"x-rots-builder-proxy-secret", kProxySecret}, {"x-rots-builder-proxy-secret", "wrong"}},
        {{"x-rots-builder-proxy-secret", kProxySecret},
         {"X-Rots-Builder-Proxy-Secret", kProxySecret}},
    };

    for (const auto &header_set : headers) {
        JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
            raw_request_with_headers("POST", "/api/js-scripts/stage", header_set, stage_body()),
            make_options());

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(403, result.http_status);
        EXPECT_EQ("builder.ingress.untrusted", result.reason_code);
        EXPECT_EQ(std::string::npos, result.http_response.find(kProxySecret));
        expect_content_length_matches_body(result.http_response);
    }
}

TEST(JsBuilderHttpServerTransport, FailsClosedWhenTrustedPublishLacksLiveStore) {
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsLivePackageStore service_store;
    JsPublishEndpointService service(service_store, service_options());
    JsBuilderHttpServerTransportOptions options = make_options(nullptr, &service, &session_store);

    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(503, result.http_status);
    EXPECT_EQ("builder.context.live-store-unavailable", result.reason_code);
    expect_response_contains(result, "\"reasonCode\":\"builder.context.live-store-unavailable\"");
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, MissingPublishServicePrecedesContextResolution) {
    JsLivePackageStore live_store;
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, nullptr, &session_store);

    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(503, result.http_status);
    EXPECT_EQ("builder.ingress.publish-unavailable", result.reason_code);
    expect_response_contains(result, "\"reasonCode\":\"builder.ingress.publish-unavailable\"");
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, ResolvesPublishContextBeforeIngressDispatch) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_STATUS_READ);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);

    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);
    JsBuilderHttpServerTransportResult status = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/status", status_body(), true, "application/json",
                    "session-token"),
        options);

    EXPECT_TRUE(stage.ok);
    EXPECT_EQ(200, stage.http_status);
    EXPECT_EQ("stage.accepted", stage.reason_code);
    expect_response_contains(stage, "\"packageId\":\"js:30:character:3001\"");
    EXPECT_TRUE(status.ok);
    EXPECT_EQ(200, status.http_status);
    EXPECT_EQ("status.current", status.reason_code);
    expect_content_length_matches_body(stage.http_response);
    expect_content_length_matches_body(status.http_response);
}

TEST(JsBuilderHttpServerTransport, RejectsValidTargetOwnedByAnotherBuilder) {
    JsLivePackageStore owned_live_store;
    JsPublishEndpointService owned_service(owned_live_store, service_options());
    JsBuilderSessionStore owned_session_store;
    insert_session(&owned_session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsBuilderHttpServerTransportOptions owned_options =
        make_options(&owned_live_store, &owned_service, &owned_session_store);

    JsBuilderHttpServerTransportResult owned_result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        owned_options);

    EXPECT_TRUE(owned_result.ok);
    EXPECT_EQ(200, owned_result.http_status);
    EXPECT_EQ("stage.accepted", owned_result.reason_code);
    EXPECT_TRUE(js_publish_latest_staged_package_status(
        owned_service.staged_repository(), "js:30:character:3001").ok);

    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsBuilderPublishTargetCatalog foreign_owned_catalog = make_catalog();
    foreign_owned_catalog.zones[0].owner_character_ids = {2002};
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);
    options.publish_context_options.target_catalog = &foreign_owned_catalog;

    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    expect_response_contains(result, "\"reasonCode\":\"stage.authorization-failed\"");
    EXPECT_EQ(std::string::npos, result.http_response.find("2002"));
    EXPECT_EQ(std::string::npos, result.http_response.find("session-token"));
    expect_content_length_matches_body(result.http_response);

    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001");
    EXPECT_FALSE(staged.ok);
    ASSERT_EQ(1u, staged.diagnostics.size());
    EXPECT_EQ(JsPublishStagingDiagnosticCode::StagedPackageNotFound, staged.diagnostics[0].code);
}

TEST(JsBuilderHttpServerTransport, RejectsActivationBaseChecksumMismatchWithoutLivePointer) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);

    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);
    ASSERT_TRUE(stage.ok) << stage.reason_code;
    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001");
    ASSERT_TRUE(staged.ok);

    JsBuilderHttpServerTransportResult activate = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/activate",
                    activate_body(staged.status.package_id, staged.status.staged_digest,
                                  "live:different"),
                    true, "application/json", "session-token"),
        options);

    EXPECT_FALSE(activate.ok);
    EXPECT_EQ(409, activate.http_status);
    EXPECT_EQ("activate.stale-live-checksum", activate.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
    expect_content_length_matches_body(activate.http_response);
}

TEST(JsBuilderHttpServerTransport, RejectsActivationWhenZoneAuthorityIsRevoked) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store,
                   JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);

    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);
    ASSERT_TRUE(stage.ok) << stage.reason_code;
    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001");
    ASSERT_TRUE(staged.ok);

    JsBuilderPublishTargetCatalog revoked_catalog = make_catalog();
    revoked_catalog.zones[0].owner_character_ids = {2002};
    options.publish_context_options.target_catalog = &revoked_catalog;
    JsBuilderHttpServerTransportResult activate = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/activate",
                    activate_body(staged.status.package_id, staged.status.staged_digest,
                                  "live:initial"),
                    true, "application/json", "session-token"),
        options);

    EXPECT_FALSE(activate.ok);
    EXPECT_EQ(403, activate.http_status);
    EXPECT_EQ("activate.authorization-failed", activate.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
    EXPECT_EQ(std::string::npos, activate.http_response.find(staged.status.package_id));
    EXPECT_EQ(std::string::npos, activate.http_response.find(staged.status.staged_digest));
    EXPECT_EQ(std::string::npos, activate.http_response.find("live:initial"));
    EXPECT_EQ(std::string::npos, activate.http_response.find("2002"));
    EXPECT_EQ(std::string::npos, activate.http_response.find("session-token"));
    expect_content_length_matches_body(activate.http_response);
}

TEST(JsBuilderHttpServerTransport, RejectsActivationWhenTokenMissingActivateScope) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_STATUS_READ);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);

    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "application/json",
                    "session-token"),
        options);
    ASSERT_TRUE(stage.ok) << stage.reason_code;
    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001");
    ASSERT_TRUE(staged.ok);

    JsBuilderHttpServerTransportResult activate = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/activate",
                    activate_body(staged.status.package_id, staged.status.staged_digest,
                                  "live:initial"),
                    true, "application/json", "session-token"),
        options);

    EXPECT_FALSE(activate.ok);
    EXPECT_EQ(403, activate.http_status);
    EXPECT_EQ("activate.missing-scope", activate.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
    EXPECT_EQ(std::string::npos, activate.http_response.find(staged.status.package_id));
    EXPECT_EQ(std::string::npos, activate.http_response.find(staged.status.staged_digest));
    EXPECT_EQ(std::string::npos, activate.http_response.find("live:initial"));
    EXPECT_EQ(std::string::npos, activate.http_response.find("session-token"));
    expect_content_length_matches_body(activate.http_response);
}

TEST(JsBuilderHttpServerTransport, HttpSessionPublishedJavaScriptExecutesThroughCharacterEnter) {
    RuntimeGlobalStateGuard guard;
    JsLiveRegistryAdminService &admin_service = guard.service;
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store,
                   JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE |
                       JS_PUBLISH_SCOPE_STATUS_READ);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);

    index_data local_mob_index[1] = {};
    local_mob_index[0].virt = 3001;
    mob_index = local_mob_index;
    top_of_mobt = 0;
    world = make_room("HTTP Runtime Room", 3001, 0);
    top_of_world = 0;
    char_data guard_character = make_character("Http Guard", true);
    char_data actor = make_character("Http Actor");
    char_data other_actor = make_character("Http Other");
    guard_character.next = &actor;
    actor.next = &other_actor;
    other_actor.next = nullptr;
    character_list = &guard_character;
    object_list = nullptr;

    const std::string body = stage_body(make_runtime_blocking_package());
    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", body, true, "application/json",
                    "session-token"),
        options);
    ASSERT_TRUE(stage.ok) << stage.reason_code << " " << stage.http_response;
    EXPECT_EQ("stage.accepted", stage.reason_code);
    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001");
    ASSERT_TRUE(staged.ok);
    ASSERT_TRUE(admin_service.refresh().ok);
    EXPECT_EQ(nullptr, admin_service.reload_service().find_package_status_by_vnum(3001));
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);
    EXPECT_EQ(trigger_char_enter(&guard_character, &actor, &world), 1);

    JsBuilderHttpServerTransportResult activate = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/activate",
                    activate_body(staged.status.package_id, staged.status.staged_digest,
                                  "live:initial"),
                    true, "application/json", "session-token"),
        options);
    ASSERT_TRUE(activate.ok) << activate.reason_code << " " << activate.http_response;
    EXPECT_EQ("activate.accepted", activate.reason_code);
    EXPECT_EQ(1u, live_store.package_record_count());
    EXPECT_EQ(1u, live_store.live_pointer_count());
    JsLivePackagePointerResult pointer = live_store.find_live_pointer(staged.status.package_id);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(staged.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_EQ(staged.status.staged_digest, pointer.pointer.staged_digest);
    expect_content_length_matches_body(stage.http_response);
    expect_content_length_matches_body(activate.http_response);

    ASSERT_TRUE(admin_service.live_store().hydrate_from_snapshot(live_store.export_snapshot()).ok);
    ASSERT_TRUE(admin_service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());

    EXPECT_EQ(trigger_char_enter(&guard_character, &actor, &world), 0);
    EXPECT_EQ(trigger_char_enter(&guard_character, &other_actor, &world), 1);
}

TEST(JsBuilderHttpServerTransport, ProductionBuilderHttpOptionsInstallLiveMutationRefreshHook) {
    const std::string source = read_first_available_file({"src/comm.cpp", "../comm.cpp"});

    ASSERT_FALSE(source.empty());
    // The production options builder is internal to comm.cpp; keep this as a source tripwire.
    EXPECT_NE(std::string::npos,
              source.find("after_successful_live_mutation =\n"
                          "        js_script_refresh_live_registry_after_publish"));
}

TEST(JsBuilderHttpServerTransport, ProductionActivationRefreshesLiveRegistryForGameplayDispatch) {
    constexpr int kProductionRefreshVnum = 3002;
    RuntimeGlobalStateGuard guard;
    JsLiveRegistryAdminService &admin_service = guard.service;
    JsBuilderSessionStore session_store;
    insert_session(&session_store,
                   JS_PUBLISH_SCOPE_PACKAGE_STAGE | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE |
                       JS_PUBLISH_SCOPE_STATUS_READ);
    JsBuilderHttpServerTransportOptions options =
        make_options(&admin_service.live_store(), &admin_service.publish_service(), &session_store);
    options.ingress_options.publish_options.after_successful_live_mutation =
        js_script_refresh_live_registry_after_publish;
    JsBuilderPublishTargetCatalog catalog;
    catalog.mobile_vnums = {kProductionRefreshVnum};
    catalog.zones.push_back({30, 3999, {1001}});
    options.publish_context_options.target_catalog = &catalog;

    index_data local_mob_index[1] = {};
    local_mob_index[0].virt = kProductionRefreshVnum;
    mob_index = local_mob_index;
    top_of_mobt = 0;
    world = make_room("HTTP Production Runtime Room", kProductionRefreshVnum, 0);
    top_of_world = 0;
    char_data guard_character = make_character("Http Guard", true);
    char_data actor = make_character("Http Actor");
    guard_character.next = &actor;
    actor.next = nullptr;
    character_list = &guard_character;
    object_list = nullptr;

    ASSERT_TRUE(admin_service.refresh().ok);
    ASSERT_TRUE(js_script_capture_live_registry_generation());
    js_script_set_legacy_trigger_dispatch_enabled(true);

    JsBuilderHttpServerTransportResult stage = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage",
                    stage_body(make_runtime_blocking_package(kProductionRefreshVnum)), true,
                    "application/json", "session-token"),
        options);
    ASSERT_TRUE(stage.ok) << stage.reason_code << " " << stage.http_response;
    EXPECT_EQ("stage.accepted", stage.reason_code);
    EXPECT_EQ(trigger_char_enter(&guard_character, &actor, &world), 1);

    JsPublishStagedPackageStatusResult staged = js_publish_latest_staged_package_status(
        admin_service.publish_service().staged_repository(), "js:30:character:3002");
    ASSERT_TRUE(staged.ok);
    JsBuilderHttpServerTransportResult activate = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/activate",
                    activate_body(staged.status.package_id, staged.status.staged_digest,
                                  "live:initial"),
                    true, "application/json", "session-token"),
        options);
    ASSERT_TRUE(activate.ok) << activate.reason_code << " " << activate.http_response;
    EXPECT_EQ("activate.accepted", activate.reason_code);
    EXPECT_NE(nullptr,
              admin_service.reload_service().find_package_status_by_vnum(kProductionRefreshVnum));
    EXPECT_EQ(trigger_char_enter(&guard_character, &actor, &world), 0);
    expect_content_length_matches_body(stage.http_response);
    expect_content_length_matches_body(activate.http_response);
}

TEST(JsBuilderHttpServerTransport, MapsContextTargetFailuresToRedactedHttpResponses) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsBuilderHttpServerTransportOptions options =
        make_options(&live_store, &service, &session_store);
    const std::string body =
        "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:initial\",\"package\":" +
        package_json(make_package()) + ",\"serverResolvedTargetZone\":30}";

    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage", body, true, "application/json",
                    "session-token"),
        options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("builder.context.invalid-request", result.reason_code);
    expect_response_contains(result, "\"reasonCode\":\"builder.context.invalid-request\"");
    EXPECT_EQ(std::string::npos, result.http_response.find("serverResolvedTargetZone"));
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, PublishPreflightFailuresDoNotResolveContext) {
    JsLivePackageStore service_store;
    JsPublishEndpointService service(service_store, service_options());
    JsBuilderSessionStore session_store;
    insert_session(&session_store, JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    JsBuilderHttpServerTransportOptions options = make_options(nullptr, &service, &session_store);

    struct Case {
        std::string raw;
        int status;
        const char *reason;
    };
    const Case cases[] = {
        {raw_request("GET", "/api/js-scripts/stage", stage_body(), true, "application/json",
                     "session-token"),
         405, "publish.method-not-allowed"},
        {raw_request("POST", "/api/js-scripts/stage", stage_body(), true, "text/plain",
                     "session-token"),
         415, "publish.unsupported-media-type"},
        {raw_request("POST", "/api/js-scripts/stage", stage_body()), 401,
         "publish.session-rejected"},
    };

    for (const Case &test_case : cases) {
        JsBuilderHttpServerTransportResult result =
            js_builder_http_server_transport_dispatch(test_case.raw, options);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(test_case.status, result.http_status);
        EXPECT_EQ(test_case.reason, result.reason_code);
        EXPECT_EQ(std::string::npos, result.reason_code.find("builder.context"));
        expect_content_length_matches_body(result.http_response);
    }
}

TEST(JsBuilderHttpServerTransport, UnknownPublishRoutesDoNotResolveContext) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderHttpServerTransportOptions options = make_options(nullptr, &service);
    const char *paths[] = {
        "/api/js-scripts/stage/extra",
        "/api/js-scripts/stage%2Fextra",
        "/api/js-scripts/garbage",
    };

    for (const char *path : paths) {
        JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
            raw_request("POST", path, stage_body()), options);

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(404, result.http_status);
        EXPECT_EQ("builder.ingress.not-found", result.reason_code) << path;
        expect_content_length_matches_body(result.http_response);
    }
}

TEST(JsBuilderHttpServerTransport, QueryPublishPathFailsInParserBeforeContextResolution) {
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderHttpServerTransportResult result = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/js-scripts/stage?debug=true", stage_body()),
        make_options(nullptr, &service));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("builder.http.invalid-start-line", result.reason_code);
    expect_content_length_matches_body(result.http_response);
}

TEST(JsBuilderHttpServerTransport, NonPublishRoutesDoNotRequireResolverDependencies) {
    JsBuilderHttpServerTransportOptions options = make_options();
    options.publish_context_options.target_catalog = nullptr;
    options.publish_context_options.live_store = nullptr;

    JsBuilderHttpServerTransportResult manifest = js_builder_http_server_transport_dispatch(
        raw_request("GET", "/api/builder/js/manifest", "", true, ""), options);
    JsBuilderHttpServerTransportResult login = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/builder/login", "{}", true), options);
    JsBuilderHttpServerTransportResult logout = js_builder_http_server_transport_dispatch(
        raw_request("POST", "/api/builder/logout", "", true, ""), options);

    EXPECT_TRUE(manifest.ok);
    EXPECT_EQ("builder.manifest.current", manifest.reason_code);
    EXPECT_NE("builder.context.catalog-unavailable", login.reason_code);
    EXPECT_NE("builder.context.catalog-unavailable", logout.reason_code);
    expect_content_length_matches_body(manifest.http_response);
    expect_content_length_matches_body(login.http_response);
    expect_content_length_matches_body(logout.http_response);
}

TEST(JsBuilderHttpServerTransport, MapsParserFramingErrorsToHttpResponses) {
    struct Case {
        std::string raw;
        int status;
        const char *reason;
    };
    const Case cases[] = {
        {"POST /api/builder/login HTTP/1.1\r\ncontent-length: 2\r\n\r\nx", 400,
         "builder.http.body-length-mismatch"},
        {"POST /api/builder/login HTTP/1.1\r\ncontent-length: 1\r\n\r\nxy", 400,
         "builder.http.body-length-mismatch"},
        {"POST /api/builder/login HTTP/1.1\r\ncontent-length: 1\r\n"
         "content-length: 1\r\n\r\nx",
         400, "builder.http.duplicate-content-length"},
        {"POST /api/builder/login HTTP/1.1\r\ncontent-length: nope\r\n\r\n", 400,
         "builder.http.invalid-content-length"},
    };

    for (const Case &test_case : cases) {
        JsBuilderHttpServerTransportResult result =
            js_builder_http_server_transport_dispatch(test_case.raw, make_options());
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(test_case.status, result.http_status);
        EXPECT_EQ(test_case.reason, result.reason_code);
        expect_content_length_matches_body(result.http_response);
    }
}
