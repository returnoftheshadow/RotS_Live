#ifndef JS_PUBLISH_AUDIT_H
#define JS_PUBLISH_AUDIT_H

#include <cstddef>
#include <string>
#include <vector>

constexpr int JS_PUBLISH_AUDIT_JSONL_SCHEMA_VERSION = 1;

struct JsPublishAuditEvent {
    std::string operation;
    std::string audit_id;
    std::string request_id;
    std::string actor_id;
    std::string builder_account_id;
    std::string package_id;
    std::string package_version_id;
    std::string staged_digest;
    std::string expected_previous_live_checksum;
    std::string current_live_checksum;
    long long occurred_at_epoch_seconds = 0;
};

struct JsPublishAuditAppendOptions {
    std::string path;
    std::size_t maximum_event_bytes = 4096;
};

struct JsPublishAuditAppendDiagnostic {
    std::string message;
};

struct JsPublishAuditAppendResult {
    bool ok = false;
    std::vector<JsPublishAuditAppendDiagnostic> diagnostics;
};

JsPublishAuditAppendResult
js_publish_audit_append_event(const JsPublishAuditEvent &event,
                              const JsPublishAuditAppendOptions &options);

#endif
