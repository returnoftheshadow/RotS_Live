#include "../js_builder_session_endpoint.h"

#include "../json_utils.h"

#include <gtest/gtest.h>

namespace {

account::AccountData make_account()
{
    account::AccountData account;
    account.account_name = "builder-account";
    account.password_hash = "stored-password";
    account.email_verified = true;
    account.characters = { "Builderone" };
    return account;
}

JsBuilderSessionOptions make_session_options()
{
    JsBuilderSessionOptions options;
    options.root_directory = "test-root";
    options.now_epoch_seconds = 100;
    options.session_ttl_seconds = 60;
    options.account_resolver = [](const std::string &, const std::string &identifier,
                                   account::AccountData *out, std::string *) {
        if (identifier != "builder@example.com")
            return false;
        *out = make_account();
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
                               const JsPublishBuilderEligibilityResult &, long long) {
        return "session-token";
    };
    return options;
}

JsBuilderSessionEndpointRequest login_request()
{
    JsBuilderSessionEndpointRequest request;
    request.method = "POST";
    request.path = "/api/builder/login";
    request.content_type = "application/json";
    request.body = "{\"account\":\"builder@example.com\",\"password\":\"CorrectPassword1\","
                   "\"requestId\":\"request:login\"}";
    request.trusted_proxy = true;
    return request;
}

JsBuilderSessionEndpointOptions endpoint_options()
{
    JsBuilderSessionEndpointOptions options;
    options.session_options = make_session_options();
    options.allow_stateless_login_for_tests = true;
    options.logout_handler = [](const std::string &token, std::string *) {
        return token == "session-token";
    };
    return options;
}

JsBuilderSessionStoreOptions store_options(long long now = 120)
{
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = now;
    options.server_audience = "server:test";
    options.workspace_id = "workspace:test";
    return options;
}

void expect_valid_json_object(const std::string &json)
{
    json_utils::JsonReader reader(json);
    std::string error_message;
    EXPECT_TRUE(reader.parse_root_object(
        [](const std::string &, json_utils::JsonReader *nested_reader,
            std::string *nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
}

void expect_contains(const std::string &text, const std::string &needle)
{
    EXPECT_NE(std::string::npos, text.find(needle)) << needle;
}

void expect_error(const JsBuilderSessionEndpointResult &result, int status,
    const std::string &reason_code)
{
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(status, result.http_status);
    EXPECT_EQ(reason_code, result.reason_code);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"ok\":false");
    expect_contains(result.json, "\"reasonCode\":\"" + reason_code + "\"");
    EXPECT_EQ(std::string::npos, result.json.find("CorrectPassword1"));
    EXPECT_EQ(std::string::npos, result.json.find("WrongPassword1"));
    EXPECT_EQ(std::string::npos, result.json.find("builder@example.com"));
    EXPECT_EQ(std::string::npos, result.json.find("session-token"));
}

} // namespace

TEST(JsBuilderSessionEndpoint, LoginReturnsSessionForTrustedProxy)
{
    JsBuilderSessionEndpointResult result =
        js_builder_session_endpoint_dispatch(login_request(), endpoint_options());

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.login.accepted", result.reason_code);
    EXPECT_TRUE(result.login_result.ok);
    expect_valid_json_object(result.json);
    expect_contains(result.json, "\"token\":\"session-token\"");
    expect_contains(result.json, "\"accountId\":\"builder-account\"");
    expect_contains(result.json, "\"expiresAtEpochSeconds\":160");
    expect_contains(result.json, "\"immortalCharacterNames\":[\"Builderone\"]");
}

TEST(JsBuilderSessionEndpoint, LoginPersistsAcceptedSessionWhenStoreIsAttached)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();

    JsBuilderSessionEndpointResult result =
        js_builder_session_endpoint_dispatch(login_request(), options);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    JsBuilderSessionStoreResult session =
        store.lookup("session-token", store_options());
    EXPECT_TRUE(session.ok);
    EXPECT_EQ("builder-account", session.token_metadata.builder_account_id);
    EXPECT_EQ("server:test", session.token_metadata.server_audience);
    EXPECT_EQ("workspace:test", session.token_metadata.workspace_id);
    EXPECT_TRUE(session.builder_eligibility.ok);
    EXPECT_EQ(1001, session.builder_eligibility.eligible_character_id);
}

TEST(JsBuilderSessionEndpoint, LoginRequiresStoreUnlessExplicitlyAllowed)
{
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.allow_stateless_login_for_tests = false;
    bool resolver_called = false;
    options.session_options.account_resolver =
        [&resolver_called](const std::string &, const std::string &,
            account::AccountData *, std::string *) {
            resolver_called = true;
            return false;
        };

    expect_error(js_builder_session_endpoint_dispatch(login_request(), options), 503,
        "builder.login.session-store-unavailable");
    EXPECT_FALSE(resolver_called);
}

TEST(JsBuilderSessionEndpoint, LoginFailsClosedWhenAcceptedSessionCannotBeStored)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();
    options.session_store_options.server_audience = "";

