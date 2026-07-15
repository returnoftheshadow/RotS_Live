#include "../js_publish_endpoint_service.h"

#include "../script.h"

#include <gtest/gtest.h>

namespace {

JsScriptPackage make_package(int zone = 30, int vnum = 3001,
    const std::string &body = "return true")
{
    const JsScriptingManifestMetadata &metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "client-pkg-" + std::to_string(zone) + "-" + std::to_string(vnum);
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

JsPublishTokenMetadata make_token(JsPublishOperation operation,
    const std::string &builder = "account:builder")
{
    JsPublishTokenMetadata token;
    token.claims_verified = true;
    token.token_id = "token:stage";
    token.actor_id = "actor:42";
    token.builder_account_id = builder;
    token.server_audience = "server:main";
    token.workspace_id = "workspace:main";
    token.scopes = js_publish_scope_for_operation(operation);
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

JsStagedPackageStageOptions make_stage_options(const std::string &base_live = "live:old")
{
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

JsPublishEndpointStageInput make_stage_input(const JsScriptPackage &package,
    const JsStagedPackageStageOptions &stage_options, const std::string &current_live = "live:old")
{
    JsPublishEndpointStageInput input;
    input.stage_options = stage_options;
    input.audit_id = "audit:stage-preflight";

    input.authorization_request.operation = JsPublishOperation::PackageStage;
    input.authorization_request.request_id = "request:stage-preflight";
    input.authorization_request.actor_id = "actor:42";
    input.authorization_request.builder_account_id =
        stage_options.identity_options.builder_account_id;
    input.authorization_request.zone = stage_options.identity_options.zone;
    input.authorization_request.vnum = package.vnum;
    input.authorization_request.host = package.host;
    input.authorization_request.package_id = package.package_id;
    input.authorization_request.base_live_checksum =
        stage_options.identity_options.base_live_checksum;
    input.authorization_request.manifest_checksum = package.manifest_checksum;
    input.authorization_request.has_package = true;
    input.authorization_request.package = package;
    input.authorization_request.token =
        make_token(JsPublishOperation::PackageStage,
            stage_options.identity_options.builder_account_id);
    input.authorization_request.transport = make_transport();

    input.authorization_options.now_epoch_seconds = 100;
    input.authorization_options.allow_mutating_operations = true;
    input.authorization_options.expected_server_audience = "server:main";
    input.authorization_options.expected_workspace_id = "workspace:main";
    input.authorization_options.current_live_checksum = current_live;
    input.authorization_options.authority.has_package_authority = true;
    input.authorization_options.authority.zone = stage_options.identity_options.zone;
    input.authorization_options.authority.vnum = package.vnum;
    input.authorization_options.authority.host = package.host;
    input.authorization_options.authority.package_id = package.package_id;
    input.authorization_options.authority.package_owner_builder_account_id =
        stage_options.identity_options.builder_account_id;
    return input;
}

JsPublishEndpointServiceOptions internal_validation_options()
{
    JsPublishEndpointServiceOptions options;
    options.package_validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    options.server_instance_id = "server:main";
    return options;
}

JsPublishEndpointStatusInput make_status_input(const std::string &package_id,
    unsigned scopes = JS_PUBLISH_SCOPE_STATUS_READ)
{
    JsPublishEndpointStatusInput input;
    input.package_id = package_id;
    input.authorization_request.operation = JsPublishOperation::StatusRead;
    input.authorization_request.request_id = "request:status";
    input.authorization_request.actor_id = "actor:42";
    input.authorization_request.builder_account_id = "account:builder";
    input.authorization_request.zone = 30;
    input.authorization_request.vnum = 3001;
    input.authorization_request.host = JsScriptPackageHost::Character;
    input.authorization_request.package_id = package_id;
    input.authorization_request.token = make_token(JsPublishOperation::StatusRead);
    input.authorization_request.token.scopes = scopes;
    input.authorization_request.transport = make_transport();
    input.authorization_options.now_epoch_seconds = 100;
    input.authorization_options.expected_server_audience = "server:main";
    input.authorization_options.expected_workspace_id = "workspace:main";
    input.authorization_options.authority.has_package_authority = true;
    input.authorization_options.authority.package_id = package_id;
    return input;
}

JsPublishStagedRequestAssemblyInput make_activation_input(
    const JsPublishEndpointResponse &stage_response, JsPublishOperation operation)
{
    JsPublishStagedRequestAssemblyInput input;
    input.operation = operation;
    input.request_id = "request:activate";
    input.actor_id = "actor:42";
    input.builder_account_id = "account:builder";
    input.package_id = stage_response.package_id;
    input.package_version_id = stage_response.package_version_id;
    input.token = make_token(operation);
    input.transport = make_transport();
    return input;
}

JsPublishActivationOptions make_activation_options(const std::string &current_live = "live:old",
    const std::string &server_instance_id = "server:main")
{
    JsPublishActivationOptions options;
    options.assembly_options.now_epoch_seconds = 100;
    options.assembly_options.allow_mutating_operations = true;
    options.assembly_options.expected_server_audience = "server:main";
    options.assembly_options.expected_workspace_id = "workspace:main";
    options.assembly_options.expected_server_instance_id = server_instance_id;
    options.assembly_options.current_live_checksum = current_live;
    options.allow_live_pointer_update = true;
    options.durable_audit_precondition_ok = true;
    options.applied_at_epoch_seconds = 200000;
    options.live_pointer_audit_id = "audit:activate";
    return options;
}

} // namespace

TEST(JsPublishEndpointService, StagesPackageAndReturnsJsonContract)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();

    JsPublishEndpointServiceResult result =
        service.stage(make_stage_input(package, make_stage_options()));

    EXPECT_TRUE(result.response.ok);
    EXPECT_EQ(200, result.response.http_status);
    EXPECT_EQ("stage.accepted", result.response.reason_code);
    EXPECT_EQ(
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum),
        result.response.package_id);
    EXPECT_FALSE(result.response.package_version_id.empty());
    EXPECT_NE(std::string::npos, result.json.find("\"packageVersionId\""));
    EXPECT_NE(std::string::npos, result.json.find("\"reasonCode\":\"stage.accepted\""));
    EXPECT_EQ(std::string::npos, result.json.find("\"body\""));
    EXPECT_EQ(std::string::npos, result.json.find("compiled_javascript"));
}

TEST(JsPublishEndpointService, RejectsPublishModePackagesWhenManifestIsClosed)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store);
    JsScriptPackage package = make_package();

    JsPublishEndpointServiceResult result =
        service.stage(make_stage_input(package, make_stage_options()));

    EXPECT_FALSE(result.response.ok);
    EXPECT_EQ(400, result.response.http_status);
    EXPECT_EQ("stage.invalid-request", result.response.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointService, RejectsStageInputThatDoesNotMatchAuthorizationRequest)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointStageInput input = make_stage_input(package, make_stage_options());
    input.authorization_request.package = make_package(30, 3002);

    JsPublishEndpointServiceResult result = service.stage(input);

    EXPECT_FALSE(result.response.ok);
    EXPECT_EQ(400, result.response.http_status);
    EXPECT_EQ("stage.invalid-request", result.response.reason_code);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointService, DerivesStageAuditFromAuthorizedRequest)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointStageInput input = make_stage_input(package, make_stage_options());
    input.stage_options.identity_options.server_instance_id = "server:forged";
    input.stage_options.audit.actor_id = "actor:forged";
    input.stage_options.audit.request_id = "request:forged";
    input.stage_options.audit.permission_snapshot_id = "permission:forged";
    input.stage_options.audit.audit_id = "audit:forged";
    input.stage_options.audit.source_policy_decision = "source-policy:forged";
    input.stage_options.audit.validation_report_digest = "validation:forged";
    input.stage_options.audit.transport_source_identifier = "transport:forged";

    JsPublishEndpointServiceResult result = service.stage(input);
    ASSERT_TRUE(result.response.ok);
    JsPublishStagedPackageStatusResult status =
        js_publish_latest_staged_package_status(service.staged_repository(),
            result.response.package_id);
    JsStagedPackageLookupResult lookup =
        service.staged_repository().find_by_version(result.response.package_id,
            result.response.package_version_id);

    ASSERT_TRUE(status.ok);
    EXPECT_EQ("audit:stage-preflight", status.status.audit_id);
    ASSERT_TRUE(lookup.ok);
    EXPECT_EQ("server:main", lookup.record.identity.server_instance_id);
    EXPECT_EQ("request:stage-preflight", lookup.record.audit.request_id);
    EXPECT_EQ("actor:42", lookup.record.audit.actor_id);
    EXPECT_EQ("token:stage", lookup.record.audit.permission_snapshot_id);
    EXPECT_EQ("audit:stage-preflight", lookup.record.audit.audit_id);
    EXPECT_EQ("publish-preflight:accepted", lookup.record.audit.source_policy_decision);
    EXPECT_EQ(package.compiled_javascript_checksum,
        lookup.record.audit.validation_report_digest);
    EXPECT_EQ("transport:verified", lookup.record.audit.transport_source_identifier);
}

