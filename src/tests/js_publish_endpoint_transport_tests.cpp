#include "../js_publish_endpoint_transport.h"

#include "../json_utils.h"
#include "../script.h"
#include "../structs.h"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

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
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

std::string read_file(const std::string &path)
{
    std::ifstream input(path.c_str());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
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
    context.expected_server_instance_id = "server:main";
    context.current_live_checksum = "live:old";
    context.publish_audit_log_path = "build/js_publish_endpoint_transport_audit.jsonl";
    return context;
}

JsPublishEndpointTransportContext make_context_for_builder(unsigned scopes,
    const std::string &builder_account_id)
{
    JsPublishEndpointTransportContext context = make_context(scopes);
    context.builder_account_id = builder_account_id;
    context.builder_eligibility.builder_account_id = builder_account_id;
    context.token.builder_account_id = builder_account_id;
    return context;
}

JsBuilderSessionLoginResult make_session_login(unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
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

JsBuilderSessionStoreOptions make_session_store_options(long long now = 100)
{
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = now;
    options.server_audience = "server:main";
    options.workspace_id = "workspace:main";
    return options;
}

JsPublishEndpointServiceOptions service_options()
{
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

TEST(JsPublishEndpointTransport, SessionContextDerivesTokenAndBuilderFromStore)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_session_login(JS_PUBLISH_SCOPE_PACKAGE_STAGE),
        make_session_store_options())
                    .ok);
    JsPublishEndpointTransportContext base_context;
    base_context.transport = make_transport();
    base_context.zone = 30;

    JsPublishEndpointSessionContextResult result =
        js_publish_endpoint_context_from_builder_session(
            base_context, store, "session-token", make_session_store_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("builder.session.accepted", result.reason_code);
    EXPECT_EQ("builderone", result.context.actor_id);
    EXPECT_EQ("account:builder", result.context.builder_account_id);
    EXPECT_EQ("account:builder", result.context.builder_eligibility.builder_account_id);
    EXPECT_EQ(1001, result.context.builder_eligibility.eligible_character_id);
    EXPECT_EQ(JS_PUBLISH_SCOPE_PACKAGE_STAGE, result.context.token.scopes);
    EXPECT_EQ("server:main", result.context.expected_server_audience);
    EXPECT_EQ("workspace:main", result.context.expected_workspace_id);
    EXPECT_EQ(100, result.context.now_epoch_seconds);
    EXPECT_EQ(30, result.context.zone);
    EXPECT_TRUE(result.context.transport.secure_channel);
}

TEST(JsPublishEndpointTransport, SessionContextRejectsExpiredOrWrongAudienceTokens)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_session_login(), make_session_store_options()).ok);

    JsPublishEndpointSessionContextResult expired =
        js_publish_endpoint_context_from_builder_session(
            make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), store, "session-token",
            make_session_store_options(200));
    EXPECT_FALSE(expired.ok);
    EXPECT_EQ(JsBuilderSessionStoreReason::Expired, expired.session_reason);
    EXPECT_TRUE(expired.context.token.token_id.empty());
    EXPECT_FALSE(expired.context.builder_eligibility.ok);
    EXPECT_TRUE(expired.context.actor_id.empty());
    EXPECT_TRUE(expired.context.builder_account_id.empty());
    EXPECT_EQ(0, expired.context.now_epoch_seconds);
    EXPECT_TRUE(expired.context.expected_server_audience.empty());
    EXPECT_TRUE(expired.context.expected_workspace_id.empty());

    JsBuilderSessionStoreOptions wrong_audience = make_session_store_options();
    wrong_audience.server_audience = "server:other";
    JsPublishEndpointSessionContextResult wrong =
        js_publish_endpoint_context_from_builder_session(
            make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE), store, "session-token",
            wrong_audience);
    EXPECT_FALSE(wrong.ok);
    EXPECT_EQ(JsBuilderSessionStoreReason::NotFound, wrong.session_reason);
    EXPECT_TRUE(wrong.context.token.token_id.empty());
    EXPECT_FALSE(wrong.context.builder_eligibility.ok);
    EXPECT_TRUE(wrong.context.actor_id.empty());
    EXPECT_TRUE(wrong.context.builder_account_id.empty());
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

