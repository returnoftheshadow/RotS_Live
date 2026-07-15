#include "../js_publish_authorization.h"

#include "../script.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

namespace {

JsScriptPackage make_package(int vnum = 3001)
{
    const JsScriptingManifestMetadata& metadata = js_scripting_manifest_metadata();
    JsScriptPackage package;
    package.vnum = vnum;
    package.package_id = "pkg-" + std::to_string(vnum);
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

JsPublishRequest make_request(JsPublishOperation operation = JsPublishOperation::StatusRead)
{
    JsPublishRequest request;
    request.operation = operation;
    request.request_id = "request-1";
    request.actor_id = "actor:42";
    request.builder_account_id = "account:builder";
    request.zone = 30;
    request.vnum = 3001;
    request.host = JsScriptPackageHost::Character;
    request.package_id = "pkg-3001";
    request.package_version_id = "version:3001:1";
    request.staged_digest = "sha256:staged";
    request.base_live_checksum = "live:old";
    request.expected_live_checksum = "live:old";
    request.manifest_checksum = js_scripting_manifest_metadata().manifest_checksum;
    request.token.token_id = "token-1";
    request.token.claims_verified = true;
    request.token.actor_id = request.actor_id;
    request.token.builder_account_id = request.builder_account_id;
    request.token.server_audience = "server:main";
    request.token.workspace_id = "workspace:main";
    request.token.scopes = js_publish_scope_for_operation(operation);
    request.token.issued_at_epoch_seconds = 90;
    request.token.expires_at_epoch_seconds = 200;
    request.transport.secure_channel = true;
    request.transport.server_identity_verified = true;
    request.transport.server_audience = "server:main";
    return request;
}

JsPublishAuthorizationOptions make_options()
{
    JsPublishAuthorizationOptions options;
    options.now_epoch_seconds = 100;
    options.expected_server_audience = "server:main";
    options.expected_workspace_id = "workspace:main";
    options.current_live_checksum = "live:old";
    options.authority.has_package_authority = true;
    options.authority.zone = 30;
    options.authority.vnum = 3001;
    options.authority.host = JsScriptPackageHost::Character;
    options.authority.package_id = "pkg-3001";
    options.authority.package_owner_builder_account_id = "account:builder";
    options.authority.allow_admin_source_view = true;
    options.authority.allow_rollback_any = true;
    options.authority.allow_admin_revoke = true;
    options.authority.allow_package_gc = true;
    options.authority.staged_record_loaded = true;
    options.authority.package_version_id = "version:3001:1";
    options.authority.staged_digest = "sha256:staged";
    options.authority.manifest_checksum = js_scripting_manifest_metadata().manifest_checksum;
    return options;
}

bool has_code(const JsPublishAuthorizationResult& result, JsPublishDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

std::string messages(const JsPublishAuthorizationResult& result)
{
    std::string joined;
    for (const JsPublishDiagnostic& diagnostic : result.diagnostics) {
        joined += diagnostic.message;
        joined += "\n";
    }
    return joined;
}

std::string eligibility_messages(const JsPublishBuilderEligibilityResult& result)
{
    std::string joined;
    for (const JsPublishDiagnostic& diagnostic : result.diagnostics) {
        joined += diagnostic.message;
        joined += "\n";
    }
    return joined;
}

bool has_eligibility_code(const JsPublishBuilderEligibilityResult& result,
    JsPublishDiagnosticCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [code](const JsPublishDiagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

JsPublishBuilderEligibilityInput make_eligibility_input()
{
    JsPublishBuilderEligibilityInput input;
    input.auth_outcome = JsPublishAccountAuthOutcome::Authenticated;
    input.authenticated_account_id = "account:builder";
    input.requested_builder_account_id = "account:builder";
    input.linked_characters.push_back({ "builderone", 1001, 92, true, true });
    return input;
}

void expect_no_sensitive_eligibility_result(const JsPublishBuilderEligibilityResult& result)
{
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.builder_account_id.empty());
    EXPECT_TRUE(result.eligible_character_name.empty());
    EXPECT_EQ(result.eligible_character_id, 0);
    EXPECT_EQ(result.eligible_character_level, 0);
}

} // namespace

TEST(JsPublishAuthorization, BuilderEligibilityRequiresAuthenticatedAccount)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.auth_outcome = JsPublishAccountAuthOutcome::NotAuthenticated;

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::TokenActorMismatch));
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsBlockedAndUnverifiedAccounts)
{
    for (JsPublishAccountAuthOutcome outcome :
        { JsPublishAccountAuthOutcome::Blocked, JsPublishAccountAuthOutcome::EmailUnverified }) {
        JsPublishBuilderEligibilityInput input = make_eligibility_input();
        input.auth_outcome = outcome;

        JsPublishBuilderEligibilityResult result =
            js_publish_evaluate_builder_account_eligibility(input);

        EXPECT_FALSE(result.ok);
        expect_no_sensitive_eligibility_result(result);
        EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
    }
}

