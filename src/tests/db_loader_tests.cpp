#include "../account_errors.h"
#include "../account_index.h"
#include "../account_management.h"
#include "../char_utils.h"
#include "../color.h"
#include "../db.h"
#include "../exploits_json.h"
#include "../handler.h"
#include "../objects_json.h"
#include "AccountRecordOnDiskBuilder.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits.h>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

void build_account_native_player_index(void);

extern struct player_index_element* player_table;
extern struct room_data world;
extern struct index_data* obj_index;
extern struct obj_data* obj_proto;
extern struct obj_data* object_list;
extern int top_of_p_table;
extern int top_of_world;
extern int top_of_objt;
void build_player_index(void);
void clear_char(struct char_data* ch, int mode);
void save_player(struct char_data* ch, int load_room, int index_pos);
void store_to_char(struct char_file_u* st, struct char_data* ch);
int Crash_alias_load(struct char_data* ch, FILE* fp);

namespace {

class ScopedPlayerTableEntry {
public:
    explicit ScopedPlayerTableEntry(const char* name = "aragorn")
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = new player_index_element[1] {};
        top_of_p_table = 0;
        player_table[0].name = strdup(name);
        player_table[0].level = 1;
        player_table[0].race = 1;
        player_table[0].idnum = 1234;
    }

    ~ScopedPlayerTableEntry()
    {
        free(player_table[0].name);
        delete[] player_table;
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

private:
    player_index_element* m_previous_player_table;
    int m_previous_top_of_p_table;
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-db-loader-XXXXXX";
        char* created_path = mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            std::string command = "rm -rf '" + m_path + "'";
            std::system(command.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

std::string read_file_contents(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr);
    if (file == nullptr)
        return "";

    std::string contents;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);
        if (bytes_read < sizeof(buffer)) {
            EXPECT_EQ(std::ferror(file), 0);
            break;
        }
    }

    std::fclose(file);
    return contents;
}

std::string replace_once(std::string text, const std::string& from, const std::string& to)
{
    const size_t position = text.find(from);
    if (position == std::string::npos)
        return text;
    text.replace(position, from.size(), to);
    return text;
}

std::string rooted_account_json_path(const std::string& root_directory, const std::string& normalized_email)
{
    const char first = normalized_email.empty() ? 'A' : static_cast<char>(std::toupper(static_cast<unsigned char>(normalized_email[0])));
    std::string bucket = "U-Z";
    if (first >= 'A' && first <= 'E')
        bucket = "A-E";
    else if (first >= 'F' && first <= 'J')
        bucket = "F-J";
    else if (first >= 'K' && first <= 'O')
        bucket = "K-O";
    else if (first >= 'P' && first <= 'T')
        bucket = "P-T";
    return root_directory + "/accounts/" + bucket + "/" + normalized_email + "/account.json";
}

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        char buffer[PATH_MAX];
        char* current_working_directory = getcwd(buffer, sizeof(buffer));
        EXPECT_NE(current_working_directory, nullptr);
        if (current_working_directory != nullptr)
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

class ScopedStderrRedirect {
public:
    explicit ScopedStderrRedirect(const std::string& path)
        : m_path(path)
    {
        m_original_stderr_fd = dup(STDERR_FILENO);
        EXPECT_GE(m_original_stderr_fd, 0);

        m_redirect_fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
        EXPECT_GE(m_redirect_fd, 0);
        if (m_redirect_fd >= 0)
            EXPECT_GE(dup2(m_redirect_fd, STDERR_FILENO), 0);
    }

    ~ScopedStderrRedirect()
    {
        if (m_original_stderr_fd >= 0) {
            fflush(stderr);
            EXPECT_GE(dup2(m_original_stderr_fd, STDERR_FILENO), 0);
            close(m_original_stderr_fd);
        }
        if (m_redirect_fd >= 0)
            close(m_redirect_fd);
    }

    std::string read_contents() const
    {
        fflush(stderr);
        return read_file_contents(m_path);
    }

private:
    std::string m_path;
    int m_original_stderr_fd = -1;
    int m_redirect_fd = -1;
};

char_file_u make_stored_character(const char* name = "aragorn")
{
    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", name);
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Ranger");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s", "A ranger from the north.");
    stored_character.sex = SEX_MALE;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 12;
    stored_character.language = LANG_HUMAN;
    stored_character.birth = 1700000000;
    stored_character.played = 456;
    stored_character.weight = 190;
    stored_character.height = 72;
    stored_character.hometown = 7;
    stored_character.last_logon = 1700000100;
    stored_character.points.gold = 1234;
    stored_character.points.exp = 5678;
    stored_character.specials2.idnum = 4242;
    stored_character.specials2.load_room = 3001;
    stored_character.specials2.act = 0;
    stored_character.specials2.pref = 1L << 5;
    stored_character.specials2.tactics = TACTICS_BERSERK;
    stored_character.specials2.shooting = SHOOTING_FAST;
    stored_character.specials2.casting = CASTING_SLOW;
    stored_character.specials2.two_handed = 1;
    stored_character.profs.colors[COLOR_MAGIC] = CBMAG;
    stored_character.profs.colors[COLOR_WEATHER] = CBCYN;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.ansi = CBMAG;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.red = 180;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.green = 80;
    stored_character.profs.color_settings[COLOR_MAGIC].foreground.blue = 255;
    stored_character.profs.color_settings[COLOR_WEATHER].background.mode = COLOR_VALUE_TRUECOLOR;
    stored_character.profs.color_settings[COLOR_WEATHER].background.ansi = CBBLU;
    stored_character.profs.color_settings[COLOR_WEATHER].background.red = 10;
    stored_character.profs.color_settings[COLOR_WEATHER].background.green = 20;
    stored_character.profs.color_settings[COLOR_WEATHER].background.blue = 35;
    stored_character.profs.prof_level[PROF_WARRIOR] = 12;
    stored_character.profs.prof_coof[PROF_WARRIOR] = 34;
    return stored_character;
}

class ScopedPlayerTableReset {
public:
    ScopedPlayerTableReset()
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    ~ScopedPlayerTableReset()
    {
        if (player_table != nullptr) {
            for (int index = 0; index <= top_of_p_table; ++index)
                RELEASE(player_table[index].name);
            RELEASE(player_table);
        }

        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

private:
    player_index_element* m_previous_player_table;
    int m_previous_top_of_p_table;
};

class ScopedObjectPrototypeTable {
public:
    ScopedObjectPrototypeTable()
        : m_previous_obj_index(obj_index)
        , m_previous_obj_proto(obj_proto)
        , m_previous_top_of_objt(top_of_objt)
        , m_previous_object_list(object_list)
    {
        obj_index = new index_data[2] {};
        obj_proto = new obj_data[2] {};
        top_of_objt = 1;
        object_list = nullptr;

        initialize_prototype(0, 1001, "steel-helm", "a steel helm", ITEM_ARMOR, WEAR_HEAD, 7, 15);
        initialize_prototype(1, 1002, "travel-pack", "a travel pack", ITEM_CONTAINER, ITEM_TAKE, 4, 5);
    }

    ~ScopedObjectPrototypeTable()
    {
        while (object_list != nullptr) {
            obj_data* next = object_list->next;
            delete object_list;
            object_list = next;
        }

        delete[] obj_proto;
        delete[] obj_index;
        obj_proto = m_previous_obj_proto;
        obj_index = m_previous_obj_index;
        top_of_objt = m_previous_top_of_objt;
        object_list = m_previous_object_list;
    }

private:
    void initialize_prototype(int index, int virt, const char* name, const char* short_description, int type_flag, long wear_flags, int weight, int value_two)
    {
        obj_index[index].virt = virt;
        obj_index[index].number = 0;
        obj_index[index].func = 0;

        clear_object(&obj_proto[index]);
        obj_proto[index].name = strdup(name);
        obj_proto[index].short_description = strdup(short_description);
        obj_proto[index].description = strdup(short_description);
        obj_proto[index].action_description = nullptr;
        obj_proto[index].item_number = index;
        obj_proto[index].obj_flags.type_flag = type_flag;
        obj_proto[index].obj_flags.wear_flags = wear_flags;
        obj_proto[index].obj_flags.weight = weight;
        obj_proto[index].obj_flags.value[2] = value_two;
    }

    index_data* m_previous_obj_index;
    obj_data* m_previous_obj_proto;
    int m_previous_top_of_objt;
    obj_data* m_previous_object_list;
};

void write_file(const std::string& path, const std::string& contents)
{
    FILE* file = fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(fclose(file), 0);
}

// Count files in dir whose names begin with "<base_name>." (dot-anchored, matching
// save_player's versioned-file naming convention). Uses std::filesystem::directory_iterator
// for consistency with finalize_player_file_rename's rename-then-enumerate behavior.
int count_versioned_files_for(const std::string& dir, const std::string& base_name)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        return -1;
    }
    int count = 0;
    const std::string prefix = base_name + ".";
    const fs::directory_iterator end;
    while (it != end) {
        const std::string filename = it->path().filename().native();
        if (filename.size() >= prefix.size() &&
            std::string_view(filename).substr(0, prefix.size()) == prefix) {
            ++count;
        }
        it.increment(ec);
        if (ec) {
            return -1;
        }
    }
    return count;
}

