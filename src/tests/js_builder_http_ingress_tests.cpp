#include "../js_builder_http_ingress.h"

#include "../json_utils.h"
#include "../js_script_package_loader.h"
#include "../script.h"

#include <gtest/gtest.h>

namespace {

const char *kProxySecret = "local-ingress-secret";

account::AccountData make_account()
{
    account::AccountData account;
    account.account_name = "builderaccount";
    account.password_hash = "stored-password";
    account.email_verified = true;
    account.characters = { "Builderone" };
    return account;
}

JsBuilderHttpIngressRequest ingress_request(
    const std::string &method, const std::string &path)
{
    JsBuilderHttpIngressRequest request;
    request.method = method;
    request.path = path;
    request.headers.push_back({ "x-rots-builder-proxy-secret", kProxySecret });
    return request;
}

JsBuilderSessionOptions session_login_options()
{
    JsBuilderSessionOptions options;
    options.root_directory = "test-root";
    options.now_epoch_seconds = 100;
    options.session_ttl_seconds = 60;
    options.account_resolver = [](const std::string &, const std::string &identifier,
                                   account::AccountData *out, std::string *) {
        if (identifier != "builder@example.com")
            return false;
        *out = make_account();
        return true;
    };
    options.password_verifier = [](const std::string &password,
                                    const std::string &hash) {
        return password == "CorrectPassword1" && hash == "stored-password";
    };
    options.character_loader = [](const std::string &, const account::AccountData &,
                                   const std::string &character_name,
                                   JsPublishLinkedCharacterEligibility *character,
                                   std::string *) {
        character->character_name = character_name;
        character->character_id = 1001;
        character->level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
        character->immortal = true;
        character->character_loaded = true;
        return true;
    };
    options.token_issuer = [](const account::AccountData &,
                               const JsPublishBuilderEligibilityResult &, long long) {
        return "session-token";
    };
    return options;
}

JsBuilderSessionStoreOptions session_store_options(long long now = 100)
{
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = now;
    options.server_audience = "server:main";
    options.workspace_id = "workspace:main";
    return options;
}

JsBuilderHttpIngressOptions ingress_options(JsBuilderSessionStore *store = nullptr)
{
    JsBuilderHttpIngressOptions options;
    options.expected_proxy_secret = kProxySecret;
    options.session_options.session_options = session_login_options();
    options.session_options.session_store = store;
    options.session_options.session_store_options = session_store_options();
    options.publish_options.session_store = store;
    options.publish_options.session_store_options = session_store_options();
    options.publish_context.request_id = "request:ingress";
    options.publish_context.audit_id = "audit:ingress";
    options.publish_context.zone = 30;
    options.publish_context.target_zone_resolved = true;
    options.publish_context.server_resolved_target_zone = 30;
    options.publish_context.server_resolved_target_host = JsScriptPackageHost::Character;
    options.publish_context.zone_exists = true;
    options.publish_context.zone_owner_character_ids = { 1001 };
    options.publish_context.publish_audit_log_path = "build/js_builder_http_ingress_audit.jsonl";
    options.publish_context.transport.secure_channel = true;
    options.publish_context.transport.server_identity_verified = true;
    options.publish_context.transport.server_audience = "server:main";
    options.publish_context.transport.source_identifier = "transport:local-proxy";
    options.publish_context.now_epoch_seconds = 100;
    options.publish_context.allow_mutating_operations = true;
    options.publish_context.allow_live_pointer_update = true;
    options.publish_context.applied_at_epoch_seconds = 200;
    options.publish_context.expected_server_audience = "server:main";
    options.publish_context.expected_workspace_id = "workspace:main";
    options.publish_context.current_live_checksum = "live:old";
    return options;
}

JsPublishEndpointServiceOptions service_options()
{
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

JsScriptPackage make_package()
{
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = 3001;
    package.package_id = "client-pkg-30-3001";
    package.host = JsScriptPackageHost::Character;
    package.package_format_version = metadata.package_format_version;
    package.manifest_schema_version = metadata.schema_version;
    package.trigger_catalog_revision = metadata.trigger_catalog_revision;
    package.manifest_checksum = metadata.manifest_checksum;
    package.runtime_name = metadata.selected_runtime_name;
    package.runtime_version = metadata.selected_runtime_version;
    package.generated_typings_version = metadata.generated_typings_version;
    package.compiled_javascript = "function onEnter(ctx) { return true; }";
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter" });
    package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(package);
    return package;
}

std::string quote(const std::string &value)
{
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

std::string package_json(const JsScriptPackage &package)
{
    std::string bindings;
    for (std::size_t index = 0; index < package.trigger_bindings.size(); ++index) {
        const JsScriptTriggerBinding &binding = package.trigger_bindings[index];
        if (!bindings.empty())
            bindings += ",";
        bindings += "{\"kind\":\"";
        bindings += js_scripting_manifest_kind_name(binding.kind);
        bindings += "\",\"legacyValue\":";
        bindings += std::to_string(binding.legacy_value);
        bindings += ",\"handlerName\":";
        bindings += quote(binding.handler_name);
        bindings += "}";
    }
    return "{\"packageFormatVersion\":"
        + std::to_string(package.package_format_version)
        + ",\"packageId\":" + quote(package.package_id)
        + ",\"host\":" + quote(js_script_package_host_name(package.host))
        + ",\"vnum\":" + std::to_string(package.vnum)
        + ",\"manifestSchemaVersion\":"
        + std::to_string(package.manifest_schema_version)
        + ",\"triggerCatalogRevision\":"
        + std::to_string(package.trigger_catalog_revision)
        + ",\"manifestChecksum\":" + quote(package.manifest_checksum)
        + ",\"runtimeName\":" + quote(package.runtime_name)
        + ",\"runtimeVersion\":" + quote(package.runtime_version)
        + ",\"generatedTypingsVersion\":" + quote(package.generated_typings_version)
        + ",\"compiledJavaScript\":" + quote(package.compiled_javascript)
        + ",\"compiledJavaScriptChecksum\":"
        + quote(package.compiled_javascript_checksum)
        + ",\"triggerBindings\":[" + bindings + "]}";
}

void expect_valid_json_object(const std::string &json)
{
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

void expect_contains(const std::string &text, const std::string &needle)
{
    EXPECT_NE(std::string::npos, text.find(needle)) << needle;
}

void expect_error(const JsBuilderHttpIngressResult &result, int status,
    const std::string &reason_code)
{
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(status, result.http_status);
    EXPECT_EQ(reason_code, result.reason_code);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"ok\":false");
    expect_contains(result.json, "\"reasonCode\":\"" + reason_code + "\"");
    EXPECT_EQ(std::string::npos, result.json.find(kProxySecret));
    EXPECT_EQ(std::string::npos, result.json.find("session-token"));
    EXPECT_EQ(std::string::npos, result.json.find("CorrectPassword1"));
    EXPECT_EQ(std::string::npos, result.json.find("builder@example.com"));
}

void expect_no_staged_package(const JsPublishEndpointService &service)
{
    EXPECT_FALSE(js_publish_latest_staged_package_status(
                     service.staged_repository(), "js:30:character:3001")
                     .ok);
}

JsBuilderHttpIngressResult login(
    JsBuilderSessionStore &store, JsBuilderHttpIngressOptions *used_options = nullptr)
{
    JsBuilderHttpIngressOptions options = ingress_options(&store);
    if (used_options != nullptr)
        *used_options = options;
    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/builder/login");
    request.headers.push_back({ "content-type", "application/json" });
    request.body = "{\"account\":\"builder@example.com\","
                   "\"password\":\"CorrectPassword1\"}";
    return js_builder_http_ingress_dispatch(request, options);
}

} // namespace

TEST(JsBuilderHttpIngress, RoutesManifestThroughTrustedProxy)
{
    JsBuilderHttpIngressResult result = js_builder_http_ingress_dispatch(
        ingress_request("GET", "/api/builder/js/manifest"), ingress_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.manifest.current", result.reason_code);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"exportKind\":\"builderManifest\"");
}

TEST(JsBuilderHttpIngress, RoutesLoginAndPersistsSessionThroughStore)
{
    JsBuilderSessionStore store;

    JsBuilderHttpIngressResult result = login(store);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.login.accepted", result.reason_code);
    expect_contains(result.json, "\"token\":\"session-token\"");
    EXPECT_TRUE(store.lookup("session-token", session_store_options()).ok);
}

TEST(JsBuilderHttpIngress, RoutesLogoutAndRevokesSession)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(login(store).ok);

    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/builder/logout");
    request.headers.push_back({ "authorization", "Bearer session-token" });
    JsBuilderHttpIngressResult result =
        js_builder_http_ingress_dispatch(request, ingress_options(&store));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.logout.accepted", result.reason_code);
    EXPECT_FALSE(store.lookup("session-token", session_store_options()).ok);
}

TEST(JsBuilderHttpIngress, RoutesPublishThroughStoreBackedBearer)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(login(store).ok);
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderHttpIngressOptions options = ingress_options(&store);
    options.publish_service = &service;

    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/js-scripts/stage");
    request.headers.push_back({ "content-type", "application/json" });
    request.headers.push_back({ "authorization", "Bearer session-token" });
    request.body = "{\"baseLiveChecksum\":\"live:old\",\"package\":"
        + package_json(make_package()) + "}";

