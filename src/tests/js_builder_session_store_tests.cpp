#include "../js_builder_session_store.h"

#include <gtest/gtest.h>

namespace {

JsBuilderSessionLoginResult make_login()
{
    JsBuilderSessionLoginResult login;
    login.ok = true;
    login.session_token = "session-token";
    login.account_id = "builder-account";
    login.expires_at_epoch_seconds = 200;
    login.token_metadata.claims_verified = true;
    login.token_metadata.token_id = js_builder_session_token_id(login.session_token);
    login.token_metadata.actor_id = "Builderone";
    login.token_metadata.builder_account_id = "builder-account";
    login.token_metadata.scopes = JS_PUBLISH_SCOPE_STATUS_READ;
    login.token_metadata.issued_at_epoch_seconds = 100;
    login.token_metadata.expires_at_epoch_seconds = 200;
    login.builder_eligibility.ok = true;
    login.builder_eligibility.builder_account_id = "builder-account";
    login.builder_eligibility.eligible_character_id = 1001;
    login.builder_eligibility.eligible_character_name = "Builderone";
    login.builder_eligibility.eligible_character_level =
        JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL;
    return login;
}

JsBuilderSessionStoreOptions make_options(long long now = 150)
{
    JsBuilderSessionStoreOptions options;
    options.now_epoch_seconds = now;
    options.server_audience = "test-server";
    options.workspace_id = "workspace";
    return options;
}

} // namespace

TEST(JsBuilderSessionStore, InsertsAndLooksUpIssuedSessionMetadata)
{
    JsBuilderSessionStore store;

    JsBuilderSessionStoreResult inserted =
        store.insert_login_result(make_login(), make_options());
    JsBuilderSessionStoreResult found =
        store.lookup("session-token", make_options());

    EXPECT_TRUE(inserted.ok);
    EXPECT_EQ("builder.session.accepted", inserted.reason_code);
    EXPECT_EQ(1u, store.size());
    EXPECT_TRUE(found.ok);
    EXPECT_TRUE(found.token_metadata.claims_verified);
    EXPECT_EQ(js_builder_session_token_id("session-token"), found.token_metadata.token_id);
    EXPECT_EQ("Builderone", found.token_metadata.actor_id);
    EXPECT_EQ("builder-account", found.token_metadata.builder_account_id);
    EXPECT_EQ("test-server", found.token_metadata.server_audience);
    EXPECT_EQ("workspace", found.token_metadata.workspace_id);
    EXPECT_EQ(JS_PUBLISH_SCOPE_STATUS_READ, found.token_metadata.scopes);
    EXPECT_TRUE(found.builder_eligibility.ok);
    EXPECT_EQ("builder-account", found.builder_eligibility.builder_account_id);
    EXPECT_EQ(1001, found.builder_eligibility.eligible_character_id);
    EXPECT_EQ("Builderone", found.builder_eligibility.eligible_character_name);
}

TEST(JsBuilderSessionStore, RejectsInvalidLoginResultsWithoutMutatingStore)
{
    JsBuilderSessionStore store;

    JsBuilderSessionLoginResult failed = make_login();
    failed.ok = false;
    EXPECT_FALSE(store.insert_login_result(failed, make_options()).ok);

    JsBuilderSessionLoginResult unsafe_token = make_login();
    unsafe_token.session_token = "bad token";
    EXPECT_FALSE(store.insert_login_result(unsafe_token, make_options()).ok);

    JsBuilderSessionLoginResult missing_metadata = make_login();
    missing_metadata.token_metadata.claims_verified = false;
    EXPECT_FALSE(store.insert_login_result(missing_metadata, make_options()).ok);

    JsBuilderSessionLoginResult mismatched_token_id = make_login();
    mismatched_token_id.token_metadata.token_id = "wrong-token-id";
    EXPECT_FALSE(store.insert_login_result(mismatched_token_id, make_options()).ok);

    JsBuilderSessionStoreOptions missing_audience = make_options();
    missing_audience.server_audience = "   ";
    EXPECT_FALSE(store.insert_login_result(make_login(), missing_audience).ok);

    JsBuilderSessionLoginResult missing_eligibility = make_login();
    missing_eligibility.builder_eligibility.ok = false;
    EXPECT_FALSE(store.insert_login_result(missing_eligibility, make_options()).ok);

    JsBuilderSessionLoginResult mismatched_account = make_login();
    mismatched_account.builder_eligibility.builder_account_id = "other-account";
    EXPECT_FALSE(store.insert_login_result(mismatched_account, make_options()).ok);

    JsBuilderSessionLoginResult mismatched_actor = make_login();
    mismatched_actor.builder_eligibility.eligible_character_name = "Otherbuilder";
    EXPECT_FALSE(store.insert_login_result(mismatched_actor, make_options()).ok);

    EXPECT_EQ(0u, store.size());
}

