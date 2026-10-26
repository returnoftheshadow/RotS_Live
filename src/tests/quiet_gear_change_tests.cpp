/* Gear goes on and comes off quietly for someone coming into or leaving the game: renting, a
 * follower dismissed at rent, logging in, an immortal quitting and a purge. A harmful item spell
 * is not cast and an item's poison hurts nobody, while a spell that gives a lasting affect for as
 * long as the item is worn still gives it and takes it away. Taking gear off a player who is
 * killed is not quiet, and the player still dies only once. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../objects_json.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

void Crash_rentsave(struct char_data* ch, int cost);
void load_character(struct char_data* ch);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_quit(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_purge(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

extern struct weather_data weather_info;
extern int top_of_p_table;
extern int r_mortal_start_room[];
extern struct index_data* mob_index;

using namespace scripted_room;

namespace {

/* The poison bit, as the APPLY_BITVECTOR modifier that gives it (the bit's position). */
constexpr int POISON_BIT_POSITION = 11;
/* The mana regeneration a test ring gives as a stat affect. */
constexpr int RING_MANA_REGENERATION = 7;
/* The vnum a test mob (one with no prototype) is saved under as a follower. */
constexpr int TEST_MOB_VNUM = 9900;
/* Few enough hit points that an item's poison (5) or chill ray (10 or more) kills. */
constexpr int LOW_HIT_POINTS = 1;
/* The hit points the player starts with, plenty to survive anything the tests set off. */
constexpr int PLAYER_HIT_POINTS = 500;
/* The largest object file the tests read back. */
constexpr std::uintmax_t OBJECT_FILE_SIZE_LIMIT = 1 << 20;
/* The subfolders of plrobjs that Crash_get_filename files a character's objects under. */
constexpr const char* OBJECT_FILE_FOLDERS[] = { "plrobjs/A-E", "plrobjs/F-J", "plrobjs/K-O",
    "plrobjs/P-T", "plrobjs/U-Z", "plrobjs/ZZZ" };

/* Runs the test in a fresh, empty temporary folder holding the object file folders, so whatever
 * a rent, a login or a death reads and writes lands there and goes with it. */
class ScopedObjectFileFolder {
public:
    ScopedObjectFileFolder()
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
        m_folder = temporary_root / (std::string("rots_quiet_gear_") + test->name());
        std::filesystem::remove_all(m_folder, error);
        for (const char* subfolder : OBJECT_FILE_FOLDERS) {
            std::filesystem::create_directories(m_folder / subfolder, error);
            if (error) {
                ADD_FAILURE() << "cannot make " << (m_folder / subfolder) << ": " << error.message();
                return;
            }
        }
        std::filesystem::current_path(m_folder, error);
        if (error) {
            ADD_FAILURE() << "cannot enter " << m_folder << ": " << error.message();
        }
    }

    ~ScopedObjectFileFolder()
    {
        std::error_code error;
        if (!m_original.empty()) {
            std::filesystem::current_path(m_original, error);
        }
        if (!m_folder.empty()) {
            std::filesystem::remove_all(m_folder, error);
        }
    }

    ScopedObjectFileFolder(const ScopedObjectFileFolder&) = delete;
    ScopedObjectFileFolder& operator=(const ScopedObjectFileFolder&) = delete;

private:
    // The working folder before the test, entered again when it ends.
    std::filesystem::path m_original;
    // The temporary folder the test runs in, removed when it ends.
    std::filesystem::path m_folder;
};

class QuietGearChangeTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; damage and a death report to them. Making them again keeps the ones
         * there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        /* No player table: a save finds nobody to save. */
        m_saved_top_of_p_table = top_of_p_table;
        top_of_p_table = -1;
        /* A player who comes back to life, or logs in, starts in room 0 and looks at it. */
        m_saved_mortal_start_room = r_mortal_start_room[RACE_HUMAN];
        r_mortal_start_room[RACE_HUMAN] = 0;
        m_saved_room_name = world[0].name;
        m_saved_room_description = world[0].description;
        m_saved_room_flags = world[0].room_flags;
        world[0].name = m_room_name;
        world[0].description = m_room_description;
        /* No crack opens under an earthquake indoors (it would add a room). */
        world[0].room_flags = INDOORS;
        /* A test mob has no prototype (nr -1), and a follower is saved under its mob's vnum from
         * mob_index[nr]: index -1 is given an entry. */
        m_saved_mob_index = mob_index;
        m_mob_index[0].virt = TEST_MOB_VNUM;
        mob_index = &m_mob_index[1];
        /* Enough of a character for a login not to take it for a damaged one (calc_load_room). */
        m_player.abilities.str = 18;
        m_player.abilities.dex = 18;
        m_player.tmpabilities.move = 50;
        m_player.abilities.hit = PLAYER_HIT_POINTS;
        m_player.tmpabilities.hit = PLAYER_HIT_POINTS;
    }

    void TearDown() override
    {
        take_player_off_the_character_list();
        world[0].name = m_saved_room_name;
        world[0].description = m_saved_room_description;
        world[0].room_flags = m_saved_room_flags;
        ScriptedRoomFixture::TearDown();
        top_of_p_table = m_saved_top_of_p_table;
        mob_index = m_saved_mob_index;
        r_mortal_start_room[RACE_HUMAN] = m_saved_mortal_start_room;
    }

    /* Puts the player on the character list, as one in the game is. */
    void put_player_on_the_character_list()
    {
        m_player.next = character_list;
        character_list = &m_player;
    }

    /* Takes the player off the character list, if it is there: the fixture never frees it. */
    void take_player_off_the_character_list()
    {
        for (char_data** link = &character_list; *link; link = &(*link)->next) {
            if (*link == &m_player) {
                *link = m_player.next;
                return;
            }
        }
    }

    /* Gives <item> the spell <spell_number> as its first affect, cast at its wearer's level. */
    static void give_spell(obj_data* item, int spell_number)
    {
        item->affected[0].location = APPLY_SPELL;
        item->affected[0].modifier = spell_number;
    }

    /* A ring that poisons whoever wears it, as the poison flag it gives. */
    obj_data* make_poisonous_ring()
    {
        obj_data* ring = make_object(RING_VNUM, 0);
        ring->affected[0].location = APPLY_BITVECTOR;
        ring->affected[0].modifier = POISON_BIT_POSITION;
        return ring;
    }

    /* Puts on <wearer> a ring that casts <spell_number> as it comes off. It takes its spell only
     * once it is on, so putting it on casts nothing. */
    obj_data* put_on_ring_casting(char_data* wearer, int spell_number)
    {
        obj_data* ring = make_object(RING_VNUM, 0);
        equip_char(wearer, ring, WEAR_FINGER_R);
        EXPECT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
        give_spell(ring, spell_number);
        return ring;
    }

    /* The path of the player's object file, as the game names it. */
    std::filesystem::path player_object_file() const
    {
        char path[MAX_INPUT_LENGTH];
        if (!Crash_get_filename(GET_NAME(&m_player), path)) {
            ADD_FAILURE() << "the player's object file has no name";
            return {};
        }
        return std::filesystem::path(path);
    }

    /* Reads back the player's object file into <out_data>; false, after reporting why, when
     * there is none or it does not parse. */
    bool read_player_object_file(objects_json::ObjectSaveData& out_data) const
    {
        const std::filesystem::path path = player_object_file();
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) {
            ADD_FAILURE() << "no object file at " << path << ": " << error.message();
            return false;
        }
        if (size > OBJECT_FILE_SIZE_LIMIT) {
            ADD_FAILURE() << path << " is " << size << " bytes, more than an object file holds";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error_message;
        if (!objects_json::object_save_data_from_binary(bytes, &out_data, &error_message)) {
            ADD_FAILURE() << path << " does not parse: " << error_message;
            return false;
        }
        return true;
    }

    /* Writes <data> as the player's object file; false, after reporting why, when it cannot. */
    bool write_player_object_file(const objects_json::ObjectSaveData& data) const
    {
        std::string bytes;
        std::string error_message;
        if (!objects_json::object_save_data_to_binary(data, &bytes, &error_message)) {
            ADD_FAILURE() << "the object data does not encode: " << error_message;
            return false;
        }
        std::ofstream file(player_object_file(), std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            ADD_FAILURE() << "cannot write " << player_object_file();
            return false;
        }
        return true;
    }

    /* An object file record for the test prototype <vnum>, at <wear_pos> (MAX_WEAR: carried). */
    static objects_json::ObjectRecord make_record(int vnum, int wear_pos)
    {
        objects_json::ObjectRecord record;
        record.item_number = vnum;
        record.wear_pos = wear_pos;
        record.weight = 1;
        record.timer = -1;
        return record;
    }

    /* The objects lying in room 0 that are corpses. */
    static int count_corpses_in_room()
    {
        int corpses = 0;
        for (const obj_data* content = world[0].contents; content; content = content->next_content) {
            if (GET_ITEM_TYPE(content) == ITEM_CONTAINER && content->obj_flags.value[3] == 1) {
                ++corpses;
            }
        }
        return corpses;
    }

    /* How many times <character> is on room 0's list of people. Stops counting past the most
     * the list could hold without a loop, so a list that runs in a circle still ends. */
    static int count_in_room(const char_data* character)
    {
        constexpr int MOST_PEOPLE_COUNTED = 16;
        int count = 0;
        int visited = 0;
        for (const char_data* person = world[0].people; person && visited < MOST_PEOPLE_COUNTED;
             person = person->next_in_room) {
            ++visited;
            if (person == character) {
                ++count;
            }
        }
        return count;
    }

    // The player table's last index before the test, put back by TearDown.
    int m_saved_top_of_p_table = 0;
    // A human's start room before the test, put back by TearDown.
    int m_saved_mortal_start_room = 0;
    // Room 0's name, description and flags before the test, put back by TearDown.
    char* m_saved_room_name = nullptr;
    char* m_saved_room_description = nullptr;
    long m_saved_room_flags = 0;
    // The name and description room 0 has during the test.
    char m_room_name[16] = "A room";
    char m_room_description[16] = "A room.\n\r";
    // The mob index the tests use: its first entry is index -1, a test mob's.
    index_data m_mob_index[2] {};
    // The mob index SetUp found, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    // Holds the object files the tests read and write.
    ScopedObjectFileFolder m_object_file_folder;
};

} // namespace

