#ifndef JS_PUBLISH_ENDPOINT_TRANSPORT_H
#define JS_PUBLISH_ENDPOINT_TRANSPORT_H

#include "js_builder_session_store.h"
#include "js_publish_endpoint_service.h"

#include <cstddef>
#include <string>
#include <vector>

struct JsPublishEndpointTransportOptions {
    std::size_t maximum_request_bytes = 64 * 1024;
};

struct JsPublishEndpointTransportContext {
    std::string request_id;
    std::string audit_id;
    std::string actor_id;
    std::string builder_account_id;
    int zone = 0;

    // Server-derived publish authority inputs. Populate these from trusted account,
    // character, vnum, and zone lookups only; the fail-closed defaults are intentional.
    JsPublishBuilderEligibilityResult builder_eligibility;
    bool target_zone_resolved = false;
    int server_resolved_target_zone = 0;
    JsScriptPackageHost server_resolved_target_host = JsScriptPackageHost::Character;
    bool zone_exists = false;
    bool zone_allows_all_builders = false;
    std::vector<int> zone_owner_character_ids;
    JsPublishTokenMetadata token;
    JsPublishTransportMetadata transport;
    long long now_epoch_seconds = 0;
    bool allow_mutating_operations = false;
    bool allow_live_pointer_update = false;
    bool allow_rollback_any = false;
    long long applied_at_epoch_seconds = 0;
    std::string expected_server_audience;
    std::string expected_workspace_id;
    std::string current_live_checksum;
    std::string live_store_persistence_path;
    std::string publish_audit_log_path;
};

struct JsPublishEndpointTransportResult {
    bool ok = false;
    int http_status = 500;
    std::string reason_code;
    std::string json;
};

struct JsPublishEndpointSessionContextResult {
    bool ok = false;
    std::string reason_code;
    JsBuilderSessionStoreReason session_reason = JsBuilderSessionStoreReason::InvalidRequest;
    JsPublishEndpointTransportContext context;
};

JsPublishEndpointSessionContextResult js_publish_endpoint_context_from_builder_session(
    const JsPublishEndpointTransportContext &base_context,
    const JsBuilderSessionStore &session_store, const std::string &bearer_token,
    const JsBuilderSessionStoreOptions &session_options);

JsPublishEndpointTransportResult js_publish_endpoint_dispatch_json(
    JsPublishEndpointService &service, const std::string &request_json,
    const JsPublishEndpointTransportContext &context,
    const JsPublishEndpointTransportOptions &options = {});

#endif
