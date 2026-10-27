/* Gear goes on and comes off quietly for someone coming into or leaving the game: renting, a
 * follower dismissed at rent, logging in, an immortal quitting and a purge. A harmful item spell
 * is not cast and an item's poison hurts nobody, while a spell that gives a lasting affect for as
 * long as the item is worn still gives it and takes it away. Taking gear off a player who is
 * killed is not quiet, and the player still dies only once.
 *
 * A login then casts the rest, once the player and its followers all stand in the room: every
 * item spell, and the blow of an item's poison, as putting the gear on casts them. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../limits.h"
#include "../objects_json.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

void Crash_rentsave(struct char_data* ch, int cost);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_quit(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_purge(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

extern struct weather_data weather_info;
extern int top_of_p_table;
extern int r_mortal_start_room[];
extern int r_mortal_idle_room[];
extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

/* The poison bit, as the APPLY_BITVECTOR modifier that gives it (the bit's position). */
constexpr int POISON_BIT_POSITION = 11;
/* The mana regeneration a test ring gives as a stat affect. */
constexpr int RING_MANA_REGENERATION = 7;
/* The vnum a test mob (one with no prototype) is saved under as a follower. */
constexpr int TEST_MOB_VNUM = 9900;
/* The vnums of the two mob prototypes: the one a login makes followers from, and the one it makes
 * their mounts from. */
constexpr int FOLLOWER_VNUM = 9901;
constexpr int MOUNT_VNUM = 9902;
/* Their indexes in the prototype table: read_mobile gives a mob its prototype's index as nr. */
constexpr int FOLLOWER_PROTOTYPE = 0;
constexpr int MOUNT_PROTOTYPE = 1;
constexpr int MOB_PROTOTYPE_COUNT = 2;
/* A follower's constitution: low enough that it dies at 0 hit points, as a player does. */
constexpr int FOLLOWER_CONSTITUTION = 1;
/* A vnum for room 0 that is also another room's index (SECOND_ROOM's): the room a login that took
 * the vnum for a room would strike. */
constexpr int LOGIN_ROOM_VNUM = SECOND_ROOM;
/* The idle timer, in minutes, from which the next idle check pulls a player into the void and
 * removes them from the game at once. */
constexpr int IDLE_MINUTES_BEFORE_REMOVAL = 28;
/* What the room sees when an item's poison blow kills its wearer. */
constexpr const char* POISON_COLLAPSE_MESSAGE = "suddenly collapses on the ground.";
/* The damage an item's poison deals its wearer as the item goes on (equip_char). */
constexpr int POISON_BLOW_DAMAGE = 5;
/* Enough long aliases, saved between a player's objects and their followers, that the followers
 * lie past what the C library has read ahead of the aliases: a rewrite of the object file while
 * it is being read would lose them. */
