#include "js_builder_session.h"

#include "account_management.h"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <limits>

namespace {

constexpr std::size_t SessionTokenRandomBytes = 32;
constexpr std::size_t MaxSessionTokenBytes = 512;
constexpr std::size_t MaxLoginDiagnosticBytes = 120;
constexpr long long MaxSessionTtlSeconds = 12 * 60 * 60;

std::string trim_copy(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size()
        && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;
    std::size_t end = value.size();
    while (end > start
        && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}

std::string bounded_single_line(std::string message)
{
    for (char &ch : message) {
        if (ch == '\n' || ch == '\r' || static_cast<unsigned char>(ch) < 0x20)
            ch = ' ';
    }
    if (message.size() > MaxLoginDiagnosticBytes)
        message.resize(MaxLoginDiagnosticBytes);
    return message;
}

bool default_account_resolver(const std::string &root_directory,
    const std::string &account_identifier, account::AccountData *account,
    std::string *error_message)
{
    return account::read_account_file_by_identifier(
        root_directory, account_identifier, account, error_message);
}

bool default_character_loader(const std::string &root_directory,
    const account::AccountData &account_data, const std::string &character_name,
    JsPublishLinkedCharacterEligibility *character, std::string *error_message)
{
    if (character == nullptr) {
        if (error_message)
            *error_message = "Character output parameter must not be null.";
        return false;
    }

    char_file_u stored_character {};
    if (!account::read_account_character_file(root_directory, account_data.account_name,
            character_name, &stored_character, error_message))
        return false;

    character->character_name = stored_character.name;
    character->character_id = stored_character.specials2.idnum;
    character->level = stored_character.level;
    character->immortal = stored_character.level >= LEVEL_IMMORT;
    character->character_loaded = true;
    return true;
}

std::string default_token_issuer(const account::AccountData &,
    const JsPublishBuilderEligibilityResult &, long long)
{
    return js_builder_generate_session_token();
}

JsBuilderSessionLoginResult result_for(JsBuilderSessionReason reason, int http_status)
{
    JsBuilderSessionLoginResult result;
    result.ok = false;
    result.http_status = http_status;
    result.reason = reason;
    result.reason_code = js_builder_session_reason_code(reason);
    return result;
}

void add_public_diagnostic(JsBuilderSessionLoginResult &result, const std::string &diagnostic)
{
    result.diagnostics.push_back(bounded_single_line(diagnostic));
}

bool is_safe_session_token(const std::string &token)
{
    return !token.empty() && token.size() <= MaxSessionTokenBytes
        && std::none_of(token.begin(), token.end(), [](unsigned char ch) {
               return std::iscntrl(ch) || std::isspace(ch);
           });
}

long long effective_now(long long configured_now)
{
    if (configured_now > 0)
        return configured_now;
    const std::time_t now = std::time(nullptr);
    return now > 0 ? static_cast<long long>(now) : 0;
}

unsigned builder_session_scopes()
{
    return JS_PUBLISH_SCOPE_STATUS_READ | JS_PUBLISH_SCOPE_PACKAGE_STAGE
        | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE | JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN;
}

std::string session_token_id(const std::string &token)
{
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char ch : token) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }

    static constexpr char Hex[] = "0123456789abcdef";
    std::string id = "rots-builder-session-id:";
    for (int shift = 60; shift >= 0; shift -= 4)
        id.push_back(Hex[(hash >> shift) & 0x0f]);
    return id;
}

} // namespace