TEST(JsPublishEndpointService, StagePreflightConflictDoesNotWriteStagedPackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();

    JsPublishEndpointServiceResult result =
        service.stage(make_stage_input(package, make_stage_options("live:old"), "live:new"));

    EXPECT_FALSE(result.response.ok);
    EXPECT_EQ(409, result.response.http_status);
    EXPECT_EQ("stage.stale-live-checksum", result.response.reason_code);
    EXPECT_EQ("live:new", result.response.live_checksum);
    ASSERT_EQ(1u, result.response.diagnostics.size());
    EXPECT_EQ("refresh status and rebuild against the latest live checksum",
        result.response.diagnostics[0]);
    EXPECT_TRUE(service.staged_repository().empty());
}

TEST(JsPublishEndpointService, StatusReadsStagedRepository)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    ASSERT_TRUE(service.stage(make_stage_input(package, make_stage_options())).response.ok);

    const std::string package_id =
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum);
    JsPublishEndpointServiceResult status = service.status(make_status_input(package_id));

    EXPECT_TRUE(status.response.ok);
    EXPECT_EQ("status.current", status.response.reason_code);
    EXPECT_EQ(
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum),
        status.response.package_id);
    EXPECT_NE(std::string::npos, status.json.find("\"diagnostics\""));
}