constexpr int ALIASES_PAST_THE_READ_BUFFER = 24;
constexpr std::size_t ALIAS_COMMAND_LENGTH = 250;
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
        /* A player idling out is pulled into the void, the second room. */
        m_saved_mortal_idle_room = r_mortal_idle_room[RACE_HUMAN];
        r_mortal_idle_room[RACE_HUMAN] = SECOND_ROOM;
        m_saved_room_name = world[0].name;
        m_saved_room_description = world[0].description;
        m_saved_room_flags = world[0].room_flags;
        world[0].name = m_room_name;
        world[0].description = m_room_description;
        /* The second room is where a player who dies may come back to life, and looks at it. */
        m_saved_second_room_name = world[SECOND_ROOM].name;
        m_saved_second_room_description = world[SECOND_ROOM].description;
        world[SECOND_ROOM].name = m_room_name;
        world[SECOND_ROOM].description = m_room_description;
        /* No crack opens under an earthquake indoors (it would add a room). */
        world[0].room_flags = INDOORS;
        /* A test mob has no prototype (nr -1), and a follower is saved under its mob's vnum from
         * mob_index[nr]: index -1 is given an entry. */
        m_saved_mob_index = mob_index;
        m_mob_index[0].virt = TEST_MOB_VNUM;
        mob_index = &m_mob_index[1];
        /* Then the prototypes read_mobile copies, in vnum order for real_mobile's search. */
        m_saved_mob_proto = mob_proto;
        m_saved_top_of_mobt = top_of_mobt;
        m_mob_index[1 + FOLLOWER_PROTOTYPE].virt = FOLLOWER_VNUM;
        m_mob_index[1 + MOUNT_PROTOTYPE].virt = MOUNT_VNUM;
        make_mob_prototype(FOLLOWER_PROTOTYPE, m_follower_keywords);
        make_mob_prototype(MOUNT_PROTOTYPE, m_mount_keywords);
        mob_proto = m_mob_prototypes;
        top_of_mobt = MOB_PROTOTYPE_COUNT - 1;
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
        release_player_aliases();
        world[0].name = m_saved_room_name;
        world[0].description = m_saved_room_description;
        world[0].room_flags = m_saved_room_flags;
        world[SECOND_ROOM].name = m_saved_second_room_name;
        world[SECOND_ROOM].description = m_saved_second_room_description;
        ScriptedRoomFixture::TearDown();
        top_of_p_table = m_saved_top_of_p_table;
        mob_index = m_saved_mob_index;
        mob_proto = m_saved_mob_proto;
        top_of_mobt = m_saved_top_of_mobt;
        for (char_data& prototype : m_mob_prototypes) {
            release_cleared_character(&prototype);
        }
        r_mortal_start_room[RACE_HUMAN] = m_saved_mortal_start_room;
        r_mortal_idle_room[RACE_HUMAN] = m_saved_mortal_idle_room;
    }

    /* Releases the aliases a login gave the player, as do_alias removes one: the fixture never
     * frees the player. */
    void release_player_aliases()
    {
        alias_list* alias = GET_ALIAS(&m_player);
        while (alias != nullptr) {
            alias_list* next_alias = alias->next;
            RELEASE(alias->command);
            RELEASE(alias);
            alias = next_alias;
        }
        GET_ALIAS(&m_player) = nullptr;
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

    /* A player with no connection standing in room 0 beside the fixture's player, idle long enough
     * that the next idle check removes them from the game and frees them. The void is the second
     * room, and room 0 has no exit. */
    char_data* make_idle_player()
    {
        link_second_room();
        /* No exit: an earthquake opens no way down, and nobody leaves. */
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            world[0].dir_option[direction] = nullptr;
        }
        char_data* idler = nullptr;
        CREATE1(idler, char_data);
        clear_char(idler, 0);
        new (&idler->damage_details) player_damage_details();
        idler->player.name = str_dup("Idler");
        idler->player.race = RACE_HUMAN;
        idler->player.level = 10;
        idler->abilities.str = 18;
        idler->abilities.dex = 18;
        idler->tmpabilities.str = 18;
        idler->tmpabilities.dex = 18;
        idler->abilities.hit = PLAYER_HIT_POINTS;
        idler->tmpabilities.hit = PLAYER_HIT_POINTS;
        idler->specials.position = POSITION_STANDING;
        idler->in_room = NOWHERE;
        register_pc_char(idler);
        /* extract_char takes a player off the character list, and aborts if they are not on it. */
        idler->next = character_list;
        character_list = idler;
        char_to_room(idler, 0);
        idler->specials.timer = IDLE_MINUTES_BEFORE_REMOVAL;
        return idler;
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

    /* Makes prototype <index> a mob called <keywords> with MOB_HIT_POINTS hit points, which
     * read_mobile copies exactly. free_char leaves a prototype's text and profession block alone,
     * and TearDown releases the block. */
    void make_mob_prototype(int index, char* keywords)
    {
        char_data& prototype = m_mob_prototypes[index];
        clear_char(&prototype, MOB_ISNPC);
        new (&prototype.damage_details) player_damage_details();
        SET_BIT(MOB_FLAGS(&prototype), MOB_ISNPC);
        prototype.nr = index;
        prototype.player.name = keywords;
        prototype.player.short_descr = keywords;
        prototype.player.race = RACE_HUMAN;
        prototype.player.level = 10;
        prototype.abilities.str = 18;
        prototype.abilities.dex = 18;
        prototype.abilities.intel = 18;
        prototype.abilities.wil = 18;
        prototype.abilities.lea = 18;
        prototype.abilities.con = FOLLOWER_CONSTITUTION;
        prototype.abilities.hit = MOB_HIT_POINTS;
        prototype.tmpabilities.hit = MOB_HIT_POINTS;
        prototype.specials.position = POSITION_STANDING;
        prototype.specials.default_pos = POSITION_STANDING;
    }

    /* The hit points every follower a login makes from now on starts with. */
    void set_follower_hit_points(int hit_points)
    {
        /* read_mobile draws a mob's hit points between these two. */
        m_mob_prototypes[FOLLOWER_PROTOTYPE].abilities.hit = hit_points;
        m_mob_prototypes[FOLLOWER_PROTOTYPE].tmpabilities.hit = hit_points;
    }

    /* Makes the player's hit points, as the gear's affects work them out at login, many times
     * any one blow a test's earthquake deals: a warrior grown to the most their level allows. */
    void make_player_hardy()
    {
        m_player.profs->prof_coof[PROF_WARRIOR] = 100;
        m_player.specials2.mini_level = LEVEL_MAX * 100;
    }

    /* A follower record for the player's object file: a tamed mob made from the follower
     * prototype, wearing <gear>, and riding in on a mob made from the mount prototype when
     * <with_mount>. */
    static objects_json::FollowerData make_follower_record(
        std::vector<objects_json::ObjectRecord> gear, bool with_mount = false)
    {
        objects_json::FollowerData follower;
        follower.fol_vnum = FOLLOWER_VNUM;
        follower.flag_config = FOL_TAMED;
        if (with_mount) {
            follower.mount_vnum = MOUNT_VNUM;
        }
        follower.objects = std::move(gear);
        return follower;
    }

    /* An object file record for a ring worn on the right hand that casts <spell_number>. */
    static objects_json::ObjectRecord make_ring_record_casting(int spell_number)
    {
        objects_json::ObjectRecord ring = make_record(RING_VNUM, WEAR_FINGER_R);
        ring.affects[0].location = APPLY_SPELL;
        ring.affects[0].modifier = spell_number;
        return ring;
    }

    /* Writes <rented> as the player's object file and takes the player out of the game, as it is
     * before a login: in no room, coming back to room 0. False, after reporting why, when the
     * file cannot be written. */
    bool rent_out_player(const objects_json::ObjectSaveData& rented)
    {
        if (!write_player_object_file(rented)) {
            return false;
        }
        char_from_room(&m_player);
        m_player.specials2.load_room = ROOM_VNUM;
        return true;
    }

    /* The mobs made from prototype <prototype> that stand in room <room>, in the order they stand
     * there. */
    static std::vector<char_data*> mobs_in_room(int prototype, int room = 0)
    {
        std::vector<char_data*> mobs;
        for (char_data* person = world[room].people; person; person = person->next_in_room) {
            if (IS_NPC(person) && person->nr == prototype) {
                mobs.push_back(person);
            }
        }
        return mobs;
    }

    /* The followers a login made that stand in room <room>. */
    static std::vector<char_data*> followers_in_room(int room = 0)
    {
        return mobs_in_room(FOLLOWER_PROTOTYPE, room);
    }

    /* How many mobs made from the prototypes are in the game, wherever they are. */
    static int count_prototype_mobs_in_game()
    {
        int count = 0;
        for (const char_data* character = character_list; character; character = character->next) {
            if (IS_NPC(character) && character->nr >= 0) {
                ++count;
            }
        }
        return count;
    }

    /* How many objects made from the prototype with <vnum> are in the game. */
    static int count_objects_in_game(int vnum)
    {
        int count = 0;
        for (const obj_data* object = object_list; object; object = object->next) {
            if (object->item_number >= 0 && obj_index[object->item_number].virt == vnum) {
                ++count;
            }
        }
        return count;
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
    // A human's idle room before the test, put back by TearDown.
    int m_saved_mortal_idle_room = 0;
    // Room 0's name, description and flags before the test, put back by TearDown.
    char* m_saved_room_name = nullptr;
    char* m_saved_room_description = nullptr;
    long m_saved_room_flags = 0;
    // The second room's name and description before the test, put back by TearDown.
    char* m_saved_second_room_name = nullptr;
    char* m_saved_second_room_description = nullptr;
    // The name and description rooms 0 and SECOND_ROOM have during the test.
    char m_room_name[16] = "A room";
    char m_room_description[16] = "A room.\n\r";
    // The mob index the tests use: its first entry is index -1, a test mob's, and then one for
    // each prototype.
    index_data m_mob_index[1 + MOB_PROTOTYPE_COUNT] {};
    // The mob index SetUp found, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    // The mob prototype table and its last index before the test, put back by TearDown.
    char_data* m_saved_mob_proto = nullptr;
    int m_saved_top_of_mobt = 0;
    // The tests' mob prototype table: what a login makes followers and their mounts from
    // (make_mob_prototype).
    char_data m_mob_prototypes[MOB_PROTOTYPE_COUNT] {};
    // The prototypes' keywords and short descriptions.
    char m_follower_keywords[16] = "follower";
    char m_mount_keywords[16] = "mount";
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

/* A player too hurt to survive their ring's chill ray logs in wearing it: the ray kills them once
 * they stand in their room, and they come back to life there once, leaving one corpse that holds
 * everything they rented with. */
TEST_F(QuietGearChangeTest, APlayerTheirGearsSpellKillsAtLoginDiesOnceInTheirRoom)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_CHILL_RAY));
    rented.objects.push_back(make_record(STONE_VNUM, MAX_WEAR));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    const bool player_remains = load_character(&m_player);

    EXPECT_TRUE(player_remains);
    EXPECT_EQ(0, m_player.in_room);
    EXPECT_EQ(1, count_in_room(&m_player));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, m_player.carrying);
}