    JsBuilderSessionEndpointResult result =
        js_builder_session_endpoint_dispatch(login_request(), options);

    expect_error(result, 503, "builder.login.session-store-unavailable");
    EXPECT_EQ(0u, store.size());
}

TEST(JsBuilderSessionEndpoint, LoginRejectsMalformedRequestsAndRedactsSecrets)
{
    JsBuilderSessionEndpointRequest duplicate = login_request();
    duplicate.body = "{\"account\":\"builder@example.com\",\"account\":\"other\","
                     "\"password\":\"CorrectPassword1\"}";
    expect_error(js_builder_session_endpoint_dispatch(duplicate, endpoint_options()), 400,
        "builder.login.invalid-request");

    JsBuilderSessionEndpointRequest wrong_password = login_request();
    wrong_password.body =
        "{\"account\":\"builder@example.com\",\"password\":\"WrongPassword1\"}";
    expect_error(js_builder_session_endpoint_dispatch(wrong_password, endpoint_options()), 401,
        "builder.login.authentication-failed");
    EXPECT_FALSE(js_builder_session_endpoint_dispatch(wrong_password, endpoint_options())
                     .login_result.ok);

    JsBuilderSessionEndpointRequest unsupported_media = login_request();
    unsupported_media.content_type = "text/plain";
    expect_error(js_builder_session_endpoint_dispatch(unsupported_media, endpoint_options()),
        415, "builder.login.unsupported-media-type");

    JsBuilderSessionEndpointRequest malformed_media = login_request();
    malformed_media.content_type = "application/json anything";
    expect_error(js_builder_session_endpoint_dispatch(malformed_media, endpoint_options()),
        415, "builder.login.unsupported-media-type");

    JsBuilderSessionEndpointRequest missing_media = login_request();
    missing_media.content_type = "";
    expect_error(js_builder_session_endpoint_dispatch(missing_media, endpoint_options()), 415,
        "builder.login.unsupported-media-type");

    JsBuilderSessionEndpointRequest blank_media = login_request();
    blank_media.content_type = "   ";
    expect_error(js_builder_session_endpoint_dispatch(blank_media, endpoint_options()), 415,
        "builder.login.unsupported-media-type");
}

TEST(JsBuilderSessionEndpoint, LoginRejectsMalformedJsonBeforeSessionLookup)
{
    const char *bodies[] = {
        "{\"account\":\"builder@example.com\",\"password\":\"CorrectPassword1\","
        "\"password\":\"again\"}",
        "{\"account\":\"builder@example.com\",\"password\":\"CorrectPassword1\","
        "\"requestId\":\"one\",\"requestId\":\"two\"}",
        "{\"account\":\"builder@example.com\",\"password\":\"CorrectPassword1\","
        "\"extra\":true}",
        "{\"password\":\"CorrectPassword1\"}",
        "{\"account\":\"builder@example.com\"}",
        "{\"account\":1,\"password\":\"CorrectPassword1\"}",
        "{\"account\":\"builder@example.com\",\"password\":false}",
        "[]",
        "{",
    };

    for (const char *body : bodies) {
        JsBuilderSessionEndpointOptions options = endpoint_options();
        bool resolver_called = false;
        options.session_options.account_resolver =
            [&resolver_called](const std::string &, const std::string &,
                account::AccountData *, std::string *) {
                resolver_called = true;
                return false;
            };

        JsBuilderSessionEndpointRequest request = login_request();
        request.body = body;
        expect_error(js_builder_session_endpoint_dispatch(request, options), 400,
            "builder.login.invalid-request");
        EXPECT_FALSE(resolver_called) << body;
    }
}