std::string write_valid_legacy_player_file(const std::string& root_directory, const char_file_u& stored_character, const std::string& destination_path = "")
{
    ScopedWorkingDirectory working_directory(root_directory);
    player_index_element* previous_player_table = player_table;
    const int previous_top_of_p_table = top_of_p_table;

    player_table = new player_index_element[1] {};
    top_of_p_table = 0;
    player_table[0].name = strdup(stored_character.name);

    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    char_data* character = new char_data {};
    clear_char(character, MOB_VOID);

    char_file_u mutable_store = stored_character;
    store_to_char(&mutable_store, character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character->desc = &descriptor;

    save_player(character, stored_character.specials2.load_room, 0);
    const std::string generated_path = player_table[0].ch_file;

    // After save_player, the bucket dir must hold exactly ONE versioned file for this
    // character (the atomic finalize pruned stale siblings).
    {
        const std::size_t slash = generated_path.rfind('/');
        if (slash != std::string::npos) {
            const std::string bucket_dir = generated_path.substr(0, slash);
            std::string base_lower = stored_character.name;
            for (char& c : base_lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            EXPECT_EQ(count_versioned_files_for(bucket_dir, base_lower), 1);
        }
    }

    const std::string player_text = read_file_contents(generated_path);
    const std::string final_path = destination_path.empty() ? account::legacy_player_file_path(root_directory, stored_character.name) : destination_path;
    write_file(final_path, player_text);
    if (generated_path != final_path)
        std::remove(generated_path.c_str());

    free(player_table[0].name);
    delete[] player_table;
    if (previous_player_table != nullptr && previous_top_of_p_table >= 0) {
        player_table = previous_player_table;
        top_of_p_table = previous_top_of_p_table;
    } else {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    return player_text;
}

std::string serialize_record(const exploit_record& record)
{
    return std::string(reinterpret_cast<const char*>(&record), sizeof(exploit_record));
}

exploit_record make_record(int type, const char* timestamp, const char* victim_name, int victim_level, int killer_level, int int_param)
{
    exploit_record record {};
    record.type = type;
    strncpy(record.chtime, timestamp, sizeof(record.chtime) - 1);
    strncpy(record.chVictimName, victim_name, sizeof(record.chVictimName) - 1);
    record.iVictimLevel = victim_level;
    record.iKillerLevel = killer_level;
    record.iIntParam = int_param;
    return record;
}

void ensure_test_world_room(int room_number)
{
    if (room_data::BASE_WORLD == nullptr)
        world.create_bulk(1);

    top_of_world = 0;
    world[0].number = room_number;
}

std::string make_valid_object_bytes(int item_number = 1234, int wear_pos = WEAR_HEAD)
{
    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = item_number;
    object_data.objects[0].wear_pos = wear_pos;

    std::string error_message;
    std::string object_bytes;
    EXPECT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    return object_bytes;
}

void expect_object_save_data_equal(const objects_json::ObjectSaveData& expected, const objects_json::ObjectSaveData& actual)
{
    EXPECT_EQ(actual.rent.time, expected.rent.time);
    EXPECT_EQ(actual.rent.rentcode, expected.rent.rentcode);
    EXPECT_EQ(actual.rent.net_cost_per_hour, expected.rent.net_cost_per_hour);
    EXPECT_EQ(actual.rent.gold, expected.rent.gold);
    EXPECT_EQ(actual.rent.nitems, expected.rent.nitems);
    EXPECT_EQ(actual.objects.size(), expected.objects.size());
    for (size_t index = 0; index < expected.objects.size() && index < actual.objects.size(); ++index) {
        const auto& expected_object = expected.objects[index];
        const auto& actual_object = actual.objects[index];
        EXPECT_EQ(actual_object.item_number, expected_object.item_number);
        EXPECT_EQ(actual_object.values, expected_object.values);
        EXPECT_EQ(actual_object.extra_flags, expected_object.extra_flags);
        EXPECT_EQ(actual_object.weight, expected_object.weight);
        EXPECT_EQ(actual_object.timer, expected_object.timer);
        EXPECT_EQ(actual_object.bitvector, expected_object.bitvector);
        EXPECT_EQ(actual_object.wear_pos, expected_object.wear_pos);
        EXPECT_EQ(actual_object.loaded_by, expected_object.loaded_by);
        for (size_t affect_index = 0; affect_index < expected_object.affects.size(); ++affect_index) {
            EXPECT_EQ(actual_object.affects[affect_index].location, expected_object.affects[affect_index].location);
            EXPECT_EQ(actual_object.affects[affect_index].modifier, expected_object.affects[affect_index].modifier);
        }
    }

    EXPECT_EQ(actual.board_points, expected.board_points);
    ASSERT_EQ(actual.aliases.size(), expected.aliases.size());
    for (size_t index = 0; index < expected.aliases.size(); ++index) {
        EXPECT_EQ(actual.aliases[index].keyword, expected.aliases[index].keyword);
        EXPECT_EQ(actual.aliases[index].command, expected.aliases[index].command);
    }

    ASSERT_EQ(actual.followers.size(), expected.followers.size());
    for (size_t index = 0; index < expected.followers.size(); ++index) {
        const auto& expected_follower = expected.followers[index];
        const auto& actual_follower = actual.followers[index];
        EXPECT_EQ(actual_follower.fol_vnum, expected_follower.fol_vnum);
        EXPECT_EQ(actual_follower.mount_vnum, expected_follower.mount_vnum);
        EXPECT_EQ(actual_follower.wimpy, expected_follower.wimpy);
        EXPECT_EQ(actual_follower.exp, expected_follower.exp);
        EXPECT_EQ(actual_follower.flag_config, expected_follower.flag_config);
        EXPECT_EQ(actual_follower.spare1, expected_follower.spare1);
        EXPECT_EQ(actual_follower.spare2, expected_follower.spare2);
        ASSERT_EQ(actual_follower.objects.size(), expected_follower.objects.size());
        for (size_t object_index = 0; object_index < expected_follower.objects.size(); ++object_index) {
            EXPECT_EQ(actual_follower.objects[object_index].item_number, expected_follower.objects[object_index].item_number);
            EXPECT_EQ(actual_follower.objects[object_index].wear_pos, expected_follower.objects[object_index].wear_pos);
        }
    }
}

} // namespace

TEST(DbLoader, RejectsMalformedPlayerTextWithoutLongStringTerminator)
{
    ScopedPlayerTableEntry player_table_entry;
    char player_name[] = "aragorn";
    char_file_u character_data {};
    const char malformed_player_text[] = "#player\n"
                                         "name        aragorn\n"
                                         "description \n"
                                         "This description never terminates cleanly\n"
                                         "end\n";

    EXPECT_LT(load_char_from_text(player_name, malformed_player_text, &character_data), 0);
}

TEST(DbLoader, DoesNotLeaveTheAccountIndexAuthoritativeWhenTheAccountsDirectoryCannotBeWalked)
{
    // A walk that fails for any reason other than "there are no accounts yet" leaves the index
    // empty. If it stays enabled, every resolver answers "No account exists for that email address."
    // for every player on the box -- the exact string interpre.cpp matches to offer registration.
    // Falling back to the directory scan reports the real error instead and offers nothing.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    // A regular file where the accounts directory belongs: opendir fails with ENOTDIR, which is
    // deterministic whatever uid the test runs as.
    write_file(temp_directory.path() + "/accounts", "not a directory");

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    build_account_native_player_index();

    const bool still_enabled = account_index::is_enabled();
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_FALSE(still_enabled)
        << "an empty index must not stay authoritative after the accounts walk failed";
}

TEST(DbLoader, LegacyPlayerTextRoundTripPreservesCombatState)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u original = make_stored_character("aragorn");
    const std::string player_text = write_valid_legacy_player_file(temp_directory.path(), original);

    char player_name[] = "aragorn";
    char_file_u loaded {};
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ASSERT_EQ(load_player_from_text(player_name, player_text.c_str(), &loaded), 1);

    EXPECT_EQ(loaded.specials2.tactics, original.specials2.tactics);
    EXPECT_EQ(loaded.specials2.shooting, original.specials2.shooting);
    EXPECT_EQ(loaded.specials2.casting, original.specials2.casting);
    EXPECT_EQ(loaded.specials2.two_handed, original.specials2.two_handed);

    char_data live_character {};
    clear_char(&live_character, MOB_VOID);
    store_to_char(&loaded, &live_character);

    EXPECT_EQ(utils::get_tactics(live_character), original.specials2.tactics);
    EXPECT_EQ(utils::get_shooting(live_character), original.specials2.shooting);
    EXPECT_EQ(utils::get_casting(live_character), original.specials2.casting);
    EXPECT_TRUE(IS_TWOHANDED(&live_character));
}

TEST(DbLoader, LegacyPlayerTextRoundTripPreservesStructuredColorSettings)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u original = make_stored_character("aragorn");
    const std::string player_text = write_valid_legacy_player_file(temp_directory.path(), original);

    char player_name[] = "aragorn";
    char_file_u loaded {};
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ASSERT_EQ(load_player_from_text(player_name, player_text.c_str(), &loaded), 1);

    EXPECT_EQ(loaded.profs.colors[COLOR_MAGIC], original.profs.colors[COLOR_MAGIC]);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.ansi, original.profs.color_settings[COLOR_MAGIC].foreground.ansi);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.red, original.profs.color_settings[COLOR_MAGIC].foreground.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.green, original.profs.color_settings[COLOR_MAGIC].foreground.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_MAGIC].foreground.blue, original.profs.color_settings[COLOR_MAGIC].foreground.blue);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.ansi, original.profs.color_settings[COLOR_WEATHER].background.ansi);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.red, original.profs.color_settings[COLOR_WEATHER].background.red);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.green, original.profs.color_settings[COLOR_WEATHER].background.green);
    EXPECT_EQ(loaded.profs.color_settings[COLOR_WEATHER].background.blue, original.profs.color_settings[COLOR_WEATHER].background.blue);

    char_data live_character {};
    clear_char(&live_character, MOB_VOID);
    store_to_char(&loaded, &live_character);

    const std::string magic_sequence = get_color_sequence(&live_character, COLOR_MAGIC);
    const std::string weather_sequence = get_color_sequence(&live_character, COLOR_WEATHER);
    EXPECT_NE(magic_sequence.find("\x1B[38;2;180;80;255m"), std::string::npos) << magic_sequence;
    EXPECT_NE(weather_sequence.find("\x1B[48;2;10;20;35m"), std::string::npos) << weather_sequence;
}

