// Locks in what idling out does to a player's followers and mounts. This is historic behaviour that
// is deliberately NOT being changed -- see docs/systems/idle-void-and-followers.md. If one of these
// fails, a change elsewhere (object saves, account storage, follower handling) has altered it.

#include "../big_brother.h"
#include "../db.h"
#include "../handler.h"
#include "../limits.h"
#include "../objects_json.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern struct room_data world;
extern int top_of_world;
extern struct char_data* character_list;
extern struct descriptor_data* descriptor_list;
extern struct weather_data weather_info;
extern int r_mortal_idle_room[];
extern struct index_data* mob_index;
extern struct zone_data* zone_table;
extern struct char_data* combat_list;
extern struct char_data* waiting_list;
void clear_char(struct char_data* ch, int mode);
int register_npc_char(struct char_data* mob);
void remove_char_exists(int num);

namespace {

const int kCubVnum = 6601;
const int kSnagaVnum = 11102;
const int kFollowingHorseVnum = 4507;
const int kRiddenHorseVnum = 4508;

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-idle-followers-XXXXXX";
        char* created_path = mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path != nullptr)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            const std::string command = "rm -rf '" + m_path + "'";
            std::system(command.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        char buffer[PATH_MAX];
        if (getcwd(buffer, sizeof(buffer)) != nullptr)
            m_original_path = buffer;
        EXPECT_EQ(chdir(path.c_str()), 0);
    }

    ~ScopedWorkingDirectory()
    {
        if (!m_original_path.empty())
            EXPECT_EQ(chdir(m_original_path.c_str()), 0);
    }

private:
    std::string m_original_path;
};

bool read_file(const std::string& path, std::string* contents)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char buffer[4096];
    size_t count;
    contents->clear();
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        contents->append(buffer, count);
    std::fclose(file);
    return true;
}

std::vector<int> follower_vnums(const objects_json::ObjectSaveData& data)
{
    std::vector<int> vnums;
    for (const auto& follower : data.followers)
        vnums.push_back(follower.fol_vnum);
    std::sort(vnums.begin(), vnums.end());
    return vnums;
}

