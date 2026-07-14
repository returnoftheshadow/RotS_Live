#ifndef JS_LIVE_REGISTRY_ADMIN_H
#define JS_LIVE_REGISTRY_ADMIN_H

#include "js_live_package_store.h"
#include "js_live_package_store_persistence.h"
#include "js_live_registry_reload_service.h"
#include "js_live_registry_status.h"
#include "js_publish_endpoint_service.h"

#include <string>

struct JsLiveRegistryAdminCommandResult {
    bool handled = false;
    bool ok = false;
    std::string output;
};

struct JsLiveRegistryStartupLoadResult {
    bool ok = false;
    JsLivePackageStorePersistenceLoadResult file_load;
    JsLivePackageStoreHydrationResult store_hydration;
    JsLiveRegistryReloadResult reload;
    JsLivePackageStoreHydrationResult rollback_hydration;
};

class JsLiveRegistryAdminService {
  public:
    JsLiveRegistryAdminService();
    explicit JsLiveRegistryAdminService(const JsLiveRegistryReloadOptions &reload_options);
    JsLiveRegistryAdminService(const JsLivePackageStoreOptions &live_store_options,
                               const JsLiveRegistryReloadOptions &reload_options);
    JsLiveRegistryAdminService(const JsLiveRegistryAdminService &) = delete;
    JsLiveRegistryAdminService &operator=(const JsLiveRegistryAdminService &) = delete;
    JsLiveRegistryAdminService(JsLiveRegistryAdminService &&) = delete;
    JsLiveRegistryAdminService &operator=(JsLiveRegistryAdminService &&) = delete;

    JsLiveRegistryReloadResult refresh();
    JsLiveRegistryStartupLoadResult hydrate_from_file(const std::string &path);
    JsLiveRegistryStatusResult status_snapshot() const;
    JsLiveRegistryStatusResult status_for_package_id(const std::string &package_id) const;
    JsLiveRegistryStatusResult status_for_vnum(int vnum) const;

    // Publish/server hydration slices need the source-bearing store; admin status still uses
    // redacted DTOs from JsLiveRegistryStatusResult.
    JsLivePackageStore &live_store();
    JsPublishEndpointService &publish_service();
    const JsLiveRegistryReloadService &reload_service() const;

  private:
    JsLivePackageStore m_live_store;
    JsPublishEndpointService m_publish_service;
    JsLiveRegistryReloadService m_reload_service;
};

JsPublishEndpointServiceOptions js_publish_endpoint_server_options();
JsLiveRegistryReloadOptions js_live_registry_server_reload_options();
JsLiveRegistryAdminService &js_live_registry_admin_service();
JsLiveRegistryReloadResult js_live_registry_startup_refresh();
JsLiveRegistryStartupLoadResult js_live_registry_startup_load_file(const std::string &path);

std::string js_live_registry_format_reload_result(const JsLiveRegistryReloadResult &reload);
std::string js_live_registry_format_status_result(const JsLiveRegistryStatusResult &status);

JsLiveRegistryAdminCommandResult
js_live_registry_handle_reload_command(JsLiveRegistryAdminService &service,
                                       const std::string &argument);

#endif
