#ifndef JS_LEGACY_TRIGGER_DISPATCH_H
#define JS_LEGACY_TRIGGER_DISPATCH_H

#include "js_trigger_dispatch.h"

#include <cstddef>
#include <string>

enum class JsLegacyTriggerDispatchStatus {
    Disabled,
    RegistryNotReady,
    StaleRegistry,
    NoMatch,
    Allow,
    Block,
    Error,
};

class JsLegacyTriggerReloadGeneration {
  public:
    JsLegacyTriggerReloadGeneration();

    bool valid() const;
    std::size_t successful_reload_count() const;

  private:
    explicit JsLegacyTriggerReloadGeneration(std::size_t successful_reload_count);

    std::size_t m_successful_reload_count = 0;
    bool m_valid = false;

    friend JsLegacyTriggerReloadGeneration
    js_legacy_trigger_reload_generation(const JsLiveRegistryReloadService &service);
};

struct JsLegacyTriggerDispatchOptions {
    bool enabled = false;
    bool require_fresh_reload = true;
    JsLegacyTriggerReloadGeneration expected_reload_generation;
    JsRuntimeLimits runtime_limits;
};

struct JsLegacyTriggerDispatchResult {
    JsLegacyTriggerDispatchStatus status = JsLegacyTriggerDispatchStatus::Disabled;
    JsTriggerDispatchResult dispatch_result;
    std::string diagnostic;
};

const char *js_legacy_trigger_dispatch_status_name(JsLegacyTriggerDispatchStatus status);
JsLegacyTriggerReloadGeneration
js_legacy_trigger_reload_generation(const JsLiveRegistryReloadService &service);

JsLegacyTriggerDispatchResult js_legacy_trigger_dispatch(
    const JsLiveRegistryReloadService &service, const JsTriggerDispatchRequest &request,
    const JsGameAdapterOptions &adapter_options,
    const JsLegacyTriggerDispatchOptions &options = {});

#endif