// One idle player standing in a room with the four kinds of company a player can have: a tamed
// animal, a recruited orc, a mount that follows, and the mount being ridden. Race does not change
// any of the idle handling, so a single player carries all four.
class IdleFollowersTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_FALSE(m_directory.path().empty());
        m_working_directory = new ScopedWorkingDirectory(m_directory.path());
        for (const char* directory : { "plrobjs", "plrobjs/A-E", "plrobjs/F-J", "plrobjs/K-O", "plrobjs/P-T",
                 "plrobjs/U-Z", "plrobjs/ZZZ", "accounts" })
            ASSERT_EQ(mkdir(directory, 0700), 0) << directory;

        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);
        m_saved_top_of_world = top_of_world;
        m_saved_idle_room = r_mortal_idle_room[RACE_HUMAN];
        m_saved_mob_index = mob_index;
        m_saved_character_list = character_list;
        m_saved_descriptor_list = descriptor_list;
        m_saved_zone_table = zone_table;
        m_saved_combat_list = combat_list;
        m_saved_waiting_list = waiting_list;

        // extract_char walks both lists; earlier tests in the same binary can leave stale entries.
        combat_list = nullptr;
        waiting_list = nullptr;

        // char_to_room/char_from_room move a player's race power into the room's zone.
        zone_table = m_zone_table;
        top_of_world = 1;
        for (int room : { kPlayRoom, kIdleRoom }) {
            world[room].people = nullptr;
            world[room].contents = nullptr;
            world[room].zone = 0;
        }
        world[kPlayRoom].number = 1120;
        world[kIdleRoom].number = 1102;
        r_mortal_idle_room[RACE_HUMAN] = kIdleRoom;
        descriptor_list = nullptr;
        game_rules::big_brother::create(weather_info, &world);

        m_mob_index[0].virt = kCubVnum;
        m_mob_index[1].virt = kSnagaVnum;
        m_mob_index[2].virt = kFollowingHorseVnum;
        m_mob_index[3].virt = kRiddenHorseVnum;
        mob_index = m_mob_index;

        player = new char_data {};
        clear_char(player, MOB_VOID);
        player->player.name = strdup("Idleranger"); // free_char releases it when the disconnect extracts the player
        player->player.race = RACE_HUMAN;
        player->player.level = 40;
        register_npc_char(player);
        character_list = player; // extract_char aborts if a player is not on this list
        player->next = nullptr;
        char_to_room(player, kPlayRoom);

        cub = make_mob(0, "cub wolf", "a wolf cub");
        snaga = make_mob(1, "snaga orc", "a snaga");
        following_horse = make_mob(2, "horse", "a following horse");
        ridden_horse = make_mob(3, "horse", "a ridden horse");

        add_follower(cub, player, FOLLOW_MOVE);
        charm(cub, SKILL_TAME);

        SET_BIT(MOB_FLAGS(snaga), MOB_ORC_FRIEND);
        add_follower(snaga, player, FOLLOW_MOVE);
        charm(snaga, SKILL_RECRUIT);

        SET_BIT(MOB_FLAGS(following_horse), MOB_MOUNT);
        add_follower(following_horse, player, FOLLOW_MOVE);

        // Riding, as do_ride leaves it: the ridden mount is not a follower.
        SET_BIT(MOB_FLAGS(ridden_horse), MOB_MOUNT);
        player->mount_data.mount = ridden_horse;
        player->mount_data.mount_number = ridden_horse->abs_number;
        player->mount_data.next_rider = nullptr;
        player->mount_data.next_rider_number = -1;
        ridden_horse->mount_data.rider = player;
        ridden_horse->mount_data.rider_number = player->abs_number;

        ASSERT_TRUE(IS_RIDING(player));
    }

    void TearDown() override
    {
        for (char_data* mob : { cub, snaga, following_horse, ridden_horse }) {
            if (mob == nullptr)
                continue;
            if (mob->in_room != NOWHERE)
                char_from_room(mob);
            remove_char_exists(mob->abs_number);
        }
        if (player != nullptr) {
            if (player->in_room != NOWHERE)
                char_from_room(player);
            remove_char_exists(player->abs_number);
        }

        top_of_world = m_saved_top_of_world;
        r_mortal_idle_room[RACE_HUMAN] = m_saved_idle_room;
        mob_index = m_saved_mob_index;
        character_list = m_saved_character_list;
        descriptor_list = m_saved_descriptor_list;
        zone_table = m_saved_zone_table;
        combat_list = m_saved_combat_list;
        waiting_list = m_saved_waiting_list;
        delete m_working_directory;
    }

    char_data* make_mob(int index, const char* keywords, const char* short_description)
    {
        char_data* mob = new char_data {};
        clear_char(mob, MOB_ISNPC);
        SET_BIT(MOB_FLAGS(mob), MOB_ISNPC);
        mob->nr = index;
        mob->player.name = const_cast<char*>(keywords);
        mob->player.short_descr = const_cast<char*>(short_description);
        register_npc_char(mob);
        char_to_room(mob, kPlayRoom);
        return mob;
    }

    static void charm(char_data* mob, int skill)
    {
        affected_type af {};
        af.type = skill;
        af.duration = -1;
        af.modifier = 0;
        af.location = APPLY_NONE;
        af.bitvector = AFF_CHARM;
        affect_to_char(mob, &af);
        SET_BIT(MOB_FLAGS(mob), MOB_PET);
    }

    // The player's legacy object save, read the way the game's tolerant loader reads it.
    objects_json::ObjectSaveData read_save(bool* follower_section_present)
    {
        std::string bytes;
        EXPECT_TRUE(read_file("plrobjs/F-J/idleranger.obj", &bytes)) << "no object save was written";
        objects_json::ObjectSaveData data;
        bool missing = false;
        std::string error;
        EXPECT_TRUE(objects_json::legacy_object_save_data_from_binary(bytes, &data, &missing, &error)) << error;
        *follower_section_present = !missing;
        return data;
    }

    void idle_until_voided()
    {
        player->specials.timer = 8;
        ASSERT_EQ(check_idling(player), 0);
        ASSERT_EQ(player->in_room, kIdleRoom) << "the player should have been pulled into the void";
    }

    void idle_until_disconnected()
    {
        player->specials.timer = 28;
        m_extracted_player = player;
        const int extracted = check_idling(player);
        // A player with no descriptor is freed by extract_char, so nothing may read it from here on.
        player = nullptr;
        ASSERT_EQ(extracted, 1) << "the player should have been extracted";
    }

    static constexpr int kPlayRoom = 0;
    static constexpr int kIdleRoom = 1;

    TemporaryDirectory m_directory;
    ScopedWorkingDirectory* m_working_directory = nullptr;
    index_data m_mob_index[4] {};
    zone_data m_zone_table[1] {};
    zone_data* m_saved_zone_table = nullptr;
    char_data* m_saved_combat_list = nullptr;
    char_data* m_saved_waiting_list = nullptr;
    int m_saved_top_of_world = 0;
    int m_saved_idle_room = 0;
    index_data* m_saved_mob_index = nullptr;
    char_data* m_saved_character_list = nullptr;
    descriptor_data* m_saved_descriptor_list = nullptr;

    char_data* player = nullptr;
    const char_data* m_extracted_player = nullptr; // compared by address only; never dereferenced
    char_data* cub = nullptr;
    char_data* snaga = nullptr;
    char_data* following_horse = nullptr;
    char_data* ridden_horse = nullptr;
};

