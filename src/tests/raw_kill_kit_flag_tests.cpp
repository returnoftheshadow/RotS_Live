/* raw_kill clears a dying player's "already kitted" flag. A player with no link is freed by the
 * death, and neither raw_kill nor a blow that set the death off touches it afterwards. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <filesystem>
#include <string>
#include <system_error>

void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);

extern int top_of_p_table;

using namespace scripted_room;

namespace {

/* A mortal's level: raw_kill sends a player below LEVEL_IMMORT to the mortal start room. */
constexpr int MORTAL_LEVEL = 10;

/* Runs the test in a fresh, empty temporary folder, so whatever a death saves lands there and
 * goes with it. */
class ScopedTemporaryWorkingFolder {
public:
    ScopedTemporaryWorkingFolder()
    {
        std::error_code error;
        m_original = std::filesystem::current_path(error);
        if (error) {
            ADD_FAILURE() << "no current folder: " << error.message();
            return;
        }
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        if (error) {
            ADD_FAILURE() << "no temporary folder: " << error.message();
            return;
        }
        const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
        m_folder = temporary_root / (std::string("rots_raw_kill_") + test->name());
        std::filesystem::remove_all(m_folder, error);
        std::filesystem::create_directories(m_folder, error);
        if (error) {
            ADD_FAILURE() << "cannot make " << m_folder << ": " << error.message();
            return;
        }
        std::filesystem::current_path(m_folder, error);
        if (error) {
            ADD_FAILURE() << "cannot enter " << m_folder << ": " << error.message();
        }
    }

    ~ScopedTemporaryWorkingFolder()
    {
        std::error_code error;
        if (!m_original.empty()) {
            std::filesystem::current_path(m_original, error);
        }
        if (!m_folder.empty()) {
            std::filesystem::remove_all(m_folder, error);
        }
    }

    ScopedTemporaryWorkingFolder(const ScopedTemporaryWorkingFolder&) = delete;
    ScopedTemporaryWorkingFolder& operator=(const ScopedTemporaryWorkingFolder&) = delete;

private:
    // The working folder before the test, entered again when it ends.
    std::filesystem::path m_original;
    // The temporary folder the test runs in, removed when it ends.
    std::filesystem::path m_folder;
};

class RawKillKitFlagTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; a death reports to it. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        /* No player table: a death's save finds nobody to save. */
        m_saved_top_of_p_table = top_of_p_table;
        top_of_p_table = -1;
        /* A player who comes back to life looks at the room. */
        m_saved_room_name = world[0].name;
        m_saved_room_description = world[0].description;
        world[0].name = m_room_name;
        world[0].description = m_room_description;
    }

    void TearDown() override
    {
        ScriptedRoomFixture::TearDown();
        top_of_p_table = m_saved_top_of_p_table;
        world[0].name = m_saved_room_name;
        world[0].description = m_saved_room_description;
    }

    /* A player with no link standing in room 0, on the character list as one in the game is. The
     * game frees it when it is extracted; TearDown extracts it if it is still in the game. */
    static char_data* make_linkless_player(const char* name)
    {
        char_data* player = owned_character().release();
        player->player.name = str_dup(name);
        player->player.race = RACE_HUMAN;
        player->player.level = MORTAL_LEVEL;
        player->abilities.hit = MOB_HIT_POINTS;
        player->tmpabilities.hit = MOB_HIT_POINTS;
        player->specials.position = POSITION_STANDING;
        player->desc = nullptr;
        player->next = character_list;
        character_list = player;
        char_to_room(player, 0);
        return player;
    }

    // Holds whatever the deaths save.
    ScopedTemporaryWorkingFolder m_working_folder;
    // The player table's last index before the test, put back by TearDown.
    int m_saved_top_of_p_table = 0;
    // Room 0's name and description before the test, put back by TearDown.
    char* m_saved_room_name = nullptr;
    char* m_saved_room_description = nullptr;
    // The name and description room 0 has during the test.
    char m_room_name[16] = "A room";
    char m_room_description[16] = "A room.\n\r";
};

} // namespace

TEST_F(RawKillKitFlagTest, ALinklessPlayerIsFreedByItsDeath)
{
    char_data* player = make_linkless_player("Linkless");
    SET_BIT(PLR_FLAGS(player), PLR_WAS_KITTED);

    raw_kill(player, nullptr, 0);

    EXPECT_FALSE(is_in_game(player));
}

TEST_F(RawKillKitFlagTest, AConnectedPlayersDeathClearsItsKitFlag)
{
    SET_BIT(PLR_FLAGS(&m_player), PLR_WAS_KITTED);

    raw_kill(&m_player, nullptr, 0);

    EXPECT_FALSE(IS_SET(PLR_FLAGS(&m_player), PLR_WAS_KITTED));
}