std::string activate_json(const std::string &package_id, const std::string &staged_digest,
    const std::string &base_live_checksum = "live:old")
{
    return "{\"operation\":\"activate\",\"packageId\":" + quote(package_id)
        + ",\"stagedDigest\":" + quote(staged_digest)
        + ",\"baseLiveChecksum\":" + quote(base_live_checksum) + "}";
}

std::string rollback_json(const std::string &package_id,
    const std::string &target_live_checksum = "live:old")
{
    return "{\"operation\":\"rollback\",\"packageId\":" + quote(package_id)
        + ",\"targetLiveChecksum\":" + quote(target_live_checksum)
        + ",\"reason\":\"restore previous script\"}";
}

std::string rollback_json_without_reason(const std::string &package_id,
    const std::string &target_live_checksum = "live:old")
{
    return "{\"operation\":\"rollback\",\"packageId\":" + quote(package_id)
        + ",\"targetLiveChecksum\":" + quote(target_live_checksum) + "}";
}

struct ActivatedPackage {
    JsPublishStagedPackageStatus status;
    std::string live_checksum;
};

ActivatedPackage activate_package_through_transport_as(JsPublishEndpointService &service,
    const JsScriptPackage &package, const std::string &base_live_checksum,
    const std::string &builder_account_id)
{
    JsPublishEndpointTransportContext stage_context =
        make_context_for_builder(JS_PUBLISH_SCOPE_PACKAGE_STAGE, builder_account_id);
    stage_context.current_live_checksum = base_live_checksum;
    EXPECT_TRUE(js_publish_endpoint_dispatch_json(service,
        stage_json(package, base_live_checksum), stage_context).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    EXPECT_TRUE(status.ok);

    JsPublishEndpointTransportContext activate_context =
        make_context_for_builder(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE, builder_account_id);
    activate_context.current_live_checksum = base_live_checksum;
    EXPECT_TRUE(js_publish_endpoint_dispatch_json(service,
        activate_json(status.status.package_id, status.status.staged_digest,
            base_live_checksum),
        activate_context)
                    .ok);
    JsLivePackagePointerResult pointer =
        service.live_store().find_live_pointer(status.status.package_id);
    EXPECT_TRUE(pointer.ok);
    return { status.status, pointer.pointer.current_live_checksum };
}

ActivatedPackage activate_package_through_transport(JsPublishEndpointService &service,
    const JsScriptPackage &package, const std::string &base_live_checksum)
{
    return activate_package_through_transport_as(
        service, package, base_live_checksum, "account:builder");
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
    EXPECT_EQ("js:30:character:3001", lookup.record.package.package_id);
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

TEST(JsPublishEndpointTransport, StageRequiresServerSuppliedBuilderEligibility)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.builder_eligibility = {};

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageBindsEligibilityToAuthenticatedBuilder)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.builder_eligibility.builder_account_id = "account:other";

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageBindsBuilderContextToVerifiedToken)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.builder_account_id = "account:intruder";
    context.builder_eligibility.builder_account_id = "account:intruder";

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("stage.invalid-request", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageRequiresServerResolvedZoneAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.server_resolved_target_zone = 31;

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageRequiresResolvedExistingZone)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext unresolved = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    unresolved.target_zone_resolved = false;
    JsPublishEndpointTransportContext missing_zone = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    missing_zone.zone_exists = false;

    JsPublishEndpointTransportResult unresolved_result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), unresolved);
    JsPublishEndpointTransportResult missing_zone_result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), missing_zone);

    EXPECT_FALSE(unresolved_result.ok);
    EXPECT_EQ(403, unresolved_result.http_status);
    EXPECT_EQ("stage.authorization-failed", unresolved_result.reason_code);
    EXPECT_FALSE(missing_zone_result.ok);
    EXPECT_EQ(403, missing_zone_result.http_status);
    EXPECT_EQ("stage.authorization-failed", missing_zone_result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageRequiresServerResolvedHostAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.server_resolved_target_host = JsScriptPackageHost::Object;

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageRejectsNonOwnerZoneAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.zone_owner_character_ids = { 2002, 3003 };

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("stage.authorization-failed", result.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointTransport, StageAllowsServerSuppliedAllBuildersAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    JsPublishEndpointTransportContext context = make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    context.zone_allows_all_builders = true;
    context.zone_owner_character_ids.clear();

    JsPublishEndpointTransportResult result =
        js_publish_endpoint_dispatch_json(service, stage_json(make_package()), context);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_FALSE(service.staged_repository().empty());
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

TEST(JsPublishEndpointTransport, ActivatesLatestStagedPackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("activate.accepted", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("\"auditId\":\"audit:transport\""));
    EXPECT_EQ(1u, live_store.live_pointer_count());
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer(status.status.package_id);
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(status.status.package_id, pointer.pointer.package_id);
    EXPECT_EQ(status.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_EQ(status.status.staged_digest, pointer.pointer.staged_digest);
    EXPECT_EQ("live:old", pointer.pointer.expected_previous_live_checksum);
    EXPECT_EQ("audit:transport", pointer.pointer.load_audit_id);
    EXPECT_EQ(200, pointer.pointer.loaded_at_epoch_seconds);
}

TEST(JsPublishEndpointTransport, ActivationAppendsAuditBeforeLiveWrite)
{
    const std::string audit_path = "build/js_publish_endpoint_transport_activation_audit.jsonl";
    std::remove(audit_path.c_str());
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.publish_audit_log_path = audit_path;

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest), context);

    ASSERT_TRUE(result.ok);
    const std::string audit = read_file(audit_path);
    EXPECT_NE(std::string::npos, audit.find("\"operation\":\"activate\""));
    EXPECT_NE(std::string::npos, audit.find("\"auditId\":\"audit:transport\""));
    EXPECT_NE(std::string::npos, audit.find("\"packageId\":\"js:30:character:3001\""));
    EXPECT_NE(std::string::npos, audit.find("\"expectedPreviousLiveChecksum\":\"live:old\""));
    EXPECT_NE(std::string::npos, audit.find("\"currentLiveChecksum\":\"live:sha256:v1:"));
}

TEST(JsPublishEndpointTransport, ActivateMissingAuditIdIsRejectedBeforeLiveWrite)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.audit_id.clear();

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("activate.rejected", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(status.status.package_id));
    EXPECT_EQ(std::string::npos, result.json.find(status.status.staged_digest));
    EXPECT_EQ(std::string::npos, result.json.find("live:old"));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateAuditAppendFailureDoesNotWriteLivePointer)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.publish_audit_log_path = "build/missing-publish-audit-dir/audit.jsonl";

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(500, result.http_status);
    EXPECT_EQ("activate.audit-precondition-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(status.status.package_id));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateWrongServerInstanceDoesNotAppendAuditOrWriteLivePointer)
{
    const std::string audit_path =
        "build/js_publish_endpoint_transport_wrong_server_audit.jsonl";
    std::remove(audit_path.c_str());
    JsLivePackageStore live_store;
    JsPublishEndpointServiceOptions options = service_options();
    options.server_instance_id = "server:other";
    JsPublishEndpointService service(live_store, options);
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.publish_audit_log_path = audit_path;

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("activate.staged-package-not-found", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(status.status.package_id));
    EXPECT_EQ(std::string::npos, result.json.find(status.status.staged_digest));
    EXPECT_EQ(std::string::npos, result.json.find("server:other"));
    EXPECT_EQ(std::string::npos, result.json.find("server:main"));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    EXPECT_EQ(0u, live_store.live_pointer_count());
    EXPECT_TRUE(read_file(audit_path).empty());
}

TEST(JsPublishEndpointTransport, ActivateRejectsDigestMismatchWithoutLiveWrite)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json("js:30:character:3001", "sha256:not-the-staged-digest"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("activate.authorization-failed", result.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateMissingScopeDoesNotProbeDigest)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json("js:30:character:3001", "sha256:not-the-staged-digest"),
        make_context(0));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("activate.missing-scope", result.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateStaleLiveConflictDoesNotWrite)
{
    const std::string audit_path = "build/js_publish_endpoint_transport_activate_stale_audit.jsonl";
    std::remove(audit_path.c_str());
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);

    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.publish_audit_log_path = audit_path;
    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest,
            "live:stale"),
        context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(409, result.http_status);
    EXPECT_EQ("activate.stale-live-checksum", result.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
    EXPECT_TRUE(read_file(audit_path).empty());
}