TEST_F(IdleFollowersTest, TheVoidReleasesTheRiddenMountOnTheSpot)
{
    idle_until_voided();

    EXPECT_FALSE(IS_RIDING(player));
    EXPECT_EQ(ridden_horse->mount_data.rider, nullptr);
    EXPECT_EQ(ridden_horse->master, nullptr) << "a dismount would re-add it as a follower; the void does not";
    EXPECT_EQ(ridden_horse->in_room, kPlayRoom) << "it stays behind in the room the player idled in";
}

TEST_F(IdleFollowersTest, TheVoidKeepsEveryOtherFollowerFollowingInTheOldRoom)
{
    idle_until_voided();

    EXPECT_EQ(player->specials.was_in_room, kPlayRoom);
    for (char_data* follower : { cub, snaga, following_horse }) {
        EXPECT_EQ(follower->master, player) << GET_NAME(follower);
        EXPECT_EQ(follower->in_room, kPlayRoom) << GET_NAME(follower);
    }
    EXPECT_TRUE(IS_AFFECTED(cub, AFF_CHARM));
    EXPECT_NE(affected_by_spell(cub, SKILL_TAME), nullptr);
    EXPECT_TRUE(IS_AFFECTED(snaga, AFF_CHARM));
    EXPECT_NE(affected_by_spell(snaga, SKILL_RECRUIT), nullptr);
}

TEST_F(IdleFollowersTest, TheVoidSaveHoldsTheFollowersButNotTheReleasedMount)
{
    idle_until_voided();

    bool follower_section_present = false;
    const objects_json::ObjectSaveData save = read_save(&follower_section_present);
    EXPECT_EQ(save.rent.rentcode, RENT_CRASH);
    EXPECT_TRUE(follower_section_present);
    EXPECT_EQ(follower_vnums(save), (std::vector<int> { kFollowingHorseVnum, kCubVnum, kSnagaVnum }))
        << "saved while still in the room, after stop_riding";
}

TEST_F(IdleFollowersTest, AnAutosaveWhileInTheVoidHoldsNoFollowers)
{
    idle_until_voided();

    // The 30-second Crash_save_all cadence: followers are only written if they share the player's
    // room, and the player is now in the idle room. This is why nothing comes back later.
    Crash_crashsave(player);

    bool follower_section_present = false;
    const objects_json::ObjectSaveData save = read_save(&follower_section_present);
    EXPECT_TRUE(follower_section_present);
    EXPECT_TRUE(save.followers.empty());
}

TEST_F(IdleFollowersTest, TheIdleDisconnectReleasesEveryFollowerIntoTheWorld)
{
    idle_until_voided();
    idle_until_disconnected();

    for (char_data* follower : { cub, snaga, following_horse, ridden_horse }) {
        EXPECT_EQ(follower->master, nullptr) << GET_NAME(follower);
        EXPECT_EQ(follower->in_room, kPlayRoom) << GET_NAME(follower) << " should be left in the room, not extracted";
    }
}

TEST_F(IdleFollowersTest, TheIdleDisconnectStripsTheTameAndRecruitCharm)
{
    idle_until_voided();
    idle_until_disconnected();

    EXPECT_FALSE(IS_AFFECTED(cub, AFF_CHARM));
    EXPECT_EQ(affected_by_spell(cub, SKILL_TAME), nullptr);
    EXPECT_FALSE(MOB_FLAGGED(cub, MOB_PET));
    EXPECT_FALSE(IS_AFFECTED(snaga, AFF_CHARM));
    EXPECT_EQ(affected_by_spell(snaga, SKILL_RECRUIT), nullptr);
    EXPECT_FALSE(MOB_FLAGGED(snaga, MOB_PET));
}

TEST_F(IdleFollowersTest, TheIdleDisconnectExtractsThePlayer)
{
    idle_until_voided();
    idle_until_disconnected();

    // Returning to the old room happens inside check_idling before the extraction frees the player,
    // so it is not observable here; the live run in docs/systems/idle-void-and-followers.md covers
    // it (the next login starts in the room the player idled in).
    EXPECT_EQ(character_list, nullptr);
    for (int room : { kPlayRoom, kIdleRoom }) {
        for (char_data* occupant = world[room].people; occupant; occupant = occupant->next_in_room)
            EXPECT_NE(occupant, m_extracted_player) << "the extracted player is still listed in room " << room;
    }
}

TEST_F(IdleFollowersTest, TheIdleSaveHasNoFollowerSectionSoNothingIsRestored)
{
    idle_until_voided();
    idle_until_disconnected();

    bool follower_section_present = true;
    const objects_json::ObjectSaveData save = read_save(&follower_section_present);
    EXPECT_EQ(save.rent.rentcode, RENT_TIMEDOUT);
    EXPECT_FALSE(follower_section_present) << "Crash_idlesave writes no follower section";
    EXPECT_TRUE(save.followers.empty());
}

} // namespace
