/* raw_kill clears a dying player's "already kitted" flag. A player with no link is freed by the
 * death, and raw_kill does not touch it afterwards. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"

#include <filesystem>
#include <string>
#include <system_error>

void raw_kill(char_data* ch, const char_data* killer, int attacktype);

extern struct weather_data weather_info;
extern int top_of_p_table;

using namespace scripted_room;

namespace {

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
        char_data* player = nullptr;
        CREATE1(player, char_data);
        clear_char(player, 0);
        /* clear_char zeroes the whole character, the map inside its damage record included. */
        new (&player->damage_details) player_damage_details();
        player->player.name = str_dup(name);
        player->player.race = RACE_HUMAN;
        player->player.level = 10;
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