TEST(JsBuilderSessionStore, RejectsIncompleteMetadataWithoutMutatingStore)
{
    JsBuilderSessionStore store;

    JsBuilderSessionLoginResult missing_token_id = make_login();
    missing_token_id.token_metadata.token_id = "";
    EXPECT_FALSE(store.insert_login_result(missing_token_id, make_options()).ok);

    JsBuilderSessionLoginResult missing_actor = make_login();
    missing_actor.token_metadata.actor_id = "";
    EXPECT_FALSE(store.insert_login_result(missing_actor, make_options()).ok);

    JsBuilderSessionLoginResult missing_account = make_login();
    missing_account.token_metadata.builder_account_id = "";
    EXPECT_FALSE(store.insert_login_result(missing_account, make_options()).ok);

    JsBuilderSessionLoginResult missing_issue_time = make_login();
    missing_issue_time.token_metadata.issued_at_epoch_seconds = 0;
    EXPECT_FALSE(store.insert_login_result(missing_issue_time, make_options()).ok);

    JsBuilderSessionLoginResult zero_ttl = make_login();
    zero_ttl.token_metadata.expires_at_epoch_seconds =
        zero_ttl.token_metadata.issued_at_epoch_seconds;
    EXPECT_FALSE(store.insert_login_result(zero_ttl, make_options()).ok);

    JsBuilderSessionLoginResult backwards_ttl = make_login();
    backwards_ttl.token_metadata.expires_at_epoch_seconds =
        backwards_ttl.token_metadata.issued_at_epoch_seconds - 1;
    EXPECT_FALSE(store.insert_login_result(backwards_ttl, make_options()).ok);

    EXPECT_EQ(0u, store.size());
}

TEST(JsBuilderSessionStore, RejectsIncompleteEligibilityWithoutMutatingStore)
{
    JsBuilderSessionStore store;

    JsBuilderSessionLoginResult missing_account = make_login();
    missing_account.builder_eligibility.builder_account_id = "";
    EXPECT_FALSE(store.insert_login_result(missing_account, make_options()).ok);

    JsBuilderSessionLoginResult missing_name = make_login();
    missing_name.builder_eligibility.eligible_character_name = "";
    EXPECT_FALSE(store.insert_login_result(missing_name, make_options()).ok);

    JsBuilderSessionLoginResult missing_id = make_login();
    missing_id.builder_eligibility.eligible_character_id = 0;
    EXPECT_FALSE(store.insert_login_result(missing_id, make_options()).ok);

    JsBuilderSessionLoginResult below_level = make_login();
    below_level.builder_eligibility.eligible_character_level =
        JS_PUBLISH_MIN_BUILDER_IMMORTAL_LEVEL - 1;
    EXPECT_FALSE(store.insert_login_result(below_level, make_options()).ok);

    EXPECT_EQ(0u, store.size());
}

TEST(JsBuilderSessionStore, LookupFailsClosedForMissingUnsafeExpiredAndRevokedTokens)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_login(), make_options()).ok);

    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.lookup("bad token", make_options()).reason);
    EXPECT_EQ(JsBuilderSessionStoreReason::NotFound,
        store.lookup("missing-token", make_options()).reason);
    EXPECT_EQ(JsBuilderSessionStoreReason::Expired,
        store.lookup("session-token", make_options(200)).reason);
    EXPECT_TRUE(store.lookup("session-token", make_options(200))
                    .token_metadata.token_id.empty());

    JsBuilderSessionStoreResult revoked =
        store.revoke("session-token", make_options(150));
    EXPECT_TRUE(revoked.ok);
    EXPECT_EQ(JsBuilderSessionStoreReason::Accepted, revoked.reason);
    EXPECT_TRUE(revoked.token_metadata.revoked);
    JsBuilderSessionStoreResult revoked_lookup =
        store.lookup("session-token", make_options(150));
    EXPECT_EQ(JsBuilderSessionStoreReason::Revoked, revoked_lookup.reason);
    EXPECT_TRUE(revoked_lookup.token_metadata.token_id.empty());
}