    JsBuilderHttpIngressResult result =
        js_builder_http_ingress_dispatch(request, options);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("stage.accepted", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("\"auditId\":\"audit:ingress\""));
}

TEST(JsBuilderHttpIngress, RejectsUntrustedBeforeRouteHeadersOrBody)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderHttpIngressOptions options = ingress_options();
    options.publish_service = &service;
    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/js-scripts/stage");
    request.headers.clear();
    request.headers.push_back({ "x-rots-builder-proxy-secret", "wrong-secret" });
    request.headers.push_back({ "content-type", "application/json" });
    request.headers.push_back({ "content-type", "application/json" });
    request.headers.push_back({ "authorization", "Bearer session-token" });
    request.body = "{\"password\":\"CorrectPassword1\"}";

    expect_error(js_builder_http_ingress_dispatch(request, options), 403,
        "builder.ingress.untrusted");
    expect_no_staged_package(service);
}

TEST(JsBuilderHttpIngress, RejectsDuplicateSelectedHeadersForTrustedRequests)
{
    bool resolver_called = false;
    JsBuilderHttpIngressOptions options = ingress_options();
    options.session_options.session_options.account_resolver =
        [&resolver_called](const std::string &, const std::string &,
            account::AccountData *, std::string *) {
            resolver_called = true;
            return false;
        };
    JsBuilderHttpIngressRequest duplicate_content =
        ingress_request("POST", "/api/builder/login");
    duplicate_content.headers.push_back({ "Content-Type", "application/json" });
    duplicate_content.headers.push_back({ "content-type", "application/json" });
    expect_error(js_builder_http_ingress_dispatch(duplicate_content, options),
        400, "builder.ingress.invalid-headers");
    EXPECT_FALSE(resolver_called);

    JsBuilderSessionStore store;
    ASSERT_TRUE(login(store).ok);
    JsBuilderHttpIngressRequest duplicate_auth =
        ingress_request("POST", "/api/builder/logout");
    duplicate_auth.headers.push_back({ "Authorization", "Bearer session-token" });
    duplicate_auth.headers.push_back({ "authorization", "Bearer session-token" });
    expect_error(js_builder_http_ingress_dispatch(duplicate_auth, ingress_options(&store)),
        400, "builder.ingress.invalid-headers");
    EXPECT_TRUE(store.lookup("session-token", session_store_options()).ok);
}

