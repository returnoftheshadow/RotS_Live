#include "../js_publish_endpoint_contract.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001, const std::string &body = "return true")
{
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
        { JsScriptingManifestKind::LegacyScriptTrigger, ON_ENTER, "onEnter" });
    package.compiled_javascript_checksum = js_script_package_compiled_javascript_checksum(package);
    return package;
}

JsStagedPackageStageOptions make_stage_options(const std::string &builder = "account:builder",
    const std::string &base_live = "live:old")
{
    JsStagedPackageStageOptions options;
    options.identity_options.zone = 30;
    options.identity_options.builder_account_id = builder;
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

JsStagedPackageRecord stage_package(JsStagedPackageRepository &repository,
    const JsScriptPackage &package, const JsStagedPackageStageOptions &options)
{
    JsStagedPackageStageResult staged = repository.stage_package(package, options);
    EXPECT_TRUE(staged.ok);
    return staged.record;
}

JsPublishTokenMetadata make_token(JsPublishOperation operation,
    const std::string &builder = "account:builder")
{
    JsPublishTokenMetadata token;
    token.token_id = "token-1";
    token.claims_verified = true;
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

JsPublishStagedRequestAssemblyInput make_input(const JsStagedPackageRecord &record,
    JsPublishOperation operation, const std::string &builder = "account:builder")
{
    JsPublishStagedRequestAssemblyInput input;
    input.operation = operation;
    input.request_id = "request:operation";
    input.actor_id = "actor:42";
    input.builder_account_id = builder;
    input.package_id = record.identity.package_id;
    input.package_version_id = record.identity.package_version_id;
    input.token = make_token(operation, builder);
    input.transport = make_transport();
    return input;
}

JsPublishActivationOptions make_activation_options(const std::string &current_live = "live:old")
{
    JsPublishActivationOptions options;
    options.assembly_options.now_epoch_seconds = 100;
    options.assembly_options.allow_mutating_operations = true;
    options.assembly_options.expected_server_audience = "server:main";
    options.assembly_options.expected_workspace_id = "workspace:main";
    options.assembly_options.expected_server_instance_id = "server:main";
    options.assembly_options.current_live_checksum = current_live;
    options.allow_live_pointer_update = true;
    options.durable_audit_precondition_ok = true;
    options.applied_at_epoch_seconds = 200000;
    options.live_pointer_audit_id = "audit:activate";
    return options;
}

bool diagnostic_contains(const JsPublishEndpointResponse &response, const std::string &text)
{
    return std::any_of(response.diagnostics.begin(), response.diagnostics.end(),
        [&text](const std::string &diagnostic) {
            return diagnostic.find(text) != std::string::npos;
        });
}

bool json_contains(const std::string &json, const std::string &text)
{
    return json.find(text) != std::string::npos;
}

} // namespace

TEST(JsPublishEndpointContract, MapsStageSuccessToClientResponseShape)
{
    JsStagedPackageRepository repository;
    JsStagedPackageStageResult staged =
        repository.stage_package(make_package(), make_stage_options());
    ASSERT_TRUE(staged.ok);

    JsPublishEndpointResponse response = js_publish_endpoint_stage_response(staged);

    EXPECT_TRUE(response.ok);
    EXPECT_EQ(200, response.http_status);
    EXPECT_EQ("stage", response.operation);
    EXPECT_EQ("stage.accepted", response.reason_code);
    EXPECT_EQ(staged.record.identity.package_id, response.package_id);
    EXPECT_EQ(staged.record.identity.package_version_id, response.package_version_id);
    EXPECT_EQ(staged.record.identity.canonical_digest, response.staged_digest);
    EXPECT_EQ(staged.record.identity.base_live_checksum, response.live_checksum);
    EXPECT_EQ("audit:stage", response.audit_id);
    EXPECT_TRUE(response.diagnostics.empty());

    const std::string json = js_publish_endpoint_response_json(response);
    EXPECT_TRUE(json_contains(json, "\"httpStatus\":200"));
    EXPECT_TRUE(json_contains(json, "\"packageVersionId\""));
    EXPECT_TRUE(json_contains(json, "\"stagedDigest\""));
    EXPECT_TRUE(json_contains(json, "\"liveChecksum\""));
    EXPECT_TRUE(json_contains(json, "\"reasonCode\":\"stage.accepted\""));
    EXPECT_FALSE(json_contains(json, "package_version_id"));
    EXPECT_FALSE(json_contains(json, "staged_digest"));
}

TEST(JsPublishEndpointContract, MapsStageInvalidRequestToBadRequest)
{
    JsStagedPackageRepository repository;
    JsStagedPackageStageOptions options = make_stage_options();
    options.audit.audit_id.clear();

    JsPublishEndpointResponse response =
        js_publish_endpoint_stage_response(repository.stage_package(make_package(), options));

    EXPECT_FALSE(response.ok);
    EXPECT_EQ(400, response.http_status);
    EXPECT_EQ("stage.invalid-request", response.reason_code);
    EXPECT_TRUE(diagnostic_contains(response, "stage request is invalid"));
}

TEST(JsPublishEndpointContract, MapsStagePreflightStaleLiveToConflict)
{
    JsPublishStagePreflightEndpointInput input;
    input.package_id = "js:zone-30:character:3001";
    input.current_live_checksum = "sha256:changed-live";
    input.audit_id = "audit:stage-conflict";
    input.authorization_result.diagnostics.push_back(
        { JsPublishDiagnosticCode::PackagePreconditionMismatch, "request:redacted",
            "package:redacted", "raw internal message" });

    JsPublishEndpointResponse response = js_publish_endpoint_stage_preflight_response(input);

    EXPECT_FALSE(response.ok);
    EXPECT_EQ(409, response.http_status);
    EXPECT_EQ("stage.stale-live-checksum", response.reason_code);
    EXPECT_EQ("js:zone-30:character:3001", response.package_id);
    EXPECT_EQ("sha256:changed-live", response.live_checksum);
    EXPECT_EQ("audit:stage-conflict", response.audit_id);
    EXPECT_TRUE(diagnostic_contains(response, "refresh status"));
    EXPECT_FALSE(diagnostic_contains(response, "raw internal"));
}

TEST(JsPublishEndpointContract, MapsStagePreflightAuthFailureWithoutLeakingConflictMetadata)
{
    JsPublishStagePreflightEndpointInput input;
    input.package_id = "js:zone-30:character:3001";
    input.current_live_checksum = "sha256:changed-live";
    input.audit_id = "audit:stage-conflict";
    input.authorization_result.diagnostics.push_back(
        { JsPublishDiagnosticCode::MissingScope, "request:redacted", "package:redacted",
            "missing scope" });
    input.authorization_result.diagnostics.push_back(
        { JsPublishDiagnosticCode::PackagePreconditionMismatch, "request:redacted",
            "package:redacted", "stale checksum" });

    JsPublishEndpointResponse response = js_publish_endpoint_stage_preflight_response(input);

    EXPECT_FALSE(response.ok);
    EXPECT_EQ(403, response.http_status);
    EXPECT_EQ("stage.authorization-failed", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "required publish scope is missing"));
}