std::string js_builder_session_reason_code(JsBuilderSessionReason reason)
{
    switch (reason) {
    case JsBuilderSessionReason::Accepted:
        return "builder.login.accepted";
    case JsBuilderSessionReason::InvalidRequest:
        return "builder.login.invalid-request";
    case JsBuilderSessionReason::AuthenticationFailed:
        return "builder.login.authentication-failed";
    case JsBuilderSessionReason::EmailUnverified:
        return "builder.login.email-unverified";
    case JsBuilderSessionReason::Blocked:
        return "builder.login.blocked";
    case JsBuilderSessionReason::AccountLookupFailed:
        return "builder.login.account-lookup-failed";
    case JsBuilderSessionReason::NoEligibleImmortal:
        return "builder.login.no-eligible-immortal";
    case JsBuilderSessionReason::CharacterLookupFailed:
        return "builder.login.character-lookup-failed";
    case JsBuilderSessionReason::TokenIssueFailed:
        return "builder.login.token-issue-failed";
    }
    return "builder.login.authentication-failed";
}

std::string js_builder_generate_session_token()
{
    unsigned char bytes[SessionTokenRandomBytes] {};
    FILE *file = std::fopen("/dev/urandom", "rb");
    if (file == nullptr)
        return "";
    const std::size_t read = std::fread(bytes, sizeof(unsigned char),
        SessionTokenRandomBytes, file);
    std::fclose(file);
    if (read != SessionTokenRandomBytes)
        return "";

    static constexpr char Hex[] = "0123456789abcdef";
    std::string token = "rots-builder-session:";
    token.reserve(token.size() + SessionTokenRandomBytes * 2);
    for (unsigned char byte : bytes) {
        token.push_back(Hex[(byte >> 4) & 0x0f]);
        token.push_back(Hex[byte & 0x0f]);
    }
    return token;
}

