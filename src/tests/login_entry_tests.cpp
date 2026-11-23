#include "login_test_fixtures.h"
#include "login_world_test.h"
#include "scoped_global.h"
#include "test_connection.h"

#include "account_management.h"
#include "interpre.h"
#include "objects_json.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using namespace login_tests;

// The character menu an account-backed character is shown once selected.
constexpr std::string_view ACCOUNT_CHARACTER_MENU = "\n\r"
                                                    "Welcome to Arda!\n\r"
                                                    "0) Back to Account Menu.\n\r"
                                                    "1) Enter the game.\n\r"
                                                    "3) Read the background story.\n\r"
                                                    "4) Change password.\n\r"
                                                    "5) Delete this character.\n\r"
                                                    "6) View class powers.\n\r"
                                                    "\n\r"
                                                    "   Make your choice: ";

// The character menu a character logged in without an account is shown.
constexpr std::string_view LEGACY_CHARACTER_MENU = "\n\r"
                                                   "Welcome to Arda!\n\r"
                                                   "0) Exit from the MUD.\n\r"
                                                   "1) Enter the game.\n\r"
                                                   "3) Read the background story.\n\r"
                                                   "4) Change password.\n\r"
                                                   "5) Delete this character.\n\r"
                                                   "6) View class powers.\n\r"
                                                   "\n\r"
                                                   "   Make your choice: ";

// The password of the character logged in without an account. The game compares passwords in
// plain text.
constexpr char LEGACY_PASSWORD[] = "LegacyPw1";

// The last words of the account roster's prompt.
constexpr std::string_view ROSTER_PROMPT_END = "Character number or name: ";

// Where text first appears in output, or npos.
std::size_t position_of(std::string_view output, std::string_view text)
{
    return output.find(text);
}

// Whether output ends with text.
bool ends_with(std::string_view output, std::string_view text)
{
    return output.size() >= text.size() && output.substr(output.size() - text.size()) == text;
}

// One connection at the account roster, for driving the login from character selection into the
// game.
class LoginEntryTest : public LoginWorldTest {
protected:
    // Puts legacy_connection at the password prompt of a character with no account, gimli, whose
    // legacy player-table entry and password are in place and who has failed_logins recorded.
    void prepare_legacy_password_prompt(TestConnection& legacy_connection, int failed_logins, char_data** out_gimli)
    {
        std::error_code error;
        std::filesystem::create_directories("players/F-J", error);
        ASSERT_FALSE(error) << error.message();

        char_file_u stored_gimli = make_stored_character("gimli", 30, 7070);
        stored_gimli.specials2.bad_pws = failed_logins;
        const int player_index = create_entry("gimli");
        player_table[player_index].level = stored_gimli.level;
        player_table[player_index].race = stored_gimli.race;
        player_table[player_index].idnum = stored_gimli.specials2.idnum;

        descriptor_data& legacy_descriptor = legacy_connection.descriptor();
        legacy_descriptor.account_name[0] = '\0';
        legacy_descriptor.pos = player_index;
        std::snprintf(legacy_descriptor.pwd, sizeof(legacy_descriptor.pwd), "%s", LEGACY_PASSWORD);
        char_data* gimli = nullptr;
        CREATE(gimli, struct char_data, 1);
        clear_char(gimli, MOB_VOID);
        register_pc_char(gimli);
        store_to_char(&stored_gimli, gimli);
        gimli->desc = &legacy_descriptor;
        legacy_descriptor.character = gimli;
        *out_gimli = gimli;
    }

    // Selects character_name at the roster and checks the character menu followed.
    void select_from_roster(TestConnection& selecting_connection, std::string_view character_name)
    {
        selecting_connection.send(character_name);
        ASSERT_EQ(selecting_connection.state(), CON_SLCT) << selecting_connection.output();
        ASSERT_NE(selecting_connection.character(), nullptr);
    }

    // The connection logging in, waiting at the account roster.
    TestConnection connection { CON_ACCTSLCT };
};

} // namespace

TEST_F(LoginEntryTest, SelectingACharacterShowsTheMotdAndTheCharacterMenu)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_SLCT);
    ASSERT_NE(connection.character(), nullptr);
    EXPECT_STREQ(GET_NAME(connection.character()), "legolas");
    EXPECT_EQ(connection.character()->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), std::string(MORTAL_MOTD) + std::string(ACCOUNT_CHARACTER_MENU));
    ASSERT_EQ(top_of_p_table, 0);
    EXPECT_EQ(connection.descriptor().pos, 0);
    EXPECT_STREQ(player_table[0].name, "legolas");
}