TEST(JsPublishEndpointContract, MapsStatusSuccessAndMissingStatus)
{
    JsStagedPackageRepository repository;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());

    JsPublishEndpointResponse current = js_publish_endpoint_status_response(
        js_publish_latest_staged_package_status(repository, record.identity.package_id));
    JsPublishEndpointResponse missing = js_publish_endpoint_status_response(
        js_publish_latest_staged_package_status(repository, "missing"));

    EXPECT_TRUE(current.ok);
    EXPECT_EQ("status.current", current.reason_code);
    EXPECT_EQ(record.identity.package_version_id, current.package_version_id);
    EXPECT_EQ("audit:stage", current.audit_id);
    EXPECT_EQ("Current staged and live metadata returned.", current.message);
    EXPECT_TRUE(diagnostic_contains(current, "metadata-only"));
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(404, missing.http_status);
    EXPECT_EQ("status.not-found", missing.reason_code);
    EXPECT_TRUE(diagnostic_contains(missing, "not found"));
}

TEST(JsPublishEndpointContract, MapsStatusInvalidRequestToBadRequest)
{
    JsStagedPackageRepository repository;

    JsPublishEndpointResponse response =
        js_publish_endpoint_status_response(js_publish_latest_staged_package_status(repository, ""));

    EXPECT_FALSE(response.ok);
    EXPECT_EQ(400, response.http_status);
    EXPECT_EQ("status.invalid-request", response.reason_code);
    EXPECT_TRUE(diagnostic_contains(response, "status request is invalid"));
}

