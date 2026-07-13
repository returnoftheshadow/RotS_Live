#include "js_publish_staging.h"

#include <algorithm>

namespace {

constexpr std::size_t MaxDiagnosticMessageBytes = 220;

bool is_blank(const std::string& value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    });
}

std::string bounded_single_line(std::string message)
{
    for (char& ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxDiagnosticMessageBytes)
        message.resize(MaxDiagnosticMessageBytes);
    return message;
}

void add_diagnostic(JsPublishStagedPackageStatusResult& result,
    JsPublishStagingDiagnosticCode code, const std::string& message)
{
    result.diagnostics.push_back({ code, bounded_single_line(message) });
}

void add_diagnostic(JsPublishStagedRequestAssemblyResult& result,
    JsPublishStagingDiagnosticCode code, const std::string& message)
{
    result.diagnostics.push_back({ code, bounded_single_line(message) });
}

bool is_staged_mutation_operation(JsPublishOperation operation)
{
    return operation == JsPublishOperation::PackageActivate
        || operation == JsPublishOperation::PackageRollbackOwn
        || operation == JsPublishOperation::PackageRollbackAny;
}

JsPublishStagedPackageStatus status_from_record(const JsStagedPackageRecord& record)
{
    JsPublishStagedPackageStatus status;
    status.zone = record.identity.zone;
    status.vnum = record.identity.vnum;
    status.host = record.identity.host;
    status.package_id = record.identity.package_id;
    status.package_version_id = record.identity.package_version_id;
    status.staged_digest = record.identity.canonical_digest;
    status.digest_algorithm = record.identity.digest_algorithm;
    status.canonical_format_version = record.identity.canonical_format_version;
    status.package_format_version = record.identity.package_format_version;
    status.base_live_checksum = record.identity.base_live_checksum;
    status.manifest_checksum = record.identity.manifest_checksum;
    status.compiled_javascript_checksum = record.identity.compiled_javascript_checksum;
    status.runtime_name = record.identity.runtime_name;
    status.runtime_version = record.identity.runtime_version;
    status.generated_typings_version = record.identity.generated_typings_version;
    return status;
}

JsPublishStagedPackageStatusResult status_from_lookup(JsStagedPackageLookupResult lookup)
{
    JsPublishStagedPackageStatusResult result;
    if (lookup.ok) {
        result.ok = true;
        result.status = status_from_record(lookup.record);
        return result;
    }

    JsPublishStagingDiagnosticCode code = JsPublishStagingDiagnosticCode::StagedPackageNotFound;
    for (const JsStagedPackageRepositoryDiagnostic& diagnostic : lookup.diagnostics) {
        if (diagnostic.code == JsStagedPackageRepositoryDiagnosticCode::InvalidRequest)
            code = JsPublishStagingDiagnosticCode::InvalidRequest;
        add_diagnostic(result, code, diagnostic.message);
    }
    if (result.diagnostics.empty())
        add_diagnostic(result, code, "Staged package record was not found.");
    return result;
}

} // namespace

JsPublishStagedPackageStatusResult
js_publish_staged_package_status(const JsStagedPackageRepository& repository,
    const std::string& package_id, const std::string& package_version_id)
{
    return status_from_lookup(repository.find_by_version(package_id, package_version_id));
}

JsPublishStagedPackageStatusResult
js_publish_latest_staged_package_status(const JsStagedPackageRepository& repository,
    const std::string& package_id)
{
    return status_from_lookup(repository.find_latest_for_package(package_id));
}

JsPublishStagedRequestAssemblyResult
js_publish_assemble_staged_package_request(const JsStagedPackageRepository& repository,
    const JsPublishStagedRequestAssemblyInput& input,
    const JsPublishStagedRequestAssemblyOptions& assembly_options)
{
    JsPublishStagedRequestAssemblyResult result;
    result.request.operation = input.operation;
    result.request.request_id = input.request_id;
    result.request.actor_id = input.actor_id;
    result.request.builder_account_id = input.builder_account_id;
    result.request.token = input.token;
    result.request.transport = input.transport;

    if (!is_staged_mutation_operation(input.operation)) {
        add_diagnostic(result, JsPublishStagingDiagnosticCode::InvalidRequest,
            "Only staged package activate and rollback operations can be assembled.");
        return result;
    }
    if (is_blank(input.package_id) || is_blank(input.package_version_id)) {
        add_diagnostic(result, JsPublishStagingDiagnosticCode::InvalidRequest,
            "Staged package request assembly requires package id and version id.");
        return result;
    }
    if (is_blank(assembly_options.current_live_checksum)) {
        add_diagnostic(result, JsPublishStagingDiagnosticCode::InvalidRequest,
            "Staged package request assembly requires current live checksum.");
        return result;
    }

    JsStagedPackageLookupResult lookup =
        repository.find_by_version(input.package_id, input.package_version_id);
    if (!lookup.ok) {
        for (const JsStagedPackageRepositoryDiagnostic& diagnostic : lookup.diagnostics) {
            JsPublishStagingDiagnosticCode code =
                diagnostic.code == JsStagedPackageRepositoryDiagnosticCode::InvalidRequest
                ? JsPublishStagingDiagnosticCode::InvalidRequest
                : JsPublishStagingDiagnosticCode::StagedPackageNotFound;
            add_diagnostic(result, code, diagnostic.message);
        }
        if (result.diagnostics.empty())
            add_diagnostic(result, JsPublishStagingDiagnosticCode::StagedPackageNotFound,
                "Staged package record was not found.");
        return result;
    }

    const JsStagedPackageRecord& record = lookup.record;
    result.status = status_from_record(record);

    result.request.zone = record.identity.zone;
    result.request.vnum = record.identity.vnum;
    result.request.host = record.identity.host;
    result.request.package_id = record.identity.package_id;
    result.request.package_version_id = record.identity.package_version_id;
    result.request.staged_digest = record.identity.canonical_digest;
    result.request.expected_live_checksum =
        is_blank(input.expected_live_checksum) ? record.identity.base_live_checksum
                                               : input.expected_live_checksum;
    result.request.manifest_checksum = record.identity.manifest_checksum;

    result.authorization_options.now_epoch_seconds = assembly_options.now_epoch_seconds;
    result.authorization_options.allow_mutating_operations =
        assembly_options.allow_mutating_operations;
    result.authorization_options.rate_limited = assembly_options.rate_limited;
    result.authorization_options.expected_server_audience =
        assembly_options.expected_server_audience;
    result.authorization_options.expected_workspace_id = assembly_options.expected_workspace_id;
    result.authorization_options.current_live_checksum = assembly_options.current_live_checksum;
    result.authorization_options.authority =
        js_publish_authority_context_from_staged_identity(record.identity);
    result.authorization_options.authority.allow_rollback_any =
        assembly_options.allow_rollback_any;

    result.assembled = true;
    result.authorization_result =
        js_publish_authorization_preflight(result.request, result.authorization_options);
    return result;
}

const char* js_publish_staging_diagnostic_code_name(JsPublishStagingDiagnosticCode code)
{
    switch (code) {
    case JsPublishStagingDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsPublishStagingDiagnosticCode::StagedPackageNotFound:
        return "staged-package-not-found";
    }
    return "unknown";
}
