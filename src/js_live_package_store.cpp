#include "js_live_package_store.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t MaxLivePointerFieldBytes = 180;

bool is_blank(const std::string &value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    });
}

std::string digest_body(const std::string &digest) {
    const std::string::size_type colon = digest.find(':');
    return colon == std::string::npos ? digest : digest.substr(colon + 1);
}

std::string live_checksum_for_record(const JsLivePackageRecord &record) {
    return js_live_package_current_checksum_for_identity(record.identity);
}

bool is_bounded_identifier(const std::string &value) {
    if (value.size() > MaxLivePointerFieldBytes)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == ':' || ch == '_' || ch == '-' || ch == '.';
    });
}

void add_diagnostic(JsLivePackageStoreRecordResult &result, JsLivePackageStoreDiagnosticCode code,
                    const std::string &message) {
    result.diagnostics.push_back({code, message});
}

void add_diagnostic(JsLivePackagePointerResult &result, JsLivePackageStoreDiagnosticCode code,
                    const std::string &message) {
    result.diagnostics.push_back({code, message});
}

void add_diagnostic(JsLivePackageRegistrySnapshotResult &result,
                    JsLivePackageStoreDiagnosticCode code, const std::string &message) {
    result.diagnostics.push_back({code, message});
}

bool same_binding(const JsScriptTriggerBinding &left, const JsScriptTriggerBinding &right) {
    return left.kind == right.kind && left.legacy_value == right.legacy_value &&
           left.handler_name == right.handler_name;
}

bool same_package(const JsScriptPackage &left, const JsScriptPackage &right) {
    if (left.vnum != right.vnum || left.package_id != right.package_id || left.host != right.host ||
        left.package_format_version != right.package_format_version ||
        left.manifest_schema_version != right.manifest_schema_version ||
        left.trigger_catalog_revision != right.trigger_catalog_revision ||
        left.manifest_checksum != right.manifest_checksum ||
        left.runtime_name != right.runtime_name || left.runtime_version != right.runtime_version ||
        left.generated_typings_version != right.generated_typings_version ||
        left.compiled_javascript_checksum != right.compiled_javascript_checksum ||
        left.compiled_javascript != right.compiled_javascript ||
        left.trigger_bindings.size() != right.trigger_bindings.size())
        return false;

    for (std::size_t index = 0; index < left.trigger_bindings.size(); ++index) {
        if (!same_binding(left.trigger_bindings[index], right.trigger_bindings[index]))
            return false;
    }
    return true;
}

bool same_identity(const JsStagedPackageIdentity &left, const JsStagedPackageIdentity &right) {
    return left.zone == right.zone && left.vnum == right.vnum && left.host == right.host &&
           left.package_id == right.package_id &&
           left.package_version_id == right.package_version_id &&
           left.canonical_digest == right.canonical_digest &&
           left.digest_algorithm == right.digest_algorithm &&
           left.canonical_format_version == right.canonical_format_version &&
           left.package_format_version == right.package_format_version &&
           left.builder_account_id == right.builder_account_id &&
           left.server_instance_id == right.server_instance_id &&
           left.base_live_checksum == right.base_live_checksum &&
           left.manifest_checksum == right.manifest_checksum &&
           left.compiled_javascript_checksum == right.compiled_javascript_checksum &&
           left.runtime_name == right.runtime_name &&
           left.runtime_version == right.runtime_version &&
           left.generated_typings_version == right.generated_typings_version;
}

bool same_audit(const JsStagedPackageAuditMetadata &left,
                const JsStagedPackageAuditMetadata &right) {
    return left.staged_at_epoch_seconds == right.staged_at_epoch_seconds &&
           left.request_id == right.request_id && left.actor_id == right.actor_id &&
           left.permission_snapshot_id == right.permission_snapshot_id &&
           left.audit_id == right.audit_id &&
           left.source_policy_decision == right.source_policy_decision &&
           left.validation_report_digest == right.validation_report_digest &&
           left.transport_source_identifier == right.transport_source_identifier;
}