/* ---- renting ---- */

/* A renter too hurt to survive their ring's chill ray is not struck by it as the ring comes off:
 * they rent alive, nothing is left in the room, and the ring is in the rent file and nowhere
 * else. */
TEST_F(QuietGearChangeTest, ARenterIsNotStruckByTheirGearsSpell)
{
    obj_data* ring = put_on_ring_casting(&m_player, SPELL_CHILL_RAY);
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    Crash_rentsave(&m_player, 0);

    EXPECT_EQ(LOW_HIT_POINTS, GET_HIT(&m_player));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_FALSE(is_in_object_list(ring));
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
    objects_json::ObjectSaveData saved;
    ASSERT_TRUE(read_player_object_file(saved));
    EXPECT_EQ(RENT_RENTED, saved.rent.rentcode);
    ASSERT_EQ(1u, saved.objects.size());
    EXPECT_EQ(RING_VNUM, saved.objects[0].item_number);
    EXPECT_EQ(WEAR_FINGER_R, saved.objects[0].wear_pos);
}

/* A renter's follower too hurt to survive its poisonous ring is dismissed without it hurting:
 * no corpse is left and the ring goes with the follower. */
TEST_F(QuietGearChangeTest, ARentersFollowerLeavesWithoutItsGearsPoisonHurtingIt)
{
    char_data* follower = make_mob("follower", 0);
    obj_data* ring = make_poisonous_ring();
    obj_to_char(ring, follower);
    run_command_as(follower, do_wear, "ring");
    ASSERT_EQ(ring, follower->equipment[WEAR_FINGER_R]);
    follower->tmpabilities.hit = LOW_HIT_POINTS;
    add_follower(follower, &m_player, FOLLOW_MOVE);

    Crash_rentsave(&m_player, 0);

    EXPECT_FALSE(is_in_game(follower));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_FALSE(is_in_object_list(ring));
}

/* ---- logging in ---- */

/* A player too hurt to survive their ring's chill ray logs in wearing it: they are not struck,
 * they stand in their room once, and everything they rented with comes back. */
