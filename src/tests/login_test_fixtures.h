/* login_test_fixtures.h */

#ifndef LOGIN_TEST_FIXTURES_H
#define LOGIN_TEST_FIXTURES_H

#include "account_management.h"
#include "db.h"
#include "handler.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

extern struct char_data* character_list;
extern struct room_data world;

// Portable fixtures for tests that take a connection from an account's characters into the game:
// an account in a temporary directory, a two-room world, and a connection driven a line at a time.
// The classes are in headers of their own; LoginWorldTest (login_world_test.h) brings them together.
namespace login_tests {

// The account every test logs in to.
inline constexpr char ACCOUNT_NAME[] = "acct";
// That account's email address, as the account menu shows it.
inline constexpr char ACCOUNT_EMAIL[] = "player@example.com";
// That account's password.
inline constexpr char ACCOUNT_PASSWORD[] = "ValidPass1";
// When the account was created; any fixed time will do.
inline constexpr long ACCOUNT_CREATED_AT = 1700010200;
// When characters are linked to the account.
inline constexpr long CHARACTER_LINKED_AT = 1700010201;
// The address the connection logging in comes from.
inline constexpr char CONNECTION_HOST[] = "127.0.0.1";
// A socket number no write reaches. The login code sends telnet negotiation straight to the
// socket, and it skips the room look for a connection whose socket number is zero.
inline constexpr int NO_SOCKET = -1;
// The socket number of another connection a test puts in the game. Any number but zero, which
// the login code reads as link-dead, will do; nothing is written to it.
inline constexpr int OTHER_PLAYER_SOCKET = 7;

// The room test characters are saved in, and the index the test world gives it.
inline constexpr int LOAD_ROOM_VNUM = 3001;
inline constexpr int LOAD_ROOM_RNUM = 1;
// A second room ahead of it in the world, so the load room's index differs from zero.
inline constexpr int OTHER_ROOM_VNUM = 1000;
// The load room's name, which the room look on entering the game shows.
inline constexpr char LOAD_ROOM_NAME[] = "The Testing Meadow";

// The message of the day each kind of character is shown.
inline constexpr std::string_view MORTAL_MOTD = "Mortal message of the day.\n\r";
inline constexpr std::string_view IMMORTAL_MOTD = "Immortal message of the day.\n\r";
// The wizlock message an immortal set.
inline constexpr std::string_view WIZLOCK_MESSAGE = "Closed for testing.\n\r";

// A human character named name as stored on disk, saved in the load room, with stats the game
// accepts as sound. A name longer than the stored name field is cut short.
inline char_file_u make_stored_character(std::string_view name, int level, long idnum)
{
    char_file_u stored_character { };
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%.*s",
        static_cast<int>(name.size()), name.data());
    stored_character.level = level;
    stored_character.race = RACE_HUMAN;
    stored_character.player_index = -1;
    stored_character.specials2.idnum = idnum;
    stored_character.specials2.load_room = LOAD_ROOM_VNUM;
    for (char_ability_data* abilities : { &stored_character.tmpabilities, &stored_character.constabilities }) {
        abilities->str = 15;
        abilities->lea = 15;
        abilities->intel = 15;
        abilities->wil = 15;
        abilities->dex = 15;
        abilities->con = 15;
        abilities->hit = 50;
        abilities->mana = 50;
        abilities->move = 50;
    }
    return stored_character;
}

// A copy of text in memory from malloc, which free_char releases names with.
inline char* copy_for_game(std::string_view text)
{
    char* copy = static_cast<char*>(std::malloc(text.size() + 1));
    if (copy == nullptr) {
        ADD_FAILURE() << "Could not allocate a copy of " << text;
        return nullptr;
    }
    std::memcpy(copy, text.data(), text.size());
    copy[text.size()] = '\0';
    return copy;
}

// A character already in the game, created as the login code creates one.
inline char_data* create_character_in_game(std::string_view name, int level, long idnum)
{
    char_data* character = nullptr;
    CREATE(character, struct char_data, 1);
    clear_char(character, MOB_VOID);
    character->player.name = copy_for_game(name);
    character->player.level = level;
    character->player.race = RACE_HUMAN;
    character->specials2.idnum = idnum;
    register_pc_char(character);
    return character;
}

// Puts character in the room at room_index, in the room's list of people and the game's list of
// characters.
inline void place_in_room(char_data& character, int room_index)
{
    character.in_room = room_index;
    character.next_in_room = world[room_index].people;
    world[room_index].people = &character;
    character.next = character_list;
    character_list = &character;
}

// Whether character is among the people in the room at room_index.
inline bool room_holds(int room_index, const char_data* character)
{
    for (const char_data* person = world[room_index].people; person != nullptr; person = person->next_in_room) {
        if (person == character) {
            return true;
        }
    }
    return false;
}

// Whether character is in the game's list of characters.
inline bool game_holds(const char_data* character)
{
    for (const char_data* listed = character_list; listed != nullptr; listed = listed->next) {
        if (listed == character) {
            return true;
        }
    }
    return false;
}

// Reads the account file's copy of the linked character name into out_stored_character.
inline ::testing::AssertionResult read_stored_character(const std::string& name, char_file_u& out_stored_character)
{
    std::string error_message;
    if (!account::read_account_character_file(".", ACCOUNT_NAME, name, &out_stored_character, &error_message)) {
        return ::testing::AssertionFailure() << "Could not read " << name << ": " << error_message;
    }
    return ::testing::AssertionSuccess();
}

} // namespace login_tests

#endif /* LOGIN_TEST_FIXTURES_H */
