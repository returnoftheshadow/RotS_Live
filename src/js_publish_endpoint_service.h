#ifndef JS_PUBLISH_ENDPOINT_SERVICE_H
#define JS_PUBLISH_ENDPOINT_SERVICE_H

#include "js_publish_endpoint_contract.h"

struct JsPublishEndpointServiceResult {
    JsPublishEndpointResponse response;
    std::string json;
};

struct JsPublishEndpointServiceOptions {
    JsScriptPackageValidationOptions package_validation_options;
    std::string server_instance_id = "server:local";
};

struct JsPublishEndpointStageInput {
    JsStagedPackageStageOptions stage_options;
    JsPublishRequest authorization_request;
    JsPublishAuthorizationOptions authorization_options;
    std::string audit_id;
};

struct JsPublishEndpointStatusInput {
    std::string package_id;
    JsPublishRequest authorization_request;
    JsPublishAuthorizationOptions authorization_options;
};

class JsPublishEndpointService {
  public:
    explicit JsPublishEndpointService(JsLivePackageStore &live_store);
    JsPublishEndpointService(JsLivePackageStore &live_store,
                             const JsPublishEndpointServiceOptions &options);
    JsPublishEndpointService(JsLivePackageStore &live_store,
                             const JsStagedPackageRepositoryOptions &staged_options);
    JsPublishEndpointService(JsLivePackageStore &live_store,
                             const JsStagedPackageRepositoryOptions &staged_options,
                             const JsPublishEndpointServiceOptions &options);

    JsPublishEndpointServiceResult stage(const JsPublishEndpointStageInput &input);
    JsPublishEndpointServiceResult status(const JsPublishEndpointStatusInput &input) const;
    JsPublishEndpointServiceResult activate(const JsPublishStagedRequestAssemblyInput &input,
                                            const JsPublishActivationOptions &options);
    JsPublishEndpointServiceResult rollback(const JsPublishStagedRequestAssemblyInput &input,
                                            const JsPublishActivationOptions &options);

    const JsStagedPackageRepository &staged_repository() const;
    const JsLivePackageStore &live_store() const;

  private:
    JsPublishEndpointServiceResult with_json(const JsPublishEndpointResponse &response) const;

    JsLivePackageStore &m_live_store;
    JsStagedPackageRepository m_staged_repository;
    JsPublishEndpointServiceOptions m_options;
};

#endif
