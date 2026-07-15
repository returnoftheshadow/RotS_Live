#ifndef JS_BUILDER_SESSION_H
#define JS_BUILDER_SESSION_H

#include "account_management_types.h"
#include "js_publish_authorization.h"

#include <functional>
#include <string>
#include <vector>

enum class JsBuilderSessionReason {
    Accepted,
    InvalidRequest,
    AuthenticationFailed,
    EmailUnverified,
    Blocked,
    AccountLookupFailed,
    NoEligibleImmortal,
    CharacterLookupFailed,
    TokenIssueFailed,
};

struct JsBuilderSessionLoginRequest {
    std::string account_identifier;
    std::string password;
    std::string request_id;
};

struct JsBuilderSessionLoginResult {
    bool ok = false;
    int http_status = 500;
    JsBuilderSessionReason reason = JsBuilderSessionReason::AuthenticationFailed;
    std::string reason_code;
    std::string session_token;
    std::string account_id;
    long long expires_at_epoch_seconds = 0;
    std::vector<std::string> immortal_character_names;
    JsPublishBuilderEligibilityResult builder_eligibility;
    // Server-issued metadata for future session-store insertion. Publish requests must derive
    // equivalent metadata from server-side token lookup, never from BuilderClient JSON.
    JsPublishTokenMetadata token_metadata;
    std::vector<std::string> diagnostics;
};

using JsBuilderAccountResolver = std::function<bool(const std::string &root_directory,
    const std::string &account_identifier, account::AccountData *account,
    std::string *error_message)>;
using JsBuilderPasswordVerifier =
    std::function<bool(const std::string &password, const std::string &password_hash)>;
using JsBuilderCharacterEligibilityLoader = std::function<bool(
    const std::string &root_directory, const account::AccountData &account,
    const std::string &character_name, JsPublishLinkedCharacterEligibility *character,
    std::string *error_message)>;
using JsBuilderSessionTokenIssuer = std::function<std::string(
    const account::AccountData &account,
    const JsPublishBuilderEligibilityResult &eligibility, long long expires_at_epoch_seconds)>;

struct JsBuilderSessionOptions {
    std::string root_directory = "lib";
    long long now_epoch_seconds = 0;
    long long session_ttl_seconds = 60 * 60;
    JsBuilderAccountResolver account_resolver;
    JsBuilderPasswordVerifier password_verifier;
    JsBuilderCharacterEligibilityLoader character_loader;
    JsBuilderSessionTokenIssuer token_issuer;
};

JsBuilderSessionLoginResult js_builder_session_login(
    const JsBuilderSessionLoginRequest &request,
    const JsBuilderSessionOptions &options = {});

std::string js_builder_session_reason_code(JsBuilderSessionReason reason);
std::string js_builder_generate_session_token();

#endif
