#include "../js_builder_session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace {

account::AccountData make_account()
{
    account::AccountData account;
    account.account_name = "builder-account";
    account.normalized_email = "builder@example.com";
    account.password_hash = "stored-password";
    account.email_verified = true;
    account.characters = { "Builderone" };
    return account;
}

JsBuilderSessionOptions make_options(account::AccountData account_data = make_account())
{
    JsBuilderSessionOptions options;
    options.root_directory = "test-root";
    options.now_epoch_seconds = 100;
    options.session_ttl_seconds = 60;
    options.account_resolver = [account_data](const std::string &, const std::string &identifier,
                                account::AccountData *out, std::string *) {
        if (identifier != "builder@example.com")
            return false;
        *out = account_data;
        return true;
    };
    options.password_verifier = [](const std::string &password,
                                    const std::string &hash) {
        return password == "CorrectPassword1" && hash == "stored-password";
    };
    options.character_loader = [](const std::string &, const account::AccountData &,
                                   const std::string &character_name,
                                   JsPublishLinkedCharacterEligibility *character,
                                   std::string *) {
        character->character_name = character_name;
        character->character_id = 1001;
        character->level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
        character->immortal = true;
        character->character_loaded = true;
        return true;
    };
    options.token_issuer = [](const account::AccountData &,
                               const JsPublishBuilderEligibilityResult &eligibility,
                               long long expires_at) {
        return eligibility.ok && expires_at == 160 ? "session-token" : "";
    };
    return options;
}

JsBuilderSessionLoginRequest make_request()
{
    JsBuilderSessionLoginRequest request;
    request.account_identifier = " builder@example.com ";
    request.password = "CorrectPassword1";
    request.request_id = "request:login";
    return request;
}

std::string diagnostics(const JsBuilderSessionLoginResult &result)
{
    std::string joined;
    for (const std::string &diagnostic : result.diagnostics)
        joined += diagnostic + "\n";
    return joined;
}

unsigned expected_builder_session_scopes()
{
    return JS_PUBLISH_SCOPE_STATUS_READ | JS_PUBLISH_SCOPE_PACKAGE_STAGE
        | JS_PUBLISH_SCOPE_PACKAGE_ACTIVATE | JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_OWN;
}

} // namespace

TEST(JsBuilderSession, AcceptsVerifiedAccountWithLinkedLevelNinetyTwoImmortal)
{
    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), make_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.login.accepted", result.reason_code);
    EXPECT_EQ("session-token", result.session_token);
    EXPECT_EQ("builder-account", result.account_id);
    EXPECT_EQ(160, result.expires_at_epoch_seconds);
    ASSERT_EQ(1u, result.immortal_character_names.size());
    EXPECT_EQ("Builderone", result.immortal_character_names[0]);
    EXPECT_TRUE(result.builder_eligibility.ok);
    EXPECT_EQ(1001, result.builder_eligibility.eligible_character_id);
    EXPECT_TRUE(result.token_metadata.claims_verified);
    EXPECT_EQ(0u, result.token_metadata.token_id.find("rots-builder-session-id:"));
    EXPECT_NE("session-token", result.token_metadata.token_id);
    EXPECT_EQ("builder-account", result.token_metadata.builder_account_id);
    EXPECT_EQ("Builderone", result.token_metadata.actor_id);
    EXPECT_EQ(100, result.token_metadata.issued_at_epoch_seconds);
    EXPECT_EQ(160, result.token_metadata.expires_at_epoch_seconds);
    EXPECT_EQ(expected_builder_session_scopes(), result.token_metadata.scopes);
    EXPECT_FALSE(result.token_metadata.scopes & JS_PUBLISH_SCOPE_PACKAGE_ROLLBACK_ANY);
    EXPECT_FALSE(result.token_metadata.scopes & JS_PUBLISH_SCOPE_ADMIN_REVOKE);
    EXPECT_FALSE(result.token_metadata.revoked);
    EXPECT_TRUE(result.token_metadata.server_audience.empty());
    EXPECT_TRUE(result.token_metadata.workspace_id.empty());
}

TEST(JsBuilderSession, RejectsBlankIdentifierOrPasswordBeforeResolvers)
{
    JsBuilderSessionOptions options = make_options();
    bool resolver_called = false;
    options.account_resolver = [&resolver_called](const std::string &, const std::string &,
                                  account::AccountData *, std::string *) {
        resolver_called = true;
        return false;
    };

    JsBuilderSessionLoginRequest missing_identifier = make_request();
    missing_identifier.account_identifier = " ";
    JsBuilderSessionLoginResult missing_identifier_result =
        js_builder_session_login(missing_identifier, options);

    JsBuilderSessionLoginRequest missing_password = make_request();
    missing_password.password = "";
    JsBuilderSessionLoginResult missing_password_result =
        js_builder_session_login(missing_password, options);

    EXPECT_FALSE(missing_identifier_result.ok);
    EXPECT_EQ(400, missing_identifier_result.http_status);
    EXPECT_EQ("builder.login.invalid-request", missing_identifier_result.reason_code);
    EXPECT_FALSE(missing_password_result.ok);
    EXPECT_EQ(400, missing_password_result.http_status);
    EXPECT_FALSE(resolver_called);
}