TEST(JsBuilderSessionEndpoint, RejectsUnsupportedMethods)
{
    JsBuilderSessionEndpointRequest login = login_request();
    login.method = "GET";
    expect_error(js_builder_session_endpoint_dispatch(login, endpoint_options()), 405,
        "builder.login.method-not-allowed");

    JsBuilderSessionEndpointRequest logout;
    logout.method = "GET";
    logout.path = "/api/builder/logout";
    logout.trusted_proxy = true;
    logout.bearer_token = "session-token";
    expect_error(js_builder_session_endpoint_dispatch(logout, endpoint_options()), 405,
        "builder.logout.method-not-allowed");
}

TEST(JsBuilderSessionEndpoint, EnforcesTrustedProxyAndRouteShapeBeforeParsing)
{
    JsBuilderSessionEndpointRequest untrusted = login_request();
    untrusted.trusted_proxy = false;
    untrusted.body = "{\"password\":\"CorrectPassword1\"}";
    expect_error(js_builder_session_endpoint_dispatch(untrusted, endpoint_options()), 403,
        "builder.session.untrusted");

    JsBuilderSessionEndpointRequest wrong_path = login_request();
    wrong_path.path = "/api/builder/login?debug=true";
    expect_error(js_builder_session_endpoint_dispatch(wrong_path, endpoint_options()), 404,
        "builder.session.not-found");

    JsBuilderSessionEndpointOptions invalid_route = endpoint_options();
    invalid_route.login_path = "";
    expect_error(js_builder_session_endpoint_dispatch(login_request(), invalid_route), 500,
        "builder.session.invalid-route");
}

TEST(JsBuilderSessionEndpoint, RejectsOversizedRequestsBeforeParsing)
{
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.maximum_request_bytes = 4;

    expect_error(js_builder_session_endpoint_dispatch(login_request(), options), 413,
        "builder.session.request-too-large");
}

TEST(JsBuilderSessionEndpoint, LogoutRequiresSessionAndHandler)
{
    JsBuilderSessionEndpointRequest request;
    request.method = "POST";
    request.path = "/api/builder/logout";
    request.trusted_proxy = true;
    request.bearer_token = "session-token";

    JsBuilderSessionEndpointResult result =
        js_builder_session_endpoint_dispatch(request, endpoint_options());
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(200, result.http_status);
    EXPECT_EQ("builder.logout.accepted", result.reason_code);
    EXPECT_EQ("{\"ok\":true,\"reasonCode\":\"builder.logout.accepted\"}", result.json);

    JsBuilderSessionEndpointRequest missing_token = request;
    missing_token.bearer_token = "";
    expect_error(js_builder_session_endpoint_dispatch(missing_token, endpoint_options()), 401,
        "builder.logout.missing-session");

    for (const std::string token : { std::string(" token"), std::string("token "),
             std::string("token\nbad"), std::string("token\tbad"),
             std::string(513, 'x') }) {
        JsBuilderSessionEndpointOptions options = endpoint_options();
        bool logout_called = false;
        options.logout_handler = [&logout_called](const std::string &, std::string *) {
            logout_called = true;
            return true;
        };
        JsBuilderSessionEndpointRequest unsafe_token = request;
        unsafe_token.bearer_token = token;
        expect_error(js_builder_session_endpoint_dispatch(unsafe_token, options), 401,
            "builder.logout.missing-session");
        EXPECT_FALSE(logout_called) << token;
    }

    JsBuilderSessionEndpointRequest with_body = request;
    with_body.body = "{}";
    expect_error(js_builder_session_endpoint_dispatch(with_body, endpoint_options()), 400,
        "builder.logout.invalid-request");

    JsBuilderSessionEndpointOptions no_handler = endpoint_options();
    no_handler.logout_handler = {};
    expect_error(js_builder_session_endpoint_dispatch(request, no_handler), 503,
        "builder.logout.unavailable");

    JsBuilderSessionEndpointOptions rejecting = endpoint_options();
    rejecting.logout_handler = [](const std::string &, std::string *) { return false; };
    expect_error(js_builder_session_endpoint_dispatch(request, rejecting), 401,
        "builder.logout.rejected");
}