/* A player logging in wearing a ring of sanctuary is given sanctuary once they stand in their
 * room. */
TEST_F(QuietGearChangeTest, APlayerLoggingInIsGivenTheirGearsSpell)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_SANCTUARY));
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_NE(nullptr, affected_by_spell(&m_player, SPELL_SANCTUARY));
    EXPECT_TRUE(IS_AFFECTED(&m_player, AFF_SANCTUARY));
    ASSERT_NE(nullptr, m_player.equipment[WEAR_FINGER_R]);
}

/* A ring's evasion, a lasting affect the quiet login gives as the ring goes on, is not cast a
 * second time once the player stands in their room: it is given, and announced, once. */
TEST_F(QuietGearChangeTest, APlayerLoggingInIsGivenTheirGearsWornAffectOnce)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_ARMOR));
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    int evasion_affects = 0;
    for (const affected_type* affect = m_player.affected; affect; affect = affect->next) {
        if (affect->type == SPELL_ARMOR) {
            ++evasion_affects;
        }
    }
    EXPECT_EQ(1, evasion_affects);
    EXPECT_EQ(1, count_in_output("You feel someone protecting you."));
}

/* A player logging in to the room whose vnum is another room's index shakes the room they stand
 * in with their ring's earthquake: the mob beside them is hurt, and the one in the other room is
 * not. */