TEST(JsBuilderSession, RejectsUnknownAccountAndWrongPasswordGenerically)
{
    JsBuilderSessionLoginRequest unknown = make_request();
    unknown.account_identifier = "missing@example.com";
    JsBuilderSessionLoginResult unknown_result =
        js_builder_session_login(unknown, make_options());

    JsBuilderSessionLoginRequest wrong_password = make_request();
    wrong_password.password = "WrongPassword1";
    JsBuilderSessionLoginResult wrong_password_result =
        js_builder_session_login(wrong_password, make_options());

    EXPECT_FALSE(unknown_result.ok);
    EXPECT_EQ(401, unknown_result.http_status);
    EXPECT_EQ("builder.login.authentication-failed", unknown_result.reason_code);
    EXPECT_FALSE(wrong_password_result.ok);
    EXPECT_EQ(401, wrong_password_result.http_status);
    EXPECT_EQ("builder.login.authentication-failed", wrong_password_result.reason_code);
    EXPECT_TRUE(wrong_password_result.session_token.empty());
}

TEST(JsBuilderSession, ResolverFailureDiagnosticsDoNotLeakInternalErrors)
{
    JsBuilderSessionOptions options = make_options();
    options.account_resolver = [](const std::string &, const std::string &,
                                  account::AccountData *, std::string *error_message) {
        if (error_message)
            *error_message = "failed to read /private/accounts/builder-account.json";
        return false;
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(401, result.http_status);
    EXPECT_EQ("builder.login.authentication-failed", result.reason_code);
    EXPECT_EQ(std::string::npos, diagnostics(result).find("/private/accounts"));
}

TEST(JsBuilderSession, DoesNotLoadCharactersForFailedAccountAuthentication)
{
    int character_load_count = 0;
    auto counting_loader = [&character_load_count](const std::string &,
                               const account::AccountData &, const std::string &,
                               JsPublishLinkedCharacterEligibility *,
                               std::string *) {
        ++character_load_count;
        return false;
    };

    JsBuilderSessionOptions wrong_password_options = make_options();
    wrong_password_options.character_loader = counting_loader;
    JsBuilderSessionLoginRequest wrong_password = make_request();
    wrong_password.password = "WrongPassword1";
    JsBuilderSessionLoginResult wrong_password_result =
        js_builder_session_login(wrong_password, wrong_password_options);

    account::AccountData blocked = make_account();
    blocked.blocked = true;
    JsBuilderSessionOptions blocked_options = make_options(blocked);
    blocked_options.character_loader = counting_loader;
    JsBuilderSessionLoginResult blocked_result =
        js_builder_session_login(make_request(), blocked_options);

    account::AccountData unverified = make_account();
    unverified.email_verified = false;
    JsBuilderSessionOptions unverified_options = make_options(unverified);
    unverified_options.character_loader = counting_loader;
    JsBuilderSessionLoginResult unverified_result =
        js_builder_session_login(make_request(), unverified_options);

    EXPECT_FALSE(wrong_password_result.ok);
    EXPECT_FALSE(blocked_result.ok);
    EXPECT_FALSE(unverified_result.ok);
    EXPECT_EQ(0, character_load_count);
}

TEST(JsBuilderSession, WrongPasswordTakesPrecedenceOverBlockedOrUnverifiedState)
{
    account::AccountData blocked = make_account();
    blocked.blocked = true;
    JsBuilderSessionLoginRequest blocked_request = make_request();
    blocked_request.password = "WrongPassword1";
    JsBuilderSessionLoginResult blocked_result =
        js_builder_session_login(blocked_request, make_options(blocked));

    account::AccountData unverified = make_account();
    unverified.email_verified = false;
    JsBuilderSessionLoginRequest unverified_request = make_request();
    unverified_request.password = "WrongPassword1";
    JsBuilderSessionLoginResult unverified_result =
        js_builder_session_login(unverified_request, make_options(unverified));

    EXPECT_FALSE(blocked_result.ok);
    EXPECT_EQ(401, blocked_result.http_status);
    EXPECT_EQ("builder.login.authentication-failed", blocked_result.reason_code);
    EXPECT_FALSE(unverified_result.ok);
    EXPECT_EQ(401, unverified_result.http_status);
    EXPECT_EQ("builder.login.authentication-failed", unverified_result.reason_code);
}

TEST(JsBuilderSession, RejectsBlockedAndUnverifiedAccountsWithoutToken)
{
    account::AccountData blocked = make_account();
    blocked.blocked = true;
    JsBuilderSessionLoginResult blocked_result =
        js_builder_session_login(make_request(), make_options(blocked));

    account::AccountData unverified = make_account();
    unverified.email_verified = false;
    JsBuilderSessionLoginResult unverified_result =
        js_builder_session_login(make_request(), make_options(unverified));

    EXPECT_FALSE(blocked_result.ok);
    EXPECT_EQ(403, blocked_result.http_status);
    EXPECT_EQ("builder.login.blocked", blocked_result.reason_code);
    EXPECT_TRUE(blocked_result.session_token.empty());
    EXPECT_FALSE(unverified_result.ok);
    EXPECT_EQ(403, unverified_result.http_status);
    EXPECT_EQ("builder.login.email-unverified", unverified_result.reason_code);
    EXPECT_TRUE(unverified_result.session_token.empty());
}

TEST(JsBuilderSession, RejectsAccountsWithoutEligibleLinkedImmortal)
{
    JsBuilderSessionOptions options = make_options();
    options.character_loader = [](const std::string &, const account::AccountData &,
                                   const std::string &character_name,
                                   JsPublishLinkedCharacterEligibility *character,
                                   std::string *) {
        character->character_name = character_name;
        character->character_id = 1002;
        character->level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL - 1;
        character->immortal = true;
        character->character_loaded = true;
        return true;
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("builder.login.no-eligible-immortal", result.reason_code);
    EXPECT_TRUE(result.session_token.empty());
    EXPECT_FALSE(result.builder_eligibility.ok);
}

TEST(JsBuilderSession, RejectsCorruptResolvedAccountIdentity)
{
    account::AccountData corrupt = make_account();
    corrupt.account_name = "";

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), make_options(corrupt));

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(503, result.http_status);
    EXPECT_EQ("builder.login.account-lookup-failed", result.reason_code);
    EXPECT_TRUE(result.session_token.empty());
}