TEST(JsPublishEndpointContract, MapsActivationSuccessToAcceptedResponse)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record, JsPublishOperation::PackageActivate),
        make_activation_options());
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(200, response.http_status);
    EXPECT_EQ("activate", response.operation);
    EXPECT_EQ("activate.accepted", response.reason_code);
    EXPECT_EQ(record.identity.package_id, response.package_id);
    EXPECT_EQ(record.identity.package_version_id, response.package_version_id);
    EXPECT_EQ(record.identity.canonical_digest, response.staged_digest);
    EXPECT_EQ("audit:activate", response.audit_id);
    EXPECT_FALSE(response.live_checksum.empty());
    EXPECT_TRUE(response.diagnostics.empty());
}

TEST(JsPublishEndpointContract, MapsAuthorizationFailureWithoutLeakingMetadata)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options("account:owner"));

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store,
        make_input(record, JsPublishOperation::PackageActivate, "account:intruder"),
        make_activation_options());
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(response.ok);
    EXPECT_EQ(403, response.http_status);
    EXPECT_EQ("activate.authorization-failed", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.package_version_id.empty());
    EXPECT_TRUE(response.staged_digest.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "package permission check failed"));
}

TEST(JsPublishEndpointContract, MapsMissingScopeWithoutLeakingMetadata)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageActivate);
    input.token.scopes = 0;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, input, make_activation_options());
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, response.http_status);
    EXPECT_EQ("activate.missing-scope", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.staged_digest.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "required publish scope is missing"));
}

TEST(JsPublishEndpointContract, MapsActivationAuditPreconditionFailureToServerFailure)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_activation_options();
    options.durable_audit_precondition_ok = false;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record, JsPublishOperation::PackageActivate), options);
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(500, response.http_status);
    EXPECT_EQ("activate.audit-precondition-failed", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.package_version_id.empty());
    EXPECT_TRUE(response.staged_digest.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "publish audit precondition failed"));
}

TEST(JsPublishEndpointContract, MapsRollbackAuditPreconditionFailureToServerFailure)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());
    JsPublishActivationOptions options = make_activation_options();
    options.durable_audit_precondition_ok = false;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(record, JsPublishOperation::PackageRollbackOwn),
        options);
    JsPublishEndpointResponse response = js_publish_endpoint_rollback_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(500, response.http_status);
    EXPECT_EQ("rollback.audit-precondition-failed", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.package_version_id.empty());
    EXPECT_TRUE(response.staged_digest.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "publish audit precondition failed"));
}

TEST(JsPublishEndpointContract, MapsCombinedMissingScopeAndPreconditionAsForbidden)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageActivate);
    input.expected_live_checksum = "live:old";
    input.token.scopes = 0;

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, input, make_activation_options("live:new"));
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, response.http_status);
    EXPECT_EQ("activate.missing-scope", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "required publish scope is missing"));
}

TEST(JsPublishEndpointContract, MapsCombinedPermissionAndPreconditionAsForbidden)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options("account:owner"));
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageActivate, "account:intruder");
    input.expected_live_checksum = "live:old";

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, input, make_activation_options("live:new"));
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, response.http_status);
    EXPECT_EQ("activate.authorization-failed", response.reason_code);
    EXPECT_TRUE(response.package_id.empty());
    EXPECT_TRUE(response.live_checksum.empty());
    EXPECT_TRUE(response.audit_id.empty());
    EXPECT_TRUE(diagnostic_contains(response, "package permission check failed"));
}