bool same_record(const JsLivePackageRecord &left, const JsLivePackageRecord &right) {
    return same_identity(left.identity, right.identity) &&
           same_audit(left.staged_audit, right.staged_audit) &&
           same_package(left.package, right.package);
}

bool same_live_slot(const JsLivePackagePointer &left, const JsLivePackagePointer &right) {
    return left.zone == right.zone && left.host == right.host && left.vnum == right.vnum;
}

JsLivePackageRecord live_record_from_staged(const JsStagedPackageRecord &record) {
    JsLivePackageRecord live_record;
    live_record.identity = record.identity;
    live_record.staged_audit = record.audit;
    live_record.package = record.package;
    live_record.package.package_id = record.identity.package_id;
    return live_record;
}

bool validate_live_record_shape(JsLivePackageStoreRecordResult &result,
                                const JsLivePackageRecord &record) {
    if (is_blank(record.identity.package_id) || is_blank(record.identity.package_version_id) ||
        is_blank(record.identity.canonical_digest)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package records require package id, version id, and staged digest.");
        return false;
    }
    if (record.identity.zone <= 0 || record.identity.vnum <= 0 ||
        record.identity.vnum != record.package.vnum ||
        record.identity.host != record.package.host ||
        record.identity.package_id != js_staged_package_logical_package_id(record.identity.zone,
                                                                           record.identity.host,
                                                                           record.identity.vnum) ||
        record.identity.package_version_id != js_staged_package_version_id(record.identity) ||
        record.identity.manifest_checksum != record.package.manifest_checksum ||
        record.identity.compiled_javascript_checksum !=
            js_script_package_compiled_javascript_checksum(record.package)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package record identity does not match package metadata.");
        return false;
    }
    if (record.staged_audit.staged_at_epoch_seconds <= 0 ||
        is_blank(record.staged_audit.request_id) || is_blank(record.staged_audit.actor_id) ||
        is_blank(record.staged_audit.permission_snapshot_id) ||
        is_blank(record.staged_audit.audit_id) ||
        is_blank(record.staged_audit.source_policy_decision) ||
        is_blank(record.staged_audit.validation_report_digest) ||
        is_blank(record.staged_audit.transport_source_identifier)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package record requires complete staged audit metadata.");
        return false;
    }
    JsScriptPackageValidationOptions validation_options;
    validation_options.mode = JsScriptPackageValidationMode::InternalValidationOnly;
    JsScriptPackageValidationResult validation =
        js_script_package_validate(record.package, validation_options);
    if (!validation.ok) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package record package validation failed.");
        return false;
    }
    JsStagedPackageIdentityOptions identity_options;
    identity_options.zone = record.identity.zone;
    identity_options.builder_account_id = record.identity.builder_account_id;
    identity_options.base_live_checksum = record.identity.base_live_checksum;
    identity_options.server_instance_id = record.identity.server_instance_id;
    identity_options.canonical_format_version = record.identity.canonical_format_version;
    identity_options.package_validation_options = validation_options;
    JsStagedPackageIdentityResult rebuilt_identity =
        js_staged_package_identity_build(record.package, identity_options);
    if (!rebuilt_identity.ok || !same_identity(record.identity, rebuilt_identity.identity)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package record canonical identity verification failed.");
        return false;
    }
    return true;
}

bool validate_live_pointer_shape(JsLivePackagePointerResult &result,
                                 const JsLivePackagePointer &pointer) {
    if (pointer.zone < 0 || pointer.vnum <= 0 || is_blank(pointer.package_id) ||
        is_blank(pointer.package_version_id) || is_blank(pointer.staged_digest) ||
        is_blank(pointer.current_live_checksum) || !is_bounded_identifier(pointer.package_id) ||
        !is_bounded_identifier(pointer.package_version_id) ||
        !is_bounded_identifier(pointer.staged_digest) ||
        !is_bounded_identifier(pointer.current_live_checksum) ||
        (!pointer.expected_previous_live_checksum.empty() &&
         !is_bounded_identifier(pointer.expected_previous_live_checksum)) ||
        !is_bounded_identifier(pointer.load_audit_id) || pointer.loaded_at_epoch_seconds <= 0 ||
        is_blank(pointer.load_audit_id)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package pointer requires slot, package version, digest, checksum, "
                       "timestamp, and audit id.");
        return false;
    }
    return true;
}