TEST(JsPublishEndpointService, StatusReportsCurrentLiveChecksumAfterActivation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointServiceResult staged =
        service.stage(make_stage_input(package, make_stage_options()));
    ASSERT_TRUE(staged.response.ok);
    JsPublishEndpointServiceResult activated = service.activate(
        make_activation_input(staged.response, JsPublishOperation::PackageActivate),
        make_activation_options());
    ASSERT_TRUE(activated.response.ok);

    JsPublishEndpointServiceResult status = service.status(make_status_input(staged.response.package_id));

    ASSERT_TRUE(status.response.ok);
    EXPECT_EQ("status.current", status.response.reason_code);
    EXPECT_EQ(activated.response.live_checksum, status.response.live_checksum);
    EXPECT_NE("live:old", status.response.live_checksum);
}

TEST(JsPublishEndpointService, StatusDerivesPackageAuthorityFromStagedRecord)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    ASSERT_TRUE(service.stage(make_stage_input(package, make_stage_options())).response.ok);
    const std::string package_id =
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum);
    JsPublishEndpointStatusInput input = make_status_input(package_id);
    input.authorization_options.authority = {};

    JsPublishEndpointServiceResult status = service.status(input);

    EXPECT_TRUE(status.response.ok);
    EXPECT_EQ("status.current", status.response.reason_code);
    EXPECT_EQ(package_id, status.response.package_id);
}

TEST(JsPublishEndpointService, StatusAuthorizationFailureDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    ASSERT_TRUE(service.stage(make_stage_input(package, make_stage_options())).response.ok);

    JsPublishEndpointServiceResult status = service.status(
        make_status_input(js_staged_package_logical_package_id(30,
            JsScriptPackageHost::Character, package.vnum), 0));

    EXPECT_FALSE(status.response.ok);
    EXPECT_EQ(403, status.response.http_status);
    EXPECT_EQ("status.authorization-failed", status.response.reason_code);
    EXPECT_TRUE(status.response.package_id.empty());
    EXPECT_TRUE(status.response.package_version_id.empty());
    EXPECT_TRUE(status.response.staged_digest.empty());
}

TEST(JsPublishEndpointService, StatusRejectsNonStatusReadOperationWithoutMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    ASSERT_TRUE(service.stage(make_stage_input(package, make_stage_options())).response.ok);
    const std::string package_id =
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum);
    JsPublishEndpointStatusInput input = make_status_input(package_id);
    input.authorization_request.operation = JsPublishOperation::ManifestRead;
    input.authorization_request.token.scopes = JS_PUBLISH_SCOPE_MANIFEST_READ;

    JsPublishEndpointServiceResult status = service.status(input);

    EXPECT_FALSE(status.response.ok);
    EXPECT_EQ(400, status.response.http_status);
    EXPECT_TRUE(status.response.package_id.empty());
    EXPECT_TRUE(status.response.staged_digest.empty());
}

