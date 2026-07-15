#ifndef JS_PUBLISH_ACTIVATION_H
#define JS_PUBLISH_ACTIVATION_H

#include "js_live_package_store.h"
#include "js_live_package_store_persistence.h"
#include "js_publish_staging.h"

#include <functional>
#include <string>
#include <vector>

enum class JsPublishActivationDiagnosticCode {
    InvalidRequest,
    AssemblyFailed,
    AuthorizationFailed,
    LiveUpdateDisabled,
    AuditPreconditionFailed,
    LivePointerConflict,
    StoreFailed,
    PointerFailed,
    PersistenceFailed,
};

struct JsPublishActivationOptions {
    JsPublishStagedRequestAssemblyOptions assembly_options;
    bool allow_live_pointer_update = false;
    bool durable_audit_precondition_ok = false;
    std::function<bool(const JsPublishStagedPackageStatus &)> durable_audit_append;
    long long applied_at_epoch_seconds = 0;
    std::string live_pointer_audit_id;
    std::string persist_live_store_path;
};

struct JsPublishActivationDiagnostic {
    JsPublishActivationDiagnosticCode code = JsPublishActivationDiagnosticCode::InvalidRequest;
    std::string message;
};

struct JsPublishActivationResult {
    bool ok = false;
    bool assembled = false;
    bool authorized = false;
    bool applied = false;
    JsPublishStagedRequestAssemblyResult assembly;
    JsLivePackagePointerResult live_pointer_result;
    JsLivePackageStorePersistenceFileResult persistence_result;
    JsLivePackageStoreHydrationResult rollback_hydration;
    std::vector<JsPublishActivationDiagnostic> diagnostics;
};

JsPublishActivationResult
js_publish_apply_staged_package_activation(const JsStagedPackageRepository &repository,
                                           JsLivePackageStore &live_store,
                                           const JsPublishStagedRequestAssemblyInput &input,
                                           const JsPublishActivationOptions &options = {});

JsPublishActivationResult
js_publish_apply_live_package_activation(const JsLivePackageRecord &record,
                                         JsLivePackageStore &live_store,
                                         const JsPublishStagedRequestAssemblyInput &input,
                                         const JsPublishActivationOptions &options = {});

const char *js_publish_activation_diagnostic_code_name(JsPublishActivationDiagnosticCode code);

#endif