TEST(JsBuilderSessionStore, LookupRequiresServerClockAndMatchingAudience)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_login(), make_options()).ok);

    JsBuilderSessionStoreOptions missing_clock = make_options(0);
    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.lookup("session-token", missing_clock).reason);

    JsBuilderSessionStoreOptions missing_audience = make_options();
    missing_audience.server_audience = "";
    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.lookup("session-token", missing_audience).reason);

    JsBuilderSessionStoreOptions wrong_audience = make_options();
    wrong_audience.server_audience = "other-server";
    EXPECT_EQ(JsBuilderSessionStoreReason::NotFound,
        store.lookup("session-token", wrong_audience).reason);

    JsBuilderSessionStoreOptions wrong_workspace = make_options();
    wrong_workspace.workspace_id = "other-workspace";
    EXPECT_EQ(JsBuilderSessionStoreReason::NotFound,
        store.lookup("session-token", wrong_workspace).reason);
}

TEST(JsBuilderSessionStore, RevokeRejectsUnsafeAndMissingTokens)
{
    JsBuilderSessionStore store;

    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.revoke("bad\n token", make_options()).reason);
    EXPECT_EQ(JsBuilderSessionStoreReason::NotFound,
        store.revoke("missing-token", make_options()).reason);
}

TEST(JsBuilderSessionStore, RevokeRejectsExpiredAndAlreadyRevokedTokens)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_login(), make_options()).ok);

    EXPECT_EQ(JsBuilderSessionStoreReason::Expired,
        store.revoke("session-token", make_options(200)).reason);
    EXPECT_TRUE(store.revoke("session-token", make_options(150)).ok);
    EXPECT_EQ(JsBuilderSessionStoreReason::Revoked,
        store.revoke("session-token", make_options(150)).reason);
}

TEST(JsBuilderSessionStore, EnforcesSessionTokenSafetyBoundaries)
{
    JsBuilderSessionStore store;

    JsBuilderSessionLoginResult empty_token = make_login();
    empty_token.session_token = "";
    empty_token.token_metadata.token_id = js_builder_session_token_id(empty_token.session_token);
    EXPECT_FALSE(store.insert_login_result(empty_token, make_options()).ok);

    JsBuilderSessionLoginResult tab_token = make_login();
    tab_token.session_token = "session\ttoken";
    tab_token.token_metadata.token_id = js_builder_session_token_id(tab_token.session_token);
    EXPECT_FALSE(store.insert_login_result(tab_token, make_options()).ok);

    JsBuilderSessionLoginResult max_token = make_login();
    max_token.session_token.assign(512, 'a');
    max_token.token_metadata.token_id = js_builder_session_token_id(max_token.session_token);
    EXPECT_TRUE(store.insert_login_result(max_token, make_options()).ok);
    EXPECT_TRUE(store.lookup(max_token.session_token, make_options()).ok);

    JsBuilderSessionLoginResult oversized_token = make_login();
    oversized_token.session_token.assign(513, 'a');
    oversized_token.token_metadata.token_id =
        js_builder_session_token_id(oversized_token.session_token);
    EXPECT_FALSE(store.insert_login_result(oversized_token, make_options()).ok);
    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.lookup(oversized_token.session_token, make_options()).reason);
    EXPECT_EQ(JsBuilderSessionStoreReason::InvalidRequest,
        store.revoke(oversized_token.session_token, make_options()).reason);
}

TEST(JsBuilderSessionStore, ReinsertingSameTokenReplacesPreviousSessionState)
{
    JsBuilderSessionStore store;
    ASSERT_TRUE(store.insert_login_result(make_login(), make_options()).ok);
    ASSERT_TRUE(store.revoke("session-token", make_options()).ok);

    JsBuilderSessionLoginResult replacement = make_login();
    replacement.token_metadata.actor_id = "Buildertwo";
    replacement.builder_eligibility.eligible_character_name = "Buildertwo";
    replacement.token_metadata.expires_at_epoch_seconds = 300;
    EXPECT_TRUE(store.insert_login_result(replacement, make_options()).ok);

    JsBuilderSessionStoreResult found =
        store.lookup("session-token", make_options(250));
    EXPECT_TRUE(found.ok);
    EXPECT_FALSE(found.token_metadata.revoked);
    EXPECT_EQ("Buildertwo", found.token_metadata.actor_id);
    EXPECT_EQ("Buildertwo", found.builder_eligibility.eligible_character_name);
    EXPECT_EQ(1u, store.size());
}
