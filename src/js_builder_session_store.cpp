#include "js_builder_session_store.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t MaxSessionTokenBytes = 512;

bool is_safe_session_token(const std::string &token)
{
    return !token.empty() && token.size() <= MaxSessionTokenBytes
        && std::none_of(token.begin(), token.end(), [](unsigned char ch) {
               return std::iscntrl(ch) || std::isspace(ch);
           });
}

bool is_blank(const std::string &value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
}

JsBuilderSessionStoreResult result_for(JsBuilderSessionStoreReason reason)
{
    JsBuilderSessionStoreResult result;
    result.ok = reason == JsBuilderSessionStoreReason::Accepted;
    result.reason = reason;
    result.reason_code = js_builder_session_store_reason_code(reason);
    return result;
}

bool metadata_is_usable(const JsPublishTokenMetadata &metadata)
{
    return metadata.claims_verified && !metadata.token_id.empty()
        && !metadata.actor_id.empty() && !metadata.builder_account_id.empty()
        && metadata.issued_at_epoch_seconds > 0
        && metadata.expires_at_epoch_seconds > metadata.issued_at_epoch_seconds;
}

bool lookup_context_is_usable(const JsBuilderSessionStoreOptions &options)
{
    return options.now_epoch_seconds > 0 && !is_blank(options.server_audience);
}

JsBuilderSessionStoreResult result_with_metadata(
    JsBuilderSessionStoreReason reason, const JsPublishTokenMetadata &metadata)
{
    JsBuilderSessionStoreResult result = result_for(reason);
    result.token_metadata = metadata;
    return result;
}

} // namespace

std::string js_builder_session_store_reason_code(JsBuilderSessionStoreReason reason)
{
    switch (reason) {
    case JsBuilderSessionStoreReason::Accepted:
        return "builder.session.accepted";
    case JsBuilderSessionStoreReason::InvalidRequest:
        return "builder.session.invalid-request";
    case JsBuilderSessionStoreReason::NotFound:
        return "builder.session.not-found";
    case JsBuilderSessionStoreReason::Expired:
        return "builder.session.expired";
    case JsBuilderSessionStoreReason::Revoked:
        return "builder.session.revoked";
    }
    return "builder.session.invalid-request";
}

JsBuilderSessionStoreResult JsBuilderSessionStore::insert_login_result(
    const JsBuilderSessionLoginResult &login, const JsBuilderSessionStoreOptions &options)
{
    if (!login.ok || !is_safe_session_token(login.session_token)
        || !metadata_is_usable(login.token_metadata) || is_blank(options.server_audience)
        || login.token_metadata.token_id != js_builder_session_token_id(login.session_token))
        return result_for(JsBuilderSessionStoreReason::InvalidRequest);

    JsPublishTokenMetadata metadata = login.token_metadata;
    metadata.server_audience = options.server_audience;
    metadata.workspace_id = options.workspace_id;
    metadata.revoked = false;
    m_sessions[login.session_token] = metadata;
    return result_with_metadata(JsBuilderSessionStoreReason::Accepted, metadata);
}

JsBuilderSessionStoreResult JsBuilderSessionStore::lookup(
    const std::string &session_token, const JsBuilderSessionStoreOptions &options) const
{
    if (!is_safe_session_token(session_token))
        return result_for(JsBuilderSessionStoreReason::InvalidRequest);
    if (!lookup_context_is_usable(options))
        return result_for(JsBuilderSessionStoreReason::InvalidRequest);
    const auto found = m_sessions.find(session_token);
    if (found == m_sessions.end())
        return result_for(JsBuilderSessionStoreReason::NotFound);

    const JsPublishTokenMetadata &metadata = found->second;
    if (metadata.server_audience != options.server_audience
        || metadata.workspace_id != options.workspace_id)
        return result_for(JsBuilderSessionStoreReason::NotFound);
    if (metadata.revoked)
        return result_for(JsBuilderSessionStoreReason::Revoked);
    if (metadata.expires_at_epoch_seconds <= options.now_epoch_seconds)
        return result_for(JsBuilderSessionStoreReason::Expired);
    return result_with_metadata(JsBuilderSessionStoreReason::Accepted, metadata);
}

JsBuilderSessionStoreResult JsBuilderSessionStore::revoke(
    const std::string &session_token, const JsBuilderSessionStoreOptions &options)
{
    JsBuilderSessionStoreResult current = lookup(session_token, options);
    if (!current.ok)
        return current;

    auto found = m_sessions.find(session_token);
    found->second.revoked = true;
    return result_with_metadata(JsBuilderSessionStoreReason::Accepted, found->second);
}

std::size_t JsBuilderSessionStore::size() const
{
    return m_sessions.size();
}
