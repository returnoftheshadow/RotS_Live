#include "login_test_fixtures.h"
#include "login_world_test.h"
#include "scoped_empty_player_table.h"
#include "temporary_working_directory.h"
#include "test_connection.h"
#include "token_entry_policy.h"

#include "account_cache.h"
#include "account_management.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace login_tests;

// The run token the policy under test was given.
constexpr std::string_view RUN_TOKEN = "0123456789abcdef0123456789abcdef";
// A well-formed token from some other run.
constexpr std::string_view OTHER_RUN_TOKEN = "fedcba9876543210fedcba9876543210";
// What a refused connection is told before the reason.
constexpr std::string_view REFUSED = harness::TokenEntryPolicy::REFUSAL_PREFIX;
// What the log line of a refused malformed line, wrong token or roster number starts with.
constexpr std::string_view BAD_LINE_LOG = "SYSERR: refuse_entry: ";
// What the log line of a refusal by the game's rules starts with; no SYSERR, as a scenario may
// expect the refusal.
constexpr std::string_view REFUSAL_LOG = harness::TokenEntryPolicy::REFUSAL_PREFIX;
// When the test account's email was verified, or later unverified or blocked; any fixed time will do.
constexpr long ACCOUNT_STATUS_CHANGED_AT = ACCOUNT_CREATED_AT + 10;

// The fields of the saved player file that hold the time of the save, which differs between two
// saves of the same character a moment apart.
constexpr std::string_view SAVE_TIME_FIELDS[] = { "\"last_logon\": ", "\"played_seconds\": " };

// Returns the entry line naming token, account_email and character_name.
std::string entry_line(std::string_view token, std::string_view account_email, std::string_view character_name)
{
    std::string line(harness::TokenEntryPolicy::ENTRY_WORD);
    line += ' ';
    line.append(token);
    line += ' ';
    line.append(account_email);
    line += ' ';
    line.append(character_name);
    return line;
}

// Returns the entry line for character_name on the test account with this run's token.
std::string good_entry_line(std::string_view character_name)
{
    return entry_line(RUN_TOKEN, ACCOUNT_EMAIL, character_name);
}

// Returns the bytes of the file at path; adds a failure and returns nothing when it cannot be read.
std::string read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        ADD_FAILURE() << "Could not open " << path;
        return std::string();
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Returns player_file with the number after each of SAVE_TIME_FIELDS replaced by 0.
std::string with_save_times_cleared(std::string player_file)
{
    for (const std::string_view field : SAVE_TIME_FIELDS) {
        const std::size_t field_start = player_file.find(field);
        if (field_start == std::string::npos) {
            ADD_FAILURE() << "The player file has no " << field;
            continue;
        }
        const std::size_t number_start = field_start + field.size();
        const std::size_t number_end = player_file.find_first_not_of("-0123456789", number_start);
        player_file.replace(number_start, number_end - number_start, "0");
    }
    return player_file;
}

// A stored immortal, testimm, with one affect, so entering the game has affects to restore.
char_file_u make_affected_immortal()
{
    char_file_u stored_character = make_stored_character("testimm", LEVEL_IMMORT, 4343);
    affected_type& armor = stored_character.affected[0];
    armor.type = SPELL_ARMOR;
    armor.duration = 10;
    armor.modifier = -20;
    armor.location = APPLY_ARMOR;
    armor.bitvector = 0;
    return stored_character;
}

// What entering the game left behind for one character.
struct EnteredCharacter {
    // The connection's state afterwards.
    int connection_state = -1;
    // What the connection was sent from the moment the character entered.
    std::string arrival_text;
    // The number of the room the character stands in; -1 when it is in none.
    int room_vnum = -1;
    // The character's affects, one description each, in the order the character holds them.
    std::vector<std::string> affects;
    // The character's preference flags.
    long preference_flags = 0;
    // The saved player file, with SAVE_TIME_FIELDS cleared.
    std::string saved_player_bytes;
};