TEST(JsBuilderSession, RejectsWhenLinkedCharacterLookupFails)
{
    JsBuilderSessionOptions options = make_options();
    options.character_loader = [](const std::string &, const account::AccountData &,
                                   const std::string &,
                                   JsPublishLinkedCharacterEligibility *,
                                   std::string *error_message) {
        if (error_message)
            *error_message = "private character path failed";
        return false;
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("builder.login.character-lookup-failed", result.reason_code);
    EXPECT_TRUE(result.session_token.empty());
    EXPECT_EQ(std::string::npos, diagnostics(result).find("private character path"));
}

TEST(JsBuilderSession, RejectsPartialLinkedCharacterLookupEvenWithEligibleCharacter)
{
    account::AccountData account = make_account();
    account.characters = { "Missingone", "Builderone" };
    JsBuilderSessionOptions options = make_options(account);
    options.character_loader = [](const std::string &, const account::AccountData &,
                                   const std::string &character_name,
                                   JsPublishLinkedCharacterEligibility *character,
                                   std::string *) {
        if (character_name == "Missingone")
            return false;
        character->character_name = character_name;
        character->character_id = 1001;
        character->level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
        character->immortal = true;
        character->character_loaded = true;
        return true;
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(403, result.http_status);
    EXPECT_EQ("builder.login.character-lookup-failed", result.reason_code);
    EXPECT_TRUE(result.session_token.empty());
}

TEST(JsBuilderSession, RejectsTokenIssuerFailureWithoutDowngradingEligibility)
{
    JsBuilderSessionOptions options = make_options();
    options.token_issuer = [](const account::AccountData &,
                               const JsPublishBuilderEligibilityResult &, long long) {
        return "";
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(503, result.http_status);
    EXPECT_EQ("builder.login.token-issue-failed", result.reason_code);
    EXPECT_TRUE(result.session_token.empty());
    EXPECT_TRUE(result.builder_eligibility.ok);
}

TEST(JsBuilderSession, RejectsUnsafeTokenIssuerOutput)
{
    const std::vector<std::string> tokens = {
        "bad token",
        "bad\r\ntoken",
        std::string(600, 'a'),
    };
    for (const std::string &token : tokens) {
        JsBuilderSessionOptions options = make_options();
        options.token_issuer = [token](const account::AccountData &,
                                   const JsPublishBuilderEligibilityResult &, long long) {
            return token;
        };

        JsBuilderSessionLoginResult result =
            js_builder_session_login(make_request(), options);

        EXPECT_FALSE(result.ok);
        EXPECT_EQ(503, result.http_status);
        EXPECT_EQ("builder.login.token-issue-failed", result.reason_code);
        EXPECT_TRUE(result.session_token.empty());
    }
}

TEST(JsBuilderSession, DoesNotIssueTokensForRejectedLogins)
{
    auto expect_no_token_issue = [](JsBuilderSessionLoginRequest request,
                                     JsBuilderSessionOptions options) {
        int token_issue_count = 0;
        options.token_issuer = [&token_issue_count](const account::AccountData &,
                                  const JsPublishBuilderEligibilityResult &, long long) {
            ++token_issue_count;
            return "session-token";
        };

        JsBuilderSessionLoginResult result =
            js_builder_session_login(request, options);

        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.session_token.empty());
        EXPECT_EQ(0, token_issue_count);
    };

    JsBuilderSessionLoginRequest wrong_password = make_request();
    wrong_password.password = "WrongPassword1";
    expect_no_token_issue(wrong_password, make_options());

    account::AccountData blocked = make_account();
    blocked.blocked = true;
    expect_no_token_issue(make_request(), make_options(blocked));

    account::AccountData unverified = make_account();
    unverified.email_verified = false;
    expect_no_token_issue(make_request(), make_options(unverified));

    JsBuilderSessionOptions no_eligible = make_options();
    no_eligible.character_loader = [](const std::string &, const account::AccountData &,
                                      const std::string &character_name,
                                      JsPublishLinkedCharacterEligibility *character,
                                      std::string *) {
        character->character_name = character_name;
        character->character_id = 1003;
        character->level = JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL - 1;
        character->immortal = true;
        character->character_loaded = true;
        return true;
    };
    expect_no_token_issue(make_request(), no_eligible);

    account::AccountData corrupt = make_account();
    corrupt.account_name = "";
    expect_no_token_issue(make_request(), make_options(corrupt));

    JsBuilderSessionOptions partial_lookup = make_options();
    partial_lookup.character_loader = [](const std::string &, const account::AccountData &,
                                         const std::string &,
                                         JsPublishLinkedCharacterEligibility *,
                                         std::string *) {
        return false;
    };
    expect_no_token_issue(make_request(), partial_lookup);
}

TEST(JsBuilderSession, CapsSessionTtlToServerMaximum)
{
    JsBuilderSessionOptions options = make_options();
    options.session_ttl_seconds = 90 * 24 * 60 * 60;
    long long issued_expiry = 0;
    options.token_issuer = [&issued_expiry](const account::AccountData &,
                               const JsPublishBuilderEligibilityResult &, long long expires_at) {
        issued_expiry = expires_at;
        return "session-token";
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), options);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(100 + 12 * 60 * 60, issued_expiry);
    EXPECT_EQ(issued_expiry, result.expires_at_epoch_seconds);
}

TEST(JsBuilderSession, ClampsShortTtlAndRejectsExpiryOverflow)
{
    for (long long ttl : { 0LL, -60LL }) {
        JsBuilderSessionOptions options = make_options();
        options.session_ttl_seconds = ttl;
        long long issued_expiry = 0;
        options.token_issuer = [&issued_expiry](const account::AccountData &,
                                   const JsPublishBuilderEligibilityResult &, long long expires_at) {
            issued_expiry = expires_at;
            return "session-token";
        };

        JsBuilderSessionLoginResult result =
            js_builder_session_login(make_request(), options);

        EXPECT_TRUE(result.ok);
        EXPECT_EQ(101, issued_expiry);
    }

    JsBuilderSessionOptions overflow = make_options();
    overflow.now_epoch_seconds = std::numeric_limits<long long>::max() - 10;
    int token_issue_count = 0;
    overflow.token_issuer = [&token_issue_count](const account::AccountData &,
                                const JsPublishBuilderEligibilityResult &, long long) {
        ++token_issue_count;
        return "session-token";
    };

    JsBuilderSessionLoginResult result =
        js_builder_session_login(make_request(), overflow);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(503, result.http_status);
    EXPECT_EQ("builder.login.token-issue-failed", result.reason_code);
    EXPECT_EQ(0, token_issue_count);
}

TEST(JsBuilderSession, GeneratesOpaqueRandomSessionTokenShape)
{
    const std::string token = js_builder_generate_session_token();

    ASSERT_EQ(85u, token.size());
    EXPECT_EQ(0u, token.find("rots-builder-session:"));
    EXPECT_TRUE(std::all_of(token.begin() + 21, token.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    }));
}