TEST(JsPublishEndpointService, StatusRejectsPackageIdMismatchWithoutMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    ASSERT_TRUE(service.stage(make_stage_input(package, make_stage_options())).response.ok);
    const std::string package_id =
        js_staged_package_logical_package_id(30, JsScriptPackageHost::Character, package.vnum);
    JsPublishEndpointStatusInput input = make_status_input(package_id);
    input.authorization_request.package_id = "js:30:character:9999";

    JsPublishEndpointServiceResult status = service.status(input);

    EXPECT_FALSE(status.response.ok);
    EXPECT_EQ(400, status.response.http_status);
    EXPECT_TRUE(status.response.package_id.empty());
    EXPECT_TRUE(status.response.staged_digest.empty());
}

TEST(JsPublishEndpointService, ActivatePublishesStagedPackageToLiveStore)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointServiceResult staged =
        service.stage(make_stage_input(package, make_stage_options()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointServiceResult activated = service.activate(
        make_activation_input(staged.response, JsPublishOperation::PackageActivate),
        make_activation_options());

    EXPECT_TRUE(activated.response.ok);
    EXPECT_EQ("activate.accepted", activated.response.reason_code);
    EXPECT_EQ("Package activated.", activated.response.message);
    EXPECT_EQ("audit:activate", activated.response.audit_id);
    EXPECT_EQ(1u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointService, ActivateRejectsRollbackOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointServiceResult staged =
        service.stage(make_stage_input(package, make_stage_options()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointServiceResult activated = service.activate(
        make_activation_input(staged.response, JsPublishOperation::PackageRollbackOwn),
        make_activation_options());

    EXPECT_FALSE(activated.response.ok);
    EXPECT_EQ(400, activated.response.http_status);
    EXPECT_EQ("activate.rejected", activated.response.reason_code);
}

TEST(JsPublishEndpointService, ActivateRejectsStagedPackageFromDifferentServerInstance)
{
    JsLivePackageStore live_store;
    JsPublishEndpointServiceOptions other_options = internal_validation_options();
    other_options.server_instance_id = "server:other";
    JsPublishEndpointService service(live_store, other_options);
    JsPublishEndpointServiceResult staged = service.stage(
        make_stage_input(make_package(30, 3001, "return true"), make_stage_options()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointServiceResult activated = service.activate(
        make_activation_input(staged.response, JsPublishOperation::PackageActivate),
        make_activation_options("live:old", "server:main"));

    EXPECT_FALSE(activated.response.ok);
    EXPECT_EQ(400, activated.response.http_status);
    EXPECT_EQ("activate.staged-package-not-found", activated.response.reason_code);
    EXPECT_TRUE(activated.response.package_id.empty());
    EXPECT_TRUE(activated.response.package_version_id.empty());
    EXPECT_TRUE(activated.response.staged_digest.empty());
    EXPECT_TRUE(activated.response.live_checksum.empty());
    EXPECT_TRUE(activated.response.audit_id.empty());
    EXPECT_EQ(std::string::npos, activated.json.find("server:other"));
    EXPECT_EQ(std::string::npos, activated.json.find("server:main"));
    EXPECT_EQ(0u, live_store.package_record_count());
    EXPECT_EQ(0u, live_store.live_pointer_count());
}

TEST(JsPublishEndpointService, RollbackPublishesRequestedRetainedLivePackage)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsPublishEndpointServiceResult first = service.stage(
        make_stage_input(make_package(30, 3001, "return true"), make_stage_options()));
    ASSERT_TRUE(first.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(first.response, JsPublishOperation::PackageActivate),
        make_activation_options()).response.ok);
    JsLivePackagePointerResult live_pointer =
        live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string first_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishEndpointServiceResult second = service.stage(
        make_stage_input(make_package(30, 3001, "return false"),
            make_stage_options(first_live_checksum), first_live_checksum));
    ASSERT_TRUE(second.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(second.response, JsPublishOperation::PackageActivate),
        make_activation_options(first_live_checksum))
                    .response.ok);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string second_live_checksum = live_pointer.pointer.current_live_checksum;

    JsPublishActivationOptions options = make_activation_options(second_live_checksum);
    options.live_pointer_audit_id = "audit:rollback";
    JsPublishStagedRequestAssemblyInput rollback_input =
        make_activation_input(first.response, JsPublishOperation::PackageRollbackOwn);
    rollback_input.expected_live_checksum = second_live_checksum;
    JsPublishEndpointServiceResult rollback = service.rollback(rollback_input, options);

    EXPECT_TRUE(rollback.response.ok);
    EXPECT_EQ("rollback.accepted", rollback.response.reason_code);
    EXPECT_EQ("Rollback activated prior package.", rollback.response.message);
    EXPECT_EQ("audit:rollback", rollback.response.audit_id);
    EXPECT_EQ(first.response.package_version_id, rollback.response.package_version_id);
    EXPECT_EQ(1u, live_store.live_pointer_count());
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    EXPECT_EQ(first.response.package_version_id, live_pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointService, RollbackAuditPreconditionFailurePreservesCurrentLivePointer)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsPublishEndpointServiceResult first = service.stage(
        make_stage_input(make_package(30, 3001, "return true"), make_stage_options()));
    ASSERT_TRUE(first.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(first.response, JsPublishOperation::PackageActivate),
        make_activation_options()).response.ok);
    JsLivePackagePointerResult live_pointer =
        live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string first_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishEndpointServiceResult second = service.stage(
        make_stage_input(make_package(30, 3001, "return false"),
            make_stage_options(first_live_checksum), first_live_checksum));
    ASSERT_TRUE(second.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(second.response, JsPublishOperation::PackageActivate),
        make_activation_options(first_live_checksum))
                    .response.ok);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string second_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishActivationOptions options = make_activation_options(second_live_checksum);
    options.live_pointer_audit_id = "audit:rollback";
    options.durable_audit_precondition_ok = false;
    JsPublishStagedRequestAssemblyInput rollback_input =
        make_activation_input(first.response, JsPublishOperation::PackageRollbackOwn);
    rollback_input.expected_live_checksum = second_live_checksum;

    JsPublishEndpointServiceResult rollback = service.rollback(rollback_input, options);

    EXPECT_FALSE(rollback.response.ok);
    EXPECT_EQ(500, rollback.response.http_status);
    EXPECT_EQ("rollback.audit-precondition-failed", rollback.response.reason_code);
    EXPECT_TRUE(rollback.response.audit_id.empty());
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    EXPECT_EQ(second.response.package_version_id, live_pointer.pointer.package_version_id);
    EXPECT_TRUE(live_store.find_record(first.response.package_id,
                         first.response.package_version_id)
                    .ok);
}

TEST(JsPublishEndpointService,
    RollbackLivePointerConflictPrecedesAuditPreconditionFailure)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsPublishEndpointServiceResult first = service.stage(
        make_stage_input(make_package(30, 3001, "return true"), make_stage_options()));
    ASSERT_TRUE(first.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(first.response, JsPublishOperation::PackageActivate),
        make_activation_options()).response.ok);
    JsLivePackagePointerResult live_pointer =
        live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string first_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishEndpointServiceResult second = service.stage(
        make_stage_input(make_package(30, 3001, "return false"),
            make_stage_options(first_live_checksum), first_live_checksum));
    ASSERT_TRUE(second.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(second.response, JsPublishOperation::PackageActivate),
        make_activation_options(first_live_checksum))
                    .response.ok);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string second_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishEndpointServiceResult third = service.stage(
        make_stage_input(make_package(30, 3001, "return ctx.self.level > 10"),
            make_stage_options(second_live_checksum), second_live_checksum));
    ASSERT_TRUE(third.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(third.response, JsPublishOperation::PackageActivate),
        make_activation_options(second_live_checksum))
                    .response.ok);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string third_live_checksum = live_pointer.pointer.current_live_checksum;
    ASSERT_NE(second_live_checksum, third_live_checksum);
    JsPublishActivationOptions options = make_activation_options(second_live_checksum);
    options.live_pointer_audit_id = "audit:rollback";
    options.durable_audit_precondition_ok = false;
    JsPublishStagedRequestAssemblyInput rollback_input =
        make_activation_input(first.response, JsPublishOperation::PackageRollbackOwn);
    rollback_input.expected_live_checksum = second_live_checksum;

    JsPublishEndpointServiceResult rollback = service.rollback(rollback_input, options);

    EXPECT_FALSE(rollback.response.ok);
    EXPECT_EQ(409, rollback.response.http_status);
    EXPECT_EQ("rollback.stale-live-checksum", rollback.response.reason_code);
    EXPECT_EQ(first.response.package_id, rollback.response.package_id);
    EXPECT_EQ(third_live_checksum, rollback.response.live_checksum);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    EXPECT_EQ(third.response.package_version_id, live_pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointService, RollbackRejectsPriorLivePackageFromDifferentServerInstance)
{
    JsLivePackageStore live_store;
    JsPublishEndpointServiceOptions other_options = internal_validation_options();
    other_options.server_instance_id = "server:other";
    JsPublishEndpointService service(live_store, other_options);
    JsPublishEndpointServiceResult first = service.stage(
        make_stage_input(make_package(30, 3001, "return true"), make_stage_options()));
    ASSERT_TRUE(first.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(first.response, JsPublishOperation::PackageActivate),
        make_activation_options("live:old", "server:other")).response.ok);
    JsLivePackagePointerResult live_pointer =
        live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string first_live_checksum = live_pointer.pointer.current_live_checksum;
    JsPublishEndpointServiceResult second = service.stage(
        make_stage_input(make_package(30, 3001, "return false"),
            make_stage_options(first_live_checksum), first_live_checksum));
    ASSERT_TRUE(second.response.ok);
    ASSERT_TRUE(service.activate(
        make_activation_input(second.response, JsPublishOperation::PackageActivate),
        make_activation_options(first_live_checksum, "server:other"))
                    .response.ok);
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    const std::string second_live_checksum = live_pointer.pointer.current_live_checksum;

    JsPublishActivationOptions rollback_options =
        make_activation_options(second_live_checksum, "server:main");
    rollback_options.live_pointer_audit_id = "audit:rollback";
    JsPublishStagedRequestAssemblyInput rollback_input =
        make_activation_input(first.response, JsPublishOperation::PackageRollbackOwn);
    rollback_input.expected_live_checksum = second_live_checksum;
    JsPublishEndpointServiceResult rollback = service.rollback(rollback_input, rollback_options);

    EXPECT_FALSE(rollback.response.ok);
    EXPECT_EQ(400, rollback.response.http_status);
    EXPECT_EQ("rollback.staged-package-not-found", rollback.response.reason_code);
    EXPECT_TRUE(rollback.response.package_id.empty());
    EXPECT_TRUE(rollback.response.package_version_id.empty());
    EXPECT_TRUE(rollback.response.staged_digest.empty());
    EXPECT_TRUE(rollback.response.live_checksum.empty());
    EXPECT_TRUE(rollback.response.audit_id.empty());
    EXPECT_EQ(std::string::npos, rollback.json.find("server:other"));
    EXPECT_EQ(std::string::npos, rollback.json.find("server:main"));
    live_pointer = live_store.find_live_pointer(first.response.package_id);
    ASSERT_TRUE(live_pointer.ok);
    EXPECT_EQ(second.response.package_version_id, live_pointer.pointer.package_version_id);
}