TEST(JsBuilderHttpIngress, RejectsPublishWhenServiceIsMissing)
{
    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/js-scripts/stage");
    request.headers.push_back({ "authorization", "Bearer session-token" });

    expect_error(js_builder_http_ingress_dispatch(request, ingress_options()), 503,
        "builder.ingress.publish-unavailable");
}

TEST(JsBuilderHttpIngress, RejectsUnknownRouteAfterTrustMarker)
{
    expect_error(js_builder_http_ingress_dispatch(
                     ingress_request("GET", "/api/builder/not-here"), ingress_options()),
        404, "builder.ingress.not-found");
}

TEST(JsBuilderHttpIngress, RejectsUnknownPublishRoutesBeforeServiceAvailability)
{
    const char *paths[] = {
        "/api/js-scripts/not-real",
        "/api/js-scripts/stage/extra",
    };
    for (const char *path : paths) {
        expect_error(js_builder_http_ingress_dispatch(
                         ingress_request("POST", path), ingress_options()),
            404, "builder.ingress.not-found");

        JsLivePackageStore live_store;
        JsPublishEndpointService service(live_store, service_options());
        JsBuilderHttpIngressOptions options = ingress_options();
        options.publish_service = &service;
        expect_error(js_builder_http_ingress_dispatch(
                         ingress_request("POST", path), options),
            404, "builder.ingress.not-found");
        expect_no_staged_package(service);
    }
}