TEST(JsPublishEndpointTransport, ActivateAuthorizationFailureDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest),
        make_context(0));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("activate.missing-scope", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(status.status.package_id));
    EXPECT_EQ(std::string::npos, result.json.find(status.status.staged_digest));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateRequiresCurrentZoneAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.zone_owner_character_ids = { 2002 };

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest),
        context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("activate.authorization-failed", result.reason_code);
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, ActivateUnauthorizedCallerCannotProbeDigest)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(status.ok);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE);
    context.builder_account_id = "account:intruder";
    context.token.builder_account_id = "account:intruder";

    JsPublishEndpointTransportResult matched = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, status.status.staged_digest),
        context);
    JsPublishEndpointTransportResult mismatched = js_publish_endpoint_dispatch_json(
        service, activate_json(status.status.package_id, "sha256:not-the-staged-digest"),
        context);

    EXPECT_FALSE(matched.ok);
    EXPECT_EQ(403, matched.http_status);
    EXPECT_EQ("activate.authorization-failed", matched.reason_code);
    EXPECT_FALSE(mismatched.ok);
    EXPECT_EQ(matched.http_status, mismatched.http_status);
    EXPECT_EQ(matched.reason_code, mismatched.reason_code);
    EXPECT_EQ(matched.json, mismatched.json);
    EXPECT_EQ(std::string::npos, matched.json.find(status.status.package_id));
    EXPECT_EQ(std::string::npos, matched.json.find(status.status.staged_digest));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, RollbackRestoresMostRecentPriorLiveVersion)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("rollback.accepted", result.reason_code);
    EXPECT_EQ(1u, live_store.live_pointer_count());
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(first.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_EQ(first.status.staged_digest, pointer.pointer.staged_digest);
    EXPECT_EQ(second.live_checksum, pointer.pointer.expected_previous_live_checksum);
    EXPECT_EQ("audit:transport", pointer.pointer.load_audit_id);
    EXPECT_EQ(200, pointer.pointer.loaded_at_epoch_seconds);
}