TEST(DbLoader, LegacyPlayerTextNormalizesOutOfRangeCombatStateValues)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);

    char_file_u original = make_stored_character("aragorn");
    std::string player_text = write_valid_legacy_player_file(temp_directory.path(), original);
    player_text = replace_once(player_text, "tactics     5", "tactics     99");
    player_text = replace_once(player_text, "shooting    3", "shooting    255");
    player_text = replace_once(player_text, "casting     1", "casting     7");
    player_text = replace_once(player_text, "twohanded   1", "twohanded   4");

    char player_name[] = "aragorn";
    char_file_u loaded {};
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ASSERT_EQ(load_player_from_text(player_name, player_text.c_str(), &loaded), 1);

    EXPECT_EQ(loaded.specials2.tactics, TACTICS_NORMAL);
    EXPECT_EQ(loaded.specials2.shooting, SHOOTING_NORMAL);
    EXPECT_EQ(loaded.specials2.casting, CASTING_NORMAL);
    EXPECT_EQ(loaded.specials2.two_handed, 1);

    char_data live_character {};
    clear_char(&live_character, MOB_VOID);
    store_to_char(&loaded, &live_character);

    EXPECT_EQ(utils::get_tactics(live_character), TACTICS_NORMAL);
    EXPECT_EQ(utils::get_shooting(live_character), SHOOTING_NORMAL);
    EXPECT_EQ(utils::get_casting(live_character), CASTING_NORMAL);
    EXPECT_TRUE(IS_TWOHANDED(&live_character));
}

TEST(DbLoader, LoadsExploitRecordsFromAccountNativeJsonWhenRuntimeFileIsMissing)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const exploit_record expected_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(expected_record));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    EXPECT_NE(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].type, expected_record.type);
    EXPECT_STREQ(records[0].chtime, expected_record.chtime);
    EXPECT_STREQ(records[0].chVictimName, expected_record.chVictimName);
    EXPECT_EQ(records[0].iIntParam, expected_record.iIntParam);
}

TEST(DbLoader, LoadsExploitRecordsFromAccountNativeJsonWhenPresent)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    std::vector<exploit_record> expected_records;
    expected_records.push_back(make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", expected_records, &error_message)) << error_message;

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].type, expected_records[0].type);
    EXPECT_EQ(records[0].iIntParam, expected_records[0].iIntParam);
}

TEST(DbLoader, ReturnsEmptyExploitHistoryForLinkedCharacterWithoutAccountNativeOrRuntimeFile)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    EXPECT_TRUE(records.empty());
}

TEST(DbLoader, LoadsObjectAndExploitDataFromRuntimeLegacyFilesWhenAccountNativeJsonIsAbsent)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    const std::string runtime_object_bytes = make_valid_object_bytes();
    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), runtime_object_bytes);

    const exploit_record runtime_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(runtime_record));

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_EQ(object_bytes, runtime_object_bytes);

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].type, runtime_record.type);
    EXPECT_STREQ(records[0].chtime, runtime_record.chtime);
}

TEST(DbLoader, PrefersAccountNativeObjectAndExploitJsonOverConflictingRuntimeLegacyFiles)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData account_object_data;
    account_object_data.rent.rentcode = RENT_CRASH;
    account_object_data.objects.push_back(objects_json::ObjectRecord {});
    account_object_data.objects[0].item_number = 4321;
    account_object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string account_object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(account_object_data, &account_object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", account_object_bytes, &error_message)) << error_message;
    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());

    std::vector<exploit_record> account_records;
    account_records.push_back(make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "authoritative", 10, 0, 20));
    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", account_records, &error_message)) << error_message;
    const exploit_record stale_runtime_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "stale", 11, 0, 0);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(stale_runtime_record));

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_EQ(object_bytes, account_object_bytes);

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &loaded_records, &error_message)) << error_message;
    ASSERT_EQ(loaded_records.size(), 1u);
    EXPECT_EQ(loaded_records[0].type, account_records[0].type);
    EXPECT_STREQ(loaded_records[0].chVictimName, account_records[0].chVictimName);
}

TEST(DbLoader, FailsClosedWhenAccountNativeExploitJsonIsMalformed)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    std::vector<exploit_record> expected_records;
    expected_records.push_back(make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", expected_records, &error_message)) << error_message;
    write_file(account::account_character_exploits_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");

    const exploit_record stale_runtime_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "stale", 11, 0, 0);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(stale_runtime_record));

    std::vector<exploit_record> records;
    EXPECT_FALSE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message));
    EXPECT_FALSE(error_message.empty());
    EXPECT_EQ(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0)
        << "Failing closed on authoritative account-native exploit JSON should not silently consume or retire the stale legacy runtime file.";
}

TEST(DbLoader, BuildPlayerIndexIncludesLegacyAndAccountNativeCharacters)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    write_file("players/A-E/aragorn.20.2.111.1700010000.0", "");

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "legolas", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "legolas");
    stored_character.level = 25;
    stored_character.race = 3;
    stored_character.last_logon = 1700010200;
    stored_character.specials2.idnum = 222;
    stored_character.specials2.act = 0;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    build_player_index();

    ASSERT_EQ(top_of_p_table, 1);
    EXPECT_STREQ(player_table[0].name, "aragorn");
    EXPECT_STREQ(player_table[1].name, "legolas");
    EXPECT_EQ(player_table[1].idnum, 222);
    EXPECT_EQ(player_table[1].level, 25);
    EXPECT_EQ(player_table[1].race, 3);
    EXPECT_NE(std::string(player_table[1].ch_file).find("legolas.character.json"), std::string::npos);

    char lookup_name[] = "legolas";
    char_file_u loaded_character {};
    ASSERT_EQ(load_player(lookup_name, &loaded_character), 1);
    EXPECT_STREQ(loaded_character.name, "legolas");
    EXPECT_EQ(loaded_character.specials2.idnum, 222);
    EXPECT_EQ(loaded_character.player_index, 1);
}

TEST(DbLoader, LoadsAnAccountNativeCharacterIndexedUnderALongEmailAddress)
{
    // Nothing caps the length of an email address -- is_valid_email checks shape, not size -- and an
    // account-native path is "./accounts/<bucket>/<email>/<name>.character.json": 31 fixed bytes
    // plus the address plus the character name. player_index_element::ch_file holds 160 of those and
    // update_player_index_entry_from_store accepts anything that fits, so an ordinary long corporate
    // address indexes without complaint. load_player then has to carry that path, and it runs on
    // every login for that character.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);

    const std::string long_email = "a-very-long-but-entirely-ordinary-address@long-subdomain.example.com";

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", long_email, "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "legolas", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "legolas");
    stored_character.level = 25;
    stored_character.race = 3;
    stored_character.last_logon = 1700010200;
    stored_character.specials2.idnum = 222;
    stored_character.specials2.act = 0;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    build_player_index();

    ASSERT_EQ(top_of_p_table, 0);
    // The premise of the test: a path the player index holds happily but that does not fit in a
    // 100-byte buffer. If this ever stops being true the test below proves nothing.
    ASSERT_GT(std::strlen(player_table[0].ch_file), static_cast<std::size_t>(100));

    // Forked, because the regression this guards against is a stack-buffer overflow: in-process it
    // aborts the whole binary and every later suite goes unreported with it.
    EXPECT_EXIT(
        {
            char lookup_name[] = "legolas";
            char_file_u loaded_character {};
            const int result = load_player(lookup_name, &loaded_character);
            std::exit((result == 1 && std::strcmp(loaded_character.name, "legolas") == 0) ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(DbLoader, TheLongestPermittedEmailAndCharacterNameStillFitThePlayerIndexField)
{
    // MAX_EMAIL_LENGTH exists to keep this true, so assert it against the real path composer and
    // the real index rather than re-deriving the arithmetic here. If either the cap or the field
    // changes, this is what says whether they still agree.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);

    const std::string longest_email = std::string(account::MAX_EMAIL_LENGTH - std::strlen("@example.com"), 'a') + "@example.com";
    ASSERT_EQ(longest_email.length(), static_cast<std::size_t>(account::MAX_EMAIL_LENGTH));
    const char longest_character_name[] = "abcdefghijkl";
    ASSERT_EQ(std::strlen(longest_character_name), static_cast<std::size_t>(MAX_NAME_LENGTH));

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", longest_email, "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", longest_character_name, 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", longest_character_name);
    stored_character.level = 25;
    stored_character.race = 3;
    stored_character.last_logon = 1700010200;
    stored_character.specials2.idnum = 333;
    stored_character.specials2.act = 0;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    build_player_index();

    ASSERT_EQ(top_of_p_table, 0);
    EXPECT_LT(std::strlen(player_table[0].ch_file), sizeof(player_table[0].ch_file));
    EXPECT_NE(std::string(player_table[0].ch_file).find(".character.json"), std::string::npos);

    char lookup_name[] = "abcdefghijkl";
    char_file_u loaded_character {};
    ASSERT_EQ(load_player(lookup_name, &loaded_character), 1);
    EXPECT_EQ(loaded_character.specials2.idnum, 333);
}

TEST(DbLoader, BuildPlayerIndexKeepsTheAccountUsableWhenOneCharacterFileIsUnreadable)
{
    // One unreadable <name>.character.json must not lock its owner out of the whole account. The
    // boot walker used to quarantine the ACCOUNT for a per-character asset failure, which erases the
    // account's keys and reserves its email -- so a single bad character file locked the player out
    // of every other character they own, and refused their address for account creation, over a file
    // that says nothing about the account record's own integrity. And it is reachable: one unknown
    // skill/slot/flag NAME rejects an entire character file in this codebase.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;
    account_index::clear();

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "twochar", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "twochar", "legolas", 1700010102, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "twochar", "gimli", 1700010103, nullptr, &error_message)) << error_message;

    char_file_u good_character = make_stored_character("legolas");
    good_character.specials2.idnum = 222;
    ASSERT_TRUE(account::write_account_character_file(".", "twochar", good_character, &error_message)) << error_message;

    char_file_u broken_character = make_stored_character("gimli");
    broken_character.specials2.idnum = 223;
    ASSERT_TRUE(account::write_account_character_file(".", "twochar", broken_character, &error_message)) << error_message;
    // Present (so the walker's "does it exist?" branch says yes) but unreadable -- the shape a
    // character file with one unrecognised skill name has.
    write_file(account::account_character_player_path(".", "twochar", "gimli"),
        "{ this is not valid character json");

    // Cleared last, so what the boot walk itself produced is what the assertions below observe
    // rather than the upserts create_account/admin_link_character did during setup.
    account_index::clear();
    account_errors::clear();
    build_player_index();

    EXPECT_EQ(account_index::quarantined_count(), 0u)
        << "a bad character file is not the ACCOUNT failing to parse";

    // The character is silently absent from the player index, and its owner is told only that it
    // does not exist. The boot line naming it scrolls away; this is what an immortal can still ask.
    const std::vector<account_errors::Entry> recorded = account_errors::recent(10);
    ASSERT_EQ(recorded.size(), 1u) << "exactly the one character that could not be read";
    EXPECT_EQ(recorded[0].source, account_errors::Source::Boot);
    EXPECT_EQ(recorded[0].character, "gimli");
    EXPECT_EQ(recorded[0].account, "twochar");
    EXPECT_FALSE(recorded[0].reason.empty());
    account_errors::clear();

    std::string record_path;
    EXPECT_TRUE(account_index::find_path_by_email("player@example.com", &record_path, nullptr))
        << "the owner must still be able to log in";
    EXPECT_TRUE(account_index::find_path_by_account_name("twochar", &record_path, nullptr));

    std::string owner_email;
    EXPECT_TRUE(account_index::find_owner_email_by_character("legolas", &owner_email, nullptr))
        << "the account's OTHER characters must still resolve, or their saves stop";
    EXPECT_EQ(owner_email, "player@example.com");

    // The broken character is simply absent from the player index -- which is what the log line the
    // walker still prints is evidence of.
    ASSERT_EQ(top_of_p_table, 0);
    EXPECT_STREQ(player_table[0].name, "legolas");

    account_index::clear();
}