TEST(JsPublishAuthorization, BuilderEligibilityBindsRequestedAccountToAuthenticatedAccount)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.authenticated_account_id = "account:real";
    input.requested_builder_account_id = "account:other";

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::TokenActorMismatch));
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsBlankAuthenticatedAccountId)
{
    for (const std::string account_id : { std::string(), std::string(" \t\r\n") }) {
        JsPublishBuilderEligibilityInput input = make_eligibility_input();
        input.authenticated_account_id = account_id;
        input.requested_builder_account_id = account_id;

        JsPublishBuilderEligibilityResult result =
            js_publish_evaluate_builder_account_eligibility(input);

        EXPECT_FALSE(result.ok);
        expect_no_sensitive_eligibility_result(result);
        EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::InvalidRequest));
    }
}

TEST(JsPublishAuthorization, BuilderEligibilityRequiresLinkedLevelNinetyTwoImmortal)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.builder_account_id, "account:builder");
    EXPECT_EQ(result.eligible_character_name, "builderone");
    EXPECT_EQ(result.eligible_character_id, 1001);
    EXPECT_EQ(result.eligible_character_level, JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL);
    EXPECT_TRUE(result.diagnostics.empty());
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsNoLinkedCharacters)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters.clear();

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsNonImmortalLinkedCharacters)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters = {
        { "regularone", 1001, 30, true, false },
        { "regulartwo", 1002, 90, true, false },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
    EXPECT_NE(eligibility_messages(result).find("no linked immortal"), std::string::npos);
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsHighLevelNonImmortalLinkedCharacter)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters = {
        { "highlevel", 1001, 100, true, false },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsImmortalBelowLevelNinetyTwo)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters = {
        { "lowimmortal", 1001, JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL - 1, true, true },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
    EXPECT_NE(eligibility_messages(result).find("below the builder publishing level"),
        std::string::npos);
}

TEST(JsPublishAuthorization, BuilderEligibilitySkipsUnloadedCharactersButAllowsLaterEligibleOne)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters = {
        { "missing", 1001, 100, false, true },
        { "eligible", 1002, 95, true, true },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.eligible_character_name, "eligible");
    EXPECT_EQ(result.eligible_character_id, 1002);
    EXPECT_EQ(result.eligible_character_level, 95);
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsAllUnloadedCharacters)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.linked_characters = {
        { "missing", 1001, 100, false, true },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::PermissionMismatch));
    EXPECT_NE(eligibility_messages(result).find("could not be loaded"), std::string::npos);
}

TEST(JsPublishAuthorization, BuilderEligibilityRejectsMalformedEligibleCharacterEvidence)
{
    const JsPublishLinkedCharacterEligibility malformed_characters[] = {
        { "", 1001, 92, true, true },
        { "nameless-id", 0, 92, true, true },
    };

    for (const JsPublishLinkedCharacterEligibility& character : malformed_characters) {
        JsPublishBuilderEligibilityInput input = make_eligibility_input();
        input.linked_characters = { character };

        JsPublishBuilderEligibilityResult result =
            js_publish_evaluate_builder_account_eligibility(input);

        EXPECT_FALSE(result.ok);
        expect_no_sensitive_eligibility_result(result);
        EXPECT_TRUE(has_eligibility_code(result, JsPublishDiagnosticCode::InvalidRequest));
    }
}

TEST(JsPublishAuthorization, BuilderEligibilityDiagnosticsDoNotEchoAccountOrCharacterNames)
{
    JsPublishBuilderEligibilityInput input = make_eligibility_input();
    input.authenticated_account_id = "account:private-builder";
    input.requested_builder_account_id = "account:private-builder";
    input.linked_characters = {
        { "privatecharacter", 1001, 91, true, true },
    };

    JsPublishBuilderEligibilityResult result =
        js_publish_evaluate_builder_account_eligibility(input);

    EXPECT_FALSE(result.ok);
    expect_no_sensitive_eligibility_result(result);
    EXPECT_EQ(eligibility_messages(result).find("private-builder"), std::string::npos);
    EXPECT_EQ(eligibility_messages(result).find("privatecharacter"), std::string::npos);
    EXPECT_EQ(eligibility_messages(result).find("1001"), std::string::npos);
    EXPECT_EQ(eligibility_messages(result).find("91"), std::string::npos);
}