bool validate_pointer_matches_record(JsLivePackagePointerResult &result,
                                     const JsLivePackagePointer &pointer,
                                     const JsLivePackageRecord &record) {
    if (record.identity.zone != pointer.zone || record.identity.vnum != pointer.vnum ||
        record.identity.host != pointer.host ||
        record.identity.canonical_digest != pointer.staged_digest) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live pointer does not match the stored package record.");
        return false;
    }
    const std::string derived_live_checksum = live_checksum_for_record(record);
    if (pointer.current_live_checksum != derived_live_checksum) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live pointer checksum does not match the stored package record.");
        return false;
    }
    return true;
}

void copy_record_diagnostics(JsLivePackagePointerResult &result,
                             const JsLivePackageStoreRecordResult &record_result) {
    for (const JsLivePackageStoreDiagnostic &diagnostic : record_result.diagnostics)
        add_diagnostic(result, diagnostic.code, diagnostic.message);
}

} // namespace

JsLivePackageStore::JsLivePackageStore() = default;

JsLivePackageStore::JsLivePackageStore(const JsLivePackageStoreOptions &options)
    : m_options(options) {}

JsLivePackageStoreRecordResult
JsLivePackageStore::store_staged_record(const JsStagedPackageRecord &record) {
    JsLivePackageStoreRecordResult result;
    JsLivePackageRecord candidate = live_record_from_staged(record);
    if (!validate_live_record_shape(result, candidate))
        return result;

    const auto existing =
        std::find_if(m_records.begin(), m_records.end(), [&](const JsLivePackageRecord &stored) {
            return stored.identity.package_id == candidate.identity.package_id &&
                   stored.identity.package_version_id == candidate.identity.package_version_id;
        });
    if (existing != m_records.end()) {
        result.record = *existing;
        if (same_record(*existing, candidate)) {
            result.ok = true;
            result.idempotent = true;
            return result;
        }
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict,
                       "Live package record already exists with different metadata.");
        return result;
    }

    if (m_records.size() >= m_options.maximum_package_records) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded,
                       "Live package record limit exceeded.");
        return result;
    }

    m_records.push_back(candidate);
    result.ok = true;
    result.inserted = true;
    result.record = candidate;
    return result;
}

JsLivePackageStoreRecordResult
JsLivePackageStore::find_record(const std::string &package_id,
                                const std::string &package_version_id) const {
    JsLivePackageStoreRecordResult result;
    if (is_blank(package_id) || is_blank(package_version_id)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package record lookup requires package id and version id.");
        return result;
    }

    const auto existing =
        std::find_if(m_records.begin(), m_records.end(), [&](const JsLivePackageRecord &stored) {
            return stored.identity.package_id == package_id &&
                   stored.identity.package_version_id == package_version_id;
        });
    if (existing == m_records.end()) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::PackageRecordNotFound,
                       "Live package record was not found.");
        return result;
    }

    result.ok = true;
    result.record = *existing;
    return result;
}

