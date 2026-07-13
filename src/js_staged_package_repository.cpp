#include "js_staged_package_repository.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t MaxAuditFieldBytes = 160;
void add_diagnostic(JsStagedPackageStageResult &result,
                    JsStagedPackageRepositoryDiagnosticCode code, const std::string &message) {
    result.diagnostics.push_back({code, message});
}

void add_diagnostic(JsStagedPackageLookupResult &result,
                    JsStagedPackageRepositoryDiagnosticCode code, const std::string &message) {
    result.diagnostics.push_back({code, message});
}

bool same_identity_record(const JsStagedPackageRecord &left, const JsStagedPackageRecord &right) {
    if (left.identity.zone != right.identity.zone || left.identity.vnum != right.identity.vnum ||
        left.identity.host != right.identity.host ||
        left.identity.package_id != right.identity.package_id ||
        left.identity.package_version_id != right.identity.package_version_id ||
        left.identity.canonical_digest != right.identity.canonical_digest ||
        left.identity.digest_algorithm != right.identity.digest_algorithm ||
        left.identity.builder_account_id != right.identity.builder_account_id ||
        left.identity.server_instance_id != right.identity.server_instance_id ||
        left.identity.base_live_checksum != right.identity.base_live_checksum ||
        left.identity.manifest_checksum != right.identity.manifest_checksum ||
        left.identity.runtime_name != right.identity.runtime_name ||
        left.identity.runtime_version != right.identity.runtime_version ||
        left.identity.generated_typings_version != right.identity.generated_typings_version ||
        left.audit.staged_at_epoch_seconds != right.audit.staged_at_epoch_seconds ||
        left.audit.request_id != right.audit.request_id ||
        left.audit.actor_id != right.audit.actor_id ||
        left.audit.permission_snapshot_id != right.audit.permission_snapshot_id ||
        left.audit.audit_id != right.audit.audit_id ||
        left.audit.source_policy_decision != right.audit.source_policy_decision ||
        left.audit.validation_report_digest != right.audit.validation_report_digest ||
        left.audit.transport_source_identifier != right.audit.transport_source_identifier ||
        left.package.vnum != right.package.vnum || left.package.host != right.package.host ||
        left.package.compiled_javascript != right.package.compiled_javascript ||
        left.package.trigger_bindings.size() != right.package.trigger_bindings.size())
        return false;

    std::vector<JsScriptTriggerBinding> left_bindings = left.package.trigger_bindings;
    std::vector<JsScriptTriggerBinding> right_bindings = right.package.trigger_bindings;
    const auto binding_less = [](const JsScriptTriggerBinding &a, const JsScriptTriggerBinding &b) {
        if (a.kind != b.kind)
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        if (a.legacy_value != b.legacy_value)
            return a.legacy_value < b.legacy_value;
        return a.handler_name < b.handler_name;
    };
    std::sort(left_bindings.begin(), left_bindings.end(), binding_less);
    std::sort(right_bindings.begin(), right_bindings.end(), binding_less);

    for (std::size_t index = 0; index < left_bindings.size(); ++index) {
        const JsScriptTriggerBinding &left_binding = left_bindings[index];
        const JsScriptTriggerBinding &right_binding = right_bindings[index];
        if (left_binding.kind != right_binding.kind ||
            left_binding.legacy_value != right_binding.legacy_value ||
            left_binding.handler_name != right_binding.handler_name)
            return false;
    }

    return true;
}

bool is_blank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
}

bool is_bounded_single_line_field(const std::string &value) {
    if (value.size() > MaxAuditFieldBytes)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == ':' || ch == '_' || ch == '-' || ch == '.';
    });
}

void validate_audit_field(JsStagedPackageStageResult &result, const std::string &value,
                          const char *field_name, bool required) {
    if (required && is_blank(value)) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest,
                       std::string(field_name) + " is required for staged package audit metadata.");
        return;
    }
    if (!value.empty() && !is_bounded_single_line_field(value)) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest,
                       std::string(field_name) +
                           " must be a bounded single-line audit metadata value.");
    }
}

bool validate_audit_metadata(JsStagedPackageStageResult &result,
                             const JsStagedPackageAuditMetadata &audit) {
    if (audit.staged_at_epoch_seconds <= 0)
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest,
                       "staged_at_epoch_seconds must be positive.");
    validate_audit_field(result, audit.request_id, "request_id", true);
    validate_audit_field(result, audit.actor_id, "actor_id", true);
    validate_audit_field(result, audit.permission_snapshot_id, "permission_snapshot_id", true);
    validate_audit_field(result, audit.audit_id, "audit_id", true);
    validate_audit_field(result, audit.source_policy_decision, "source_policy_decision", true);
    validate_audit_field(result, audit.validation_report_digest, "validation_report_digest", true);
    validate_audit_field(result, audit.transport_source_identifier, "transport_source_identifier",
                         true);
    return result.diagnostics.empty();
}

} // namespace