JsBuilderSessionLoginResult js_builder_session_login(
    const JsBuilderSessionLoginRequest &request,
    const JsBuilderSessionOptions &options)
{
    const std::string account_identifier = trim_copy(request.account_identifier);
    if (account_identifier.empty() || request.password.empty()) {
        JsBuilderSessionLoginResult result =
            result_for(JsBuilderSessionReason::InvalidRequest, 400);
        add_public_diagnostic(result, "account identifier and password are required");
        return result;
    }

    const JsBuilderAccountResolver account_resolver =
        options.account_resolver ? options.account_resolver : default_account_resolver;
    const JsBuilderPasswordVerifier password_verifier =
        options.password_verifier ? options.password_verifier : account::verify_password;
    const JsBuilderCharacterEligibilityLoader character_loader =
        options.character_loader ? options.character_loader : default_character_loader;
    const JsBuilderSessionTokenIssuer token_issuer =
        options.token_issuer ? options.token_issuer : default_token_issuer;

    account::AccountData account_data;
    std::string account_error;
    if (!account_resolver(options.root_directory, account_identifier, &account_data,
            &account_error)) {
        JsBuilderSessionLoginResult result =
            result_for(JsBuilderSessionReason::AuthenticationFailed, 401);
        add_public_diagnostic(result, "account authentication failed");
        return result;
    }
    if (!account::is_valid_account_name(account_data.account_name, nullptr)) {
        JsBuilderSessionLoginResult result =
            result_for(JsBuilderSessionReason::AccountLookupFailed, 503);
        add_public_diagnostic(result, "account record could not be used");
        return result;
    }

    JsPublishAccountAuthOutcome auth_outcome =
        JsPublishAccountAuthOutcome::Authenticated;
    if (!password_verifier(request.password, account_data.password_hash))
        auth_outcome = JsPublishAccountAuthOutcome::NotAuthenticated;
    else if (account_data.blocked)
        auth_outcome = JsPublishAccountAuthOutcome::Blocked;
    else if (!account_data.email_verified)
        auth_outcome = JsPublishAccountAuthOutcome::EmailUnverified;

    std::vector<JsPublishLinkedCharacterEligibility> linked_characters;
    bool character_lookup_failed = false;
    if (auth_outcome == JsPublishAccountAuthOutcome::Authenticated) {
        for (const std::string &character_name : account_data.characters) {
            JsPublishLinkedCharacterEligibility character;
            std::string character_error;
            if (character_loader(options.root_directory, account_data, character_name,
                    &character, &character_error)) {
                linked_characters.push_back(character);
            } else {
                character_lookup_failed = true;
                linked_characters.push_back({ character_name, 0, 0, false, false });
            }
        }
    }

    JsPublishBuilderEligibilityInput eligibility_input;
    eligibility_input.auth_outcome = auth_outcome;
    eligibility_input.authenticated_account_id = account_data.account_name;
    eligibility_input.requested_builder_account_id = account_data.account_name;
    eligibility_input.linked_characters = linked_characters;
    JsPublishBuilderEligibilityResult eligibility =
        js_publish_evaluate_builder_account_eligibility(eligibility_input);

    if (!eligibility.ok || character_lookup_failed) {
        JsBuilderSessionReason reason = JsBuilderSessionReason::AuthenticationFailed;
        int http_status = 401;
        if (auth_outcome == JsPublishAccountAuthOutcome::Blocked) {
            reason = JsBuilderSessionReason::Blocked;
            http_status = 403;
        } else if (auth_outcome == JsPublishAccountAuthOutcome::EmailUnverified) {
            reason = JsBuilderSessionReason::EmailUnverified;
            http_status = 403;
        } else if (auth_outcome == JsPublishAccountAuthOutcome::Authenticated
            && character_lookup_failed) {
            reason = JsBuilderSessionReason::CharacterLookupFailed;
            http_status = 403;
        } else if (auth_outcome == JsPublishAccountAuthOutcome::Authenticated) {
            reason = JsBuilderSessionReason::NoEligibleImmortal;
            http_status = 403;
        }
        JsBuilderSessionLoginResult result = result_for(reason, http_status);
        result.account_id = account_data.account_name;
        result.builder_eligibility = eligibility;
        add_public_diagnostic(result, "builder login was rejected");
        return result;
    }

    const long long now = effective_now(options.now_epoch_seconds);
    const long long ttl = std::min<long long>(
        MaxSessionTtlSeconds, std::max<long long>(1, options.session_ttl_seconds));
    if (now <= 0 || ttl > std::numeric_limits<long long>::max() - now) {
        JsBuilderSessionLoginResult result =
            result_for(JsBuilderSessionReason::TokenIssueFailed, 503);
        result.account_id = account_data.account_name;
        result.builder_eligibility = eligibility;
        add_public_diagnostic(result, "builder session could not be issued");
        return result;
    }
    const long long expires_at = now + ttl;
    const std::string token = token_issuer(account_data, eligibility, expires_at);
    if (!is_safe_session_token(token)) {
        JsBuilderSessionLoginResult result =
            result_for(JsBuilderSessionReason::TokenIssueFailed, 503);
        result.account_id = account_data.account_name;
        result.builder_eligibility = eligibility;
        add_public_diagnostic(result, "builder session could not be issued");
        return result;
    }

    JsBuilderSessionLoginResult result;
    result.ok = true;
    result.http_status = 200;
    result.reason = JsBuilderSessionReason::Accepted;
    result.reason_code = js_builder_session_reason_code(result.reason);
    result.session_token = token;
    result.account_id = account_data.account_name;
    result.expires_at_epoch_seconds = expires_at;
    result.builder_eligibility = eligibility;
    result.token_metadata.claims_verified = true;
    result.token_metadata.token_id = session_token_id(token);
    result.token_metadata.actor_id = eligibility.eligible_character_name;
    result.token_metadata.builder_account_id = account_data.account_name;
    result.token_metadata.scopes = builder_session_scopes();
    result.token_metadata.issued_at_epoch_seconds = now;
    result.token_metadata.expires_at_epoch_seconds = expires_at;
    for (const JsPublishLinkedCharacterEligibility &character : linked_characters) {
        if (character.character_loaded && character.immortal
            && character.level >= JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL)
            result.immortal_character_names.push_back(character.character_name);
    }
    return result;
}
