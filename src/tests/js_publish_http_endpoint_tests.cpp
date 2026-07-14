#include "../js_publish_http_endpoint.h"

#include "../json_utils.h"
#include "../js_script_package_loader.h"
#include "../script.h"

#include <gtest/gtest.h>

namespace {

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
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

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
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);

    JsPublishEndpointTransportResult result = js_publish_http_endpoint_dispatch(
        service, request("status", "{\"packageId\":\"js:30:character:3001\"}"),
        make_context(JS_PUBLISH_SCOPE_STATUS_READ));

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
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
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
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));
    EXPECT_TRUE(activate.ok);
    EXPECT_EQ(200, activate.http_status);
    EXPECT_EQ("activate.accepted", activate.reason_code);

    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
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
        rollback_context);

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
        make_context(JS_PUBLISH_SCOPE_STATUS_READ));

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
        js_publish_http_endpoint_dispatch(service, method, make_context());
    JsPublishEndpointTransportResult path_result =
        js_publish_http_endpoint_dispatch(service, path, make_context());
    JsPublishEndpointTransportResult media_result =
        js_publish_http_endpoint_dispatch(service, media, make_context());

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
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("client-controlled"));
}