TEST(DbLoader, BuildPlayerIndexFailsClosedWhenAccountNativePathDoesNotFitPlayerIndex)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    const char* account_name = "abcdefghijklmnopqrst";
    // Derived from the buffer, not hardcoded: ch_file was widened once already, and a fixture that
    // silently stops exceeding it turns this into a test of nothing.
    const std::string long_email = std::string(sizeof(player_table[0].ch_file), 'a') + "@example.com";
    // Planted rather than registered: MAX_EMAIL_LENGTH refuses this address at create_account, so a
    // record put on disk by hand -- restored from a backup, edited by an operator -- is the one
    // route that can still walk an over-length path into the boot index. That is the case this
    // guard has to survive. See tests/AccountRecordOnDiskBuilder.h.
    const std::string account_directory = rots_tests::plant_account_record_with_unvalidated_email(
        ".", account_name, long_email, { "aragorn" });

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.idnum = 222;
    rots_tests::plant_account_character_file(account_directory, stored_character);
    ASSERT_GE((account_directory + "/aragorn.character.json").size(), sizeof(player_table[0].ch_file))
        << "Test setup must exceed the legacy player index path buffer.";

    EXPECT_EXIT(build_player_index(), ::testing::ExitedWithCode(1),
        "too long for the live player index");
    EXPECT_EQ(top_of_p_table, -1)
        << "Boot index failure should leave the parent test process without a truncated live entry.";
}

TEST(DbLoader, BuildPlayerIndexPrefersVersionedLegacyPlayerSaveOverFlatArtifactBeforeMigration)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);

    write_valid_legacy_player_file(".", make_stored_character("aragorn"));

    char_file_u versioned_character = make_stored_character("aragorn");
    versioned_character.level = 40;
    versioned_character.specials2.idnum = 2222;
    const long recent_log_time = time(0);
    versioned_character.last_logon = recent_log_time;
    write_valid_legacy_player_file(".", versioned_character, account::legacy_player_file_path(".", "aragorn") + ".1.1.2222." + std::to_string(recent_log_time) + ".0");

    build_player_index();

    ASSERT_EQ(top_of_p_table, 0);
    EXPECT_STREQ(player_table[0].name, "aragorn");
    EXPECT_EQ(player_table[0].idnum, 2222);
    EXPECT_NE(std::string(player_table[0].ch_file).find(".1.1.2222." + std::to_string(recent_log_time) + ".0"), std::string::npos);
}

TEST(DbLoader, BuildPlayerIndexRemainsConsistentAfterVersionedMigrationRetiresStaleFlatFile)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableReset player_table_reset;

    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players/K-O", 0700), 0);
    ASSERT_EQ(mkdir("players/P-T", 0700), 0);
    ASSERT_EQ(mkdir("players/U-Z", 0700), 0);
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stale_flat_character = make_stored_character("aragorn");
    stale_flat_character.points.gold = 111;
    stale_flat_character.specials2.idnum = 1111;
    write_valid_legacy_player_file(".", stale_flat_character);

    char_file_u versioned_character = make_stored_character("aragorn");
    versioned_character.points.gold = 222;
    versioned_character.specials2.idnum = 2222;
    write_valid_legacy_player_file(".", versioned_character, account::legacy_player_file_path(".", "aragorn") + ".1.1.2222.1700010000.0");

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(".", "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;

    build_player_index();

    ASSERT_EQ(top_of_p_table, 0);
    EXPECT_STREQ(player_table[0].name, "aragorn");
    EXPECT_EQ(player_table[0].idnum, 2222);
    EXPECT_NE(std::string(player_table[0].ch_file).find("aragorn.character.json"), std::string::npos);
}

TEST(DbLoader, LoadsObjectSaveBytesFromAccountNativeJsonWhenRuntimeFileIsMissing)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const std::string expected_bytes = make_valid_object_bytes();
    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), expected_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    EXPECT_NE(access(account::legacy_object_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_EQ(object_bytes, expected_bytes);
}

TEST(DbLoader, LoadsObjectSaveBytesFromAccountNativeJsonWhenPresent)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 4321;
    object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string expected_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &expected_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", expected_bytes, &error_message)) << error_message;

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_EQ(object_bytes, expected_bytes);
}

TEST(DbLoader, FailsClosedWhenAccountNativeObjectJsonIsMalformed)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData account_object_data;
    account_object_data.rent.rentcode = RENT_CRASH;
    account_object_data.objects.push_back(objects_json::ObjectRecord {});
    account_object_data.objects[0].item_number = 4321;
    account_object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string account_object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(account_object_data, &account_object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", account_object_bytes, &error_message)) << error_message;
    write_file(account::account_character_object_path(temp_directory.path(), "alpha-admin", "aragorn"), "{bad-json");
    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());

    std::string object_bytes;
    EXPECT_FALSE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message));
    EXPECT_FALSE(error_message.empty());
    EXPECT_EQ(access(account::legacy_object_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0)
        << "Failing closed on authoritative account-native object JSON should not silently consume the stale runtime object file.";
}

TEST(DbLoader, FailsClosedWhenAccountNativeObjectOrExploitJsonCannotBeRead)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 4321;
    object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string account_object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &account_object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", account_object_bytes, &error_message)) << error_message;

    std::vector<exploit_record> account_records;
    account_records.push_back(make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "authoritative", 10, 0, 20));
    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", account_records, &error_message)) << error_message;

    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), make_valid_object_bytes());
    const exploit_record stale_runtime_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "stale", 11, 0, 0);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(stale_runtime_record));

    const std::string account_object_path = account::account_character_object_path(temp_directory.path(), "alpha-admin", "aragorn");
    const std::string account_exploits_path = account::account_character_exploits_path(temp_directory.path(), "alpha-admin", "aragorn");
    ASSERT_EQ(chmod(account_object_path.c_str(), 0000), 0);
    ASSERT_EQ(chmod(account_exploits_path.c_str(), 0000), 0);

    std::string object_bytes;
    EXPECT_FALSE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message));
    EXPECT_NE(error_message.find("Failed to open file"), std::string::npos);

    std::vector<exploit_record> loaded_records;
    EXPECT_FALSE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &loaded_records, &error_message));
    EXPECT_NE(error_message.find("Failed to open file"), std::string::npos);

    ASSERT_EQ(chmod(account_object_path.c_str(), 0600), 0);
    ASSERT_EQ(chmod(account_exploits_path.c_str(), 0600), 0);
    EXPECT_EQ(access(account::legacy_object_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);
    EXPECT_EQ(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);
}

TEST(DbLoader, ReturnsEmptyObjectSaveBytesForLinkedCharacterWithoutAccountNativeOrRuntimeFile)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    std::string object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_TRUE(object_bytes.empty());
}

