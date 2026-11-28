#include "password_login_only.h"
#include "structs.h"

#include <gtest/gtest.h>

#include <string_view>

TEST(PasswordLoginOnly, DeclinesEveryLineAtTheEmailPrompt)
{
    PasswordLoginOnly policy;
    descriptor_data connection { };
    connection.connected = CON_NME;
    for (const std::string_view line : { "player@example.com", "harness-enter 00ff player@example.com Testimm", "", "   " }) {
        EXPECT_FALSE(policy.try_enter(connection, line)) << "Expected the line declined: '" << line << "'";
        EXPECT_EQ(connection.connected, CON_NME) << "Expected the state unchanged by: '" << line << "'";
    }
}