namespace {

/* An offensive bonus that lands a blow on anyone without a far greater parry. */
constexpr int SURE_TO_LAND_OB = 200;
/* A parry that turns aside a blow with SURE_TO_LAND_OB behind it. */
constexpr int CERTAIN_PARRY = 1000;
/* The whole of a character's parry, unspent. */
constexpr int FULL_CURRENT_PARRY = 100;
/* The defender's strength and dexterity: what a parry and a riposte divide and scale by. */
constexpr int DEFENDER_ABILITY = 18;
/* The riposte skill of a player who knows it. */
constexpr int RIPOSTE_SKILL = 80;
/* A roll that number() turns into its lowest result. */
constexpr double LOWEST_ROLL = 0.0;
/* What a blow deals that leaves its hurt victim alive to flee. */
constexpr int NO_DAMAGE = 0;
/* Above the hit points the player is given here. */
constexpr int WIMPY_ABOVE_ITS_HIT_POINTS = MOB_HIT_POINTS + 1;

/* Blows at a player with no link: the guard in the next room, or the attacker's own damage script,
 * kills the player outright, which frees it. */
class LinklessPlayerBlowTest : public RawKillKitFlagTest {
protected:
    void SetUp() override
    {
        RawKillKitFlagTest::SetUp();
        link_second_room();
        /* Whoever comes into the guard's room dies. */
        install_scripts({ { SCRIPT_VNUM,
            { { ON_BEFORE_ENTER, { { SCRIPT_RAW_KILL, nullptr, { SCRIPT_PARAM_CH2 } } } },
                { ON_DAMAGE, { { SCRIPT_RAW_KILL, nullptr, { SCRIPT_PARAM_CH2 } } } } } } });
    }
};

} // namespace

/* A wimpy player flees the blow, into a room where it is killed: the blow says the player is gone.
 * AddressSanitizer is what protects this test: the blow would go on to read the freed player. */
TEST_F(LinklessPlayerBlowTest, ABlowLetsGoOfAWimpyPlayerKilledWhereItFlees)
{
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* attacker = make_mob("attacker", 0);
    char_data* player = make_linkless_player("Wimp");
    WIMP_LEVEL(player) = WIMPY_ABOVE_ITS_HIT_POINTS;

    EXPECT_EQ(1, damage(attacker, player, NO_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(player));
    EXPECT_TRUE(is_in_game(attacker));
}

/* A player with no link flees any blow in a fight, here into a room where it is killed: the blow
 * says the player is gone. AddressSanitizer is what protects this test: the blow would go on to
 * write to the freed player. */
TEST_F(LinklessPlayerBlowTest, ABlowLetsGoOfAPlayerKilledWhereItFleesForHavingNoLink)
{
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* attacker = make_mob("attacker", 0);
    char_data* player = make_linkless_player("Absent");

    EXPECT_EQ(1, damage(attacker, player, NO_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(player));
    EXPECT_TRUE(is_in_game(attacker));
}

/* The player turns the attacker's blow aside and ripostes, and the attacker's damage script kills
 * the player for it. AddressSanitizer is what protects this test: the blow would go on to check the
 * freed player's grip. */
TEST_F(LinklessPlayerBlowTest, ABlowLetsGoOfADefenderItsRiposteGotKilled)
{
    char_data* attacker = make_mob("attacker", SCRIPT_VNUM);
    attacker->points.OB = SURE_TO_LAND_OB;
    attacker->specials.ENERGY = ENE_TO_HIT * 2;
    char_data* defender = make_linkless_player("Defender");
    defender->specials.position = POSITION_FIGHTING;
    defender->tmpabilities.str = DEFENDER_ABILITY;
    defender->tmpabilities.dex = DEFENDER_ABILITY;
    defender->points.parry = CERTAIN_PARRY;
    SET_CURRENT_PARRY(defender) = FULL_CURRENT_PARRY;
    defender->knowledge[SKILL_RIPOSTE] = RIPOSTE_SKILL;
    equip_char(defender, make_object(SWORD_VNUM, 0), WIELD);
    push_test_random_value(LOWEST_ROLL); /* the to-hit roll: not the one that never misses */
    push_test_random_value(LOWEST_ROLL); /* the offensive bonus's roll */
    push_test_random_value(LOWEST_ROLL); /* the riposte's roll: it lands */

    hit(attacker, defender, TYPE_UNDEFINED);

    EXPECT_FALSE(is_in_game(defender));
    EXPECT_TRUE(is_in_game(attacker));
}