JsLivePackagePointerResult
JsLivePackageStore::activate_staged_record_pointer(const JsStagedPackageRecord &record,
                                                   const JsLivePackagePointer &pointer) {
    JsLivePackagePointerResult result;
    JsLivePackageStoreRecordResult record_validation;
    JsLivePackageRecord candidate = live_record_from_staged(record);
    if (!validate_live_record_shape(record_validation, candidate)) {
        copy_record_diagnostics(result, record_validation);
        return result;
    }
    if (!validate_live_pointer_shape(result, pointer) ||
        !validate_pointer_matches_record(result, pointer, candidate))
        return result;

    bool insert_record = false;
    const auto existing_record =
        std::find_if(m_records.begin(), m_records.end(), [&](const JsLivePackageRecord &stored) {
            return stored.identity.package_id == candidate.identity.package_id &&
                   stored.identity.package_version_id == candidate.identity.package_version_id;
        });
    if (existing_record != m_records.end()) {
        if (!same_record(*existing_record, candidate)) {
            add_diagnostic(result, JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict,
                           "Live package record already exists with different metadata.");
            return result;
        }
    } else {
        if (m_records.size() >= m_options.maximum_package_records) {
            add_diagnostic(result, JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded,
                           "Live package record limit exceeded.");
            return result;
        }
        insert_record = true;
    }
    const std::string derived_live_checksum = live_checksum_for_record(candidate);

    const auto existing = std::find_if(
        m_live_pointers.begin(), m_live_pointers.end(),
        [&](const JsLivePackagePointer &stored) { return same_live_slot(stored, pointer); });
    if (existing == m_live_pointers.end()) {
        if (m_live_pointers.size() >= m_options.maximum_live_pointers) {
            add_diagnostic(result, JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded,
                           "Live package pointer limit exceeded.");
            return result;
        }
        if (insert_record)
            m_records.push_back(candidate);
        JsLivePackagePointer stored_pointer = pointer;
        stored_pointer.current_live_checksum = derived_live_checksum;
        m_live_pointers.push_back(stored_pointer);
        result.ok = true;
        result.inserted = true;
        result.pointer = stored_pointer;
        return result;
    }

    if (pointer.expected_previous_live_checksum != existing->current_live_checksum) {
        add_diagnostic(
            result, JsLivePackageStoreDiagnosticCode::LivePointerConflict,
            "Live pointer replacement expected checksum does not match current live pointer.");
        return result;
    }

    if (insert_record)
        m_records.push_back(candidate);
    JsLivePackagePointer stored_pointer = pointer;
    stored_pointer.current_live_checksum = derived_live_checksum;
    *existing = stored_pointer;
    result.ok = true;
    result.replaced = true;
    result.pointer = stored_pointer;
    return result;
}

JsLivePackagePointerResult
JsLivePackageStore::load_live_pointer(const JsLivePackagePointer &pointer) {
    JsLivePackagePointerResult result;
    if (!validate_live_pointer_shape(result, pointer))
        return result;

    JsLivePackageStoreRecordResult record =
        find_record(pointer.package_id, pointer.package_version_id);
    if (!record.ok) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::PackageRecordNotFound,
                       "Live pointer package record was not found.");
        return result;
    }
    if (!validate_pointer_matches_record(result, pointer, record.record))
        return result;
    const std::string derived_live_checksum = live_checksum_for_record(record.record);

    const auto existing = std::find_if(
        m_live_pointers.begin(), m_live_pointers.end(),
        [&](const JsLivePackagePointer &stored) { return same_live_slot(stored, pointer); });
    if (existing == m_live_pointers.end()) {
        if (m_live_pointers.size() >= m_options.maximum_live_pointers) {
            add_diagnostic(result, JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded,
                           "Live package pointer limit exceeded.");
            return result;
        }
        JsLivePackagePointer stored_pointer = pointer;
        stored_pointer.current_live_checksum = derived_live_checksum;
        m_live_pointers.push_back(stored_pointer);
        result.ok = true;
        result.inserted = true;
        result.pointer = stored_pointer;
        return result;
    }

    if (pointer.expected_previous_live_checksum != existing->current_live_checksum) {
        add_diagnostic(
            result, JsLivePackageStoreDiagnosticCode::LivePointerConflict,
            "Live pointer replacement expected checksum does not match current live pointer.");
        return result;
    }

    JsLivePackagePointer stored_pointer = pointer;
    stored_pointer.current_live_checksum = derived_live_checksum;
    *existing = stored_pointer;
    result.ok = true;
    result.replaced = true;
    result.pointer = stored_pointer;
    return result;
}