TEST(JsBuilderSessionEndpoint, LogoutRevokesAttachedSessionStore)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();
    ASSERT_TRUE(js_builder_session_endpoint_dispatch(login_request(), options).ok);

    JsBuilderSessionEndpointRequest logout;
    logout.method = "POST";
    logout.path = "/api/builder/logout";
    logout.trusted_proxy = true;
    logout.bearer_token = "session-token";

    JsBuilderSessionEndpointResult result =
        js_builder_session_endpoint_dispatch(logout, options);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ("builder.logout.accepted", result.reason_code);
    EXPECT_EQ(JsBuilderSessionStoreReason::Revoked,
        store.lookup("session-token", store_options()).reason);

    expect_error(js_builder_session_endpoint_dispatch(logout, options), 401,
        "builder.logout.rejected");
}

TEST(JsBuilderSessionEndpoint, LogoutRejectsExpiredStoreSession)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();
    ASSERT_TRUE(js_builder_session_endpoint_dispatch(login_request(), options).ok);

    JsBuilderSessionEndpointRequest logout;
    logout.method = "POST";
    logout.path = "/api/builder/logout";
    logout.trusted_proxy = true;
    logout.bearer_token = "session-token";

    options.session_store_options = store_options(160);
    expect_error(js_builder_session_endpoint_dispatch(logout, options), 401,
        "builder.logout.rejected");
}

TEST(JsBuilderSessionEndpoint, StoreBackedLogoutSkipsLegacyLogoutHandler)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();
    bool handler_called = false;
    options.logout_handler = [&handler_called](const std::string &, std::string *) {
        handler_called = true;
        return false;
    };
    ASSERT_TRUE(js_builder_session_endpoint_dispatch(login_request(), options).ok);

    JsBuilderSessionEndpointRequest logout;
    logout.method = "POST";
    logout.path = "/api/builder/logout";
    logout.trusted_proxy = true;
    logout.bearer_token = "session-token";

    EXPECT_TRUE(js_builder_session_endpoint_dispatch(logout, options).ok);
    EXPECT_FALSE(handler_called);
}

TEST(JsBuilderSessionEndpoint, StoreBackedLogoutRejectsInvalidStoreContextAndUnknownToken)
{
    JsBuilderSessionStore store;
    JsBuilderSessionEndpointOptions options = endpoint_options();
    options.session_store = &store;
    options.session_store_options = store_options();
    ASSERT_TRUE(js_builder_session_endpoint_dispatch(login_request(), options).ok);

    JsBuilderSessionEndpointRequest logout;
    logout.method = "POST";
    logout.path = "/api/builder/logout";
    logout.trusted_proxy = true;
    logout.bearer_token = "session-token";

    JsBuilderSessionEndpointOptions missing_clock = options;
    missing_clock.session_store_options.now_epoch_seconds = 0;
    expect_error(js_builder_session_endpoint_dispatch(logout, missing_clock), 401,
        "builder.logout.rejected");

    JsBuilderSessionEndpointOptions wrong_audience = options;
    wrong_audience.session_store_options.server_audience = "server:other";
    expect_error(js_builder_session_endpoint_dispatch(logout, wrong_audience), 401,
        "builder.logout.rejected");

    JsBuilderSessionEndpointRequest unknown = logout;
    unknown.bearer_token = "other-session-token";
    expect_error(js_builder_session_endpoint_dispatch(unknown, options), 401,
        "builder.logout.rejected");

    EXPECT_TRUE(store.lookup("session-token", store_options()).ok);
}