TEST_F(LoginEntryTest, SelectingACharacterRemovesItsLeftoverLegacyObjectAndExploitFiles)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    const std::filesystem::path legacy_object_file = account::legacy_object_file_path(".", "legolas");
    const std::filesystem::path legacy_exploits_file = account::legacy_exploits_file_path(".", "legolas");
    for (const std::filesystem::path& leftover : { legacy_object_file, legacy_exploits_file }) {
        std::error_code error;
        std::filesystem::create_directories(leftover.parent_path(), error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream leftover_file(leftover, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(leftover_file.is_open()) << leftover;
        leftover_file << "left over";
    }

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_SLCT);
    std::error_code error;
    EXPECT_FALSE(std::filesystem::exists(legacy_object_file, error)) << legacy_object_file;
    EXPECT_FALSE(std::filesystem::exists(legacy_exploits_file, error)) << legacy_exploits_file;
}

TEST_F(LoginEntryTest, SelectingAnImmortalShowsTheImmortalMotd)
{
    ASSERT_TRUE(link_character(make_stored_character("gandalf", LEVEL_IMMORT, 9191)));

    connection.send("gandalf");

    EXPECT_EQ(connection.state(), CON_SLCT);
    EXPECT_EQ(connection.output(), std::string(IMMORTAL_MOTD) + std::string(ACCOUNT_CHARACTER_MENU));
}

TEST_F(LoginEntryTest, SelectingACharacterShowsNoFailedLoginNoticeBecauseItsFileKeepsNoCount)
{
    // The account character file does not record failed password attempts, so a count set before
    // the file is written is gone when the character is selected.
    char_file_u legolas = make_stored_character("legolas", 45, 5252);
    legolas.specials2.bad_pws = 2;
    ASSERT_TRUE(link_character(legolas));

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_SLCT);
    EXPECT_EQ(connection.output(), std::string(MORTAL_MOTD) + std::string(ACCOUNT_CHARACTER_MENU));
    ASSERT_NE(connection.character(), nullptr);
    EXPECT_EQ(connection.character()->specials2.bad_pws, 0);

    char_file_u saved { };
    ASSERT_TRUE(read_stored_character("legolas", saved));
    EXPECT_EQ(saved.specials2.bad_pws, 0);
}

TEST_F(LoginEntryTest, SelectingADeletedCharacterRefusesAndStaysOnTheRoster)
{
    char_file_u legolas = make_stored_character("legolas", 45, 5252);
    SET_BIT(legolas.specials2.act, PLR_DELETED);
    ASSERT_TRUE(link_character(legolas));

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_ACCTSLCT);
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_EQ(connection.descriptor().pos, -1);
    const std::string output = connection.output();
    EXPECT_EQ(position_of(output, "That linked character is deleted and cannot be selected.\n\r"), 0u) << output;
    EXPECT_TRUE(ends_with(output, ROSTER_PROMPT_END)) << output;
}

TEST_F(LoginEntryTest, SelectingACharacterWithUnreadableObjectDataRefusesAndStaysOnTheRoster)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    {
        std::ofstream object_file(account::account_character_object_path(".", ACCOUNT_NAME, "legolas"),
            std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(object_file.is_open());
        object_file << "not object data";
    }

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_ACCTSLCT);
    EXPECT_EQ(connection.character(), nullptr);
    const std::string output = connection.output();
    EXPECT_EQ(position_of(output,
                  "That linked character is missing required object-save data in account storage.\n\r"
                  "Expected JSON object.\n\r"),
        0u)
        << output;
    EXPECT_TRUE(ends_with(output, ROSTER_PROMPT_END)) << output;
}

TEST_F(LoginEntryTest, ACharacterNotClearedForABannedSiteIsRefusedAndDisconnected)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    ban_list_element site_ban { };
    std::snprintf(site_ban.site, sizeof(site_ban.site), "%s", CONNECTION_HOST);
    site_ban.type = BAN_SELECT;
    // Declared after site_ban, so the ban list is emptied again before site_ban goes.
    const rots_tests::ScopedGlobal<ban_list_element*> banned_site { ban_list, &site_ban };

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), "Sorry, this character has not been cleared for login from your site!\n\r");
}

TEST_F(LoginEntryTest, ACharacterClearedForABannedSiteIsAdmitted)
{
    char_file_u legolas = make_stored_character("legolas", 45, 5252);
    SET_BIT(legolas.specials2.act, PLR_SITEOK);
    ASSERT_TRUE(link_character(legolas));
    ban_list_element site_ban { };
    std::snprintf(site_ban.site, sizeof(site_ban.site), "%s", CONNECTION_HOST);
    site_ban.type = BAN_SELECT;
    // Declared after site_ban, so the ban list is emptied again before site_ban goes.
    const rots_tests::ScopedGlobal<ban_list_element*> banned_site { ban_list, &site_ban };

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_SLCT);
    EXPECT_EQ(connection.output(), std::string(MORTAL_MOTD) + std::string(ACCOUNT_CHARACTER_MENU));
}

