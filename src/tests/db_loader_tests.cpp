#include "../account_management.h"
#include "../db.h"
#include "../exploits_json.h"
#include "../handler.h"
#include "../objects_json.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

TEST(DbLoader, RejectsMalformedPlayerTextWithoutLongStringTerminator) {
    ScopedPlayerTableEntry player_table_entry;
    char player_name[] = "aragorn";
    char_file_u character_data {};
    const char malformed_player_text[] =
        "#player\n"
        "name        aragorn\n"
        "description \n"
        "This description never terminates cleanly\n"
        "end\n";

    EXPECT_LT(load_char_from_text(player_name, malformed_player_text, &character_data), 0);
}

TEST(DbLoader, LoadsExploitRecordsFromAccountNativeJsonWhenRuntimeFileIsMissing) {
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

TEST(DbLoader, LoadsExploitRecordsFromAccountNativeJsonWhenPresent) {
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

TEST(DbLoader, ReturnsEmptyExploitHistoryForLinkedCharacterWithoutAccountNativeOrRuntimeFile) {
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

TEST(DbLoader, LoadsObjectAndExploitDataFromRuntimeLegacyFilesWhenAccountNativeJsonIsAbsent) {
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

TEST(DbLoader, PrefersAccountNativeObjectAndExploitJsonOverConflictingRuntimeLegacyFiles) {
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

TEST(DbLoader, FailsClosedWhenAccountNativeExploitJsonIsMalformed) {
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

TEST(DbLoader, BuildPlayerIndexIncludesLegacyAndAccountNativeCharacters) {
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

TEST(DbLoader, BuildPlayerIndexPrefersVersionedLegacyPlayerSaveOverFlatArtifactBeforeMigration) {
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

TEST(DbLoader, BuildPlayerIndexRemainsConsistentAfterVersionedMigrationRetiresStaleFlatFile) {
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

TEST(DbLoader, LoadsObjectSaveBytesFromAccountNativeJsonWhenRuntimeFileIsMissing) {
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

TEST(DbLoader, LoadsObjectSaveBytesFromAccountNativeJsonWhenPresent) {
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

TEST(DbLoader, FailsClosedWhenAccountNativeObjectJsonIsMalformed) {
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

TEST(DbLoader, FailsClosedWhenAccountNativeObjectOrExploitJsonCannotBeRead) {
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

TEST(DbLoader, ReturnsEmptyObjectSaveBytesForLinkedCharacterWithoutAccountNativeOrRuntimeFile) {
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

TEST(DbLoader, CrashLoadConsumesStagedAccountBackedObjectBytesAndLoadsAliasTail) {
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

TEST(DbLoader, CrashLoadConsumesStagedAccountBackedObjectBytesAndEquipsWearableItems) {
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

TEST(DbLoader, AccountNativeCharacterAndObjectsJsonSupportEquippedLoginWithoutMigration) {
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

TEST(DbLoader, SavingAccountNativeCharacterDoesNotAttemptLegacySnapshotRefreshAfterMigrationRetirement) {
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

TEST(DbLoader, SavingLinkedCharacterRepairsMissingAccountNativeCharacterFileDirectly) {
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
    EXPECT_EQ(stat(account::account_character_snapshot_path(".", "alpha-admin", "aragorn").c_str(), &file_info), 0);

    const std::string stderr_output = read_file_contents(stderr_path);
    EXPECT_EQ(stderr_output.find("failed to refresh account snapshot"), std::string::npos);
    EXPECT_EQ(stderr_output.find("failed to repair missing account-native character file"), std::string::npos);
}

TEST(DbLoader, SavingAccountNativeCharacterWithUnreadableAccountRecordDoesNotReviveLegacyPlayerFile) {
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

TEST(DbLoader, CrashLoadDoesNotConsumeStaleStagedObjectBytesForDifferentCharacter) {
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

TEST(DbLoader, MigratedLegacyObjectPayloadMatchesAccountNativeObjectsJson) {
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

TEST(DbLoader, ReturnsEmptyObjectSaveBytesWhenNoRuntimeOrSnapshotExists) {
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

TEST(DbLoader, SeedsLegacyExploitFileFromAccountSnapshotWhenAppendingNewRecord) {
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

TEST(DbLoader, WritesExploitRecordsIntoAccountNativeJsonForLinkedCharacters) {
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

TEST(DbLoader, FallsBackToAccountSnapshotWhenRuntimeExploitFileIsMalformed) {
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

TEST(DbLoader, FailsClosedWhenTemporaryExploitPathAlreadyExists) {
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
