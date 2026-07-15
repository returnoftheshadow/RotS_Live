#ifndef JS_BUILDER_SESSION_STORE_H
#define JS_BUILDER_SESSION_STORE_H

#include "js_builder_session.h"

#include <string>
#include <unordered_map>

enum class JsBuilderSessionStoreReason {
    Accepted,
    InvalidRequest,
    NotFound,
    Expired,
    Revoked,
};

struct JsBuilderSessionStoreOptions {
    long long now_epoch_seconds = 0;
    std::string server_audience;
    std::string workspace_id;
};

struct JsBuilderSessionStoreResult {
    bool ok = false;
    JsBuilderSessionStoreReason reason = JsBuilderSessionStoreReason::InvalidRequest;
    std::string reason_code;
    JsPublishTokenMetadata token_metadata;
    JsPublishBuilderEligibilityResult builder_eligibility;
};

struct JsBuilderSessionStoreRecord {
    JsPublishTokenMetadata token_metadata;
    JsPublishBuilderEligibilityResult builder_eligibility;
};

class JsBuilderSessionStore {
public:
    JsBuilderSessionStoreResult insert_login_result(const JsBuilderSessionLoginResult &login,
        const JsBuilderSessionStoreOptions &options = {});
    JsBuilderSessionStoreResult lookup(
        const std::string &session_token, const JsBuilderSessionStoreOptions &options = {}) const;
    JsBuilderSessionStoreResult revoke(
        const std::string &session_token, const JsBuilderSessionStoreOptions &options = {});
    std::size_t size() const;

private:
    std::unordered_map<std::string, JsBuilderSessionStoreRecord> m_sessions;
};

std::string js_builder_session_store_reason_code(JsBuilderSessionStoreReason reason);

#endif