TEST_F(LoginEntryTest, WizlockRefusesACharacterBelowTheRestrictionLevelAndDisconnects)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    restrict = 46;

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(),
        "The game is temporarily restricted.\r\nTry again later." + std::string(WIZLOCK_MESSAGE) + "\n\r");
}

TEST_F(LoginEntryTest, WizlockAdmitsACharacterAtTheRestrictionLevel)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    restrict = 45;

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_SLCT);
    EXPECT_EQ(connection.output(), std::string(MORTAL_MOTD) + std::string(ACCOUNT_CHARACTER_MENU));
}

TEST_F(LoginEntryTest, SelectingASwitchedImmortalReconnectsToTheOriginalBody)
{
    ASSERT_TRUE(link_character(make_stored_character("gandalf", 95, 9595)));

    // Gandalf's player is switched into a mob; the original body waits in the load room.
    char_data* original_body = create_character_in_game("gandalf", 95, 9595);
    place_in_room(*original_body, LOAD_ROOM_RNUM);
    char_data* mob = nullptr;
    CREATE(mob, struct char_data, 1);
    clear_char(mob, MOB_ISNPC);
    SET_BIT(mob->specials2.act, MOB_ISNPC);
    mob->nr = -1;
    mob->player.name = copy_for_game("mob");
    place_in_room(*mob, LOAD_ROOM_RNUM);
    TestConnection switched_connection(CON_PLYNG);
    switched_connection.descriptor().descriptor = OTHER_PLAYER_SOCKET;
    switched_connection.descriptor().character = mob;
    switched_connection.descriptor().original = original_body;
    mob->desc = &switched_connection.descriptor();

    connection.send("gandalf");

    EXPECT_EQ(connection.state(), CON_PLYNG);
    EXPECT_EQ(connection.character(), original_body);
    EXPECT_EQ(original_body->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), "Reconnecting to unswitched char.");
    EXPECT_EQ(switched_connection.state(), CON_CLOSE);
    EXPECT_EQ(switched_connection.character(), nullptr);
    EXPECT_EQ(switched_connection.descriptor().original, nullptr);
    EXPECT_EQ(std::string(switched_connection.descriptor().output), "Disconnecting.");

    // The game leaves the mob in the world; free it here once the old connection let it go.
    if (switched_connection.character() == nullptr) {
        mob->desc = nullptr;
        free_char(mob);
    }
}

TEST_F(LoginEntryTest, SelectingALinkDeadCharacterReconnectsToItsBody)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    // Legolas lost his connection and his body waits, link-dead, in the load room.
    char_data* waiting_body = create_character_in_game("legolas", 45, 5252);
    place_in_room(*waiting_body, LOAD_ROOM_RNUM);

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_PLYNG);
    EXPECT_EQ(connection.character(), waiting_body);
    EXPECT_EQ(waiting_body->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), "Reconnecting.\n\r");
    EXPECT_TRUE(room_holds(LOAD_ROOM_RNUM, waiting_body));
}

TEST_F(LoginEntryTest, SelectingACharacterPlayedElsewhereTakesOverThatBody)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    // Legolas is already playing on another connection.
    TestConnection other_connection(CON_PLYNG);
    other_connection.descriptor().descriptor = OTHER_PLAYER_SOCKET;
    char_data* playing_body = create_character_in_game("legolas", 45, 5252);
    playing_body->desc = &other_connection.descriptor();
    other_connection.descriptor().character = playing_body;
    place_in_room(*playing_body, LOAD_ROOM_RNUM);

    connection.send("legolas");

    EXPECT_EQ(connection.state(), CON_PLYNG);
    EXPECT_EQ(connection.character(), playing_body);
    EXPECT_EQ(playing_body->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), "You take over your own body, already in use!\n\r");
    EXPECT_EQ(other_connection.state(), CON_CLOSE);
    EXPECT_EQ(other_connection.character(), nullptr);
    EXPECT_EQ(std::string(other_connection.descriptor().output), "This body has been usurped!\n\r");
}