TEST_F(QuietGearChangeTest, AHarmfulSpellAtLoginStrikesTheRoomThePlayerLogsInto)
{
    link_second_room();
    /* No exit: the quake opens no way down, and nobody leaves. */
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        world[0].dir_option[direction] = nullptr;
    }
    world[0].number = LOGIN_ROOM_VNUM;
    char_data* beside = make_mob("beside", 0);
    char_data* elsewhere = make_mob("elsewhere", 0, SECOND_ROOM);
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_EARTHQUAKE));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.specials2.load_room = LOGIN_ROOM_VNUM;

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_EQ(0, m_player.in_room);
    EXPECT_LT(GET_HIT(beside), MOB_HIT_POINTS);
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(elsewhere));
}

/* A player logging in wearing a poisonous ring is poisoned, takes the poison's blow once as they
 * come into their room, and the poison goes on hurting them with each tick. */
TEST_F(QuietGearChangeTest, GearPoisonHurtsAPlayerAsTheyLogInAndGoesOnHurtingThem)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    objects_json::ObjectRecord ring = make_record(RING_VNUM, WEAR_FINGER_R);
    ring.affects[0].location = APPLY_BITVECTOR;
    ring.affects[0].modifier = POISON_BIT_POSITION;
    rented.objects.push_back(ring);
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_TRUE(IS_AFFECTED(&m_player, AFF_POISON));
    /* The player comes in at full health, which putting the ring on works out again. */
    EXPECT_EQ(GET_MAX_HIT(&m_player) - POISON_BLOW_DAMAGE, GET_HIT(&m_player));
    const int hit_points_after_login = GET_HIT(&m_player);

    point_update();

    EXPECT_LT(GET_HIT(&m_player), hit_points_after_login);
}

