#include "../js_publish_http_endpoint.h"

#include "../json_utils.h"
#include "../js_script_package_loader.h"
#include "../script.h"

#include <gtest/gtest.h>

namespace {

JsScriptPackage make_package(const std::string &body = "return true")
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
    package.compiled_javascript = "function onEnter(ctx) { " + body + "; }";
    package.trigger_bindings.push_back(
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter" });
    package.compiled_javascript_checksum =
        js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsPublishTokenMetadata make_token(unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishTokenMetadata token;
    token.claims_verified = true;
    token.token_id = "token:http";
    token.actor_id = "actor:42";
    token.builder_account_id = "account:builder";
    token.server_audience = "server:main";
    token.workspace_id = "workspace:main";
    token.scopes = scopes;
    token.issued_at_epoch_seconds = 90;
    token.expires_at_epoch_seconds = 200;
    return token;
}

JsPublishTransportMetadata make_transport()
{
    JsPublishTransportMetadata transport;
    transport.secure_channel = true;
    transport.server_identity_verified = true;
    transport.server_audience = "server:main";
    transport.source_identifier = "transport:https";
    return transport;
}

JsPublishEndpointTransportContext make_context(
    unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishEndpointTransportContext context;
    context.request_id = "request:http";
    context.audit_id = "audit:http";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    context.zone = 30;
    context.builder_eligibility.ok = true;
    context.builder_eligibility.builder_account_id = "account:builder";
    context.builder_eligibility.eligible_character_name = "builderone";
    context.builder_eligibility.eligible_character_id = 1001;
    context.builder_eligibility.eligible_character_level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    context.target_zone_resolved = true;
    context.server_resolved_target_zone = 30;
    context.server_resolved_target_host = JsScriptPackageHost::Character;
    context.zone_exists = true;
    context.zone_owner_character_ids = { 1001 };
    context.token = make_token(scopes);
    context.transport = make_transport();
    context.now_epoch_seconds = 100;
    context.allow_mutating_operations = true;
    context.allow_live_pointer_update = true;
    context.applied_at_epoch_seconds = 200;
    context.expected_server_audience = "server:main";
    context.expected_workspace_id = "workspace:main";
    context.current_live_checksum = "live:old";
    return context;
}

JsPublishEndpointServiceOptions service_options()
{
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode =
        JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

JsPublishHttpEndpointOptions prederived_context_options()
{
    JsPublishHttpEndpointOptions options;
    options.allow_prederived_context_for_tests = true;
    return options;
}

JsBuilderSessionLoginResult make_session_login(unsigned scopes)
{
    JsBuilderSessionLoginResult login;
    login.ok = true;
    login.session_token = "session-token";
    login.account_id = "account:builder";
    login.expires_at_epoch_seconds = 200;
    login.token_metadata.claims_verified = true;
    login.token_metadata.token_id = js_builder_session_token_id(login.session_token);
    login.token_metadata.actor_id = "builderone";
    login.token_metadata.builder_account_id = "account:builder";
    login.token_metadata.scopes = scopes;
    login.token_metadata.issued_at_epoch_seconds = 90;
    login.token_metadata.expires_at_epoch_seconds = 200;
    login.builder_eligibility.ok = true;
    login.builder_eligibility.builder_account_id = "account:builder";
    login.builder_eligibility.eligible_character_name = "builderone";
    login.builder_eligibility.eligible_character_id = 1001;
    login.builder_eligibility.eligible_character_level =
        JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    return login;
}

JsBuilderSessionStoreOptions session_store_options(long long now = 100)
{
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = now;
    options.server_audience = "server:main";
    options.workspace_id = "workspace:main";
    return options;
}

JsPublishHttpEndpointOptions session_options(const JsBuilderSessionStore &store)
{
    JsPublishHttpEndpointOptions options;
    options.session_store = &store;
    options.session_store_options = session_store_options();
    return options;
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

JsPublishHttpEndpointRequest request(
    const std::string &operation, const std::string &body)
{
    JsPublishHttpEndpointRequest request;
    request.method = "POST";
    request.path = "/api/js-scripts/" + operation;
    request.content_type = "application/json; charset=utf-8";
    request.body = body;
    request.bearer_token = "session-token";
    request.trusted_proxy = true;
    return request;
}

} // namespace

TEST(JsPublishHttpEndpoint, DispatchesBuilderClientStageBodyThroughRouteOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":" + package_json(make_package())
                + "}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), prederived_context_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("stage.accepted", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("\"auditId\":\"audit:http\""));
}

TEST(JsPublishHttpEndpoint, DispatchesStatusBodyThroughRouteOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":" + package_json(make_package())
                + "}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), prederived_context_options()).ok);

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service, request("status", "{\"packageId\":\"js:30:character:3001\"}"),
        make_context(JS_PUBLISH_SCOPE_STATUS_READ), prederived_context_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("status.current", result.reason_code);
}

TEST(JsPublishHttpEndpoint, DispatchesActivateAndRollbackThroughRouteOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":" + package_json(make_package())
                + "}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), prederived_context_options()).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);

    JsPublishEndpointTransportResult activate = js_publish_http_endpoint_dispatch(
        service,
        request("activate",
            "{\"packageId\":\"js:30:character:3001\",\"stagedDigest\":"
                + quote(status.status.staged_digest)
                + ",\"baseLiveChecksum\":\"live:old\"}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE), prederived_context_options());
    EXPECT_TRUE(activate.ok);
    EXPECT_EQ(200, activate.http_status);
    EXPECT_EQ("activate.accepted", activate.reason_code);

    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    const std::string first_live_checksum = pointer.pointer.current_live_checksum;
    ASSERT_TRUE(js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":" + quote(first_live_checksum)
                + ",\"package\":" + package_json(make_package("return false"))
                + "}"),
        [&] {
            JsPublishEndpointTransportContext context =
                make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
            context.current_live_checksum = first_live_checksum;
            return context;
        }(),
        prederived_context_options())
                    .ok);
    status = js_publish_latest_staged_package_status(service.staged_repository(),
        "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext second_activate_context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    second_activate_context.current_live_checksum = first_live_checksum;
    ASSERT_TRUE(js_publish_http_endpoint_dispatch(
        service,
        request("activate",
            "{\"packageId\":\"js:30:character:3001\",\"stagedDigest\":"
                + quote(status.status.staged_digest)
                + ",\"baseLiveChecksum\":" + quote(first_live_checksum) + "}"),
        second_activate_context, prederived_context_options()).ok);
    pointer = live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    JsPublishEndpointTransportContext rollback_context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    rollback_context.current_live_checksum = pointer.pointer.current_live_checksum;
    JsPublishEndpointTransportResult rollback = js_publish_http_endpoint_dispatch(
        service,
        request("rollback",
            "{\"packageId\":\"js:30:character:3001\","
            "\"targetLiveChecksum\":"
                + quote(pointer.pointer.current_live_checksum)
                + ",\"reason\":\"builder rollback\"}"),
        rollback_context, prederived_context_options());

    EXPECT_TRUE(rollback.ok);
    EXPECT_EQ(200, rollback.http_status);
    EXPECT_EQ("rollback.accepted", rollback.reason_code);
}

TEST(JsPublishHttpEndpoint, RejectsOperationSmugglingInBody)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service,
        request("status",
            "{\"operation\":\"stage\",\"packageId\":\"js:30:character:3001\"}"),
        make_context(JS_PUBLISH_SCOPE_STATUS_READ), prederived_context_options());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("stage"));
}