TEST(JsPublishEndpointTransport, RollbackAppendsAuditBeforeLiveWrite)
{
    const std::string audit_path = "build/js_publish_endpoint_transport_rollback_audit.jsonl";
    std::remove(audit_path.c_str());
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.publish_audit_log_path = audit_path;

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);

    ASSERT_TRUE(result.ok);
    const std::string audit = read_file(audit_path);
    EXPECT_NE(std::string::npos, audit.find("\"operation\":\"rollback\""));
    EXPECT_NE(std::string::npos, audit.find("\"auditId\":\"audit:transport\""));
    EXPECT_NE(std::string::npos, audit.find("\"packageId\":\"js:30:character:3001\""));
    EXPECT_NE(std::string::npos,
              audit.find("\"packageVersionId\":\"" + first.status.package_version_id + "\""));
    EXPECT_NE(std::string::npos,
              audit.find("\"stagedDigest\":\"" + first.status.staged_digest + "\""));
    EXPECT_NE(std::string::npos,
              audit.find("\"expectedPreviousLiveChecksum\":\"" + second.live_checksum + "\""));
    EXPECT_NE(std::string::npos,
              audit.find("\"currentLiveChecksum\":\"" + first.live_checksum + "\""));
}

TEST(JsPublishEndpointTransport, RollbackIgnoresNeverLiveLatestStagedPackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext stage_context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE);
    stage_context.current_live_checksum = second.live_checksum;
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service,
        stage_json(make_package("return 'never-live'"), second.live_checksum),
        stage_context)
                    .ok);
    JsPublishStagedPackageStatusResult never_live =
        js_publish_latest_staged_package_status(service.staged_repository(),
            "js:30:character:3001");
    ASSERT_TRUE(never_live.ok);
    ASSERT_NE(first.status.package_version_id, never_live.status.package_version_id);
    ASSERT_NE(second.status.package_version_id, never_live.status.package_version_id);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("rollback.accepted", result.reason_code);
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(first.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_NE(never_live.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackStaleLiveConflictDoesNotWrite)
{
    const std::string audit_path = "build/js_publish_endpoint_transport_rollback_stale_audit.jsonl";
    std::remove(audit_path.c_str());
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);

    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.publish_audit_log_path = audit_path;
    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", "live:stale"),
        context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(409, result.http_status);
    EXPECT_EQ("rollback.stale-live-checksum", result.reason_code);
    EXPECT_NE(std::string::npos, result.json.find("js:30:character:3001"));
    EXPECT_NE(std::string::npos, result.json.find(second.live_checksum));
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(second.status.package_version_id, pointer.pointer.package_version_id);
    EXPECT_TRUE(read_file(audit_path).empty());
}