/* A player logging in in an earthquake ring shakes their room before their follower comes in:
 * the follower, too hurt to survive a quake, comes in unhurt and follows them. */
TEST_F(QuietGearChangeTest, ALeadersEarthquakeAtLoginDoesNotReachTheirFollower)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_EARTHQUAKE));
    rented.followers.push_back(make_follower_record({}));
    ASSERT_TRUE(rent_out_player(rented));
    set_follower_hit_points(LOW_HIT_POINTS);

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    EXPECT_EQ(LOW_HIT_POINTS, GET_HIT(followers[0]));
    EXPECT_EQ(&m_player, followers[0]->master);
    EXPECT_EQ(0, count_corpses_in_room());
}

/* A player too hurt to survive their ring's chill ray dies of it once at login and comes back to
 * life in the start room. Their two tamed followers, in earthquake rings, come in beside them
 * there with their gear and the one's mount, follow them still charmed, and cast nothing: the
 * mob in the start room is unhurt. The death's crash save rewrites the object file in place, and
 * nothing that lay past what had been read of it, their aliases or followers, is lost; once the
 * followers have joined, the file is saved again and lists them. */
TEST_F(QuietGearChangeTest, ALeaderKilledAtLoginComesBackWithAllTheirFollowersAndTheirGear)
{
    link_second_room();
    r_mortal_start_room[RACE_HUMAN] = SECOND_ROOM;
    char_data* bystander = make_mob("bystander", 0, SECOND_ROOM);
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_CHILL_RAY));
    for (int alias = 0; alias < ALIASES_PAST_THE_READ_BUFFER; ++alias) {
        objects_json::AliasData long_alias;
        long_alias.keyword = "alias" + std::to_string(alias);
        long_alias.command.assign(ALIAS_COMMAND_LENGTH, 'x');
        rented.aliases.push_back(std::move(long_alias));
    }
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }, true));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.tmpabilities.hit = LOW_HIT_POINTS;
    /* Experience their level needs, so that one death's loss costs no level. */
    const int experience_before_login = xp_to_level(GET_LEVEL(&m_player));
    m_player.points.exp = experience_before_login;

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_EQ(1, count_in_output("You are dead!"));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    /* One death's loss, as die() takes it from a player who killed themselves. */
    const int one_death = (experience_before_login - 3000) / (GET_LEVEL(&m_player) + 2) / 10;
    EXPECT_EQ(experience_before_login - one_death, m_player.points.exp);
    const std::vector<char_data*> followers = followers_in_room(SECOND_ROOM);
    ASSERT_EQ(2u, followers.size());
    for (const char_data* follower : followers) {
        EXPECT_EQ(&m_player, follower->master);
        EXPECT_TRUE(IS_AFFECTED(follower, AFF_CHARM));
        EXPECT_NE(nullptr, follower->equipment[WEAR_FINGER_R]);
        EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(follower));
    }
    const std::vector<char_data*> mounts = mobs_in_room(MOUNT_PROTOTYPE, SECOND_ROOM);
    ASSERT_EQ(1u, mounts.size());
    EXPECT_EQ(followers[0], mounts[0]->master);
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(bystander));
    int aliases = 0;
    for (const alias_list* alias = GET_ALIAS(&m_player); alias; alias = alias->next) {
        ++aliases;
    }
    EXPECT_EQ(ALIASES_PAST_THE_READ_BUFFER, aliases);
    /* Saved again once the followers joined, so that the file lists them. */
    objects_json::ObjectSaveData saved;
    ASSERT_TRUE(read_player_object_file(saved));
    EXPECT_EQ(RENT_CRASH, saved.rent.rentcode);
    EXPECT_EQ(2u, saved.followers.size());
}

/* A follower's earthquake at login shakes their leader, who stands in the room it comes into. */
TEST_F(QuietGearChangeTest, AFollowersEarthquakeAtLoginShakesTheirLeader)
{
    make_player_hardy();
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_LT(GET_HIT(&m_player), GET_MAX_HIT(&m_player));
    EXPECT_EQ(0, count_corpses_in_room());
    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    EXPECT_EQ(&m_player, followers[0]->master);
}