TEST_F(LoginEntryTest, EnteringTheGamePutsTheCharacterInItsLoadRoomAndPlaying)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    select_from_roster(connection, "legolas");
    char_data* legolas = connection.character();

    connection.send("1");

    EXPECT_EQ(connection.state(), CON_PLYNG);
    ASSERT_EQ(connection.character(), legolas);
    EXPECT_EQ(legolas->in_room, LOAD_ROOM_RNUM);
    EXPECT_TRUE(room_holds(LOAD_ROOM_RNUM, legolas));
    EXPECT_TRUE(game_holds(legolas));
    EXPECT_EQ(connection.descriptor().prompt_mode, 1);
    EXPECT_NE(connection.descriptor().pProtocol, nullptr);
}

TEST_F(LoginEntryTest, EnteringTheGameAppliesTheAccountPreferences)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    ASSERT_TRUE(set_account_preferences(PRF_COMPACT | PRF_WRAP));
    select_from_roster(connection, "legolas");
    char_data* legolas = connection.character();
    ASSERT_FALSE(PRF_FLAGGED(legolas, PRF_COMPACT));
    ASSERT_FALSE(PRF_FLAGGED(legolas, PRF_WRAP));

    connection.send("1");

    EXPECT_TRUE(PRF_FLAGGED(legolas, PRF_COMPACT));
    EXPECT_TRUE(PRF_FLAGGED(legolas, PRF_WRAP));
}

TEST_F(LoginEntryTest, EnteringTheGameLoadsTheAliasesSavedWithTheCharactersObjects)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.aliases.push_back({ "gg", "get all corpse" });
    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(".", ACCOUNT_NAME, "legolas", object_bytes, &error_message))
        << error_message;
    select_from_roster(connection, "legolas");
    char_data* legolas = connection.character();
    ASSERT_EQ(legolas->specials.alias, nullptr);

    // Selection holds the object file's contents for the character; entering the game reads the
    // aliases from them.
    connection.send("1");

    const alias_list* alias = legolas->specials.alias;
    ASSERT_NE(alias, nullptr);
    EXPECT_STREQ(alias->keyword, "gg");
    EXPECT_STREQ(alias->command, "get all corpse");
    EXPECT_EQ(alias->next, nullptr);
}

TEST_F(LoginEntryTest, EnteringTheGameSendsTheArrivalTextAndTheRoomLook)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    select_from_roster(connection, "legolas");

    connection.send("1");

    // The news and mail reports, the welcome, then the room look: the room's name and its exits.
    // The character's preferences leave out the room description.
    EXPECT_EQ(connection.output(),
        "There is no unread news.\n\r"
        "There is no mail for you.\n\r"
        "\n\rHere we go...\n\r"
        "\n\r"
            + std::string(LOAD_ROOM_NAME) + "    Exits are:\n\r");
}

TEST_F(LoginEntryTest, EnteringTheGameSavesThePlayerFile)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    select_from_roster(connection, "legolas");
    char_file_u saved_at_selection { };
    ASSERT_TRUE(read_stored_character("legolas", saved_at_selection));

    connection.send("1");

    // Selection saves the room number; entering saves again with the room's index in the world.
    char_file_u saved_at_entry { };
    ASSERT_TRUE(read_stored_character("legolas", saved_at_entry));
    EXPECT_EQ(saved_at_selection.specials2.load_room, LOAD_ROOM_VNUM);
    EXPECT_EQ(saved_at_entry.specials2.load_room, LOAD_ROOM_RNUM);
}

TEST_F(LoginEntryTest, EnteringTheGameAfterAnotherConnectionUsedTheUnlockReturnsToTheAccountMenu)
{
    std::string error_message;
    ASSERT_TRUE(account::admin_link_character(".", ACCOUNT_NAME, "aragorn", CHARACTER_LINKED_AT, nullptr, &error_message))
        << error_message;
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    ASSERT_TRUE(link_character(make_stored_character("boromir", 40, 6262)));

    // Aragorn, below the level that lets an account play two characters, is already playing.
    TestConnection aragorn_connection(CON_PLYNG);
    aragorn_connection.descriptor().descriptor = OTHER_PLAYER_SOCKET;
    char_data* aragorn = create_character_in_game("aragorn", 50, 4242);
    aragorn->desc = &aragorn_connection.descriptor();
    aragorn_connection.descriptor().character = aragorn;
    descriptor_list = &aragorn_connection.descriptor();

    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(".", ACCOUNT_NAME, &account_data, &error_message)) << error_message;
    ASSERT_TRUE(grant_account_character_selection_unlock(account_data, &error_message)) << error_message;

    // Two connections select under the one unlock; the first to enter the game uses it.
    TestConnection second_connection(CON_ACCTSLCT);
    select_from_roster(connection, "legolas");
    select_from_roster(second_connection, "boromir");
    connection.send("1");
    ASSERT_EQ(connection.state(), CON_PLYNG) << connection.output();

    second_connection.send("1");

    EXPECT_EQ(second_connection.state(), CON_ACCTMENU);
    EXPECT_EQ(second_connection.character(), nullptr);
    EXPECT_EQ(second_connection.descriptor().pos, -1);
    EXPECT_EQ(second_connection.output(),
        "You are already connected as Aragorn. Reconnect to that character or log out before playing another character.\n\r"
        "\n\rAccount: player@example.com"
        "\n\rLinked characters: 3\n\r"
        "Active character: Aragorn (level 50, playing)\n\r"
        "\n\r"
        "1) List linked characters\n\r"
        "2) Play a linked character\n\r"
        "3) Add an existing character\n\r"
        "4) Create a new character\n\r"
        "5) Reset account password\n\r"
        "0) Log out\n\r"
        "\n\r"
        "Choice: ");

    // Aragorn's connection ends with this test.
    descriptor_list = nullptr;
}

