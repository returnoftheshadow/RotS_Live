#ifndef JS_LIVE_PACKAGE_STORE_PERSISTENCE_H
#define JS_LIVE_PACKAGE_STORE_PERSISTENCE_H

#include "js_live_package_store.h"

#include <string>
#include <vector>

constexpr int JS_LIVE_PACKAGE_STORE_JSON_SCHEMA_VERSION = 1;

struct JsLivePackageStorePersistenceDiagnostic {
    std::string message;
};

struct JsLivePackageStorePersistenceLoadResult {
    bool ok = false;
    JsLivePackageStoreSnapshot snapshot;
    std::vector<JsLivePackageStorePersistenceDiagnostic> diagnostics;
};

std::string js_live_package_store_snapshot_to_json(const JsLivePackageStoreSnapshot &snapshot);
JsLivePackageStorePersistenceLoadResult
js_live_package_store_snapshot_from_json(const std::string &json);

#endif