// Records what entering the game left for character_name on entered_connection.
EnteredCharacter record_entry(const TestConnection& entered_connection, std::string_view character_name)
{
    EnteredCharacter entered;
    entered.connection_state = entered_connection.state();
    entered.arrival_text = entered_connection.output();
    const char_data* const character = entered_connection.character();
    if (character == nullptr) {
        ADD_FAILURE() << "No character entered the game.";
        return entered;
    }

    if (character->in_room >= 0 && character->in_room <= top_of_world) {
        entered.room_vnum = world[character->in_room].number;
    }
    for (const affected_type* affect = character->affected; affect != nullptr; affect = affect->next) {
        entered.affects.push_back("type " + std::to_string(affect->type) + ", duration "
            + std::to_string(affect->duration) + ", modifier " + std::to_string(affect->modifier)
            + ", location " + std::to_string(affect->location) + ", bits "
            + std::to_string(affect->bitvector));
    }
    entered.preference_flags = PRF_FLAGS(character);
    const std::string player_path = account::account_character_player_path(".", ACCOUNT_NAME, std::string(character_name));
    entered.saved_player_bytes = with_save_times_cleared(read_file_bytes(player_path));
    return entered;
}

// Takes the characters a run left in the game out of the shared rooms and the character list,
// so their connections can free them and the next run starts from an empty game.
void clear_the_game()
{
    world[0].people = nullptr;
    world[LOAD_ROOM_RNUM].people = nullptr;
    character_list = nullptr;
}

// The test account in an empty game, a connection at the account email prompt and the policy
// that may take it into the game.
class TokenEntryPolicyTest : public LoginWorldTest {
protected:
    void SetUp() override
    {
        LoginWorldTest::SetUp();
        // An account the password login lets in.
        std::string error_message;
        ASSERT_TRUE(account::admin_verify_email(".", ACCOUNT_NAME, "VerifierAdmin", ACCOUNT_STATUS_CHANGED_AT,
            nullptr, &error_message))
            << error_message;
        // A new connection is logged in to no account.
        connection.descriptor().account_name[0] = '\0';
    }

    // Offers line to the policy on offered_connection after discarding its output, keeping what
    // the policy logged in logged. Returns whether the policy handled the line.
    bool offer(TestConnection& offered_connection, std::string_view line)
    {
        offered_connection.clear_output();
        testing::internal::CaptureStderr();
        const bool handled = policy.try_enter(offered_connection.descriptor(), line);
        logged = testing::internal::GetCapturedStderr();
        return handled;
    }

    // Enters typed_name through the login dialogue, from the roster to the character menu's
    // "1", with a player table of its own, and records what it left. stored_name is the name the
    // character's files are stored under.
    EnteredCharacter enter_through_dialogue(std::string_view typed_name, std::string_view stored_name)
    {
        ScopedEmptyPlayerTable run_player_table;
        account_cache::clear();
        TestConnection dialogue_connection(CON_ACCTSLCT);
        dialogue_connection.send(typed_name);
        EXPECT_EQ(dialogue_connection.state(), CON_SLCT) << dialogue_connection.output();
        dialogue_connection.send("1");
        const EnteredCharacter entered = record_entry(dialogue_connection, stored_name);
        clear_the_game();
        return entered;
    }

    // Enters typed_name through the policy with a good entry line, with a player table of its
    // own, and records what it left. stored_name is the name the character's files are stored
    // under.
    EnteredCharacter enter_through_policy(std::string_view typed_name, std::string_view stored_name)
    {
        ScopedEmptyPlayerTable run_player_table;
        account_cache::clear();
        TestConnection policy_connection(CON_NME);
        policy_connection.descriptor().account_name[0] = '\0';
        EXPECT_TRUE(offer(policy_connection, good_entry_line(typed_name)));
        const EnteredCharacter entered = record_entry(policy_connection, stored_name);
        clear_the_game();
        return entered;
    }

