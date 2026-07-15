#ifndef JS_BUILDER_PUBLISH_CONTEXT_H
#define JS_BUILDER_PUBLISH_CONTEXT_H

#include "js_live_package_store.h"
#include "js_publish_endpoint_transport.h"

#include <string>
#include <vector>

struct JsBuilderPublishZoneRecord {
    int number = 0;
    int top = 0;
    std::vector<int> owner_character_ids;
};

struct JsBuilderPublishTargetCatalog {
    std::vector<int> mobile_vnums;
    std::vector<int> object_vnums;
    std::vector<int> room_vnums;
    std::vector<JsBuilderPublishZoneRecord> zones;
};

struct JsBuilderPublishContextOptions {
    const JsBuilderPublishTargetCatalog *target_catalog = nullptr;
    const JsLivePackageStore *live_store = nullptr;
    std::size_t maximum_request_bytes = 64 * 1024;
};

struct JsBuilderPublishContextResult {
    bool ok = false;
    std::string reason_code;
    JsPublishEndpointTransportContext context;
};

JsBuilderPublishContextResult
js_builder_publish_context_resolve(const std::string &request_json,
                                   const JsPublishEndpointTransportContext &base_context,
                                   const JsBuilderPublishContextOptions &options);

#endif