TEST(JsPublishHttpEndpoint, RejectsUnsupportedMethodPathAndMediaType)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishHttpEndpointRequest method = request("status", "{}");
    method.method = "GET";
    JsPublishHttpEndpointRequest path = request("garbage", "{}");
    JsPublishHttpEndpointRequest media = request("status", "{}");
    media.content_type = "text/plain";

    JsPublishEndpointTransportResult method_result =
        js_publish_http_endpoint_dispatch(service, method, make_context(),
            prederived_context_options());
    JsPublishEndpointTransportResult path_result =
        js_publish_http_endpoint_dispatch(service, path, make_context(),
            prederived_context_options());
    JsPublishEndpointTransportResult media_result =
        js_publish_http_endpoint_dispatch(service, media, make_context(),
            prederived_context_options());

    EXPECT_FALSE(method_result.ok);
    EXPECT_EQ(405, method_result.http_status);
    EXPECT_EQ("publish.method-not-allowed", method_result.reason_code);
    EXPECT_FALSE(path_result.ok);
    EXPECT_EQ(404, path_result.http_status);
    EXPECT_EQ("publish.not-found", path_result.reason_code);
    EXPECT_FALSE(media_result.ok);
    EXPECT_EQ(415, media_result.http_status);
    EXPECT_EQ("publish.unsupported-media-type", media_result.reason_code);
}