TEST(JsPublishAuthorization, AllowsReadOnlyStatusWithScopedToken)
{
    JsPublishRequest request = make_request();
    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_TRUE(result.ok);
    EXPECT_FALSE(result.mutates_server_state);
    EXPECT_TRUE(result.diagnostics.empty());
}

TEST(JsPublishAuthorization, RequiresSeparateScopeForEveryOperation)
{
    const JsPublishOperation operations[] = {
        JsPublishOperation::ManifestRead,
        JsPublishOperation::StatusRead,
        JsPublishOperation::DiffRead,
        JsPublishOperation::SourceView,
        JsPublishOperation::AdminSourceView,
        JsPublishOperation::PackageStage,
        JsPublishOperation::PackageActivate,
        JsPublishOperation::PackageRollbackOwn,
        JsPublishOperation::PackageRollbackAny,
        JsPublishOperation::AdminRevoke,
        JsPublishOperation::PackageGarbageCollect,
    };

    for (JsPublishOperation operation : operations) {
        JsPublishRequest request = make_request(operation);
        request.token.scopes = 0;
        JsPublishAuthorizationResult result
            = js_publish_authorization_preflight(request, make_options());

        EXPECT_FALSE(result.ok) << js_publish_operation_name(operation);
        EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::MissingScope))
            << js_publish_operation_name(operation);
    }
}

TEST(JsPublishAuthorization, RejectsWrongSingleScopeForEveryOperation)
{
    const JsPublishOperation operations[] = {
        JsPublishOperation::ManifestRead,
        JsPublishOperation::StatusRead,
        JsPublishOperation::DiffRead,
        JsPublishOperation::SourceView,
        JsPublishOperation::AdminSourceView,
        JsPublishOperation::PackageActivate,
        JsPublishOperation::PackageRollbackOwn,
        JsPublishOperation::PackageRollbackAny,
        JsPublishOperation::AdminRevoke,
        JsPublishOperation::PackageGarbageCollect,
    };

    for (JsPublishOperation operation : operations) {
        JsPublishRequest request = make_request(operation);
        request.token.scopes = JS_PUBLISH_SCOPE_MANIFEST_READ;
        if (operation == JsPublishOperation::ManifestRead)
            request.token.scopes = JS_PUBLISH_SCOPE_STATUS_READ;
        JsPublishAuthorizationResult result
            = js_publish_authorization_preflight(request, make_options());

        EXPECT_FALSE(result.ok) << js_publish_operation_name(operation);
        EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::MissingScope))
            << js_publish_operation_name(operation);
    }
}

TEST(JsPublishAuthorization, KeepsAllMutatingPublishOperationsDisabledByDefault)
{
    const JsPublishOperation operations[] = {
        JsPublishOperation::PackageStage,
        JsPublishOperation::PackageActivate,
        JsPublishOperation::PackageRollbackOwn,
        JsPublishOperation::PackageRollbackAny,
        JsPublishOperation::AdminRevoke,
        JsPublishOperation::PackageGarbageCollect,
    };

    for (JsPublishOperation operation : operations) {
        JsPublishRequest request = make_request(operation);
        if (operation == JsPublishOperation::PackageStage) {
            request.has_package = true;
            request.package = make_package();
        }
        JsPublishAuthorizationResult result
            = js_publish_authorization_preflight(request, make_options());

        EXPECT_FALSE(result.ok) << js_publish_operation_name(operation);
        EXPECT_TRUE(result.mutates_server_state) << js_publish_operation_name(operation);
        EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PublishingDisabled))
            << js_publish_operation_name(operation);
    }
}

TEST(JsPublishAuthorization, KeepsReadOnlyOperationsNonMutating)
{
    const JsPublishOperation operations[] = {
        JsPublishOperation::ManifestRead,
        JsPublishOperation::StatusRead,
        JsPublishOperation::DiffRead,
        JsPublishOperation::SourceView,
        JsPublishOperation::AdminSourceView,
    };

    for (JsPublishOperation operation : operations) {
        JsPublishRequest request = make_request(operation);
        JsPublishAuthorizationResult result
            = js_publish_authorization_preflight(request, make_options());

        EXPECT_FALSE(result.mutates_server_state) << js_publish_operation_name(operation);
    }
}

TEST(JsPublishAuthorization, ValidatesStagePackageMetadataWithoutEnablingPublish)
{
    JsScriptPackage package = make_package();
    JsPublishRequest request = make_request(JsPublishOperation::PackageStage);
    request.has_package = true;
    request.package = package;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackageValidationFailed))
        << "Current manifest entries are still intentionally not publishable.";
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PublishingDisabled));
    EXPECT_FALSE(result.package_validation.ok);
}

