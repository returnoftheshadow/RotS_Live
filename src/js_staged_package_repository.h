#ifndef JS_STAGED_PACKAGE_REPOSITORY_H
#define JS_STAGED_PACKAGE_REPOSITORY_H

#include "js_staged_package_identity.h"

#include <cstddef>
#include <string>
#include <vector>

enum class JsStagedPackageRepositoryDiagnosticCode {
    InvalidRequest,
    IdentityBuildFailed,
    DuplicateVersionConflict,
    RecordLimitExceeded,
    NotFound,
};

struct JsStagedPackageRepositoryOptions {
    std::size_t maximum_records = 128;
};

struct JsStagedPackageAuditMetadata {
    long long staged_at_epoch_seconds = 0;
    std::string request_id;
    std::string actor_id;
    std::string permission_snapshot_id;
    std::string audit_id;
    std::string source_policy_decision;
    std::string validation_report_digest;
    std::string transport_source_identifier;
};

struct JsStagedPackageStageOptions {
    JsStagedPackageIdentityOptions identity_options;
    JsStagedPackageAuditMetadata audit;
};

struct JsStagedPackageRecord {
    JsStagedPackageIdentity identity;
    JsStagedPackageAuditMetadata audit;
    JsScriptPackage package;
};

struct JsStagedPackageRepositoryDiagnostic {
    JsStagedPackageRepositoryDiagnosticCode code =
        JsStagedPackageRepositoryDiagnosticCode::InvalidRequest;
    std::string message;
};

struct JsStagedPackageStageResult {
    bool ok = false;
    bool inserted = false;
    bool idempotent = false;
    JsStagedPackageRecord record;
    std::vector<JsStagedPackageRepositoryDiagnostic> diagnostics;
    JsStagedPackageIdentityResult identity_result;
};

struct JsStagedPackageLookupResult {
    bool ok = false;
    JsStagedPackageRecord record;
    std::vector<JsStagedPackageRepositoryDiagnostic> diagnostics;
};

class JsStagedPackageRepository {
  public:
    JsStagedPackageRepository();
    explicit JsStagedPackageRepository(const JsStagedPackageRepositoryOptions &options);

    JsStagedPackageStageResult stage_package(const JsScriptPackage &package,
                                             const JsStagedPackageIdentityOptions &options);
    JsStagedPackageStageResult stage_package(const JsScriptPackage &package,
                                             const JsStagedPackageStageOptions &options);

    JsStagedPackageLookupResult find_by_version(const std::string &package_id,
                                                const std::string &package_version_id) const;
    JsStagedPackageLookupResult find_latest_for_package(const std::string &package_id) const;

    JsPublishAuthorityContext
    authority_context_for_version(const std::string &package_id,
                                  const std::string &package_version_id) const;

    std::size_t size() const;
    bool empty() const;

  private:
    JsStagedPackageRepositoryOptions m_options;
    std::vector<JsStagedPackageRecord> m_records;
};

const char *
js_staged_package_repository_diagnostic_code_name(JsStagedPackageRepositoryDiagnosticCode code);

#endif