TEST(DbLoader, CrashLoadConsumesStagedAccountBackedObjectBytesAndLoadsAliasTail)
{
    ensure_test_world_room(3001);

    char_data character {};
    clear_char(&character, MOB_VOID);

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "aragorn");
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Ranger");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s", "A ranger.");
    stored_character.sex = SEX_MALE;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 10;
    stored_character.language = LANG_HUMAN;
    stored_character.specials2.load_room = 3001;
    stored_character.weight = 210;
    stored_character.height = 72;
    store_to_char(&stored_character, &character);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.board_points[0] = 77;
    object_data.aliases.push_back({ "assist", "kill orc" });

    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;

    stage_account_backed_object_bytes_for_character(&character, object_bytes.data(), object_bytes.size());
    FILE* fp = Crash_load(&character);
    ASSERT_NE(fp, nullptr);
    ASSERT_TRUE(Crash_alias_load(&character, fp));
    ASSERT_EQ(std::fclose(fp), 0);

    EXPECT_EQ(character.specials.board_point[0], 77);
    ASSERT_NE(GET_ALIAS(&character), nullptr);
    EXPECT_STREQ(GET_ALIAS(&character)->keyword, "assist");
    EXPECT_STREQ(GET_ALIAS(&character)->command, "kill orc");
}

TEST(DbLoader, CrashLoadTerminatesALegacyAliasKeywordThatFillsTheWholeField)
{
    ensure_test_world_room(3001);

    char_data character {};
    clear_char(&character, MOB_VOID);

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "aragorn");
    stored_character.sex = SEX_MALE;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 10;
    stored_character.language = LANG_HUMAN;
    stored_character.specials2.load_room = 3001;
    stored_character.weight = 210;
    stored_character.height = 72;
    store_to_char(&stored_character, &character);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.aliases.push_back({ "nineteen_characters", "kill orc" });

    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;

    // The legacy save stores the keyword as 20 raw bytes, so an alias of exactly 20
    // characters reaches the loader with no terminator of its own.
    const size_t keyword_offset = object_bytes.find("nineteen_characters");
    ASSERT_NE(keyword_offset, std::string::npos);
    ASSERT_EQ(object_bytes[keyword_offset + 19], '\0');
    object_bytes[keyword_offset + 19] = 'x';

    stage_account_backed_object_bytes_for_character(&character, object_bytes.data(), object_bytes.size());
    FILE* fp = Crash_load(&character);
    ASSERT_NE(fp, nullptr);
    ASSERT_TRUE(Crash_alias_load(&character, fp));
    ASSERT_EQ(std::fclose(fp), 0);

    ASSERT_NE(GET_ALIAS(&character), nullptr);
    EXPECT_STREQ(GET_ALIAS(&character)->keyword, "nineteen_characters");
    EXPECT_STREQ(GET_ALIAS(&character)->command, "kill orc");
}

TEST(DbLoader, CrashLoadConsumesStagedAccountBackedObjectBytesAndEquipsWearableItems)
{
    ScopedObjectPrototypeTable object_prototypes;
    ensure_test_world_room(3001);

    char_data character {};
    clear_char(&character, MOB_VOID);

    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "aragorn");
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Ranger");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s", "A ranger.");
    stored_character.sex = SEX_MALE;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 10;
    stored_character.language = LANG_HUMAN;
    stored_character.specials2.load_room = 3001;
    stored_character.weight = 210;
    stored_character.height = 72;
    store_to_char(&stored_character, &character);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1001;
    object_data.objects[0].wear_pos = WEAR_HEAD;
    object_data.objects[0].weight = 7;
    object_data.objects[0].values = { 0, 0, 2, 0, 0 };

    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[1].item_number = 1002;
    object_data.objects[1].wear_pos = MAX_WEAR;
    object_data.objects[1].weight = 4;
    object_data.objects[1].values = { 0, 0, 1, 0, 0 };

    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;

    stage_account_backed_object_bytes_for_character(&character, object_bytes.data(), object_bytes.size());
    FILE* fp = Crash_load(&character);
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(std::fclose(fp), 0);

    ASSERT_NE(character.equipment[WEAR_HEAD], nullptr);
    EXPECT_EQ(obj_index[character.equipment[WEAR_HEAD]->item_number].virt, 1001);
    EXPECT_EQ(character.equipment[WEAR_HEAD]->obj_flags.weight, 7);
    ASSERT_NE(character.carrying, nullptr);
    EXPECT_EQ(obj_index[character.carrying->item_number].virt, 1002);
    EXPECT_EQ(character.carrying->obj_flags.weight, 4);
}

TEST(DbLoader, AccountNativeCharacterAndObjectsJsonSupportEquippedLoginWithoutMigration)
{
    ScopedObjectPrototypeTable object_prototypes;
    ensure_test_world_room(3001);

    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", stored_character, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1001;
    object_data.objects[0].wear_pos = WEAR_HEAD;
    object_data.objects[0].weight = 7;
    object_data.objects[0].values = { 0, 0, 2, 0, 0 };

    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[1].item_number = 1002;
    object_data.objects[1].wear_pos = MAX_WEAR;
    object_data.objects[1].weight = 4;
    object_data.objects[1].values = { 0, 0, 1, 0, 0 };

    std::string expected_object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &expected_object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", expected_object_bytes, &error_message)) << error_message;

    char_file_u loaded_store {};
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_store, &error_message)) << error_message;

    std::string loaded_object_bytes;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &loaded_object_bytes, &error_message)) << error_message;
    EXPECT_EQ(loaded_object_bytes, expected_object_bytes);

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&loaded_store, &character);

    stage_account_backed_object_bytes_for_character(&character, loaded_object_bytes.data(), loaded_object_bytes.size());
    FILE* fp = Crash_load(&character);
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(std::fclose(fp), 0);

    ASSERT_NE(character.equipment[WEAR_HEAD], nullptr);
    EXPECT_EQ(obj_index[character.equipment[WEAR_HEAD]->item_number].virt, 1001);
    ASSERT_NE(character.carrying, nullptr);
    EXPECT_EQ(obj_index[character.carrying->item_number].virt, 1002);
}

TEST(DbLoader, AccountNativeCrashLoadDoesNotLogMissingLegacyObjectFileWhenFallbackSucceeds)
{
    ScopedObjectPrototypeTable object_prototypes;
    ensure_test_world_room(3001);

    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1001;
    object_data.objects[0].wear_pos = WEAR_HEAD;
    object_data.objects[0].weight = 7;
    object_data.objects[0].values = { 0, 0, 2, 0, 0 };

    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(".", "alpha-admin", "aragorn", object_bytes, &error_message)) << error_message;

    char_file_u loaded_store {};
    ASSERT_TRUE(account::read_account_character_file(".", "alpha-admin", "aragorn", &loaded_store, &error_message)) << error_message;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&loaded_store, &character);

    stage_account_backed_object_bytes_for_character(&character, object_bytes.data(), object_bytes.size());

    const std::string stderr_path = temp_directory.path() + "/account-native-crash-load.stderr";
    FILE* fp = nullptr;
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        fp = Crash_load(&character);
        ASSERT_NE(fp, nullptr);
        stderr_output = stderr_redirect.read_contents();
    }
    ASSERT_EQ(std::fclose(fp), 0);

    EXPECT_EQ(stderr_output.find("crashsave: mark0"), std::string::npos) << stderr_output;
    EXPECT_EQ(stderr_output.find("SYSERR: unable to open crashsave file"), std::string::npos) << stderr_output;
    ASSERT_NE(character.equipment[WEAR_HEAD], nullptr);
    EXPECT_EQ(obj_index[character.equipment[WEAR_HEAD]->item_number].virt, 1001);
}

TEST(DbLoader, AccountNativeCrashLoadStillLogsNonMissingLegacyObjectOpenFailures)
{
    ScopedObjectPrototypeTable object_prototypes;
    ensure_test_world_room(3001);

    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    std::string object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    ASSERT_TRUE(account::write_account_object_file(".", "alpha-admin", "aragorn", object_bytes, &error_message)) << error_message;

    ASSERT_EQ(mkdir("plrobjs/A-E/aragorn.obj", 0700), 0);

    char_file_u loaded_store {};
    ASSERT_TRUE(account::read_account_character_file(".", "alpha-admin", "aragorn", &loaded_store, &error_message)) << error_message;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&loaded_store, &character);

    stage_account_backed_object_bytes_for_character(&character, object_bytes.data(), object_bytes.size());

    const std::string stderr_path = temp_directory.path() + "/account-native-crash-load-open-failure.stderr";
    FILE* fp = nullptr;
    std::string stderr_output;
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        fp = Crash_load(&character);
        ASSERT_NE(fp, nullptr);
        stderr_output = stderr_redirect.read_contents();
    }
    ASSERT_EQ(std::fclose(fp), 0);

    EXPECT_NE(stderr_output.find("SYSERR: unable to open crashsave file"), std::string::npos) << stderr_output;
    EXPECT_NE(stderr_output.find("aragorn"), std::string::npos) << stderr_output;
}

TEST(DbLoader, AccountNativeCharacterLoadDoesNotPropagateGarbageColorStateIntoLiveCharacter)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.pref = PRF_COLOR;
    ASSERT_TRUE(account::write_account_character_file(temp_directory.path(), "alpha-admin", stored_character, &error_message)) << error_message;

    char_file_u loaded_store {};
    loaded_store.profs.color_mask = 0x5a5a5a5a;
    for (int index = 0; index < MAX_COLOR_FIELDS; ++index)
        loaded_store.profs.colors[index] = 0x5a;
    ASSERT_TRUE(account::read_account_character_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_store, &error_message)) << error_message;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&loaded_store, &character);

    EXPECT_EQ(get_colornum(&character, COLOR_ROOM), CNRM);
}

TEST(DbLoader, SavingAccountNativeCharacterDoesNotAttemptLegacySnapshotRefreshAfterMigrationRetirement)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character.desc = &descriptor;

    const std::string stderr_path = temp_directory.path() + "/save-char.stderr";
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        save_char(&character, stored_character.specials2.load_room, 0);
    }

    const std::string stderr_output = read_file_contents(stderr_path);
    EXPECT_EQ(stderr_output.find("failed to refresh account snapshot"), std::string::npos);
}