TEST(JsPublishEndpointService, RollbackRejectsActivateOperation)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointServiceResult staged =
        service.stage(make_stage_input(package, make_stage_options()));
    ASSERT_TRUE(staged.response.ok);

    JsPublishEndpointServiceResult rollback = service.rollback(
        make_activation_input(staged.response, JsPublishOperation::PackageActivate),
        make_activation_options());

    EXPECT_FALSE(rollback.response.ok);
    EXPECT_EQ(400, rollback.response.http_status);
    EXPECT_EQ("rollback.rejected", rollback.response.reason_code);
}

TEST(JsPublishEndpointService, ActivationAuthorizationFailureDoesNotLeakMetadata)
{
    JsLivePackageStore live_store;
    JsPublishEndpointService service(live_store, internal_validation_options());
    JsScriptPackage package = make_package();
    JsPublishEndpointServiceResult staged =
        service.stage(make_stage_input(package, make_stage_options()));
    ASSERT_TRUE(staged.response.ok);
    JsPublishStagedRequestAssemblyInput input =
        make_activation_input(staged.response, JsPublishOperation::PackageActivate);
    input.token.scopes = 0;

    JsPublishEndpointServiceResult denied =
        service.activate(input, make_activation_options());

    EXPECT_FALSE(denied.response.ok);
    EXPECT_EQ(403, denied.response.http_status);
    EXPECT_EQ("activate.missing-scope", denied.response.reason_code);
    EXPECT_TRUE(denied.response.package_id.empty());
    EXPECT_TRUE(denied.response.live_checksum.empty());
    EXPECT_TRUE(denied.response.audit_id.empty());
}