JsLivePackagePointerResult
JsLivePackageStore::find_live_pointer(const std::string &package_id) const {
    JsLivePackagePointerResult result;
    if (is_blank(package_id)) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package pointer lookup requires package id.");
        return result;
    }

    const auto existing = std::find_if(
        m_live_pointers.begin(), m_live_pointers.end(),
        [&](const JsLivePackagePointer &stored) { return stored.package_id == package_id; });
    if (existing == m_live_pointers.end()) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::LivePointerNotFound,
                       "Live package pointer was not found.");
        return result;
    }

    result.ok = true;
    result.pointer = *existing;
    return result;
}

JsLivePackagePointerResult JsLivePackageStore::find_live_pointer(int zone, JsScriptPackageHost host,
                                                                 int vnum) const {
    JsLivePackagePointerResult result;
    if (zone < 0 || vnum <= 0) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::InvalidRequest,
                       "Live package pointer slot lookup requires a valid zone and vnum.");
        return result;
    }

    const auto existing = std::find_if(
        m_live_pointers.begin(), m_live_pointers.end(), [&](const JsLivePackagePointer &stored) {
            return stored.zone == zone && stored.host == host && stored.vnum == vnum;
        });
    if (existing == m_live_pointers.end()) {
        add_diagnostic(result, JsLivePackageStoreDiagnosticCode::LivePointerNotFound,
                       "Live package pointer was not found.");
        return result;
    }

    result.ok = true;
    result.pointer = *existing;
    return result;
}

JsLivePackageRegistrySnapshotResult JsLivePackageStore::build_live_registry_snapshot(
    const JsScriptRegistryReplaceOptions &options) const {
    JsLivePackageRegistrySnapshotResult result;
    std::vector<JsScriptPackage> live_packages;
    for (const JsLivePackagePointer &pointer : m_live_pointers) {
        JsLivePackageStoreRecordResult record =
            find_record(pointer.package_id, pointer.package_version_id);
        if (!record.ok || record.record.identity.canonical_digest != pointer.staged_digest) {
            add_diagnostic(result, JsLivePackageStoreDiagnosticCode::PackageRecordNotFound,
                           "Live pointer package record was not found.");
            return result;
        }
        live_packages.push_back(record.record.package);
    }

    result.ok = result.registry.replace_all(live_packages, options, &result.package_validation);
    return result;
}

std::size_t JsLivePackageStore::package_record_count() const { return m_records.size(); }

std::size_t JsLivePackageStore::live_pointer_count() const { return m_live_pointers.size(); }

bool JsLivePackageStore::empty() const { return m_records.empty() && m_live_pointers.empty(); }

const char *js_live_package_store_diagnostic_code_name(JsLivePackageStoreDiagnosticCode code) {
    switch (code) {
    case JsLivePackageStoreDiagnosticCode::InvalidRequest:
        return "invalid-request";
    case JsLivePackageStoreDiagnosticCode::DuplicatePackageRecordConflict:
        return "duplicate-package-record-conflict";
    case JsLivePackageStoreDiagnosticCode::PackageRecordLimitExceeded:
        return "package-record-limit-exceeded";
    case JsLivePackageStoreDiagnosticCode::LivePointerLimitExceeded:
        return "live-pointer-limit-exceeded";
    case JsLivePackageStoreDiagnosticCode::PackageRecordNotFound:
        return "package-record-not-found";
    case JsLivePackageStoreDiagnosticCode::LivePointerNotFound:
        return "live-pointer-not-found";
    case JsLivePackageStoreDiagnosticCode::LivePointerConflict:
        return "live-pointer-conflict";
    }
    return "unknown";
}

std::string js_live_package_current_checksum_for_identity(const JsStagedPackageIdentity &identity) {
    return std::string("live:") + identity.digest_algorithm + ":" +
           digest_body(identity.canonical_digest);
}
