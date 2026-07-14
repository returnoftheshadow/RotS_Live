#include "../js_publish_endpoint_transport.h"

#include "../json_utils.h"
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
    token.token_id = "token:transport";
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
    transport.source_identifier = "transport:tls";
    return transport;
}

JsPublishEndpointTransportContext make_context(unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishEndpointTransportContext context;
    context.request_id = "request:transport";
    context.audit_id = "audit:transport";
    context.actor_id = "actor:42";
    context.builder_account_id = "account:builder";
    context.zone = 30;
    context.token = make_token(scopes);
    context.transport = make_transport();
    context.now_epoch_seconds = 100;
    context.allow_mutating_operations = true;
    context.expected_server_audience = "server:main";
    context.expected_workspace_id = "workspace:main";
    context.current_live_checksum = "live:old";
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

std::string quote(const std::string &value)
{
    return "\"" + json_utils::escape_json_string(value) + "\"";
}

std::string package_json(const JsScriptPackage &package)
{
    return "{"
        "\"vnum\":" + std::to_string(package.vnum)
        + ",\"packageId\":" + quote(package.package_id)
        + ",\"host\":" + quote(js_script_package_host_name(package.host))
        + ",\"packageFormatVersion\":" + std::to_string(package.package_format_version)
        + ",\"manifestSchemaVersion\":" + std::to_string(package.manifest_schema_version)
        + ",\"triggerCatalogRevision\":" + std::to_string(package.trigger_catalog_revision)
        + ",\"manifestChecksum\":" + quote(package.manifest_checksum)
        + ",\"runtimeName\":" + quote(package.runtime_name)
        + ",\"runtimeVersion\":" + quote(package.runtime_version)
        + ",\"generatedTypingsVersion\":" + quote(package.generated_typings_version)
        + ",\"compiledJavaScriptChecksum\":" + quote(package.compiled_javascript_checksum)
        + ",\"compiledJavaScript\":" + quote(package.compiled_javascript)
        + ",\"triggerBindings\":[{\"kind\":"
        + quote(js_scripting_manifest_kind_name(JsScriptingManifestKind::LegacyScriptTrigger))
        + ",\"legacyValue\":" + std::to_string(ON_ENTER)
        + ",\"handlerName\":\"onEnter\"}]}";
}

std::string stage_json(const JsScriptPackage &package,
    const std::string &base_live_checksum = "live:old")
{
    return "{\"operation\":\"stage\",\"baseLiveChecksum\":"
        + quote(base_live_checksum) + ",\"package\":" + package_json(package) + "}";
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

TEST(JsPublishEndpointTransport, DispatchesStageWithServerSuppliedContext)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsScriptPackage package = make_package();

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, stage_json(package), make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("stage.accepted", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("\"packageId\":\"js:30:character:3001\""));
    EXPECT_NE(std::string::npos, result.json.find("\"auditId\":\"audit:transport\""));
    EXPECT_EQ(std::string::npos, result.json.find("compiledJavaScript"));
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsStagedPackageLookupResult lookup = service.staged_repository().find_by_version(
        status.status.package_id, status.status.package_version_id);
    ASSERT_TRUE(lookup.ok);
    EXPECT_EQ(30, lookup.record.identity.zone);
    EXPECT_EQ("account:builder", lookup.record.identity.builder_account_id);
    EXPECT_EQ("live:old", lookup.record.identity.base_live_checksum);
    EXPECT_EQ("server:main", lookup.record.identity.server_instance_id);
    EXPECT_EQ("request:transport", lookup.record.audit.request_id);
    EXPECT_EQ("actor:42", lookup.record.audit.actor_id);
    EXPECT_EQ("token:transport", lookup.record.audit.permission_snapshot_id);
    EXPECT_EQ("audit:transport", lookup.record.audit.audit_id);
    EXPECT_EQ("publish-preflight:accepted", lookup.record.audit.source_policy_decision);
    EXPECT_EQ(package.compiled_javascript_checksum,
        lookup.record.audit.validation_report_digest);
    EXPECT_EQ("transport:tls", lookup.record.audit.transport_source_identifier);
}

TEST(JsPublishEndpointTransport, StagePreflightConflictDoesNotWriteStagedPackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.current_live_checksum = "live:new";

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(409, result.http_status);
    EXPECT_EQ("stage.stale-live-checksum", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageAuthorizationFailureWithConflictDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(0);
    context.current_live_checksum = "live:new";

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
    EXPECT_EQ(std::string::npos, result.json.find("live:old"));
    EXPECT_EQ(std::string::npos, result.json.find("live:new"));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    EXPECT_TRUE(service.staged_repository().empty());
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
        service, "{\"operation\":\"activate\",\"packageId\":\"js:30:character:3001\"}",
        make_context());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.unsupported-operation", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsStageWithoutBaseLiveChecksum)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, "{\"operation\":\"stage\",\"package\":" + package_json(make_package()) + "}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("stage.invalid-request", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsMalformedBaseLiveChecksum)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult control = js_publish_endpoint_dispatch_json(
        service, stage_json(make_package(), "live:bad\nvalue"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));
    JsPublishEndpointTransportResult missing_prefix = js_publish_endpoint_dispatch_json(
        service, stage_json(make_package(), "sha256:abc"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));
    JsPublishEndpointTransportResult overlong = js_publish_endpoint_dispatch_json(
        service, stage_json(make_package(), "live:" + std::string(190, 'a')),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(control.ok);
    EXPECT_EQ(400, control.http_status);
    EXPECT_EQ("stage.invalid-request", control.reason_code);
    EXPECT_FALSE(missing_prefix.ok);
    EXPECT_EQ(400, missing_prefix.http_status);
    EXPECT_EQ("stage.invalid-request", missing_prefix.reason_code);
    EXPECT_FALSE(overlong.ok);
    EXPECT_EQ(400, overlong.http_status);
    EXPECT_EQ("stage.invalid-request", overlong.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, AllowsInitialStageWithCanonicalBaseLiveChecksum)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.current_live_checksum.clear();

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, stage_json(make_package(), "live:initial"), context);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    EXPECT_EQ("live:initial", status.status.base_live_checksum);
}

TEST(JsPublishEndpointTransport, RejectsStagePackageIdField)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:old\","
        "\"packageId\":\"js:30:character:3001\",\"package\":"
            + package_json(make_package()) + "}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("stage.invalid-request", result.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsMalformedStagePackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:old\","
        "\"package\":{\"packageId\":\"pkg\"}}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("publish.invalid-json", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("pkg"));
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