TEST(JsBuilderHttpIngress, HeaderNamesAndBearerSchemeAreCaseInsensitive)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(login(store).ok);

    JsBuilderHttpIngressRequest request;
    request.method = "POST";
    request.path = "/api/builder/logout";
    request.headers.push_back({ "X-ROTS-BUILDER-PROXY-SECRET", kProxySecret });
    request.headers.push_back({ "AUTHORIZATION", "bearer session-token" });

    JsBuilderHttpIngressResult result =
        js_builder_http_ingress_dispatch(request, ingress_options(&store));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("builder.logout.accepted", result.reason_code);
}

TEST(JsBuilderHttpIngress, RejectsDuplicateOrMissingProxySecretBeforeParsingShape)
{
    const std::vector<std::vector<std::pair<std::string, std::string>>> headers = {
        {},
        {
            { "x-rots-builder-proxy-secret", kProxySecret },
            { "x-rots-builder-proxy-secret", kProxySecret },
        },
        {
            { "x-rots-builder-proxy-secret", kProxySecret },
            { "X-ROTS-BUILDER-PROXY-SECRET", "wrong-secret" },
        },
    };
    for (const auto &candidate_headers : headers) {
        JsBuilderHttpIngressRequest request =
            ingress_request("POST", "/api/js-scripts/stage");
        request.headers = candidate_headers;
        request.headers.push_back({ "content-type", "application/json" });
        request.headers.push_back({ "content-type", "application/json" });
        request.body = "{\"password\":\"CorrectPassword1\"}";
        expect_error(js_builder_http_ingress_dispatch(request, ingress_options()), 403,
            "builder.ingress.untrusted");
    }

    JsBuilderHttpIngressOptions blank_secret = ingress_options();
    blank_secret.expected_proxy_secret = "";
    expect_error(js_builder_http_ingress_dispatch(
                     ingress_request("GET", "/api/builder/js/manifest"), blank_secret),
        403, "builder.ingress.untrusted");
}

TEST(JsBuilderHttpIngress, RejectsMalformedBearerWithoutMutatingPublishState)
{
    const char *authorizations[] = {
        "Bearer",
        "Basic session-token",
        "Bearer session-token extra",
        "Bearer bad\n token",
    };
    for (const char *authorization : authorizations) {
        JsBuilderSessionStore store;
        ASSERT_TRUE(login(store).ok) << authorization;
        JsLivePackageStore live_store;
        JsPublishEndpointService service(live_store, service_options());
        JsBuilderHttpIngressOptions options = ingress_options(&store);
        options.publish_service = &service;
        JsBuilderHttpIngressRequest request =
            ingress_request("POST", "/api/js-scripts/stage");
        request.headers.push_back({ "content-type", "application/json" });
        request.headers.push_back({ "authorization", authorization });
        request.body = "{\"baseLiveChecksum\":\"live:old\",\"package\":"
            + package_json(make_package()) + "}";

        expect_error(js_builder_http_ingress_dispatch(request, options), 401,
            "publish.session-rejected");
        expect_no_staged_package(service);
    }
}

TEST(JsBuilderHttpIngress, AllowsWhitespaceAroundBearerToken)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(login(store).ok);
    JsBuilderHttpIngressRequest request =
        ingress_request("POST", "/api/builder/logout");
    request.headers.push_back({ "authorization", "  Bearer    session-token   " });

    JsBuilderHttpIngressResult result =
        js_builder_http_ingress_dispatch(request, ingress_options(&store));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("builder.logout.accepted", result.reason_code);
}

TEST(JsBuilderHttpIngress, RejectsRouteConfigurationCollisions)
{
    JsBuilderHttpIngressOptions manifest_login_collision = ingress_options();
    manifest_login_collision.manifest_options.route_path = "/api/builder/login";
    expect_error(js_builder_http_ingress_dispatch(
                     ingress_request("POST", "/api/builder/login"),
                     manifest_login_collision),
        500, "builder.ingress.invalid-route-config");

    JsBuilderHttpIngressOptions login_publish_collision = ingress_options();
    login_publish_collision.session_options.login_path = "/api/js-scripts/login";
    expect_error(js_builder_http_ingress_dispatch(
                     ingress_request("POST", "/api/js-scripts/login"),
                     login_publish_collision),
        500, "builder.ingress.invalid-route-config");

    JsBuilderHttpIngressOptions invalid_prefix = ingress_options();
    invalid_prefix.publish_options.route_prefix = "";
    expect_error(js_builder_http_ingress_dispatch(
                     ingress_request("GET", "/api/builder/js/manifest"),
                     invalid_prefix),
        500, "builder.ingress.invalid-route-config");
}