/* A follower's earthquake at login that kills its leader ends the casting: the next follower,
 * brought in beside the leader where it came back to life, casts nothing and is unhurt, and both
 * followers follow the leader. */
TEST_F(QuietGearChangeTest, AFollowersSpellThatKillsTheLeaderEndsTheCasting)
{
    /* The leader comes back to life out of the quake's reach. No exit: the quake opens no way
     * down, and nobody leaves. */
    link_second_room();
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        world[0].dir_option[direction] = nullptr;
    }
    r_mortal_start_room[RACE_HUMAN] = SECOND_ROOM;
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_EQ(1, count_in_output("You are dead!"));
    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    /* The first follower stays where its quake struck; the second comes in beside the leader. */
    const std::vector<char_data*> first_followers = followers_in_room(0);
    const std::vector<char_data*> second_followers = followers_in_room(SECOND_ROOM);
    ASSERT_EQ(1u, first_followers.size());
    ASSERT_EQ(1u, second_followers.size());
    for (const char_data* follower : { first_followers[0], second_followers[0] }) {
        EXPECT_EQ(&m_player, follower->master);
        EXPECT_TRUE(IS_AFFECTED(follower, AFF_CHARM));
        EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(follower));
    }
}

/* A later follower's earthquake at login that kills the leader releases the follower already
 * following it, as any death does: that one is no longer charmed and follows nobody, while the
 * one that killed the leader joins it after. */
TEST_F(QuietGearChangeTest, ALeaderKilledByALaterFollowerReleasesTheOneAlreadyFollowing)
{
    /* The leader comes back to life out of the quake's reach. No exit: the quake opens no way
     * down, and nobody leaves. */
    link_second_room();
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        world[0].dir_option[direction] = nullptr;
    }
    r_mortal_start_room[RACE_HUMAN] = SECOND_ROOM;
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({}));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_EQ(1, count_in_output("You are dead!"));
    const std::vector<char_data*> followers = followers_in_room(0);
    ASSERT_EQ(2u, followers.size());
    EXPECT_EQ(nullptr, followers[0]->master);
    EXPECT_FALSE(IS_AFFECTED(followers[0], AFF_CHARM));
    EXPECT_EQ(&m_player, followers[1]->master);
    EXPECT_TRUE(IS_AFFECTED(followers[1], AFF_CHARM));
}

/* A follower whose earthquake at login kills the leader takes no blow from its poisonous ring:
 * nothing more is cast once the leader has died. */
TEST_F(QuietGearChangeTest, AFollowersPoisonBlowIsNotDealtOnceTheLeaderHasDied)
{
    link_second_room();
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
        world[0].dir_option[direction] = nullptr;
    }
    r_mortal_start_room[RACE_HUMAN] = SECOND_ROOM;
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    objects_json::ObjectRecord poisonous_ring = make_record(RING_VNUM, WEAR_FINGER_L);
    poisonous_ring.affects[0].location = APPLY_BITVECTOR;
    poisonous_ring.affects[0].modifier = POISON_BIT_POSITION;
    rented.followers.push_back(
        make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE), poisonous_ring }));
    ASSERT_TRUE(rent_out_player(rented));
    m_player.tmpabilities.hit = LOW_HIT_POINTS;

    ASSERT_TRUE(load_character(&m_player));

    EXPECT_EQ(1, count_in_output("You are dead!"));
    const std::vector<char_data*> followers = followers_in_room(0);
    ASSERT_EQ(1u, followers.size());
    EXPECT_TRUE(IS_AFFECTED(followers[0], AFF_POISON));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(followers[0]));
}

/* A follower whose record the object file cuts off part way through its gear comes in at login
 * with the gear read before the cut, but follows nobody and is not charmed. */