TEST(DbLoader, SavingLinkedCharacterRefreshesStalePlayerIndexToAccountNativePath)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    stored_character.points.gold = 321;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;
    std::snprintf(player_table[0].ch_file, sizeof(player_table[0].ch_file), "%s",
        account::legacy_player_file_path(".", "aragorn").c_str());

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character.desc = &descriptor;

    save_char(&character, stored_character.specials2.load_room, 0);

    const std::string account_character_path = account::account_character_player_path(".", "alpha-admin", "aragorn");
    EXPECT_STREQ(player_table[0].ch_file, account_character_path.c_str());
    struct stat file_info {};
    EXPECT_EQ(stat(account_character_path.c_str(), &file_info), 0);
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0)
        << "Linked account-native saves should not revive a legacy player file.";
}

TEST(DbLoader, SavingLinkedCharacterRepairsMissingAccountNativeCharacterFileDirectly)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;
    write_file(account::account_character_snapshot_path(".", "alpha-admin", "aragorn"), "{bad-json");

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    stored_character.points.gold = 7777;

    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character.desc = &descriptor;

    const std::string stderr_path = temp_directory.path() + "/save-char-repair.stderr";
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        save_char(&character, stored_character.specials2.load_room, 0);
    }

    char_file_u repaired_store {};
    ASSERT_TRUE(account::read_account_character_file(".", "alpha-admin", "aragorn", &repaired_store, &error_message)) << error_message;
    EXPECT_EQ(repaired_store.points.gold, stored_character.points.gold);

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0);
    EXPECT_EQ(stat(account::account_character_snapshot_path(".", "alpha-admin", "aragorn").c_str(), &file_info), 0)
        << "Existing transitional snapshot artifacts are tolerated on the repair path, but should not be required.";

    const std::string stderr_output = read_file_contents(stderr_path);
    EXPECT_EQ(stderr_output.find("failed to refresh account snapshot"), std::string::npos);
    EXPECT_EQ(stderr_output.find("failed to repair missing account-native character file"), std::string::npos);
}

TEST(DbLoader, SavingAccountNativeCharacterWithUnreadableAccountRecordDoesNotReviveLegacyPlayerFile)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    stored_character.specials2.load_room = 3001;
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;

    std::snprintf(player_table[0].ch_file, sizeof(player_table[0].ch_file), "%s",
        account::account_character_player_path(".", "alpha-admin", "aragorn").c_str());
    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    write_file(rooted_account_json_path(".", "player@example.com"), "{bad-json");

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    character.desc = &descriptor;

    const std::string stderr_path = temp_directory.path() + "/save-char-unreadable-account.stderr";
    {
        ScopedStderrRedirect stderr_redirect(stderr_path);
        save_char(&character, stored_character.specials2.load_room, 0);
    }

    struct stat file_info {};
    EXPECT_NE(stat(account::legacy_player_file_path(".", "aragorn").c_str(), &file_info), 0);

    const std::string stderr_output = read_file_contents(stderr_path);
    EXPECT_NE(stderr_output.find("refusing legacy fallback for account-native character"), std::string::npos);
}

TEST(DbLoader, CrashLoadDoesNotConsumeStaleStagedObjectBytesForDifferentCharacter)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_EQ(mkdir("plrobjs", 0700), 0);
    ASSERT_EQ(mkdir("plrobjs/A-E", 0700), 0);
    ensure_test_world_room(3001);

    char_data staged_character {};
    clear_char(&staged_character, MOB_VOID);
    char_file_u staged_store {};
    std::snprintf(staged_store.name, sizeof(staged_store.name), "%s", "aragorn");
    std::snprintf(staged_store.title, sizeof(staged_store.title), "%s", "the Ranger");
    std::snprintf(staged_store.description, sizeof(staged_store.description), "%s", "A ranger.");
    staged_store.sex = SEX_MALE;
    staged_store.race = RACE_HUMAN;
    staged_store.bodytype = 1;
    staged_store.level = 10;
    staged_store.language = LANG_HUMAN;
    staged_store.specials2.load_room = 3001;
    store_to_char(&staged_store, &staged_character);

    objects_json::ObjectSaveData object_data;
    object_data.rent.rentcode = RENT_CRASH;
    object_data.objects.push_back(objects_json::ObjectRecord {});
    object_data.objects[0].item_number = 1001;
    object_data.objects[0].wear_pos = WEAR_HEAD;

    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(object_data, &object_bytes, &error_message)) << error_message;
    stage_account_backed_object_bytes_for_character(&staged_character, object_bytes.data(), object_bytes.size());

    char_data later_character {};
    clear_char(&later_character, MOB_VOID);
    char_file_u later_store {};
    std::snprintf(later_store.name, sizeof(later_store.name), "%s", "boromir");
    std::snprintf(later_store.title, sizeof(later_store.title), "%s", "of Gondor");
    std::snprintf(later_store.description, sizeof(later_store.description), "%s", "A captain.");
    later_store.sex = SEX_MALE;
    later_store.race = RACE_HUMAN;
    later_store.bodytype = 1;
    later_store.level = 10;
    later_store.language = LANG_HUMAN;
    later_store.specials2.load_room = 3001;
    store_to_char(&later_store, &later_character);

    EXPECT_EQ(Crash_load(&later_character), nullptr);
    clear_account_backed_object_bytes_for_character(&staged_character);
}

TEST(DbLoader, MigratedLegacyObjectPayloadMatchesAccountNativeObjectsJson)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));

    objects_json::ObjectSaveData expected_object_data;
    expected_object_data.rent.time = 1700010000;
    expected_object_data.rent.rentcode = RENT_CRASH;
    expected_object_data.rent.net_cost_per_hour = 25;
    expected_object_data.objects.push_back(objects_json::ObjectRecord {});
    expected_object_data.objects[0].item_number = 1001;
    expected_object_data.objects[0].wear_pos = WEAR_HEAD;
    expected_object_data.objects[0].values = { 1, 2, 3, 4, 5 };
    expected_object_data.objects[0].extra_flags = 9;
    expected_object_data.objects[0].weight = 7;
    expected_object_data.objects[0].timer = 12;
    expected_object_data.board_points[0] = 42;
    expected_object_data.aliases.push_back({ "assist", "kill orc" });

    objects_json::FollowerData follower;
    follower.fol_vnum = 4444;
    follower.wimpy = 10;
    follower.objects.push_back(objects_json::ObjectRecord {});
    follower.objects[0].item_number = 1002;
    follower.objects[0].wear_pos = MAX_WEAR;
    expected_object_data.followers.push_back(follower);

    std::string legacy_object_bytes;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(expected_object_data, &legacy_object_bytes, &error_message)) << error_message;
    write_file(account::legacy_object_file_path(temp_directory.path(), "aragorn"), legacy_object_bytes);

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;

    std::string migrated_object_bytes;
    ASSERT_TRUE(account::read_account_object_file(temp_directory.path(), "alpha-admin", "aragorn", &migrated_object_bytes, &error_message)) << error_message;

    objects_json::ObjectSaveData migrated_object_data;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(migrated_object_bytes, &migrated_object_data, &error_message)) << error_message;
    expect_object_save_data_equal(expected_object_data, migrated_object_data);
}

TEST(DbLoader, ReturnsEmptyObjectSaveBytesWhenNoRuntimeOrSnapshotExists)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);

    std::string object_bytes;
    std::string error_message;
    ASSERT_TRUE(load_object_save_bytes_for_character(temp_directory.path(), "aragorn", &object_bytes, &error_message)) << error_message;
    EXPECT_TRUE(object_bytes.empty());
}

TEST(DbLoader, SeedsLegacyExploitFileFromAccountSnapshotWhenAppendingNewRecord)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const exploit_record existing_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(existing_record));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    EXPECT_NE(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);

    const exploit_record new_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "Won a battle", 11, 0, 0);
    ASSERT_TRUE(write_exploit_record_for_character(temp_directory.path(), "aragorn", new_record, &error_message)) << error_message;

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].type, new_record.type);
    EXPECT_STREQ(records[0].chVictimName, new_record.chVictimName);
    EXPECT_EQ(records[1].type, existing_record.type);
    EXPECT_STREQ(records[1].chtime, existing_record.chtime);
}

TEST(DbLoader, WritesExploitRecordsIntoAccountNativeJsonForLinkedCharacters)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    std::vector<exploit_record> existing_records;
    existing_records.push_back(make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20));
    ASSERT_TRUE(account::write_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", existing_records, &error_message)) << error_message;
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "stale");

    const exploit_record new_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "Won a battle", 11, 0, 0);
    ASSERT_TRUE(write_exploit_record_for_character(temp_directory.path(), "aragorn", new_record, &error_message)) << error_message;

    std::vector<exploit_record> loaded_records;
    ASSERT_TRUE(account::read_account_exploit_file(temp_directory.path(), "alpha-admin", "aragorn", &loaded_records, &error_message)) << error_message;
    ASSERT_EQ(loaded_records.size(), 2u);
    EXPECT_EQ(loaded_records[0].type, EXPLOIT_ACHIEVEMENT);
    EXPECT_EQ(loaded_records[1].type, EXPLOIT_LEVEL);
    EXPECT_NE(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);
}

TEST(DbLoader, FallsBackToAccountSnapshotWhenRuntimeExploitFileIsMalformed)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/players").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/players/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/accounts/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/account_characters/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/plrobjs/A-E").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(temp_directory.path(), "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(temp_directory.path(), "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    write_valid_legacy_player_file(temp_directory.path(), make_stored_character("aragorn"));
    const exploit_record expected_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(expected_record));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), "bad");

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].type, expected_record.type);
    EXPECT_NE(access(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str(), F_OK), 0);
}