TEST_F(QuietGearChangeTest, APlayerLoggingInIsNotStruckByTheirGearsSpell)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    objects_json::ObjectRecord ring = make_record(RING_VNUM, WEAR_FINGER_R);
    ring.affects[0].location = APPLY_SPELL;
    ring.affects[0].modifier = SPELL_CHILL_RAY;
    rented.objects.push_back(ring);
    rented.objects.push_back(make_record(STONE_VNUM, MAX_WEAR));
    ASSERT_TRUE(write_player_object_file(rented));
    char_from_room(&m_player);
    m_player.specials2.load_room = ROOM_VNUM;
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    load_character(&m_player);

    EXPECT_EQ(LOW_HIT_POINTS, GET_HIT(&m_player));
    EXPECT_EQ(0, m_player.in_room);
    EXPECT_EQ(1, count_in_room(&m_player));
    EXPECT_EQ(0, count_corpses_in_room());
    const obj_data* worn = m_player.equipment[WEAR_FINGER_R];
    ASSERT_NE(nullptr, worn);
    EXPECT_EQ(RING_VNUM, obj_index[worn->item_number].virt);
    ASSERT_NE(nullptr, m_player.carrying);
    EXPECT_EQ(STONE_VNUM, obj_index[m_player.carrying->item_number].virt);
}

/* An evasion ring put on quietly, as a login puts gear on, gives its evasion for as long as it
 * is worn and takes it away as it comes off quietly; the ring's mana regeneration comes and goes too. */
TEST_F(QuietGearChangeTest, AWornAffectComesAndGoesWithAQuietGearChange)
{
    obj_data* ring = make_object(RING_VNUM, 0);
    give_spell(ring, SPELL_ARMOR);
    ring->affected[1].location = APPLY_MANA_REGEN;
    ring->affected[1].modifier = RING_MANA_REGENERATION;

    ASSERT_TRUE(equip_char(&m_player, ring, WEAR_FINGER_R, gear_change::quiet));

    const affected_type* evasion = affected_by_spell(&m_player, SPELL_ARMOR);
    ASSERT_NE(nullptr, evasion);
    EXPECT_EQ(-1, evasion->duration);
    const int mana_regeneration_wearing_the_ring = m_player.points.mana_regen;

    obj_data* removed = unequip_char(&m_player, WEAR_FINGER_R, gear_change::quiet);

    EXPECT_EQ(ring, removed);
    EXPECT_EQ(nullptr, affected_by_spell(&m_player, SPELL_ARMOR));
    EXPECT_EQ(mana_regeneration_wearing_the_ring - RING_MANA_REGENERATION, m_player.points.mana_regen);
    extract_obj(removed);
}

/* ---- quitting and removal ---- */

/* An immortal quitting in an earthquake ring takes it off without casting it: the mob standing
 * beside them is not shaken. */
TEST_F(QuietGearChangeTest, AnImmortalQuittingDoesNotCastTheirGearsSpell)
{
    m_player.player.level = LEVEL_IMMORT;
    put_on_ring_casting(&m_player, SPELL_EARTHQUAKE);
    char_data* bystander = make_mob("bystander", 0);
    put_player_on_the_character_list();

    run_command(do_quit, "", SCMD_QUIT);

    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(bystander));
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
}

/* A mob purged while too hurt to survive its poisonous ring is removed once: no corpse is left
 * and the ring goes with it. */
TEST_F(QuietGearChangeTest, APurgedMobLeavesWithoutItsGearsPoisonHurtingIt)
{
    m_player.player.level = LEVEL_IMPL;
    char_data* purged = make_mob("purged", 0);
    obj_data* ring = make_poisonous_ring();
    obj_to_char(ring, purged);
    run_command_as(purged, do_wear, "ring");
    ASSERT_EQ(ring, purged->equipment[WEAR_FINGER_R]);
    purged->tmpabilities.hit = LOW_HIT_POINTS;

    run_command(do_purge, "purged");

    EXPECT_FALSE(is_in_game(purged));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_FALSE(is_in_object_list(ring));
}

/* ---- dying ---- */

/* A connected player slain while wearing a poisonous ring dies once: taking the ring off the
 * dying player does not kill them again, so there is one corpse and the ring is in it. */
TEST_F(QuietGearChangeTest, AConnectedPlayerSlainInPoisonousGearDiesOnce)
{
    obj_data* ring = make_poisonous_ring();
    obj_to_char(ring, &m_player);
    run_command(do_wear, "ring");
    ASSERT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    raw_kill(&m_player, nullptr, 0);

    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_EQ(1, count_in_room(&m_player));
    ASSERT_NE(nullptr, ring->in_obj);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
}