TEST_F(LoginEntryTest, EnteringTheGameWhileTheCharacterPlaysElsewhereTakesOverThatBody)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    select_from_roster(connection, "legolas");
    char_data* selected_copy = connection.character();

    // Between the selection and entering, the same character comes into play on another connection.
    TestConnection other_connection(CON_PLYNG);
    other_connection.descriptor().descriptor = OTHER_PLAYER_SOCKET;
    char_data* playing_body = create_character_in_game("legolas", 45, 5252);
    playing_body->desc = &other_connection.descriptor();
    other_connection.descriptor().character = playing_body;
    place_in_room(*playing_body, LOAD_ROOM_RNUM);
    ASSERT_NE(selected_copy, playing_body);

    connection.send("1");

    EXPECT_EQ(connection.state(), CON_PLYNG);
    EXPECT_EQ(connection.character(), playing_body);
    EXPECT_EQ(playing_body->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), "You take over your own body, already in use!\n\r");
    EXPECT_EQ(other_connection.state(), CON_CLOSE);
    EXPECT_EQ(other_connection.character(), nullptr);
    EXPECT_EQ(std::string(other_connection.descriptor().output), "This body has been usurped!\n\r");
}

TEST_F(LoginEntryTest, LegacyPasswordLoginShowsTheMotdAndTheLegacyMenu)
{
    TestConnection legacy_connection(CON_PWDNRM);
    char_data* gimli = nullptr;
    ASSERT_NO_FATAL_FAILURE(prepare_legacy_password_prompt(legacy_connection, 0, &gimli));

    legacy_connection.send(LEGACY_PASSWORD);

    EXPECT_EQ(legacy_connection.state(), CON_SLCT);
    EXPECT_EQ(legacy_connection.character(), gimli);
    EXPECT_EQ(legacy_connection.output(), std::string(MORTAL_MOTD) + std::string(LEGACY_CHARACTER_MENU));
}

TEST_F(LoginEntryTest, LegacyPasswordLoginReportsFailedLoginsAndSavesThemCleared)
{
    TestConnection legacy_connection(CON_PWDNRM);
    char_data* gimli = nullptr;
    ASSERT_NO_FATAL_FAILURE(prepare_legacy_password_prompt(legacy_connection, 2, &gimli));

    legacy_connection.send(LEGACY_PASSWORD);

    EXPECT_EQ(legacy_connection.state(), CON_SLCT);
    EXPECT_EQ(legacy_connection.output(),
        std::string(MORTAL_MOTD) + "\n\r\n\r2 LOGIN FAILURES SINCE LAST SUCCESSFUL LOGIN.\n\r"
            + std::string(LEGACY_CHARACTER_MENU));
    EXPECT_EQ(gimli->specials2.bad_pws, 0);

    char name[] = "gimli";
    char_file_u saved { };
    ASSERT_GE(load_char(name, &saved), 0);
    EXPECT_EQ(saved.specials2.bad_pws, 0);
}

TEST_F(LoginEntryTest, LegacyPasswordLoginReportsOneFailedLoginInTheSingular)
{
    TestConnection legacy_connection(CON_PWDNRM);
    char_data* gimli = nullptr;
    ASSERT_NO_FATAL_FAILURE(prepare_legacy_password_prompt(legacy_connection, 1, &gimli));

    legacy_connection.send(LEGACY_PASSWORD);

    EXPECT_EQ(legacy_connection.state(), CON_SLCT);
    EXPECT_EQ(legacy_connection.output(),
        std::string(MORTAL_MOTD) + "\n\r\n\r1 LOGIN FAILURE SINCE LAST SUCCESSFUL LOGIN.\n\r"
            + std::string(LEGACY_CHARACTER_MENU));
}