TEST_F(QuietGearChangeTest, AFollowerWhoseRecordIsCutOffComesInFollowingNobody)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    std::string bytes_without_followers;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(rented, &bytes_without_followers));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_SANCTUARY) }));
    std::string bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(rented, &bytes));
    /* The follower records start where the end-of-followers record stands in a file with none;
     * the cut falls inside the first record of the follower's gear. */
    const std::size_t followers_start = bytes_without_followers.size() - sizeof(follower_file_elem);
    constexpr std::size_t bytes_into_the_gear = 10;
    bytes.resize(followers_start + sizeof(follower_file_elem) + bytes_into_the_gear);
    std::ofstream object_file(player_object_file(), std::ios::binary);
    object_file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    object_file.close();
    ASSERT_TRUE(object_file);
    char_from_room(&m_player);
    m_player.specials2.load_room = ROOM_VNUM;

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    EXPECT_EQ(nullptr, followers[0]->master);
    EXPECT_FALSE(IS_AFFECTED(followers[0], AFF_CHARM));
    EXPECT_EQ(nullptr, followers[0]->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, m_player.followers);
}

/* With nobody dying, every follower's gear casts at login: each follower's ring gives it
 * sanctuary. */
TEST_F(QuietGearChangeTest, EveryFollowersGearCastsAtALoginNobodyDiesIn)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_SANCTUARY) }));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_SANCTUARY) }));
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(2u, followers.size());
    for (char_data* follower : followers) {
        EXPECT_NE(nullptr, affected_by_spell(follower, SPELL_SANCTUARY));
        EXPECT_EQ(&m_player, follower->master);
    }
}

/* A follower coming in at login in a poisonous ring takes the poison's blow once. */
TEST_F(QuietGearChangeTest, AFollowersPoisonousGearHurtsItAsItComesIn)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    objects_json::ObjectRecord ring = make_record(RING_VNUM, WEAR_FINGER_R);
    ring.affects[0].location = APPLY_BITVECTOR;
    ring.affects[0].modifier = POISON_BIT_POSITION;
    rented.followers.push_back(make_follower_record({ ring }));
    ASSERT_TRUE(rent_out_player(rented));

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    EXPECT_TRUE(IS_AFFECTED(followers[0], AFF_POISON));
    EXPECT_EQ(MOB_HIT_POINTS - POISON_BLOW_DAMAGE, GET_HIT(followers[0]));
}

/* A follower too hurt to survive its own ring's chill ray dies of it at login and is not touched
 * again, and the follower loaded after it is still given its ring's sanctuary. */
TEST_F(QuietGearChangeTest, AFollowerItsOwnGearKillsAtLoginIsNotTouchedAgain)
{
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_CHILL_RAY) }, true));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_SANCTUARY) }));
    ASSERT_TRUE(rent_out_player(rented));
    set_follower_hit_points(LOW_HIT_POINTS);

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    EXPECT_NE(nullptr, affected_by_spell(followers[0], SPELL_SANCTUARY));
    ASSERT_NE(nullptr, m_player.followers);
    EXPECT_EQ(followers[0], m_player.followers->follower);
    EXPECT_EQ(nullptr, m_player.followers->next);
    EXPECT_TRUE(mobs_in_room(MOUNT_PROTOTYPE).empty());
    EXPECT_EQ(1, count_corpses_in_room());
}

/* A later follower's earthquake at login can kill an earlier one already following the leader;
 * the leader is left with the later one. */
TEST_F(QuietGearChangeTest, ALaterFollowersEarthquakeCanKillAnEarlierFollower)
{
    make_player_hardy();
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.followers.push_back(make_follower_record({}));
    rented.followers.push_back(make_follower_record({ make_ring_record_casting(SPELL_EARTHQUAKE) }));
    ASSERT_TRUE(rent_out_player(rented));
    set_follower_hit_points(LOW_HIT_POINTS);

    ASSERT_TRUE(load_character(&m_player));

    const std::vector<char_data*> followers = followers_in_room();
    ASSERT_EQ(1u, followers.size());
    ASSERT_NE(nullptr, m_player.followers);
    EXPECT_EQ(followers[0], m_player.followers->follower);
    EXPECT_EQ(nullptr, m_player.followers->next);
    EXPECT_EQ(1, count_corpses_in_room());
}

/* A player with no connection killed by their own gear at login is freed, and the login reports
 * it so that nobody touches them again. Their follower, still held, leaves the game with its gear
 * and its mount. */