TEST(JsPublishHttpEndpoint, RejectsProjectedEnvelopeOverSizeLimit)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishHttpEndpointOptions options;
    options.allow_prederived_context_for_tests = true;
    options.transport_options.maximum_request_bytes = 48;

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service, request("status", "{\"packageId\":\"js:30:character:3001\"}"),
        make_context(JS_PUBLISH_SCOPE_STATUS_READ), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(413, result.http_status);
    EXPECT_EQ("publish.request-too-large", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
}

TEST(JsPublishHttpEndpoint, KeepsServerSuppliedContextAuthoritative)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"requestId\":\"client-controlled\","
            "\"baseLiveChecksum\":\"live:old\",\"package\":"
                + package_json(make_package()) + "}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), prederived_context_options());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("client-controlled"));
}

TEST(JsPublishHttpEndpoint, DerivesPublishContextFromStoreBackedBearerToken)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);

    JsPublishEndpointTransportContext base_context;
    base_context.request_id = "request:http";
    base_context.audit_id = "audit:http";
    base_context.zone = 30;
    base_context.target_zone_resolved = true;
    base_context.server_resolved_target_zone = 30;
    base_context.server_resolved_target_host = JsScriptPackageHost::Character;
    base_context.zone_exists = true;
    base_context.zone_owner_character_ids = { 1001 };
    base_context.transport = make_transport();
    base_context.allow_mutating_operations = true;
    base_context.allow_live_pointer_update = true;
    base_context.applied_at_epoch_seconds = 200;
    base_context.current_live_checksum = "live:old";
    base_context.actor_id = "base-actor";
    base_context.builder_account_id = "base-account";
    base_context.builder_eligibility.ok = false;
    base_context.token = make_token(0);

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":"
                + package_json(make_package()) + "}"),
        base_context, session_options(store));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("stage.accepted", result.reason_code);
}

TEST(JsPublishHttpEndpoint, UsesStoreScopesInsteadOfPrederivedBaseContext)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_STATUS_READ), session_store_options())
                    .ok);

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service,
        request("stage",
            "{\"baseLiveChecksum\":\"live:old\",\"package\":"
                + package_json(make_package()) + "}"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_options(store));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_FALSE(js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001")
                     .ok);
}

TEST(JsPublishHttpEndpoint, RejectsMissingStoreUntrustedAndRejectedSessions)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishHttpEndpointRequest status =
        request("status", "{\"packageId\":\"js:30:character:3001\"}");

    JsPublishEndpointTransportResult missing_store =
        js_publish_http_endpoint_dispatch(service, status, make_context());
    EXPECT_FALSE(missing_store.ok);
    EXPECT_EQ(503, missing_store.http_status);
    EXPECT_EQ("publish.session-store-unavailable", missing_store.reason_code);

    JsPublishHttpEndpointRequest malformed_missing_store = status;
    malformed_missing_store.method = "GET";
    malformed_missing_store.path = "/api/js-scripts/status?debug=true";
    malformed_missing_store.content_type = "text/plain";
    malformed_missing_store.body = "";
    JsPublishEndpointTransportResult malformed_missing_store_result =
        js_publish_http_endpoint_dispatch(
            service, malformed_missing_store, make_context());
    EXPECT_FALSE(malformed_missing_store_result.ok);
    EXPECT_EQ(503, malformed_missing_store_result.http_status);
    EXPECT_EQ("publish.session-store-unavailable",
        malformed_missing_store_result.reason_code);

    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_STATUS_READ), session_store_options())
                    .ok);
    JsPublishHttpEndpointRequest untrusted = status;
    untrusted.trusted_proxy = false;
    JsPublishEndpointTransportResult untrusted_result =
        js_publish_http_endpoint_dispatch(
            service, untrusted, make_context(), session_options(store));
    EXPECT_FALSE(untrusted_result.ok);
    EXPECT_EQ(403, untrusted_result.http_status);
    EXPECT_EQ("publish.untrusted", untrusted_result.reason_code);

    JsPublishHttpEndpointRequest malformed_untrusted = status;
    malformed_untrusted.method = "GET";
    malformed_untrusted.path = "/api/js-scripts/status?debug=true";
    malformed_untrusted.content_type = "text/plain";
    malformed_untrusted.body = "";
    malformed_untrusted.trusted_proxy = false;
    JsPublishEndpointTransportResult malformed_untrusted_result =
        js_publish_http_endpoint_dispatch(
            service, malformed_untrusted, make_context(), session_options(store));
    EXPECT_FALSE(malformed_untrusted_result.ok);
    EXPECT_EQ(403, malformed_untrusted_result.http_status);
    EXPECT_EQ("publish.untrusted", malformed_untrusted_result.reason_code);

    JsPublishHttpEndpointRequest unknown = status;
    unknown.bearer_token = "other-session-token";
    JsPublishEndpointTransportResult unknown_result =
        js_publish_http_endpoint_dispatch(
            service, unknown, make_context(), session_options(store));
    EXPECT_FALSE(unknown_result.ok);
    EXPECT_EQ(401, unknown_result.http_status);
    EXPECT_EQ("publish.session-rejected", unknown_result.reason_code);
    EXPECT_EQ(std::string::npos, unknown_result.json.find("other-session-token"));
}

