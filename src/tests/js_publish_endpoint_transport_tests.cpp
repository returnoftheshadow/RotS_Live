#include "../js_publish_endpoint_transport.h"

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
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsPublishTokenMetadata make_token(unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishTokenMetadata token;
    token.claims_verified = true;
    token.token_id = "token:status";
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
    return transport;
}

JsPublishEndpointTransportContext make_context(unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishEndpointTransportContext context;
    context.request_id = "request:transport";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    context.token = make_token(scopes);
    context.transport = make_transport();
    context.now_epoch_seconds = 100;
    context.expected_server_audience = "server:main";
    context.expected_workspace_id = "workspace:main";
    return context;
}

JsPublishEndpointServiceOptions service_options()
{
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

JsStagedPackageStageOptions stage_options()
{
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 30;
    options.identity_options.builder_account_id = "account:builder";
    options.identity_options.base_live_checksum = "live:old";
    options.identity_options.server_instance_id = "server:main";
    options.audit.staged_at_epoch_seconds = 100;
    options.audit.request_id = "request:stage";
    options.audit.actor_id = "actor:42";
    options.audit.permission_snapshot_id = "token:stage";
    options.audit.audit_id = "audit:stage";
    options.audit.source_policy_decision = "publish-preflight:accepted";
    options.audit.validation_report_digest = "validation:sha256:abc";
    options.audit.transport_source_identifier = "transport:tls";
    return options;
}

JsPublishEndpointStageInput stage_input(const JsScriptPackage &package)
{
    JsPublishEndpointStageInput input;
    input.stage_options = stage_options();
    input.audit_id = "audit:stage";
    input.authorization_request.operation = JsPublishOperation::PackageStage;
    input.authorization_request.request_id = "request:stage";
    input.authorization_request.actor_id = "actor:42";
    input.authorization_request.builder_account_id = "account:builder";
    input.authorization_request.zone = 30;
    input.authorization_request.vnum = package.vnum;
    input.authorization_request.host = package.host;
    input.authorization_request.package_id = package.package_id;
    input.authorization_request.base_live_checksum = "live:old";
    input.authorization_request.manifest_checksum = package.manifest_checksum;
    input.authorization_request.has_package = true;
    input.authorization_request.package = package;
    input.authorization_request.token = make_token(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    input.authorization_request.transport = make_transport();
    input.authorization_options.now_epoch_seconds = 100;
    input.authorization_options.allow_mutating_operations = true;
    input.authorization_options.expected_server_audience = "server:main";
    input.authorization_options.expected_workspace_id = "workspace:main";
    input.authorization_options.current_live_checksum = "live:old";
    input.authorization_options.authority.has_package_authority = true;
    input.authorization_options.authority.zone = 30;
    input.authorization_options.authority.vnum = package.vnum;
    input.authorization_options.authority.host = package.host;
    input.authorization_options.authority.package_id = package.package_id;
    input.authorization_options.authority.package_owner_builder_account_id = "account:builder";
    return input;
}

std::string status_json(const std::string &package_id)
{
    return "{\"operation\":\"status\",\"packageId\":\"" + package_id + "\"}";
}

} // namespace

TEST(JsPublishEndpointTransport, DispatchesStatusWithServerSuppliedContext)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointServiceResult staged = service.stage(stage_input(make_package()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, status_json(staged.response.package_id), make_context());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("status.current", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("\"packageId\":\"" + staged.response.package_id));
    EXPECT_EQ(std::string::npos, result.json.find("\"body\""));
}

TEST(JsPublishEndpointTransport, RejectsMalformedJson)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, "{\"operation\":", make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("packageId"));
}

TEST(JsPublishEndpointTransport, RejectsDuplicateRoutingFields)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult duplicate_operation = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"status\",\"operation\":\"stage\","
        "\"packageId\":\"js:30:character:3001\"}",
        make_context());
    JsPublishEndpointTransportResult duplicate_package_id = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"status\",\"packageId\":\"js:30:character:3001\","
        "\"packageId\":\"js:30:object:3001\"}",
        make_context());

    EXPECT_FALSE(duplicate_operation.ok);
    EXPECT_EQ(400, duplicate_operation.http_status);
    EXPECT_EQ("publish.invalid-json", duplicate_operation.reason_code);
    EXPECT_EQ(std::string::npos, duplicate_operation.json.find("js:30:character:3001"));
    EXPECT_FALSE(duplicate_package_id.ok);
    EXPECT_EQ(400, duplicate_package_id.http_status);
    EXPECT_EQ("publish.invalid-json", duplicate_package_id.reason_code);
    EXPECT_EQ(std::string::npos, duplicate_package_id.json.find("js:30:object:3001"));
}

TEST(JsPublishEndpointTransport, RejectsOversizedRequestBeforeParsing)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportOptions options;
    options.maximum_request_bytes = 8;

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, status_json("js:30:character:3001"),
            make_context(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(413, result.http_status);
    EXPECT_EQ("publish.request-too-large", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsUnsupportedOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, "{\"operation\":\"stage\",\"packageId\":\"js:30:character:3001\"}",
        make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.unsupported-operation", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsInvalidPackageId)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, status_json("../world"), make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("status.invalid-request", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("../world"));
}

TEST(JsPublishEndpointTransport, RejectsOutOfRangePackageId)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, status_json("js:99999999999999999999:character:3001"), make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("status.invalid-request", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsNonCanonicalNumericPackageId)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, status_json("js:+30:character:3001"), make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("status.invalid-request", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsLeadingZeroPackageIdSegments)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult zone = js_publish_endpoint_dispatch_json(
        service, status_json("js:030:character:3001"), make_context());
    JsPublishEndpointTransportResult vnum = js_publish_endpoint_dispatch_json(
        service, status_json("js:30:character:03001"), make_context());

    EXPECT_FALSE(zone.ok);
    EXPECT_EQ(400, zone.http_status);
    EXPECT_EQ("status.invalid-request", zone.reason_code);
    EXPECT_FALSE(vnum.ok);
    EXPECT_EQ(400, vnum.http_status);
    EXPECT_EQ("status.invalid-request", vnum.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsClientControlledRequestId)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"status\",\"packageId\":\"js:30:character:3001\","
        "\"requestId\":\"client:forged\"}",
        make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
}

TEST(JsPublishEndpointTransport, StatusAuthorizationFailureDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointServiceResult staged = service.stage(stage_input(make_package()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, status_json(staged.response.package_id), make_context(0));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("status.authorization-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(staged.response.package_id));
    EXPECT_EQ(std::string::npos, result.json.find(staged.response.package_version_id));
}