TEST(JsPublishEndpointTransport, RollbackMissingAuditIdIsRejectedBeforeLiveWrite)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.audit_id.clear();

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(400, result.http_status);
    EXPECT_EQ("rollback.rejected", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
    EXPECT_EQ(std::string::npos, result.json.find(first.status.staged_digest));
    EXPECT_EQ(std::string::npos, result.json.find(second.live_checksum));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(second.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackAuditAppendFailureDoesNotWriteLivePointer)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.publish_audit_log_path = "build/missing-publish-audit-dir/audit.jsonl";

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(500, result.http_status);
    EXPECT_EQ("rollback.audit-precondition-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(second.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackAllowsMissingReason)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json_without_reason("js:30:character:3001", second.live_checksum),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("rollback.accepted", result.reason_code);
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(first.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackLookupMissDoesNotRevealPackageExistence)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001"),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("rollback.authorization-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, RollbackUnauthorizedCallerCannotProbePackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.builder_account_id = "account:intruder";
    context.token.builder_account_id = "account:intruder";

    JsPublishEndpointTransportResult matched = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);
    JsPublishEndpointTransportResult missing = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:9999"), context);

    EXPECT_FALSE(matched.ok);
    EXPECT_EQ(403, matched.http_status);
    EXPECT_EQ("rollback.authorization-failed", matched.reason_code);
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(matched.http_status, missing.http_status);
    EXPECT_EQ(matched.reason_code, missing.reason_code);
    EXPECT_EQ(matched.json, missing.json);
    EXPECT_EQ(std::string::npos, matched.json.find("js:30:character:3001"));
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(second.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackOwnRejectsPriorVersionOwnedByAnotherBuilder)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage foreign_prior = activate_package_through_transport_as(
        service, make_package("return 'foreign'"), "live:old", "account:other-builder");
    ActivatedPackage current = activate_package_through_transport(
        service, make_package("return 'current'"), foreign_prior.live_checksum);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", current.live_checksum),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("rollback.authorization-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find(foreign_prior.status.package_version_id));
    EXPECT_EQ(std::string::npos, result.json.find(foreign_prior.status.staged_digest));
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(current.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackAuthorizationFailureDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ASSERT_TRUE(js_publish_endpoint_dispatch_json(service, stage_json(make_package()),
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE)).ok);

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", "live:stale"), make_context(0));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("rollback.missing-scope", result.reason_code);
    EXPECT_EQ(std::string::npos, result.json.find("js:30:character:3001"));
    EXPECT_EQ(std::string::npos, result.json.find("live:stale"));
    EXPECT_EQ(std::string::npos, result.json.find("audit:transport"));
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, RollbackRequiresCurrentZoneAuthority)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN);
    context.zone_owner_character_ids = { 2002 };

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("rollback.authorization-failed", result.reason_code);
    JsLivePackagePointerResult pointer =
        live_store.find_live_pointer("js:30:character:3001");
    ASSERT_TRUE(pointer.ok);
    EXPECT_EQ(second.status.package_version_id, pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointTransport, RollbackAnyUsesServerPolicy)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());
    ActivatedPackage first = activate_package_through_transport(
        service, make_package("return true"), "live:old");
    ActivatedPackage second = activate_package_through_transport(
        service, make_package("return false"), first.live_checksum);
    JsPublishEndpointTransportContext context =
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_ANY);
    context.allow_rollback_any = true;
    context.builder_account_id = "account:admin";
    context.token.builder_account_id = "account:admin";
    context.builder_eligibility.builder_account_id = "account:admin";
    context.builder_eligibility.eligible_character_id = 9001;
    context.builder_eligibility.eligible_character_level = LEVEL_AREAGOD;
    context.zone_owner_character_ids.clear();

    JsPublishEndpointTransportResult result = js_publish_endpoint_dispatch_json(
        service, rollback_json("js:30:character:3001", second.live_checksum), context);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ("rollback.accepted", result.reason_code);
    EXPECT_EQ(1u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointTransport, RejectsMalformedActivateAndRollbackRequests)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult activate = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"activate\",\"packageId\":\"js:30:character:3001\","
        "\"stagedDigest\":\"bad\",\"baseLiveChecksum\":\"live:old\"}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));
    JsPublishEndpointTransportResult rollback = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"rollback\",\"packageId\":\"js:30:character:3001\","
        "\"targetLiveChecksum\":\"bad\",\"reason\":\"x\"}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_FALSE(activate.ok);
    EXPECT_EQ(400, activate.http_status);
    EXPECT_EQ("activate.invalid-request", activate.reason_code);
    EXPECT_FALSE(rollback.ok);
    EXPECT_EQ(400, rollback.http_status);
    EXPECT_EQ("rollback.invalid-request", rollback.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsClientControlledActivationOptions)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult activate = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"activate\",\"packageId\":\"js:30:character:3001\","
        "\"stagedDigest\":\"sha256:abc\",\"baseLiveChecksum\":\"live:old\","
        "\"allowLivePointerUpdate\":true}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE));
    JsPublishEndpointTransportResult rollback = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"rollback\",\"packageId\":\"js:30:character:3001\","
        "\"targetLiveChecksum\":\"live:old\",\"allowRollbackAny\":true}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_FALSE(activate.ok);
    EXPECT_EQ(400, activate.http_status);
    EXPECT_EQ("publish.invalid-json", activate.reason_code);
    EXPECT_FALSE(rollback.ok);
    EXPECT_EQ(400, rollback.http_status);
    EXPECT_EQ("publish.invalid-json", rollback.reason_code);
}