JsStagedPackageRepository::JsStagedPackageRepository() = default;

JsStagedPackageRepository::JsStagedPackageRepository(
    const JsStagedPackageRepositoryOptions &options)
    : m_options(options) {}

JsStagedPackageStageResult
JsStagedPackageRepository::stage_package(const JsScriptPackage &package,
                                         const JsStagedPackageIdentityOptions &options) {
    JsStagedPackageStageOptions stage_options;
    stage_options.identity_options = options;
    stage_options.audit.staged_at_epoch_seconds = 1;
    stage_options.audit.request_id = "legacy-stage";
    stage_options.audit.actor_id = "legacy-stage-actor";
    stage_options.audit.permission_snapshot_id = "legacy-permission-snapshot";
    stage_options.audit.audit_id = "legacy-audit";
    stage_options.audit.source_policy_decision = "legacy-source-policy";
    stage_options.audit.validation_report_digest = "legacy-validation-report";
    stage_options.audit.transport_source_identifier = "legacy-transport";
    return stage_package(package, stage_options);
}

JsStagedPackageStageResult
JsStagedPackageRepository::stage_package(const JsScriptPackage &package,
                                         const JsStagedPackageStageOptions &options) {
    JsStagedPackageStageResult result;
    if (!validate_audit_metadata(result, options.audit))
        return result;

    result.identity_result = js_staged_package_identity_build(package, options.identity_options);
    if (!result.identity_result.ok) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed,
                       "Staged package identity creation failed.");
        return result;
    }

    JsStagedPackageRecord candidate;
    candidate.identity = result.identity_result.identity;
    candidate.audit = options.audit;
    candidate.package = package;

    const auto existing =
        std::find_if(m_records.begin(), m_records.end(), [&](const JsStagedPackageRecord &record) {
            return record.identity.package_id == candidate.identity.package_id &&
                   record.identity.package_version_id == candidate.identity.package_version_id;
        });

    if (existing != m_records.end()) {
        result.record = *existing;
        if (same_identity_record(*existing, candidate)) {
            result.ok = true;
            result.idempotent = true;
            return result;
        }

        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict,
                       "Staged package version already exists with different metadata.");
        return result;
    }

    if (m_records.size() >= m_options.maximum_records) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::RecordLimitExceeded,
                       "Staged package repository record limit exceeded.");
        return result;
    }

    m_records.push_back(candidate);
    result.ok = true;
    result.inserted = true;
    result.record = candidate;
    return result;
}

JsStagedPackageLookupResult
JsStagedPackageRepository::find_by_version(const std::string &package_id,
                                           const std::string &package_version_id) const {
    JsStagedPackageLookupResult result;
    if (is_blank(package_id) || is_blank(package_version_id)) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest,
                       "Staged package lookup requires package id and version id.");
        return result;
    }

    const auto existing =
        std::find_if(m_records.begin(), m_records.end(), [&](const JsStagedPackageRecord &record) {
            return record.identity.package_id == package_id &&
                   record.identity.package_version_id == package_version_id;
        });
    if (existing == m_records.end()) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::NotFound,
                       "Staged package version was not found.");
        return result;
    }

    result.ok = true;
    result.record = *existing;
    return result;
}

JsStagedPackageLookupResult
JsStagedPackageRepository::find_latest_for_package(const std::string &package_id) const {
    JsStagedPackageLookupResult result;
    if (is_blank(package_id)) {
        add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::InvalidRequest,
                       "Staged package lookup requires package id.");
        return result;
    }

    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) {
        if (it->identity.package_id == package_id) {
            result.ok = true;
            result.record = *it;
            return result;
        }
    }

    add_diagnostic(result, JsStagedPackageRepositoryDiagnosticCode::NotFound,
                   "Staged package was not found.");
    return result;
}

JsPublishAuthorityContext JsStagedPackageRepository::authority_context_for_version(
    const std::string &package_id, const std::string &package_version_id) const {
    JsStagedPackageLookupResult lookup = find_by_version(package_id, package_version_id);
    if (!lookup.ok)
        return {};
    return js_publish_authority_context_from_staged_identity(lookup.record.identity);
}

std::size_t JsStagedPackageRepository::size() const { return m_records.size(); }

bool JsStagedPackageRepository::empty() const { return m_records.empty(); }

const char *
js_staged_package_repository_diagnostic_code_name(JsStagedPackageRepositoryDiagnosticCode code) {
    switch (code) {
    case JsStagedPackageRepositoryDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsStagedPackageRepositoryDiagnosticCode::IdentityBuildFailed:
        return "identity-build-failed";
    case JsStagedPackageRepositoryDiagnosticCode::DuplicateVersionConflict:
        return "duplicate-version-conflict";
    case JsStagedPackageRepositoryDiagnosticCode::RecordLimitExceeded:
        return "record-limit-exceeded";
    case JsStagedPackageRepositoryDiagnosticCode::NotFound:
        return "not-found";
    }
    return "unknown";
}