TEST(DbLoader, FailsClosedWhenTemporaryExploitPathAlreadyExists)
{
    TemporaryDirectory temp_directory;
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits").c_str(), 0700), 0);
    ASSERT_EQ(mkdir((temp_directory.path() + "/exploits/A-E").c_str(), 0700), 0);

    const std::string temp_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn") + ".tmp";
    write_file(temp_path, "occupied");

    const exploit_record new_record = make_record(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", "Won a battle", 11, 0, 0);
    std::string error_message;
    EXPECT_FALSE(write_exploit_record_for_character(temp_directory.path(), "aragorn", new_record, &error_message));
    EXPECT_NE(error_message.find("temporary exploit file"), std::string::npos);
}

TEST(DbLoader, DoesNotTrustTheIndexWhenAnAccountBucketCannotBeRead)
{
    // Five letter buckets exist (A-E, F-J, K-O, P-T, U-Z) and each unreadable bucket files as ONE
    // quarantined record, so a chmod accident across accounts/ yields exactly 5 -- which does not
    // exceed MAX_QUARANTINED_RECORDS_AT_BOOT and so boots with an authoritative, EMPTY index. Every
    // login is then told no account exists. One unreadable bucket is already enough to mean the
    // index is missing an unknown number of accounts, so it must not speak for the tree.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/P-T", 0700), 0);
    ASSERT_EQ(chmod("accounts/P-T", 0000), 0);

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    build_account_native_player_index();

    const bool still_enabled = account_index::is_enabled();
    chmod("accounts/P-T", 0700);
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_FALSE(still_enabled)
        << "a bucket the server cannot read leaves the index incomplete, so it must not be authoritative";
}

TEST(DbLoader, BootsPastMoreMisfiledRecordsThanTheQuarantineLimit)
{
    // The limit is a bug detector -- "a serialization change broke every record, we shipped
    // something" -- not a corruption tolerance. A record filed where its own email does not resolve
    // is nobody's bug but the operator's: backing up an account directory in place makes one, and
    // each one files TWO quarantine entries (the name it sits under, and the address it declares).
    // A handful of such copies used to exceed the limit and stop the server, locking out every
    // player over files a system administrator put there with no intention of preventing a boot.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    // Derived from the limit, not a fixed six: a raised limit must not quietly turn this into a
    // test of a tree that never reached it.
    const std::size_t misfiled_records = account_index::MAX_QUARANTINED_RECORDS_AT_BOOT + 1;
    std::string error_message;
    for (std::size_t index = 0; index < misfiled_records; ++index) {
        const std::string email = "a" + std::to_string(index) + "@example.com";
        const std::string account_name = "alpha-admin" + std::to_string(index);
        ASSERT_TRUE(account::create_account(".", account_name, email, "ValidPass1", 1700010101, nullptr, &error_message))
            << error_message;
        ASSERT_EQ(rename(("accounts/A-E/" + email).c_str(), ("accounts/A-E/" + email + ".bak").c_str()), 0);
    }

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    EXPECT_EXIT(
        {
            build_account_native_player_index();
            std::exit(0);
        },
        ::testing::ExitedWithCode(0), "");

    account_index::set_enabled(false);
    account_index::clear();
}

TEST(DbLoader, StillRefusesToBootPastMoreUnreadableRecordsThanTheQuarantineLimit)
{
    // The other half of the same limit: records the server could not read or parse at all are the
    // class it was written for, and six of them still stop the boot.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    for (std::size_t index = 0; index <= account_index::MAX_QUARANTINED_RECORDS_AT_BOOT; ++index)
        write_file("accounts/A-E/broken" + std::to_string(index) + ".json", "this is not an account record");

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    EXPECT_EXIT(build_account_native_player_index(), ::testing::ExitedWithCode(1), "Refusing to boot");

    account_index::set_enabled(false);
    account_index::clear();
}

TEST(DbLoader, RefusesADirectoryAccountThatIsNotAtThePathItsEmailResolvesTo)
{
    // Every path the running server composes for an account comes from
    // normalize_email(account.normalized_email) plus a bucket computed from it, so a record filed
    // anywhere else cannot be found by the game at all. An earlier version of this check compared
    // normalize_email() on BOTH sides, which accepted a directory differing only in case -- and the
    // walk then reached the character loop, where the read resolves through the normalized
    // directory, gets ENOENT, and is reported as "does not exist": every character on the account
    // dropped from the player index with no log line and no counter. Refuse the record instead.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "bob@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_EQ(rename("accounts/A-E/bob@example.com", "accounts/A-E/Bob@Example.com"), 0);

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    build_account_native_player_index();

    std::string record_path;
    const bool resolves = account_index::find_path_by_email("bob@example.com", &record_path, nullptr);
    const bool reserved = account_index::is_quarantined("bob@example.com");
    const std::size_t quarantined = account_index::quarantined_count();
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_EQ(quarantined, 1u) << "a record the game cannot resolve must be refused, not indexed";
    EXPECT_FALSE(resolves) << "it must not answer lookups for an address whose files it cannot reach";
    EXPECT_TRUE(reserved) << "and its address must stay reserved, or registration writes over the owner";
}

TEST(DbLoader, ReservesTheAddressADirectoryAccountDeclaresAsWellAsTheOneItIsFiledUnder)
{
    // Filed under alice@..., declares alice2@... . Quarantining only the directory name leaves the
    // DECLARED address -- the one the owner types at the login prompt -- reading free, so
    // create_account_for_email writes a fresh empty account there while the real record sits inert.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "alice2@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_EQ(rename("accounts/A-E/alice2@example.com", "accounts/A-E/alice@example.com"), 0);

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    build_account_native_player_index();

    const bool filed_under_reserved = account_index::is_quarantined("alice@example.com");
    const bool declared_reserved = account_index::is_quarantined("alice2@example.com");
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_TRUE(filed_under_reserved) << "the address it is filed under must be reserved";
    EXPECT_TRUE(declared_reserved) << "so must the address it declares -- that is the one its owner types";
}

TEST(DbLoader, RecordsAnAccountRecordQuarantinedAtBootSoAnImmortalCanAskAboutIt)
{
    // `account errors` is the one place an immortal can ask what went wrong since the last reboot.
    // A quarantined account locks its owner out entirely, so it belongs in that list beside the
    // character files the boot walk could not read -- not only in `account index`.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "bob@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    {
        std::FILE* file = std::fopen("accounts/A-E/bob@example.com/account.json", "w");
        ASSERT_NE(file, nullptr);
        std::fputs("{\"version\": 1, \"account_name\": \"alpha-adm", file);
        std::fclose(file);
    }

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);
    account_errors::clear();

    build_account_native_player_index();

    const std::vector<account_errors::Entry> recorded = account_errors::recent(10);
    const std::size_t quarantined = account_index::quarantined_count();
    account_index::set_enabled(false);
    account_index::clear();
    account_errors::clear();

    ASSERT_EQ(quarantined, 1u);
    ASSERT_EQ(recorded.size(), 1u) << "the quarantined record must be recorded exactly once";
    EXPECT_EQ(recorded[0].source, account_errors::Source::Boot);
    EXPECT_EQ(recorded[0].account, "bob@example.com") << "named by the address it is filed under";
    EXPECT_TRUE(recorded[0].character.empty()) << "a whole account, not one character";
    EXPECT_FALSE(recorded[0].reason.empty()) << "carrying why it could not be read";
}

TEST(DbLoader, RecordsAMisfiledAccountRecordOnceEvenThoughTwoAddressesAreReserved)
{
    // A record filed under alice@ that declares alice2@ is quarantined under BOTH addresses. That is
    // one bad record, so it must be one entry -- two would read as two broken accounts.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "alice2@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_EQ(rename("accounts/A-E/alice2@example.com", "accounts/A-E/alice@example.com"), 0);

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);
    account_errors::clear();

    build_account_native_player_index();

    const std::vector<account_errors::Entry> recorded = account_errors::recent(10);
    account_index::set_enabled(false);
    account_index::clear();
    account_errors::clear();

    ASSERT_EQ(recorded.size(), 1u) << "one misfiled record is one failure";
    EXPECT_EQ(recorded[0].source, account_errors::Source::Boot);
    EXPECT_EQ(recorded[0].account, "alice@example.com") << "named by the address it is filed under";
    EXPECT_NE(recorded[0].reason.find("alice2@example.com"), std::string::npos) << "the reason says which address it declares";
}