TEST(JsPublishAuthorization, RequiresBaseLiveChecksumForStage)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageStage);
    request.has_package = true;
    request.package = make_package();
    request.base_live_checksum.clear();
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::InvalidRequest));
}

TEST(JsPublishAuthorization, RejectsStaleBaseLiveChecksumForStage)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageStage);
    request.has_package = true;
    request.package = make_package();
    request.base_live_checksum = "live:stale";
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishAuthorization, AllowsNonblankBaseChecksumForFirstPublishEmptySlot)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageStage);
    request.has_package = true;
    request.package = make_package();
    request.base_live_checksum = "empty-slot";
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.current_live_checksum.clear();

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackageValidationFailed));
}

TEST(JsPublishAuthorization, RejectsStageRequestPackagePreconditionMismatch)
{
    JsScriptPackage package = make_package();
    package.package_id = "different-package";
    JsPublishRequest request = make_request(JsPublishOperation::PackageStage);
    request.has_package = true;
    request.package = package;

    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishAuthorization, RejectsAuthorityContextPackageIdMismatch)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageActivate);
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.authority.package_id = "pkg-other";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, RequiresServerLoadedStagedRecordForActivation)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageActivate);
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.authority.staged_record_loaded = false;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishAuthorization, RejectsStaleLiveChecksumForActivation)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageActivate);
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.current_live_checksum = "live:new";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishAuthorization, RejectsActivationManifestChecksumMismatch)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageActivate);
    request.manifest_checksum = "manifest:stale";
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PackagePreconditionMismatch));
}

TEST(JsPublishAuthorization, RejectsActivationForAnotherBuildersPackage)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageActivate);
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.authority.package_owner_builder_account_id = "account:other";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, DistinguishesOwnAndAdminSourceView)
{
    JsPublishRequest own_request = make_request(JsPublishOperation::SourceView);
    JsPublishAuthorizationOptions options = make_options();
    options.authority.package_owner_builder_account_id = "account:other";

    JsPublishAuthorizationResult own_result
        = js_publish_authorization_preflight(own_request, options);
    EXPECT_FALSE(own_result.ok);
    EXPECT_TRUE(has_code(own_result, JsPublishDiagnosticCode::PermissionMismatch));

    JsPublishRequest admin_request = make_request(JsPublishOperation::AdminSourceView);
    admin_request.token.scopes = JS_PUBLISH_SCOPE_ADMIN_SOURCE_VIEW;
    options.authority.allow_admin_source_view = true;

    JsPublishAuthorizationResult admin_result
        = js_publish_authorization_preflight(admin_request, options);
    EXPECT_TRUE(admin_result.ok);
}

TEST(JsPublishAuthorization, RejectsOwnRollbackForAnotherBuildersPackage)
{
    JsPublishRequest request = make_request(JsPublishOperation::PackageRollbackOwn);
    JsPublishAuthorizationOptions options = make_options();
    options.allow_mutating_operations = true;
    options.authority.package_owner_builder_account_id = "account:other";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, RejectsTransportDowngradeRedirectAndWrongServer)
{
    JsPublishRequest request = make_request();
    request.transport.secure_channel = false;
    request.transport.server_identity_verified = false;
    request.transport.downgrade_detected = true;
    request.transport.redirected_with_authorization = true;
    request.transport.server_audience = "server:other";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TransportRejected));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenServerMismatch));
}

TEST(JsPublishAuthorization, AllowsExplicitLocalhostDevelopmentTransport)
{
    JsPublishRequest request = make_request();
    request.transport.secure_channel = false;
    request.transport.localhost_development = true;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_TRUE(result.ok);
}

TEST(JsPublishAuthorization, RejectsExpiredRevokedWrongAudienceAndWrongActorTokens)
{
    JsPublishRequest request = make_request();
    request.token.revoked = true;
    request.token.expires_at_epoch_seconds = 99;
    request.token.actor_id = "actor:other";
    request.token.server_audience = "server:other";

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenRevoked));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenExpired));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenActorMismatch));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenAudienceMismatch));
}

TEST(JsPublishAuthorization, RejectsUnverifiedTokenClaimsMissingClockAndWorkspaceMismatch)
{
    JsPublishRequest request = make_request();
    request.token.claims_verified = false;
    request.token.workspace_id = "workspace:other";
    JsPublishAuthorizationOptions options = make_options();
    options.now_epoch_seconds = 0;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::InvalidRequest));
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::PermissionMismatch));
}