TEST(JsPublishEndpointContract, MapsActivationPreconditionMismatchToConflict)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord record =
        stage_package(repository, make_package(), make_stage_options());
    JsPublishStagedRequestAssemblyInput input =
        make_input(record, JsPublishOperation::PackageActivate);
    input.expected_live_checksum = "live:old";

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, input, make_activation_options("live:new"));
    JsPublishEndpointResponse response = js_publish_endpoint_activation_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(409, response.http_status);
    EXPECT_EQ("activate.stale-live-checksum", response.reason_code);
    EXPECT_EQ(record.identity.package_id, response.package_id);
    EXPECT_EQ(record.identity.package_version_id, response.package_version_id);
    EXPECT_EQ("live:new", response.live_checksum);
    EXPECT_TRUE(diagnostic_contains(response, "refresh status"));
}

TEST(JsPublishEndpointContract, MapsRollbackConflictToStaleLiveReason)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first =
        stage_package(repository, make_package(3001, "return true"), make_stage_options());
    ASSERT_TRUE(js_publish_apply_staged_package_activation(
        repository, live_store, make_input(first, JsPublishOperation::PackageActivate),
        make_activation_options()).ok);
    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);
    JsStagedPackageRecord second = stage_package(
        repository, make_package(3001, "return false"), make_stage_options("account:builder",
                                                      first_live_checksum));
    ASSERT_TRUE(js_publish_apply_staged_package_activation(
        repository, live_store, make_input(second, JsPublishOperation::PackageActivate),
        make_activation_options(first_live_checksum)).ok);
    JsLivePackagePointerResult current_pointer =
        live_store.find_live_pointer(second.identity.zone, second.identity.host,
            second.identity.vnum);
    ASSERT_TRUE(current_pointer.ok);
    const std::string current_live_checksum = current_pointer.pointer.current_live_checksum;
    JsStagedPackageRecord third =
        stage_package(repository, make_package(3001, "return ctx.self.level > 10"),
            make_stage_options("account:builder", first_live_checksum));
    JsPublishActivationOptions options = make_activation_options(first_live_checksum);

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(third, JsPublishOperation::PackageRollbackOwn),
        options);
    JsPublishEndpointResponse response = js_publish_endpoint_rollback_response(result);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(409, response.http_status);
    EXPECT_EQ("rollback.stale-live-checksum", response.reason_code);
    EXPECT_EQ(third.identity.package_version_id, response.package_version_id);
    EXPECT_EQ(current_live_checksum, response.live_checksum);
    EXPECT_TRUE(diagnostic_contains(response, "refresh status"));
}

TEST(JsPublishEndpointContract, MapsRollbackSuccessToAcceptedResponse)
{
    JsStagedPackageRepository repository;
    JsLivePackageStore live_store;
    JsStagedPackageRecord first =
        stage_package(repository, make_package(3001, "return true"), make_stage_options());
    ASSERT_TRUE(js_publish_apply_staged_package_activation(
        repository, live_store, make_input(first, JsPublishOperation::PackageActivate),
        make_activation_options()).ok);
    const std::string first_live_checksum =
        js_live_package_current_checksum_for_identity(first.identity);
    JsStagedPackageRecord second = stage_package(
        repository, make_package(3001, "return false"),
        make_stage_options("account:builder", first_live_checksum));
    JsPublishActivationOptions rollback_options = make_activation_options(first_live_checksum);
    rollback_options.live_pointer_audit_id = "audit:rollback";

    JsPublishActivationResult result = js_publish_apply_staged_package_activation(
        repository, live_store, make_input(second, JsPublishOperation::PackageRollbackOwn),
        rollback_options);
    JsPublishEndpointResponse response = js_publish_endpoint_rollback_response(result);

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(200, response.http_status);
    EXPECT_EQ("rollback.accepted", response.reason_code);
    EXPECT_EQ("audit:rollback", response.audit_id);
    EXPECT_EQ(second.identity.package_id, response.package_id);
    EXPECT_EQ(second.identity.package_version_id, response.package_version_id);
    EXPECT_FALSE(response.live_checksum.empty());
    EXPECT_TRUE(response.diagnostics.empty());
}