    // The policy under test, given RUN_TOKEN.
    harness::TokenEntryPolicy policy { std::string(RUN_TOKEN) };
    // The connection at the account email prompt.
    TestConnection connection { CON_NME };
    // What the last offer logged.
    std::string logged;
};

} // namespace

TEST_F(TokenEntryPolicyTest, DeclinesALineThatIsNotAnEntryLine)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    const std::string other_word_line = "harness-entered " + std::string(RUN_TOKEN) + " " + ACCOUNT_EMAIL + " legolas";
    const std::string capitalized_line = "Harness-enter " + std::string(RUN_TOKEN) + " " + ACCOUNT_EMAIL + " legolas";

    const std::string_view declined_lines[] = { ACCOUNT_EMAIL, "", "   ", other_word_line, capitalized_line };

    for (const std::string_view line : declined_lines) {
        EXPECT_FALSE(offer(connection, line)) << "Expected the policy to decline: '" << line << "'";
        EXPECT_EQ(connection.state(), CON_NME) << "Expected the state unchanged by: '" << line << "'";
        EXPECT_EQ(connection.output(), "") << "Expected nothing sent for: '" << line << "'";
        EXPECT_EQ(connection.character(), nullptr) << "Expected no character for: '" << line << "'";
        EXPECT_STREQ(connection.descriptor().account_name, "") << "Expected no account for: '" << line << "'";
    }
}

TEST_F(TokenEntryPolicyTest, RefusesAnEntryLineWithTheWrongNumberOfWords)
{
    const std::string three_words = "harness-enter " + std::string(RUN_TOKEN) + " " + ACCOUNT_EMAIL;
    const std::string five_words = good_entry_line("legolas") + " extra";

    const std::string_view malformed_lines[] = { three_words, five_words };

    for (const std::string_view line : malformed_lines) {
        TestConnection malformed_connection(CON_NME);
        EXPECT_TRUE(offer(malformed_connection, line)) << "Expected the policy to handle: " << line;
        EXPECT_EQ(malformed_connection.state(), CON_CLOSE) << line;
        EXPECT_EQ(malformed_connection.output(),
            std::string(REFUSED) + "an entry line is \"harness-enter <token> <account email> <character>\".\n\r")
            << line;
        EXPECT_NE(logged.find(std::string(BAD_LINE_LOG) + "(no character named): "), std::string::npos)
            << "Expected the refusal logged; log was: " << logged;
    }
}

TEST_F(TokenEntryPolicyTest, RefusesAnotherRunsTokenAndClosesTheConnection)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, entry_line(OTHER_RUN_TOKEN, ACCOUNT_EMAIL, "legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "the token is not this run's.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_NE(logged.find(std::string(BAD_LINE_LOG) + "legolas: the token is not this run's."), std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesATokenThatIsOnlyTheStartOfThisRunsToken)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, entry_line(RUN_TOKEN.substr(0, 8), ACCOUNT_EMAIL, "legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "the token is not this run's.\n\r");
}