TEST(JsPublishAuthorization, RejectsFutureAndMalformedTokenLifetime)
{
    JsPublishRequest request = make_request();
    request.token.issued_at_epoch_seconds = 300;
    request.token.expires_at_epoch_seconds = 200;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::TokenExpired));
}

TEST(JsPublishAuthorization, ReportsRateLimitWithoutPackageExistenceDetail)
{
    JsPublishRequest request = make_request(JsPublishOperation::SourceView);
    JsPublishAuthorizationOptions options = make_options();
    options.rate_limited = true;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, options);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(has_code(result, JsPublishDiagnosticCode::RateLimited));
    EXPECT_EQ(messages(result).find("source"), std::string::npos)
        << "Rate-limit diagnostics should stay generic and avoid source existence leaks.";
}

TEST(JsPublishAuthorization, DiagnosticsAreBoundedSingleLineAndDoNotEchoToken)
{
    JsPublishRequest request = make_request();
    request.request_id = "request\n/secret/local/path/" + std::string(120, 'x');
    request.package_id = "pkg\nsecret-token-value\nfunction onEnter(){return true;}";
    request.token.token_id = "secret-token-value";
    request.transport.server_identity_verified = false;

    JsPublishAuthorizationResult result = js_publish_authorization_preflight(request, make_options());

    EXPECT_FALSE(result.ok);
    for (const JsPublishDiagnostic& diagnostic : result.diagnostics) {
        EXPECT_EQ(diagnostic.message.find('\n'), std::string::npos);
        EXPECT_EQ(diagnostic.message.find('\r'), std::string::npos);
        EXPECT_LE(diagnostic.message.size(), 220u);
        EXPECT_EQ(diagnostic.message.find("secret-token-value"), std::string::npos);
        EXPECT_EQ(diagnostic.request_id.find('\n'), std::string::npos);
        EXPECT_EQ(diagnostic.package_id.find('\n'), std::string::npos);
        EXPECT_LE(diagnostic.request_id.size(), 80u);
        EXPECT_LE(diagnostic.package_id.size(), 80u);
        EXPECT_EQ(diagnostic.package_id.find("function"), std::string::npos);
        EXPECT_EQ(diagnostic.request_id, "request:redacted");
        EXPECT_EQ(diagnostic.package_id, "package:redacted");
    }
}

TEST(JsPublishAuthorization, PublicNamesAreStable)
{
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::ManifestRead), "manifest-read");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::StatusRead), "status-read");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::DiffRead), "diff-read");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::SourceView), "source-view");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::AdminSourceView),
        "admin-source-view");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::PackageStage), "package-stage");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::PackageActivate), "package-activate");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::PackageRollbackOwn),
        "package-rollback-own");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::PackageRollbackAny),
        "package-rollback-any");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::AdminRevoke), "admin-revoke");
    EXPECT_STREQ(js_publish_operation_name(JsPublishOperation::PackageGarbageCollect),
        "package-gc");

    EXPECT_STREQ(js_publish_diagnostic_code_name(JsPublishDiagnosticCode::MissingScope),
        "missing-scope");
    EXPECT_STREQ(js_publish_diagnostic_code_name(JsPublishDiagnosticCode::PublishingDisabled),
        "publishing-disabled");
}

TEST(JsPublishAuthorization, BuildFilesReferencePublishAuthorizationSourcesAndTests)
{
    std::ifstream cmake_file("src/CMakeLists.txt");
    std::ifstream server_makefile("src/Makefile");
    std::ifstream test_makefile("src/tests/Makefile");
    ASSERT_TRUE(cmake_file.is_open());
    ASSERT_TRUE(server_makefile.is_open());
    ASSERT_TRUE(test_makefile.is_open());

    const std::string cmake((std::istreambuf_iterator<char>(cmake_file)),
        std::istreambuf_iterator<char>());
    const std::string server_make((std::istreambuf_iterator<char>(server_makefile)),
        std::istreambuf_iterator<char>());
    const std::string test_make((std::istreambuf_iterator<char>(test_makefile)),
        std::istreambuf_iterator<char>());

    EXPECT_NE(cmake.find("js_publish_authorization.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("tests/js_publish_authorization_tests.cpp"), std::string::npos);
    EXPECT_NE(server_make.find("js_publish_authorization.o"), std::string::npos);
    EXPECT_NE(test_make.find("js_publish_authorization.o"), std::string::npos);
    EXPECT_NE(test_make.find("js_publish_authorization_tests.cpp"), std::string::npos);
}