TEST(DbLoader, RecordsAFlatAccountRecordWithNoUsableEmailByItsPath)
{
    // A legacy flat record (accounts/<bucket>/<name>.json) that discloses no email is quarantined
    // under its own path, since it has no address to be keyed by. It is still a locked-out account.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "bob@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    std::string record_text;
    {
        std::FILE* file = std::fopen("accounts/A-E/bob@example.com/account.json", "r");
        ASSERT_NE(file, nullptr);
        char chunk[4096];
        std::size_t read_bytes = 0;
        while ((read_bytes = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
            record_text.append(chunk, read_bytes);
        std::fclose(file);
    }
    const std::string with_email = "\"normalized_email\": \"bob@example.com\"";
    const std::size_t at = record_text.find(with_email);
    ASSERT_NE(at, std::string::npos) << record_text;
    record_text.replace(at, with_email.size(), "\"normalized_email\": \"\"");
    std::filesystem::remove_all("accounts/A-E/bob@example.com");
    {
        std::FILE* file = std::fopen("accounts/A-E/alpha-admin.json", "w");
        ASSERT_NE(file, nullptr);
        std::fputs(record_text.c_str(), file);
        std::fclose(file);
    }

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);
    account_errors::clear();

    build_account_native_player_index();

    const std::vector<account_errors::Entry> recorded = account_errors::recent(10);
    const std::size_t quarantined = account_index::quarantined_count();
    account_index::set_enabled(false);
    account_index::clear();
    account_errors::clear();

    ASSERT_EQ(quarantined, 1u) << "the flat record with no email must be quarantined";
    ASSERT_EQ(recorded.size(), 1u) << "and recorded exactly once";
    EXPECT_EQ(recorded[0].source, account_errors::Source::Boot);
    EXPECT_NE(recorded[0].account.find("alpha-admin.json"), std::string::npos) << "named by its path, the only key it has";
    EXPECT_TRUE(recorded[0].character.empty());
}

TEST(DbLoader, DoesNotTrustTheIndexWhenAnAccountBucketCannotBeStatted)
{
    // The opendir failure below this check synthesizes an unreadable-bucket record precisely so the
    // index refuses to speak for a tree it could not read. The stat above it just skipped the
    // bucket -- no record, no log, no counter -- and the walk still reported success. An accounts/
    // directory with read but not search permission fails EVERY bucket stat that way: the index
    // comes up empty and authoritative, and every player is told no account exists for them.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "bob@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;

    // Readable (opendir and readdir still work), but not searchable (stat on any child fails).
    ASSERT_EQ(chmod("accounts", 0400), 0);

    account_index::clear();
    account_index::set_root_directory(".");
    account_index::set_enabled(true);

    build_account_native_player_index();

    const bool still_enabled = account_index::is_enabled();
    chmod("accounts", 0700);
    account_index::set_enabled(false);
    account_index::clear();

    EXPECT_FALSE(still_enabled)
        << "a bucket the server cannot stat hides an unknown number of accounts, so the index must not be authoritative";
}

TEST(DbLoader, RecordsTheNamechangeExploitForAnAccountNativeCharacter)
{
    // The namechange achievement is the only record that a character used to be somebody else, and
    // write_exploits refuses to write for a character its account no longer lists. Writing the
    // record after the account rename therefore dropped it silently: account.json had already been
    // rewritten to the new name while GET_NAME(ch) was still the old one.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/A-E", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    ASSERT_TRUE(account::create_account(".", "alpha-admin", "player@example.com", "ValidPass1", 1700010101, nullptr, &error_message)) << error_message;
    ASSERT_TRUE(account::admin_link_character(".", "alpha-admin", "aragorn", 1700010102, nullptr, &error_message)) << error_message;

    char_file_u stored_character = make_stored_character("aragorn");
    ASSERT_TRUE(account::write_account_character_file(".", "alpha-admin", stored_character, &error_message)) << error_message;
    std::snprintf(player_table[0].ch_file, sizeof(player_table[0].ch_file), "%s",
        account::account_character_player_path(".", "alpha-admin", "aragorn").c_str());

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "alpha-admin");
    character.desc = &descriptor;

    char new_name[] = "Bartholomew";
    ASSERT_EQ(rename_char(&character, new_name), 1);

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(".", "bartholomew", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u) << "the renamed character must carry the record of what it used to be called";
    EXPECT_EQ(records[0].type, EXPLOIT_ACHIEVEMENT);
    EXPECT_NE(std::string(records[0].chVictimName).find("aragorn->"), std::string::npos)
        << "recorded as: " << records[0].chVictimName;
}

TEST(DbLoader, RefusesARenameWhoseAccountNativePathWouldNotFitThePlayerIndex)
{
    // Every other writer of player_table[].ch_file goes through update_player_index_entry_from_store,
    // which REFUSES an over-length path rather than truncating it. The rename wrote the new path
    // with a raw snprintf: in memory the entry silently lost its ".character.json" suffix, and on
    // the next boot populate_player_index_entry_from_store rebuilt the same over-length path, hit
    // the guard, and exit(1)'d -- one rename could stop the server from booting at all.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");
    ensure_test_world_room(3001);

    ASSERT_EQ(mkdir("accounts", 0700), 0);
    ASSERT_EQ(mkdir("accounts/F-J", 0700), 0);
    ASSERT_EQ(mkdir("players", 0700), 0);
    ASSERT_EQ(mkdir("players/A-E", 0700), 0);

    std::string error_message;
    // Sized so the OLD path still fits ch_file and the NEW one does not -- that is the whole
    // scenario. Path is 31 bytes of structure + email + name, "aragorn" is 7 and "bartholomew" 11,
    // so an email of (buffer - 40) puts the two either side of the limit at any buffer width.
    const std::string long_email = std::string(sizeof(player_table[0].ch_file) - 40 - 12, 'f') + "@example.com";
    // Planted, not registered: MAX_EMAIL_LENGTH refuses this address at create_account now. See
    // tests/AccountRecordOnDiskBuilder.h for why an on-disk record is still worth guarding.
    const std::string account_directory = rots_tests::plant_account_record_with_unvalidated_email(
        ".", "long-account", long_email, { "aragorn" });

    char_file_u stored_character = make_stored_character("aragorn");
    rots_tests::plant_account_character_file(account_directory, stored_character);

    const std::string old_character_path = account::account_character_player_path(".", "long-account", "aragorn");
    std::snprintf(player_table[0].ch_file, sizeof(player_table[0].ch_file), "%s", old_character_path.c_str());

    const std::string would_be_path = account::account_character_player_path(".", "long-account", "bartholomew");
    ASSERT_GE(would_be_path.size(), sizeof(player_table[0].ch_file))
        << "this fixture only means anything if the new path really does not fit: " << would_be_path;

    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    descriptor_data descriptor {};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "LegacyPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "test-host");
    std::snprintf(descriptor.account_name, sizeof(descriptor.account_name), "%s", "long-account");
    character.desc = &descriptor;

    char new_name[] = "Bartholomew";
    EXPECT_EQ(rename_char(&character, new_name), -1) << "a rename that cannot be indexed must be abandoned, not half-done";

    struct stat file_info { };
    EXPECT_EQ(stat(old_character_path.c_str(), &file_info), 0) << "the character must still be where it was";
    EXPECT_STREQ(player_table[0].ch_file, old_character_path.c_str()) << "the player index must still point at it";
    EXPECT_EQ(stat(would_be_path.c_str(), &file_info), -1) << "nothing may have been written to the path that does not fit";

    account::AccountData account_data;
    ASSERT_TRUE(account::read_account_file(".", "long-account", &account_data, &error_message)) << error_message;
    EXPECT_TRUE(account::account_has_character(account_data, "aragorn")) << "the account must still claim the character by its old name";
}

TEST(DbLoader, RefusesARenameToANameThatIsAlreadyTakenAndSaysSo)
{
    // The only caller that can reach rename_char is `wizset <victim> name <newname>`, and it has
    // nothing but the return value to go on. This refusal in particular logged nothing at all, so
    // an immortal renaming onto an existing name had no way to find out that is what happened.
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ScopedPlayerTableEntry player_table_entry("aragorn");

    char_file_u stored_character = make_stored_character("aragorn");
    char_data character {};
    clear_char(&character, MOB_VOID);
    store_to_char(&stored_character, &character);

    char taken_name[] = "Aragorn";
    std::string rename_error;
    EXPECT_EQ(rename_char(&character, taken_name, &rename_error), -1);
    EXPECT_NE(rename_error.find("already"), std::string::npos) << "reported as: " << rename_error;
}

TEST(DbLoader, ANewRoomStartsInitializedRatherThanWithWhateverWasOnTheHeap)
{
    // room_data::room_data() set six of its members and left every other one indeterminate --
    // including sector_type, room_flags and light, which are the exact three IS_DARK reads
    // (utils.h:238), and the people/contents/dir_option pointers. The world is allocated with
    // `new room_data[]`, so a room the loader has not filled in yet answers "is it dark in here?"
    // out of whatever the allocator last left in that block. In the test binary that made
    // SpellParser.MagicRoomMessageOmitsColorCodesForObserversWithoutColorEnabled pass or fail
    // depending on which other tests ran first.
    alignas(room_data) unsigned char storage[sizeof(room_data)];
    std::memset(storage, 0xAA, sizeof(storage));

    room_data* room = new (storage) room_data();

    EXPECT_EQ(static_cast<int>(room->light), 0);
    EXPECT_EQ(room->room_flags, 0L);
    EXPECT_EQ(room->sector_type, SECT_INSIDE);
    EXPECT_EQ(room->alignment, 0);
    EXPECT_EQ(room->people, nullptr);
    EXPECT_EQ(room->contents, nullptr);
    EXPECT_EQ(room->ex_description, nullptr);
    EXPECT_EQ(room->funct, nullptr);
    EXPECT_EQ(room->bfs_next, nullptr);
    for (int direction = 0; direction < NUM_OF_DIRS; ++direction)
        EXPECT_EQ(room->dir_option[direction], nullptr) << "direction " << direction;
}

TEST(DbLoader, ThePlayerIndexHoldsTheAccountNativePathOfAnOrdinaryEmailAddress)
{
    // The account-native path is 31 bytes of fixed structure plus the email plus the character
    // name. With MAX_NAME_LENGTH at 12 and ch_file at 80, that leaves 36 characters for an email
    // address -- and nothing anywhere caps email length. Worse, the conversion path writes the
    // files and links the character with NO length check, so the failure does not surface then: it
    // surfaces at the next boot, in populate_player_index_entry_from_store, as exit(1). A player
    // with an ordinary address converting a character stops the server starting.
    ScopedPlayerTableEntry player_table_entry("aragorn");

    char_file_u stored_character = make_stored_character("aragorn");
    const std::string ordinary_path = "./accounts/A-E/alexandra.richardson@student.university.edu/aragorn.character.json";
    ASSERT_GT(ordinary_path.size(), 80u) << "this fixture only means anything if it exceeded the old limit";

    std::string error_message;
    EXPECT_TRUE(update_player_index_entry_from_store(&stored_character, ordinary_path.c_str(), &error_message))
        << error_message;
    EXPECT_STREQ(player_table[stored_character.player_index].ch_file, ordinary_path.c_str())
        << "the path must be held whole -- a truncated one loses the .character.json suffix save_char tests for";
}