TEST(JsPublishEndpointTransport, RejectsMalformedRollbackReason)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult control = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"rollback\",\"packageId\":\"js:30:character:3001\","
        "\"targetLiveChecksum\":\"live:old\",\"reason\":\"bad\\nreason\"}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));
    JsPublishEndpointTransportResult overlong = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"rollback\",\"packageId\":\"js:30:character:3001\","
        "\"targetLiveChecksum\":\"live:old\",\"reason\":"
            + quote(std::string(260, 'a')) + "}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN));

    EXPECT_FALSE(control.ok);
    EXPECT_EQ(400, control.http_status);
    EXPECT_EQ("rollback.invalid-request", control.reason_code);
    EXPECT_FALSE(overlong.ok);
    EXPECT_EQ(400, overlong.http_status);
    EXPECT_EQ("rollback.invalid-request", overlong.reason_code);
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

TEST(JsPublishEndpointTransport, RejectsCrossOperationFields)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, service_options());

    JsPublishEndpointTransportResult status = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"status\",\"packageId\":\"js:30:character:3001\","
        "\"stagedDigest\":\"sha256:abc\"}",
        make_context());
    JsPublishEndpointTransportResult stage = js_publish_endpoint_dispatch_json(
        service,
        "{\"operation\":\"stage\",\"baseLiveChecksum\":\"live:old\","
        "\"targetLiveChecksum\":\"live:old\",\"package\":" + package_json(make_package()) + "}",
        make_context(JS_PUBLISH_SCOPE_PACKAGE_STAGE));

    EXPECT_FALSE(status.ok);
    EXPECT_EQ(400, status.http_status);
    EXPECT_EQ("status.invalid-request", status.reason_code);
    EXPECT_FALSE(stage.ok);
    EXPECT_EQ(400, stage.http_status);
    EXPECT_EQ("stage.invalid-request", stage.reason_code);
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
        service, "{\"operation\":\"garbageCollect\",\"packageId\":\"js:30:character:3001\"}",
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