TEST(JsPublishHttpEndpoint, RejectsUnsafeBearerTokensWithoutDispatch)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);

    const std::string body = "{\"baseLiveChecksum\":\"live:old\",\"package\":"
        + package_json(make_package()) + "}";
    for (const std::string token : { std::string(), std::string(" "),
             std::string("bad\n token"), std::string(513, 'x') }) {
        JsPublishHttpEndpointRequest stage = request("stage", body);
        stage.bearer_token = token;
        JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
            service, stage, make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE),
            session_options(store));
        EXPECT_FALSE(result.ok) << token;
        EXPECT_EQ(401, result.http_status) << token;
        EXPECT_EQ("publish.session-rejected", result.reason_code) << token;
        if (token.find("bad") != std::string::npos)
            EXPECT_EQ(std::string::npos, result.json.find(token)) << token;
        EXPECT_FALSE(js_publish_latest_staged_package_status(
            service.staged_repository(), "js:30:character:3001")
                         .ok);
    }
}

TEST(JsPublishHttpEndpoint, RejectsInvalidSessionStatesWithoutLeakingReason)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    const std::string body = "{\"baseLiveChecksum\":\"live:old\",\"package\":"
        + package_json(make_package()) + "}";

    JsBuilderSessionStore expired_store;
    ASSERT_TRUE(expired_store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);
    JsPublishHttpEndpointOptions expired_options = session_options(expired_store);
    expired_options.session_store_options = session_store_options(200);

    JsPublishEndpointTransportResult expired = js_publish_http_endpoint_dispatch(
        service, request("stage", body), make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE),
        expired_options);
    EXPECT_FALSE(expired.ok);
    EXPECT_EQ(401, expired.http_status);
    EXPECT_EQ("publish.session-rejected", expired.reason_code);
    EXPECT_EQ(std::string::npos, expired.json.find("expired"));

    JsBuilderSessionStore revoked_store;
    ASSERT_TRUE(revoked_store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);
    ASSERT_TRUE(revoked_store.revoke("session-token", session_store_options()).ok);
    JsPublishEndpointTransportResult revoked = js_publish_http_endpoint_dispatch(
        service, request("stage", body), make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE),
        session_options(revoked_store));
    EXPECT_FALSE(revoked.ok);
    EXPECT_EQ(401, revoked.http_status);
    EXPECT_EQ("publish.session-rejected", revoked.reason_code);
    EXPECT_EQ(std::string::npos, revoked.json.find("revoked"));

    JsBuilderSessionStore wrong_audience_store;
    ASSERT_TRUE(wrong_audience_store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);
    JsPublishHttpEndpointOptions wrong_audience_options =
        session_options(wrong_audience_store);
    wrong_audience_options.session_store_options.server_audience = "server:other";
    JsPublishEndpointTransportResult wrong_audience =
        js_publish_http_endpoint_dispatch(service, request("stage", body),
            make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), wrong_audience_options);
    EXPECT_FALSE(wrong_audience.ok);
    EXPECT_EQ(401, wrong_audience.http_status);
    EXPECT_EQ("publish.session-rejected", wrong_audience.reason_code);
    EXPECT_EQ(std::string::npos, wrong_audience.json.find("server:other"));

    JsBuilderSessionStore blank_audience_store;
    ASSERT_TRUE(blank_audience_store.insert_login_result(
        make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE), session_store_options())
                    .ok);
    JsPublishHttpEndpointOptions blank_audience_options =
        session_options(blank_audience_store);
    blank_audience_options.session_store_options.server_audience = "";
    JsPublishEndpointTransportResult blank_audience =
        js_publish_http_endpoint_dispatch(service, request("stage", body),
            make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), blank_audience_options);
    EXPECT_FALSE(blank_audience.ok);
    EXPECT_EQ(401, blank_audience.http_status);
    EXPECT_EQ("publish.session-rejected", blank_audience.reason_code);

    EXPECT_FALSE(js_publish_latest_staged_package_status(
        service.staged_repository(), "js:30:character:3001")
                     .ok);
}