TEST_F(TokenEntryPolicyTest, RefusesAnUnknownAccountAndClosesTheConnection)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, entry_line(RUN_TOKEN, "nobody@example.com", "legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "No account exists for that email address.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "legolas: No account exists for that email address."),
        std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesACharacterNotLinkedToTheAccount)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, good_entry_line("gimli")));

    // The roster's own words for a name it cannot match.
    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(),
        std::string(REFUSED)
            + "Select a linked character by number or name, or enter 0 to return to the account menu.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "gimli: "), std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesADeletedCharacterWithTheRostersWords)
{
    char_file_u legolas = make_stored_character("legolas", 45, 5252);
    SET_BIT(legolas.specials2.act, PLR_DELETED);
    ASSERT_TRUE(link_character(legolas));

    EXPECT_TRUE(offer(connection, good_entry_line("legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(),
        std::string(REFUSED) + "That linked character is deleted and cannot be selected.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "legolas: That linked character is deleted and cannot be selected."),
        std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesACharacterAnotherSessionOnTheAccountForbids)
{
    std::string error_message;
    ASSERT_TRUE(account::admin_link_character(".", ACCOUNT_NAME, "aragorn", CHARACTER_LINKED_AT, nullptr, &error_message))
        << error_message;
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    // Aragorn, below the level that lets an account play two characters, is already playing.
    TestConnection aragorn_connection(CON_PLYNG);
    aragorn_connection.descriptor().descriptor = OTHER_PLAYER_SOCKET;
    char_data* aragorn = create_character_in_game("aragorn", 50, 4242);
    aragorn->desc = &aragorn_connection.descriptor();
    aragorn_connection.descriptor().character = aragorn;
    descriptor_list = &aragorn_connection.descriptor();

    EXPECT_TRUE(offer(connection, good_entry_line("legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(),
        std::string(REFUSED)
            + "You are already connected as Aragorn. Reconnect to that character or log out before playing another character.\n\r");
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "legolas: You are already connected as Aragorn."), std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;

    // Aragorn's connection ends with this test.
    descriptor_list = nullptr;
}

TEST_F(TokenEntryPolicyTest, FollowsTheGamesWizlockRefusalWithTheHarnessRefusal)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    restrict = 46;

    EXPECT_TRUE(offer(connection, good_entry_line("Legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(),
        "The game is temporarily restricted.\r\nTry again later." + std::string(WIZLOCK_MESSAGE) + "\n\r"
            + std::string(REFUSED) + "the game refused legolas at admission.\n\r");
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "Legolas: the game refused legolas at admission."),
        std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesABlockedAccountAsThePasswordLoginDoes)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    std::string error_message;
    ASSERT_TRUE(account::admin_block_account(".", ACCOUNT_NAME, "AdminUser", "Chargeback",
        ACCOUNT_STATUS_CHANGED_AT, nullptr, &error_message))
        << error_message;

    EXPECT_TRUE(offer(connection, good_entry_line("legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "Account authentication failed.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_STREQ(connection.descriptor().account_name, "") << "Expected the connection bound to no account.";
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "legolas: Account authentication failed."), std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesAnUnverifiedAccountAsThePasswordLoginDoes)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    std::string error_message;
    ASSERT_TRUE(account::admin_unverify_email(".", ACCOUNT_NAME, ACCOUNT_STATUS_CHANGED_AT, nullptr, &error_message))
        << error_message;

    EXPECT_TRUE(offer(connection, good_entry_line("legolas")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "Account email verification is still pending.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_STREQ(connection.descriptor().account_name, "") << "Expected the connection bound to no account.";
    EXPECT_NE(logged.find(std::string(REFUSAL_LOG) + "legolas: Account email verification is still pending."),
        std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << "Expected no SYSERR; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, RefusesACharacterGivenByRosterNumber)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, good_entry_line("1")));

    EXPECT_EQ(connection.state(), CON_CLOSE);
    EXPECT_EQ(connection.output(), std::string(REFUSED) + "a character is entered by name, not by roster number.\n\r");
    EXPECT_EQ(connection.character(), nullptr);
    EXPECT_NE(logged.find(std::string(BAD_LINE_LOG) + "1: a character is entered by name"), std::string::npos)
        << "Expected the refusal logged with the character; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, JoinsTheBodyOfALinkDeadCharacterAlreadyInTheGame)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));
    // Legolas lost his connection and his body waits, link-dead, in the load room.
    char_data* waiting_body = create_character_in_game("legolas", 45, 5252);
    place_in_room(*waiting_body, LOAD_ROOM_RNUM);

    EXPECT_TRUE(offer(connection, good_entry_line("legolas")));

    EXPECT_EQ(connection.state(), CON_PLYNG) << connection.output();
    EXPECT_EQ(connection.character(), waiting_body);
    EXPECT_EQ(waiting_body->desc, &connection.descriptor());
    EXPECT_EQ(connection.output(), "Reconnecting.\n\r");
    EXPECT_TRUE(room_holds(LOAD_ROOM_RNUM, waiting_body));
}

TEST_F(TokenEntryPolicyTest, EntersTheNamedCharacterInItsLoadRoom)
{
    ASSERT_TRUE(link_character(make_stored_character("legolas", 45, 5252)));

    EXPECT_TRUE(offer(connection, good_entry_line("Legolas")));

    EXPECT_EQ(connection.state(), CON_PLYNG) << connection.output();
    char_data* const legolas = connection.character();
    ASSERT_NE(legolas, nullptr);
    EXPECT_STREQ(GET_NAME(legolas), "legolas");
    EXPECT_EQ(legolas->desc, &connection.descriptor());
    EXPECT_EQ(legolas->in_room, LOAD_ROOM_RNUM);
    EXPECT_TRUE(room_holds(LOAD_ROOM_RNUM, legolas));
    EXPECT_TRUE(game_holds(legolas));
    EXPECT_EQ(connection.descriptor().prompt_mode, 1);
    EXPECT_STREQ(connection.descriptor().account_name, ACCOUNT_NAME);
    EXPECT_NE(logged.find("Harness entry: legolas"), std::string::npos)
        << "Expected the entry logged; log was: " << logged;
    EXPECT_NE(logged.find("Harness account login for " + std::string(ACCOUNT_EMAIL) + " [" + CONNECTION_HOST + "]"),
        std::string::npos)
        << "Expected the account login audited; log was: " << logged;
}

TEST_F(TokenEntryPolicyTest, EntersACharacterExactlyAsTheLoginDialogueDoes)
{
    ASSERT_TRUE(link_character(make_affected_immortal()));
    ASSERT_TRUE(set_account_preferences(PRF_COMPACT | PRF_WRAP));
    std::error_code error;
    const std::filesystem::path stored_directory = std::filesystem::current_path(error);
    ASSERT_FALSE(error) << error.message();

    // The same stored character entered twice, once per path, from identical storage: the policy
    // in a copy of the storage, then the dialogue in the original.
    EnteredCharacter by_policy;
    {
        rots_tests::TemporaryWorkingDirectory copy_directory;
        std::filesystem::copy(stored_directory, ".", std::filesystem::copy_options::recursive, error);
        ASSERT_FALSE(error) << "Could not copy the storage: " << error.message();
        by_policy = enter_through_policy("Testimm", "testimm");
    }
    const EnteredCharacter by_dialogue = enter_through_dialogue("Testimm", "testimm");

    EXPECT_EQ(by_dialogue.connection_state, CON_PLYNG);
    EXPECT_EQ(by_policy.connection_state, CON_PLYNG);
    EXPECT_EQ(by_policy.arrival_text, by_dialogue.arrival_text);
    // An immortal with no load room of its own enters at the immortals' start room.
    EXPECT_NE(by_dialogue.room_vnum, -1) << "Expected the character in a room.";
    EXPECT_EQ(by_policy.room_vnum, by_dialogue.room_vnum);
    EXPECT_FALSE(by_dialogue.affects.empty()) << "Expected the stored affect restored.";
    EXPECT_EQ(by_policy.affects, by_dialogue.affects);
    EXPECT_EQ(by_policy.preference_flags, by_dialogue.preference_flags);
    EXPECT_TRUE(IS_SET(by_policy.preference_flags, PRF_COMPACT));
    EXPECT_TRUE(IS_SET(by_policy.preference_flags, PRF_WRAP));
    EXPECT_FALSE(by_dialogue.saved_player_bytes.empty());
    EXPECT_EQ(by_policy.saved_player_bytes, by_dialogue.saved_player_bytes);
}