TEST_F(QuietGearChangeTest, ALoginReportsALeaderItsGearFreedAndRemovesTheirFollowers)
{
    char_data* unconnected = nullptr;
    CREATE1(unconnected, char_data);
    clear_char(unconnected, 0);
    new (&unconnected->damage_details) player_damage_details();
    unconnected->player.name = str_dup("Unconnected");
    unconnected->player.race = RACE_HUMAN;
    unconnected->player.level = 10;
    unconnected->abilities.str = 18;
    unconnected->abilities.dex = 18;
    unconnected->tmpabilities.str = 18;
    unconnected->tmpabilities.dex = 18;
    unconnected->tmpabilities.move = 50;
    unconnected->tmpabilities.hit = LOW_HIT_POINTS;
    unconnected->specials.position = POSITION_STANDING;
    unconnected->in_room = NOWHERE;
    unconnected->specials2.load_room = ROOM_VNUM;
    register_pc_char(unconnected);
    /* Its object file, written under its own name. */
    char object_file_path[MAX_INPUT_LENGTH];
    ASSERT_TRUE(Crash_get_filename(GET_NAME(unconnected), object_file_path));
    objects_json::ObjectSaveData rented;
    rented.rent.rentcode = RENT_RENTED;
    rented.objects.push_back(make_ring_record_casting(SPELL_CHILL_RAY));
    rented.followers.push_back(make_follower_record({ make_record(SWORD_VNUM, WIELD) }, true));
    std::string bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(rented, &bytes));
    std::ofstream object_file(object_file_path, std::ios::binary);
    object_file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    object_file.close();
    ASSERT_TRUE(object_file);

    const bool leader_remains = load_character(unconnected);

    EXPECT_FALSE(leader_remains);
    EXPECT_FALSE(is_in_game(unconnected));
    EXPECT_EQ(0, count_prototype_mobs_in_game());
    EXPECT_EQ(0, count_objects_in_game(SWORD_VNUM));
    EXPECT_EQ(1, count_corpses_in_room());
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

/* A player removed for idling takes their earthquake ring off without casting it: the mob beside
 * them, in the room they idled in, is not shaken. */
TEST_F(QuietGearChangeTest, AnIdlePlayerRemovedDoesNotCastTheirGearsSpell)
{
    char_data* bystander = make_mob("bystander", 0);
    char_data* idler = make_idle_player();
    put_on_ring_casting(idler, SPELL_EARTHQUAKE);

    ASSERT_EQ(1, check_idling(idler));

    EXPECT_FALSE(is_in_game(idler));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(bystander));
    EXPECT_EQ(PLAYER_HIT_POINTS, GET_HIT(&m_player));
}

/* A player removed for idling while too hurt to survive their poisonous ring takes it off without
 * its poison hurting them: nobody beside them sees them collapse, and no corpse is left. */
TEST_F(QuietGearChangeTest, AnIdlePlayerRemovedTakesNoPoisonBlowFromTheirGear)
{
    char_data* idler = make_idle_player();
    obj_data* ring = make_poisonous_ring();
    obj_to_char(ring, idler);
    run_command_as(idler, do_wear, "ring");
    ASSERT_EQ(ring, idler->equipment[WEAR_FINGER_R]);
    idler->tmpabilities.hit = LOW_HIT_POINTS;

    ASSERT_EQ(1, check_idling(idler));

    EXPECT_FALSE(is_in_game(idler));
    EXPECT_EQ(0, count_in_output(POISON_COLLAPSE_MESSAGE));
    EXPECT_EQ(0, count_corpses_in_room());
}

/* ---- dying ---- */

/* A player slain while wearing a poisonous ring is already dead when the corpse takes the ring:
 * its poison deals them no blow, though they have the hit points to take one and live. A blow
 * the player deals themselves is recorded as damage they dealt. */
TEST_F(QuietGearChangeTest, ASlainPlayersPoisonousGearDealsNoBlowAsTheCorpseTakesIt)
{
    obj_data* ring = make_poisonous_ring();
    obj_to_char(ring, &m_player);
    run_command(do_wear, "ring");
    ASSERT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    /* Putting the ring on dealt its blow; only a blow from here on counts. */
    m_player.damage_details.reset();

    raw_kill(&m_player, nullptr, 0);

    const std::string no_damage_report
        = std::string(GET_NAME(&m_player)) + " has not recorded any damage dealt.\n";
    EXPECT_EQ(no_damage_report, m_player.damage_details.get_damage_report(&m_player));
    ASSERT_NE(nullptr, ring->in_obj);
}

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
