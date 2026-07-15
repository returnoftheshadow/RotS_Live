#ifndef JS_LIVE_PACKAGE_STORE_H
#define JS_LIVE_PACKAGE_STORE_H

#include "js_script_registry.h"
#include "js_staged_package_repository.h"

#include <cstddef>
#include <string>
#include <vector>

enum class JsLivePackageStoreDiagnosticCode {
    InvalidRequest,
    DuplicatePackageRecordConflict,
    PackageRecordLimitExceeded,
    LivePointerLimitExceeded,
    PackageRecordNotFound,
    LivePointerNotFound,
    LivePointerConflict,
};

struct JsLivePackageStoreOptions {
    std::size_t maximum_package_records = 128;
    std::size_t maximum_live_pointers = 128;
    std::size_t retained_prior_package_records_per_slot = 20;
};

struct JsLivePackageRecord {
    JsStagedPackageIdentity identity;
    JsStagedPackageAuditMetadata staged_audit;
    JsScriptPackage package;
};

struct JsLivePackagePointer {
    int zone = 0;
    int vnum = 0;
    JsScriptPackageHost host = JsScriptPackageHost::Character;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string expected_previous_live_checksum;
    std::string current_live_checksum;
    long long loaded_at_epoch_seconds = 0;
    std::string load_audit_id;
};

struct JsLivePackageStoreDiagnostic {
    JsLivePackageStoreDiagnosticCode code = JsLivePackageStoreDiagnosticCode::InvalidRequest;
    std::string message;
};

struct JsLivePackageStoreRecordResult {
    bool ok = false;
    bool inserted = false;
    bool idempotent = false;
    JsLivePackageRecord record;
    std::vector<JsLivePackageStoreDiagnostic> diagnostics;
};

struct JsLivePackagePointerResult {
    bool ok = false;
    bool inserted = false;
    bool replaced = false;
    JsLivePackagePointer pointer;
    std::vector<JsLivePackageStoreDiagnostic> diagnostics;
};

struct JsLivePackageRegistrySnapshotResult {
    bool ok = false;
    JsScriptPackageRegistry registry;
    // Source-bearing internal dispatch/reload input. Endpoint/status code must use a redacted DTO.
    std::vector<JsScriptPackage> packages;
    std::vector<JsLivePackageStoreDiagnostic> diagnostics;
    JsScriptPackageValidationResult package_validation;
};

struct JsLivePackageStoreSnapshot {
    // Source-bearing persistence/dispatch input; endpoint/status code must use redacted DTOs.
    std::vector<JsLivePackageRecord> records;
    std::vector<JsLivePackagePointer> live_pointers;
    std::vector<JsLivePackagePointer> prior_live_pointers;
};

struct JsLivePackageStoreHydrationResult {
    bool ok = false;
    std::size_t records_loaded = 0;
    std::size_t live_pointers_loaded = 0;
    std::vector<JsLivePackageStoreDiagnostic> diagnostics;
};

class JsLivePackageStore {
  public:
    JsLivePackageStore();
    explicit JsLivePackageStore(const JsLivePackageStoreOptions &options);

    JsLivePackageStoreRecordResult store_staged_record(const JsStagedPackageRecord &record);
    JsLivePackageStoreRecordResult find_record(const std::string &package_id,
                                               const std::string &package_version_id) const;

    JsLivePackagePointerResult activate_staged_record_pointer(const JsStagedPackageRecord &record,
                                                              const JsLivePackagePointer &pointer);
    JsLivePackagePointerResult load_live_pointer(const JsLivePackagePointer &pointer);
    JsLivePackagePointerResult find_live_pointer(const std::string &package_id) const;
    JsLivePackagePointerResult find_live_pointer(int zone, JsScriptPackageHost host,
                                                 int vnum) const;
    JsLivePackagePointerResult find_latest_prior_live_pointer(int zone, JsScriptPackageHost host,
                                                              int vnum) const;

    JsLivePackageRegistrySnapshotResult
    build_live_registry_snapshot(const JsScriptRegistryReplaceOptions &options) const;

    JsLivePackageStoreSnapshot export_snapshot() const;
    JsLivePackageStoreHydrationResult hydrate_from_snapshot(
        const JsLivePackageStoreSnapshot &snapshot);

    std::size_t package_record_count() const;
    std::size_t live_pointer_count() const;
    bool empty() const;

  private:
    JsLivePackageStoreOptions m_options;
    std::vector<JsLivePackageRecord> m_records;
    std::vector<JsLivePackagePointer> m_live_pointers;
    std::vector<JsLivePackagePointer> m_prior_live_pointers;
};

const char *js_live_package_store_diagnostic_code_name(JsLivePackageStoreDiagnosticCode code);
std::string js_live_package_current_checksum_for_identity(const JsStagedPackageIdentity &identity);

#endif
