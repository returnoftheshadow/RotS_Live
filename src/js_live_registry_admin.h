#ifndef JS_LIVE_REGISTRY_ADMIN_H
#define JS_LIVE_REGISTRY_ADMIN_H

#include "js_live_package_store.h"
#include "js_live_registry_reload_service.h"
#include "js_live_registry_status.h"

#include <string>

struct JsLiveRegistryAdminCommandResult {
    bool handled = false;
    bool ok = false;
    std::string output;
};

class JsLiveRegistryAdminService {
  public:
    JsLiveRegistryAdminService();
    explicit JsLiveRegistryAdminService(const JsLiveRegistryReloadOptions &reload_options);
    JsLiveRegistryAdminService(const JsLivePackageStoreOptions &live_store_options,
                               const JsLiveRegistryReloadOptions &reload_options);

    JsLiveRegistryReloadResult refresh();
    JsLiveRegistryStatusResult status_snapshot() const;
    JsLiveRegistryStatusResult status_for_package_id(const std::string &package_id) const;
    JsLiveRegistryStatusResult status_for_vnum(int vnum) const;

    // Publish/server hydration slices need the source-bearing store; admin status still uses
    // redacted DTOs from JsLiveRegistryStatusResult.
    JsLivePackageStore &live_store();
    const JsLiveRegistryReloadService &reload_service() const;

  private:
    JsLivePackageStore m_live_store;
    JsLiveRegistryReloadService m_reload_service;
};

JsLiveRegistryReloadOptions js_live_registry_server_reload_options();
JsLiveRegistryAdminService &js_live_registry_admin_service();
JsLiveRegistryReloadResult js_live_registry_startup_refresh();

std::string js_live_registry_format_reload_result(const JsLiveRegistryReloadResult &reload);
std::string js_live_registry_format_status_result(const JsLiveRegistryStatusResult &status);

JsLiveRegistryAdminCommandResult
js_live_registry_handle_reload_command(JsLiveRegistryAdminService &service,
                                       const std::string &argument);

#endif
