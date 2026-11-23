/* login_world_test.h */

#ifndef LOGIN_TESTS_LOGIN_WORLD_TEST_H
#define LOGIN_TESTS_LOGIN_WORLD_TEST_H

#include "login_test_fixtures.h"
#include "scoped_empty_news_board.h"
#include "scoped_empty_player_table.h"
#include "scoped_global.h"
#include "scoped_test_rooms.h"
#include "temporary_working_directory.h"

#include "account_management.h"
#include "structs.h"

#include <gtest/gtest.h>

#include <string>

extern struct ban_list_element* ban_list;
extern struct char_data* character_list;
extern struct descriptor_data* descriptor_list;
extern char* imotd;
extern char* motd;
extern int restrict;
extern char* wizlock_msg;
extern struct room_data world;

namespace login_tests {

// The account, the shared world state and an empty game, for taking a connection from the
// account's characters into the game. Each test runs in its own temporary working directory.
class LoginWorldTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::string error_message;
        ASSERT_TRUE(account::create_account(".", ACCOUNT_NAME, ACCOUNT_EMAIL, ACCOUNT_PASSWORD,
            ACCOUNT_CREATED_AT, nullptr, &error_message))
            << error_message;
    }

    void TearDown() override
    {
        // The characters a test leaves in the game are freed by their connections; unhook them
        // from the shared rooms and the character list first.
        world[0].people = nullptr;
        world[LOAD_ROOM_RNUM].people = nullptr;
        character_list = nullptr;
    }

    // Writes stored_character's files into the account and links it to the account, as the
    // account's existing character.
    ::testing::AssertionResult link_character(const char_file_u& stored_character)
    {
        std::string error_message;
        const std::string name = stored_character.name;
        if (!account::write_account_character_file(".", ACCOUNT_NAME, stored_character, &error_message)
            || !account::write_default_account_object_file(".", ACCOUNT_NAME, name, &error_message)
            || !account::write_default_account_exploit_file(".", ACCOUNT_NAME, name, &error_message)
            || !account::admin_link_character(".", ACCOUNT_NAME, name, CHARACTER_LINKED_AT, nullptr, &error_message)) {
            return ::testing::AssertionFailure() << "Could not link " << name << ": " << error_message;
        }
        return ::testing::AssertionSuccess();
    }

    // Stores preference_flags as the account's preferred configuration.
    ::testing::AssertionResult set_account_preferences(long preference_flags)
    {
        account::AccountData account_data;
        std::string error_message;
        if (!account::read_account_file(".", ACCOUNT_NAME, &account_data, &error_message)) {
            return ::testing::AssertionFailure() << "Could not read the account: " << error_message;
        }
        account_data.preferences.present = true;
        account_data.preferences.preference_flags = preference_flags;
        if (!account::write_account_file(".", account_data, &error_message)) {
            return ::testing::AssertionFailure() << "Could not write the account: " << error_message;
        }
        return ::testing::AssertionSuccess();
    }

    // The test's own directory, the working directory while it runs.
    rots_tests::TemporaryWorkingDirectory working_directory;
    // No connections in the game but the ones a test adds.
    rots_tests::ScopedGlobal<descriptor_data*> no_other_connections { descriptor_list, nullptr };
    // No characters in the game but the ones a test adds.
    rots_tests::ScopedGlobal<char_data*> no_characters_in_game { character_list, nullptr };
    // No player-table entries but the ones the login code or a test adds.
    ScopedEmptyPlayerTable empty_player_table;
    // The rooms characters enter.
    ScopedTestRooms test_rooms;
    // The news board entering the game reads.
    ScopedEmptyNewsBoard empty_news_board;
    // The mortals' message of the day, and the global the login code reads it from.
    std::string mortal_motd { MORTAL_MOTD };
    rots_tests::ScopedGlobal<char*> mortal_motd_in_use { motd, mortal_motd.data() };
    // The immortals' message of the day, and the global the login code reads it from.
    std::string immortal_motd { IMMORTAL_MOTD };
    rots_tests::ScopedGlobal<char*> immortal_motd_in_use { imotd, immortal_motd.data() };
    // The wizlock message, and the global the login code reads it from.
    std::string wizlock_message { WIZLOCK_MESSAGE };
    rots_tests::ScopedGlobal<char*> wizlock_message_in_use { wizlock_msg, wizlock_message.data() };
    // The level below which wizlock refuses logins; zero, no wizlock, unless a test sets one.
    rots_tests::ScopedGlobal<int> no_wizlock { restrict, 0 };
    // The banned sites; none unless a test adds one.
    rots_tests::ScopedGlobal<ban_list_element*> no_banned_sites { ban_list, nullptr };
};

} // namespace login_tests

#endif /* LOGIN_TESTS_LOGIN_WORLD_TEST_H */
